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

bool ImageStitcher::stitchImagesFromDirectory(const std::string& input_dir,
                                             const std::string& output_path,
                                             const cv::Size& grid_size) {
    try {
        updateStatus("Loading images...");
        std::vector<cv::Mat> images = loadImagesFromDirectory(input_dir);

        if (images.empty()) {
            updateStatus("No images found");
            return false;
        }

        updateStatus("Sorting images in S-curve order...");
        std::vector<cv::Mat> sorted_images = sortImagesInSCurveOrder(images, grid_size);

        updateStatus("Stitching images...");
        result_image_ = stitchImages(sorted_images, grid_size);

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
