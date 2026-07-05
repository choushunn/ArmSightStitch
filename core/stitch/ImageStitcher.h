#pragma once

#include <vector>
#include <string>
#include <functional>

#include "IStitcher.h"
#include "IStitchAlgorithm.h"
#include <memory>

namespace stitch {

class ImageStitcher : public IStitcher {
public:
    ImageStitcher();
    ~ImageStitcher();

    bool stitchImagesFromDirectory(const std::string& input_dir,
                                  const std::string& output_path,
                                  const cv::Size& grid_size = cv::Size(10, 10));

    cv::Mat stitchImages(const std::vector<cv::Mat>& images,
                        const cv::Size& grid_size = cv::Size(10, 10));

    void setProgressCallback(std::function<void(int, int)> callback);
    void setStatusCallback(std::function<void(const std::string&)> callback);

    cv::Mat getResult() const;

    /// Switch stitch strategy: 0=GridStitchAlgorithm, 1=FeatureStitchAlgorithm
    void setAlgorithm(int algo);

    /**
     * @brief Load images from directory
     * @param input_dir Path to input directory
     * @return Vector of loaded images
     */
    std::vector<cv::Mat> loadImagesFromDirectory(const std::string& input_dir);

    /**
     * @brief Sort images in S-curve order
     * @param images Vector of images
     * @param grid_size Grid size (width, height)
     * @return Vector of sorted images
     */
    std::vector<cv::Mat> sortImagesInSCurveOrder(const std::vector<cv::Mat>& images, const cv::Size& grid_size);

    /**
     * @brief Save stitched image
     * @param image Stitched image
     * @param output_path Path to save image
     * @return true if saved successfully, false otherwise
     */
    bool saveStitchedImage(const cv::Mat& image, const std::string& output_path);

private:
    /**
     * @brief Update progress
     * @param current Current progress
     * @param total Total progress
     */
    void updateProgress(int current, int total);

    /**
     * @brief Update status
     * @param message Status message
     */
    void updateStatus(const std::string& message);

private:
    cv::Mat result_image_;
    std::unique_ptr<IStitchAlgorithm> algo1_;  // GridStitchAlgorithm
    std::unique_ptr<IStitchAlgorithm> algo2_;  // FeatureStitchAlgorithm
    IStitchAlgorithm* current_algo_ = nullptr;
    std::function<void(int, int)> progress_callback_;
    std::function<void(const std::string&)> status_callback_;
};

} // namespace stitch
