#pragma once

#include <vector>
#include <string>
#include <functional>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

namespace stitch {

class IStitcher {
public:
    virtual ~IStitcher() = default;

    virtual bool stitchImagesFromDirectory(const std::string& input_dir,
                                          const std::string& output_path,
                                          const cv::Size& grid_size = cv::Size(10, 10)) = 0;

    virtual cv::Mat stitchImages(const std::vector<cv::Mat>& images,
                                const cv::Size& grid_size = cv::Size(10, 10)) = 0;

    virtual void setProgressCallback(std::function<void(int, int)> callback) = 0;
    virtual void setStatusCallback(std::function<void(const std::string&)> callback) = 0;

    virtual cv::Mat getResult() const = 0;

    virtual std::vector<cv::Mat> loadImagesFromDirectory(const std::string& input_dir) = 0;
    virtual std::vector<cv::Mat> sortImagesInSCurveOrder(const std::vector<cv::Mat>& images,
                                                         const cv::Size& grid_size) = 0;
    virtual bool saveStitchedImage(const cv::Mat& image, const std::string& output_path) = 0;
};

} // namespace stitch
