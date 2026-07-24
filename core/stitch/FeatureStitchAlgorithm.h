#pragma once

#include "IStitchAlgorithm.h"
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

namespace stitch {

/// Algorithm 2 — OpenCV feature-based stitching
/// Uses cv::Stitcher::PANORAMA mode to auto-detect overlaps and align images.
/// Does NOT require a grid size — images can be in any order.
class FeatureStitchAlgorithm : public IStitchAlgorithm {
public:
    cv::Mat stitch(const std::vector<cv::Mat>& images,
                   const cv::Size& gridSize) override;

    /// Feature-based stitching ignores positions — delegates to stitch().
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const cv::Size& gridSize);

    /// No-op: feature-based stitching doesn't use center crop.
    void setCenterCropSize(int pixels);
};

} // namespace stitch
