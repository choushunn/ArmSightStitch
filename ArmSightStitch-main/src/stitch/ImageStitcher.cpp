#include "ImageStitcher.h"

#include <opencv2/stitching.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <filesystem>
#include <iostream>
#include <regex>
#include <algorithm>

namespace stitch {

ImageStitcher::ImageStitcher() {
    // Initialize
    result_image_ = cv::Mat();
}

ImageStitcher::~ImageStitcher() {
    // Clean up
}

bool ImageStitcher::stitchImagesFromDirectory(const std::string& input_dir, 
                                             const std::string& output_path, 
                                             const cv::Size& grid_size) {
    try {
        // Load images
        updateStatus("Loading images...");
        std::vector<cv::Mat> images = loadImagesFromDirectory(input_dir);
        
        if (images.empty()) {
            updateStatus("No images found");
            return false;
        }
        
        // Sort images in S-curve order
        updateStatus("Sorting images...");
        std::vector<cv::Mat> sorted_images = sortImagesInSCurveOrder(images, grid_size);
        
        // Stitch images
        updateStatus("Stitching images...");
        result_image_ = stitchImages(sorted_images, grid_size);
        
        if (result_image_.empty()) {
            updateStatus("Stitching failed");
            return false;
        }
        
        // Save result
        updateStatus("Saving result...");
        if (!saveStitchedImage(result_image_, output_path)) {
            updateStatus("Failed to save result");
            return false;
        }
        
        updateStatus("Stitching completed successfully");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error stitching images from directory: " << e.what() << std::endl;
        updateStatus(std::string("Stitching error: ") + e.what());
        return false;
    }
}

cv::Mat ImageStitcher::stitchImages(const std::vector<cv::Mat>& images, 
                                   const cv::Size& grid_size) {
    if (images.empty()) {
        std::string log_msg = "No images to stitch";
        std::cerr << "ImageStitcher: " << log_msg << std::endl;
        updateStatus(log_msg);
        return cv::Mat();
    }
    
    try {
        // Log image count
        std::string log_msg = "Stitching " + std::to_string(images.size()) + " images with grid size " + 
                            std::to_string(grid_size.width) + "x" + std::to_string(grid_size.height);
        std::cerr << "ImageStitcher: " << log_msg << std::endl;
        updateStatus(log_msg);
        updateProgress(0, 100);
        
        // Check if we have enough images
        int expected_images = grid_size.width * grid_size.height;
        if (images.size() < expected_images) {
            log_msg = "Not enough images. Expected " + std::to_string(expected_images) + ", got " + std::to_string(images.size());
            std::cerr << "ImageStitcher: " << log_msg << std::endl;
            updateStatus(log_msg);
            updateProgress(100, 100);
            return cv::Mat();
        }
        
        // Get image dimensions from first image
        cv::Size img_size = images[0].size();
        
        // Calculate total size
        int total_width = grid_size.width * img_size.width;
        int total_height = grid_size.height * img_size.height;
        
        log_msg = "Creating stitched image with size " + std::to_string(total_width) + "x" + std::to_string(total_height);
        std::cerr << "ImageStitcher: " << log_msg << std::endl;
        updateStatus(log_msg);
        updateProgress(20, 100);
        
        // Create empty result image
        cv::Mat result(total_height, total_width, images[0].type(), cv::Scalar(0, 0, 0));
        
        log_msg = "Starting stitching process...";
        std::cerr << "ImageStitcher: " << log_msg << std::endl;
        updateStatus(log_msg);
        updateProgress(30, 100);
        
        // Use simple grid stitching method (like original Python implementation)
        int index = 0;
        for (int row = 0; row < grid_size.height; ++row) {
            // Calculate progress for each row
            int progress = 30 + (row * 70) / grid_size.height;
            updateProgress(progress, 100);
            
            if (row % 2 == 0) { // Even rows: left to right
                for (int col = 0; col < grid_size.width; ++col) {
                    if (index >= images.size()) {
                        break;
                    }
                    
                    // Get image to paste
                    const cv::Mat& img = images[index];
                    
                    // Calculate position
                    int x = col * img_size.width;
                    int y = row * img_size.height;
                    
                    // Paste image into result
                    cv::Rect roi(x, y, img_size.width, img_size.height);
                    cv::Mat roi_img = result(roi);
                    img.copyTo(roi_img);
                    
                    index++;
                }
            } else { // Odd rows: right to left
                for (int col = grid_size.width - 1; col >= 0; --col) {
                    if (index >= images.size()) {
                        break;
                    }
                    
                    // Get image to paste
                    const cv::Mat& img = images[index];
                    
                    // Calculate position
                    int x = col * img_size.width;
                    int y = row * img_size.height;
                    
                    // Paste image into result
                    cv::Rect roi(x, y, img_size.width, img_size.height);
                    cv::Mat roi_img = result(roi);
                    img.copyTo(roi_img);
                    
                    index++;
                }
            }
        }
        
        updateProgress(100, 100);
        
        log_msg = "Stitching completed successfully";
        std::cerr << "ImageStitcher: " << log_msg << std::endl;
        updateStatus(log_msg);
        
        return result;
    } catch (const std::exception& e) {
        std::string log_msg = "Error stitching images: " + std::string(e.what());
        std::cerr << "ImageStitcher: " << log_msg << std::endl;
        updateStatus(log_msg);
        updateProgress(100, 100);
        return cv::Mat();
    }
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
        // Check if directory exists
        if (!std::filesystem::exists(input_dir)) {
            std::cerr << "Directory does not exist: " << input_dir << std::endl;
            return images;
        }
        
        // Collect all image files
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
        
        // Sort files by name
        std::sort(image_files.begin(), image_files.end());
        
        // Load images
        for (size_t i = 0; i < image_files.size(); ++i) {
            cv::Mat image = cv::imread(image_files[i]);
            if (!image.empty()) {
                images.push_back(image);
                
                // Update progress
                if (progress_callback_) {
                    progress_callback_(static_cast<int>(i + 1), static_cast<int>(image_files.size()));
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading images from directory: " << e.what() << std::endl;
    }
    
    return images;
}

std::vector<cv::Mat> ImageStitcher::sortImagesInSCurveOrder(const std::vector<cv::Mat>& images, const cv::Size& grid_size) {
    std::vector<cv::Mat> sorted_images;
    
    if (images.empty() || grid_size.width <= 0 || grid_size.height <= 0) {
        return sorted_images;
    }
    
    try {
        // Get number of images
        int total_images = static_cast<int>(images.size());
        int max_images = grid_size.width * grid_size.height;
        
        // Create a grid of images
        std::vector<std::vector<cv::Mat>> image_grid(grid_size.height, std::vector<cv::Mat>(grid_size.width));
        
        // Fill grid with images
        int image_index = 0;
        for (int y = 0; y < grid_size.height; ++y) {
            for (int x = 0; x < grid_size.width; ++x) {
                if (image_index < total_images) {
                    image_grid[y][x] = images[image_index];
                    image_index++;
                }
            }
        }
        
        // Traverse grid in S-curve order
        for (int y = 0; y < grid_size.height; ++y) {
            if (y % 2 == 0) {
                // Left to right
                for (int x = 0; x < grid_size.width; ++x) {
                    if (!image_grid[y][x].empty()) {
                        sorted_images.push_back(image_grid[y][x]);
                    }
                }
            } else {
                // Right to left
                for (int x = grid_size.width - 1; x >= 0; --x) {
                    if (!image_grid[y][x].empty()) {
                        sorted_images.push_back(image_grid[y][x]);
                    }
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error sorting images in S-curve order: " << e.what() << std::endl;
    }
    
    return sorted_images;
}

bool ImageStitcher::saveStitchedImage(const cv::Mat& image, const std::string& output_path) {
    if (image.empty()) {
        return false;
    }
    
    try {
        // Save image
        if (!cv::imwrite(output_path, image)) {
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving stitched image: " << e.what() << std::endl;
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
