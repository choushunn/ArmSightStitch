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

    cv::Mat stitchImagesWithPositions(const std::vector<PositionedImage>& positioned,
                                      const cv::Size& grid_size) override;

    void setProgressCallback(std::function<void(int, int)> callback);
    void setStatusCallback(std::function<void(const std::string&)> callback);

    cv::Mat getResult() const;

    /// Switch stitch strategy: 0=Grid, 1=Feature, 2=ZScale, 3=SeamFeather, 4=AdvancedGrid
    void setAlgorithm(int algo);
    void setCropMargin(int pixels) override;
    void setCenterCropSize(int pixels) override;
    void setFeatherWidth(int pixels) override;
    void setScaleMode(int mode) override;
    void setScaleMapFile(const std::string& path) override;
    void setZCorrectionCoef(double coef) override;
    void setCropOffsetFile(const std::string& path) override;

    /**
     * @brief Load images from directory
     * @param input_dir Path to input directory
     * @return Vector of loaded images
     */
    std::vector<cv::Mat> loadImagesFromDirectory(const std::string& input_dir);

    /**
     * @brief Load images with grid positions parsed from filenames (row_col.ext).
     *        Auto-detects grid dimensions from max row/col (like docs/stitch.py).
     * @param input_dir Path to input directory
     * @param out_grid_size [out] Detected grid size (width = max_col+1, height = max_row+1)
     * @return Vector of positioned images
     */
    std::vector<PositionedImage> loadImagesWithPositions(const std::string& input_dir,
                                                         cv::Size& out_grid_size) override;

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
    std::unique_ptr<IStitchAlgorithm> algo1_;  // GridStitchAlgorithm (0)
    std::unique_ptr<IStitchAlgorithm> algo2_;  // FeatureStitchAlgorithm (1)
    std::unique_ptr<IStitchAlgorithm> algo3_;  // ZScaleGridStitchAlgorithm (2)
    std::unique_ptr<IStitchAlgorithm> algo4_;  // SeamFeatherStitchAlgorithm (3)
    std::unique_ptr<IStitchAlgorithm> algo5_;  // AdvancedGridStitchAlgorithm (4)
    IStitchAlgorithm* current_algo_ = nullptr;
    std::function<void(int, int)> progress_callback_;
    std::function<void(const std::string&)> status_callback_;
    int scale_mode_ = 0;
    std::string scale_map_file_;
    double z_correction_coef_ = 1.0;
    std::string crop_offset_file_;
};

} // namespace stitch
