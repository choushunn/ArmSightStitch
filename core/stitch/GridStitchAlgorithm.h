#pragma once

#include "IStitchAlgorithm.h"

namespace stitch {

/// Algorithm 0 — grid-based stitching with center cropping (like docs/stitch.py)
/// Images are placed at fixed grid positions. Center cropping removes overlap
/// and edge distortion between adjacent tiles.
class GridStitchAlgorithm : public IStitchAlgorithm {
public:
    /// Set crop margin in pixels (removed from each edge). 0 = no crop (full image).
    void setCropMargin(int pixels);

    /// Set center crop target size in pixels. Each tile is cropped to a square
    /// of this size from its center (like stitch.py crop_size=1775). 0 = disabled.
    void setCenterCropSize(int pixels);

    cv::Mat stitch(const std::vector<cv::Mat>& images,
                   const cv::Size& gridSize) override;

    /// Direct position-based stitching (like docs/stitch.py).
    /// Each image is placed at its (row, col) grid position regardless of input order.
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const cv::Size& gridSize);

private:
    int cropMargin_ = 0;    // pixels to crop from each edge
    int centerCropSize_ = 0; // center-crop target size (0 = disabled)

    /// Compute output tile size based on current crop settings.
    /// Priority: centerCropSize_ > cropMargin_ > full image
    void computeTileSize(const cv::Size& imgSize, int& tileW, int& tileH) const;

    /// Compute the crop ROI for a source image based on current settings.
    /// When centerCropSize_ > 0 and position info is provided, uses edge-aware
    /// cropping: boundary images preserve the outward-facing edge, interior
    /// images use center crop. Falls back to uniform center crop when row/col
    /// are -1 or grid size is unknown.
    cv::Rect computeCropRoi(const cv::Size& imgSize, int tileW, int tileH,
                            int row = -1, int col = -1,
                            int gridRows = -1, int gridCols = -1,
                            int ox = 0, int oy = 0) const;

    /// Crop and optionally resize a tile to the target dimensions.
    /// When row/col/gridSize are provided, uses edge-aware cropping.
    cv::Mat cropTile(const cv::Mat& src, int tileW, int tileH,
                     int row = -1, int col = -1,
                     int gridRows = -1, int gridCols = -1,
                     int ox = 0, int oy = 0) const;
};

} // namespace stitch
