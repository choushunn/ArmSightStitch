#pragma once

#include <vector>
#include <functional>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

namespace stitch {

/// Abstract strategy for stitching images together
class IStitchAlgorithm {
public:
    virtual ~IStitchAlgorithm() = default;

    /// Stitch a set of images and return the result
    virtual cv::Mat stitch(const std::vector<cv::Mat>& images,
                          const cv::Size& gridSize) = 0;

    /// Optional progress callback: (current, total)
    void setProgressCallback(std::function<void(int, int)> cb) { progressCb_ = std::move(cb); }

    /// Optional status callback
    void setStatusCallback(std::function<void(const std::string&)> cb) { statusCb_ = std::move(cb); }

protected:
    void reportProgress(int cur, int total) {
        if (progressCb_) progressCb_(cur, total);
    }
    void reportStatus(const std::string& msg) {
        if (statusCb_) statusCb_(msg);
    }

private:
    std::function<void(int, int)> progressCb_;
    std::function<void(const std::string&)> statusCb_;
};

} // namespace stitch
