#pragma once

#include <vector>
#include <string>
#include <functional>
#include <utility>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

namespace stitch {

/// Image with its grid position parsed from filename (row_col.jpg or row_col_z.jpg)
struct PositionedImage {
    cv::Mat image;
    int row = 0;  // 0-indexed grid row
    int col = 0;  // 0-indexed grid column
    int z = 0;    // Z-axis height (pulses) at capture time; 0 = unknown
};

class IStitcher {
public:
    virtual ~IStitcher() = default;

    virtual bool stitchImagesFromDirectory(const std::string& input_dir,
                                          const std::string& output_path,
                                          const cv::Size& grid_size = cv::Size(10, 10)) = 0;

    virtual cv::Mat stitchImages(const std::vector<cv::Mat>& images,
                                const cv::Size& grid_size = cv::Size(10, 10)) = 0;

    /// Direct position-based stitching (like docs/stitch.py).
    /// Images are placed at their (row, col) grid positions regardless of input order.
    virtual cv::Mat stitchImagesWithPositions(const std::vector<PositionedImage>& positioned,
                                              const cv::Size& grid_size) = 0;

    virtual void setProgressCallback(std::function<void(int, int)> callback) = 0;
    virtual void setStatusCallback(std::function<void(const std::string&)> callback) = 0;

    virtual cv::Mat getResult() const = 0;

    virtual void setAlgorithm(int algo) = 0;
    virtual void setCropMargin(int pixels) = 0;
    virtual void setCenterCropSize(int pixels) = 0;

    virtual std::vector<cv::Mat> loadImagesFromDirectory(const std::string& input_dir) = 0;
    /// Load images with grid positions parsed from filenames (row_col.ext).
    /// Auto-detects grid dimensions from max row/col.
    virtual std::vector<PositionedImage> loadImagesWithPositions(const std::string& input_dir,
                                                                 cv::Size& out_grid_size) = 0;
    virtual std::vector<cv::Mat> sortImagesInSCurveOrder(const std::vector<cv::Mat>& images,
                                                         const cv::Size& grid_size) = 0;
    virtual bool saveStitchedImage(const cv::Mat& image, const std::string& output_path) = 0;
};

} // namespace stitch
