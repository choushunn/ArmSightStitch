#include "FeatureStitchAlgorithm.h"
#include <opencv2/stitching.hpp>
#include <spdlog/spdlog.h>

namespace stitch {

cv::Mat FeatureStitchAlgorithm::stitch(const std::vector<cv::Mat>& images,
                                        const cv::Size& /*gridSize*/)
{
    if (images.size() < 2) {
        reportStatus("Need at least 2 images for feature-based stitching");
        return {};
    }

    reportStatus("Feature-based stitching " + std::to_string(images.size()) + " images...");
    reportProgress(10, 100);

    cv::Ptr<cv::Stitcher> stitcher = cv::Stitcher::create(cv::Stitcher::PANORAMA);
    cv::Mat result;
    cv::Stitcher::Status status = stitcher->stitch(images, result);

    if (status != cv::Stitcher::OK) {
        reportStatus("Feature-based stitching failed (code: " +
                     std::to_string(static_cast<int>(status)) + ")");
        reportProgress(100, 100);
        return {};
    }

    reportProgress(100, 100);
    reportStatus("Feature-based stitching completed");
    return result;
}

cv::Mat FeatureStitchAlgorithm::stitchWithPositions(
    const std::vector<cv::Mat>& images,
    const std::vector<std::pair<int, int>>& /*positions*/,
    const cv::Size& gridSize)
{
    return stitch(images, gridSize);
}

void FeatureStitchAlgorithm::setCenterCropSize(int /*pixels*/) {}

} // namespace stitch
