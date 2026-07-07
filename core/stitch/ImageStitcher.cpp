#include "ImageStitcher.h"
#include "GridStitchAlgorithm.h"
#include "FeatureStitchAlgorithm.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <regex>
#include <algorithm>

namespace stitch {

ImageStitcher::ImageStitcher()
    : algo1_(std::make_unique<GridStitchAlgorithm>())
    , algo2_(std::make_unique<FeatureStitchAlgorithm>())
{
    result_image_ = cv::Mat();
    // Default to algorithm 1, forwarding callbacks
    current_algo_ = algo1_.get();
    current_algo_->setProgressCallback([this](int c, int t) { updateProgress(c, t); });
    current_algo_->setStatusCallback([this](const std::string& m) { updateStatus(m); });
}

ImageStitcher::~ImageStitcher() = default;

void ImageStitcher::setAlgorithm(int algo) {
    auto* next = (algo == 1) ? algo2_.get() : algo1_.get();
    if (algo != 0 && algo != 1) {
        SPDLOG_WARN("Unknown stitch algorithm {}, defaulting to 0", algo);
        next = algo1_.get();
    }
    if (next != current_algo_) {
        current_algo_ = next;
        current_algo_->setProgressCallback([this](int c, int t) { updateProgress(c, t); });
        current_algo_->setStatusCallback([this](const std::string& m) { updateStatus(m); });
    }
}

void ImageStitcher::setCropMargin(int pixels) {
    auto* grid = dynamic_cast<GridStitchAlgorithm*>(algo1_.get());
    if (grid) grid->setCropMargin(pixels);
}

void ImageStitcher::setCenterCropSize(int pixels) {
    auto* grid = dynamic_cast<GridStitchAlgorithm*>(algo1_.get());
    if (grid) grid->setCenterCropSize(pixels);
    auto* feat = dynamic_cast<FeatureStitchAlgorithm*>(algo2_.get());
    if (feat) feat->setCenterCropSize(pixels);
}

bool ImageStitcher::stitchImagesFromDirectory(const std::string& input_dir,
                                             const std::string& output_path,
                                             const cv::Size& grid_size) {
    try {
        updateStatus("Loading images...");

        // Use position-based loading when grid_size is default (auto-detect),
        // otherwise fall back to sequential loading + S-curve sort.
        cv::Size detected_grid = grid_size;
        auto positioned = loadImagesWithPositions(input_dir, detected_grid);

        if (positioned.empty()) {
            // Fallback: try legacy sequential loading
            std::vector<cv::Mat> images = loadImagesFromDirectory(input_dir);
            if (images.empty()) {
                updateStatus("No images found");
                return false;
            }
            updateStatus("Sorting images in S-curve order...");
            std::vector<cv::Mat> sorted_images = sortImagesInSCurveOrder(images, grid_size);
            updateStatus("Stitching images...");
            result_image_ = stitchImages(sorted_images, grid_size);
        } else {
            // Use auto-detected grid if none was explicitly provided (default 10x10)
            if (grid_size.width == 10 && grid_size.height == 10
                && (detected_grid.width != 10 || detected_grid.height != 10)) {
                SPDLOG_INFO("Auto-detected grid: {}x{}", detected_grid.width, detected_grid.height);
            }
            updateStatus("Stitching " + std::to_string(positioned.size()) +
                         " images with positions...");
            result_image_ = stitchImagesWithPositions(positioned, detected_grid);
        }

        if (result_image_.empty()) {
            updateStatus("Stitching failed");
            return false;
        }

        updateStatus("Saving result...");
        if (!saveStitchedImage(result_image_, output_path)) {
            updateStatus("Failed to save result");
            return false;
        }

        updateStatus("Stitching completed successfully");
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error stitching images from directory: {}", e.what());
        updateStatus(std::string("Stitching error: ") + e.what());
        return false;
    }
}

cv::Mat ImageStitcher::stitchImages(const std::vector<cv::Mat>& images,
                                   const cv::Size& grid_size) {
    cv::Mat result = current_algo_->stitch(images, grid_size);
    if (!result.empty()) result_image_ = result;
    return result;
}

cv::Mat ImageStitcher::stitchImagesWithPositions(const std::vector<PositionedImage>& positioned,
                                                  const cv::Size& grid_size) {
    // Separate images and positions for the algorithm call
    std::vector<cv::Mat> images;
    std::vector<std::pair<int, int>> positions;
    images.reserve(positioned.size());
    positions.reserve(positioned.size());
    for (const auto& p : positioned) {
        images.push_back(p.image);
        positions.emplace_back(p.row, p.col);
    }

    cv::Mat result;
    if (auto* grid = dynamic_cast<GridStitchAlgorithm*>(current_algo_)) {
        result = grid->stitchWithPositions(images, positions, grid_size);
    } else if (auto* feat = dynamic_cast<FeatureStitchAlgorithm*>(current_algo_)) {
        result = feat->stitchWithPositions(images, positions, grid_size);
    } else {
        // Fallback: ignore positions, use sequential stitching
        result = current_algo_->stitch(images, grid_size);
    }

    if (!result.empty()) result_image_ = result;
    return result;
}

void ImageStitcher::setProgressCallback(std::function<void(int, int)> callback) {
    progress_callback_ = callback;
}

void ImageStitcher::setStatusCallback(std::function<void(const std::string&)> callback) {
    status_callback_ = callback;
}

cv::Mat ImageStitcher::getResult() const {
    return result_image_;
}

std::vector<cv::Mat> ImageStitcher::loadImagesFromDirectory(const std::string& input_dir) {
    std::vector<cv::Mat> images;

    try {
        if (!std::filesystem::exists(input_dir)) {
            SPDLOG_ERROR("Directory does not exist: {}", input_dir);
            return images;
        }

        std::vector<std::string> image_files;
        for (const auto& entry : std::filesystem::directory_iterator(input_dir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") {
                    image_files.push_back(entry.path().string());
                }
            }
        }

        std::sort(image_files.begin(), image_files.end());

        for (size_t i = 0; i < image_files.size(); ++i) {
            cv::Mat image = cv::imread(image_files[i]);
            if (!image.empty()) {
                images.push_back(image);
                if (progress_callback_) {
                    progress_callback_(static_cast<int>(i + 1), static_cast<int>(image_files.size()));
                }
            }
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error loading images from directory: {}", e.what());
    }

    return images;
}

std::vector<PositionedImage> ImageStitcher::loadImagesWithPositions(const std::string& input_dir,
                                                                     cv::Size& out_grid_size) {
    std::vector<PositionedImage> result;

    try {
        if (!std::filesystem::exists(input_dir)) {
            SPDLOG_ERROR("Directory does not exist: {}", input_dir);
            return result;
        }

        // Regex to match "row_col" at start of filename (like stitch.py parse_row_col)
        std::regex rowColPattern(R"(^(\d+)_(\d+))");

        int maxRow = -1;
        int maxCol = -1;
        std::vector<std::pair<std::string, std::pair<int, int>>> found; // path, (row, col)

        for (const auto& entry : std::filesystem::directory_iterator(input_dir)) {
            if (!entry.is_regular_file()) continue;

            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".bmp") continue;

            std::string stem = entry.path().stem().string();
            // Remove spaces from stem (like stitch.py: name.replace(" ", ""))
            stem.erase(std::remove(stem.begin(), stem.end(), ' '), stem.end());

            std::smatch match;
            if (std::regex_search(stem, match, rowColPattern)) {
                int row = std::stoi(match[1].str());
                int col = std::stoi(match[2].str());
                found.emplace_back(entry.path().string(), std::make_pair(row, col));
                maxRow = std::max(maxRow, row);
                maxCol = std::max(maxCol, col);
            }
        }

        if (found.empty()) {
            SPDLOG_WARN("No images with row_col pattern found in {}", input_dir);
            return result;
        }

        // Convert 1-indexed display coordinates to 0-indexed (like stitch.py)
        // The filenames use display coordinates (1-indexed), we need 0-indexed for grid placement
        out_grid_size = cv::Size(maxCol, maxRow);  // width = max col, height = max row
        SPDLOG_INFO("Detected grid: {}x{} from {} images", maxCol, maxRow, found.size());

        // Load images and build result
        for (const auto& [path, rc] : found) {
            cv::Mat image = cv::imread(path);
            if (!image.empty()) {
                PositionedImage pi;
                pi.image = image;
                // Filename is 1-indexed display coords; convert to 0-indexed
                pi.row = rc.first - 1;
                pi.col = rc.second - 1;
                result.push_back(pi);
            }
        }

        SPDLOG_INFO("Loaded {} images with positions", result.size());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error loading images with positions: {}", e.what());
    }

    return result;
}

std::vector<cv::Mat> ImageStitcher::sortImagesInSCurveOrder(const std::vector<cv::Mat>& images, const cv::Size& grid_size) {
    std::vector<cv::Mat> sorted_images;

    if (images.empty() || grid_size.width <= 0 || grid_size.height <= 0) {
        return sorted_images;
    }

    try {
        int total_images = static_cast<int>(images.size());
        int max_images = grid_size.width * grid_size.height;

        std::vector<std::vector<cv::Mat>> image_grid(grid_size.height, std::vector<cv::Mat>(grid_size.width));

        int image_index = 0;
        for (int y = 0; y < grid_size.height; ++y) {
            for (int x = 0; x < grid_size.width; ++x) {
                if (image_index < total_images) {
                    image_grid[y][x] = images[image_index];
                    image_index++;
                }
            }
        }

        for (int y = 0; y < grid_size.height; ++y) {
            if (y % 2 == 0) {
                for (int x = 0; x < grid_size.width; ++x) {
                    if (!image_grid[y][x].empty()) {
                        sorted_images.push_back(image_grid[y][x]);
                    }
                }
            } else {
                for (int x = grid_size.width - 1; x >= 0; --x) {
                    if (!image_grid[y][x].empty()) {
                        sorted_images.push_back(image_grid[y][x]);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error sorting images in S-curve order: {}", e.what());
    }

    return sorted_images;
}

bool ImageStitcher::saveStitchedImage(const cv::Mat& image, const std::string& output_path) {
    if (image.empty()) {
        return false;
    }

    try {
        if (!cv::imwrite(output_path, image)) {
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error saving stitched image: {}", e.what());
        return false;
    }
}

void ImageStitcher::updateProgress(int current, int total) {
    if (progress_callback_) {
        progress_callback_(current, total);
    }
}

void ImageStitcher::updateStatus(const std::string& message) {
    if (status_callback_) {
        status_callback_(message);
    }
}

} // namespace stitch
