#pragma once

#include "IStitchAlgorithm.h"
#include <opencv2/stitching.hpp>
#include <spdlog/spdlog.h>

namespace stitch {

/// Algorithm 2 — OpenCV feature-based stitching
/// Uses cv::Stitcher::PANORAMA mode to auto-detect overlaps and align images.
/// Does NOT require a grid size — images can be in any order.
class FeatureStitchAlgorithm : public IStitchAlgorithm {
public:
    cv::Mat stitch(const std::vector<cv::Mat>& images,
                  const cv::Size& /*gridSize*/) override
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

    /// Feature-based stitching ignores positions — delegates to stitch().
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& /*positions*/,
                                const cv::Size& gridSize)
    {
        return stitch(images, gridSize);
    }

    /// No-op: feature-based stitching doesn't use center crop.
    void setCenterCropSize(int /*pixels*/) {}
};

} // namespace stitch
