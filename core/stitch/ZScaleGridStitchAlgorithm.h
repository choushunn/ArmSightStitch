#pragma once

#include "IStitchAlgorithm.h"
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <string>
#include <utility>
#include <vector>

namespace stitch {

/// Algorithm 2 — Z-scale-aware grid stitching
///
/// Compensates for varying Z-axis heights during scanning by scaling each image
/// to match a reference Z (median of all Z values) before center-cropping and
/// tiling. This corrects the magnification differences caused by the camera
/// moving closer/further from the target as the Z-axis follows the spherical
/// cap compensation profile.
///
/// Z=80000 is closest to target (largest magnification); smaller Z = further away.
/// Scale factor: scale = Z_ref / Z_i  (clamped to avoid div-by-zero).
class ZScaleGridStitchAlgorithm : public IStitchAlgorithm {
public:
    void setCropMargin(int pixels);
    void setCenterCropSize(int pixels);
    void setScaleMode(int mode);
    void setScaleMapFile(const std::string& path);

    /// Standard stitch (no Z info) — falls back to plain grid tiling.
    cv::Mat stitch(const std::vector<cv::Mat>& images,
                   const cv::Size& gridSize) override;

    /// Z-aware position-based stitching.
    /// Each image is scaled by Z_ref / Z_i, center-cropped, and placed at its grid cell.
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const std::vector<int>& zValues,
                                const cv::Size& gridSize);

private:
    int cropMargin_ = 0;
    int centerCropSize_ = 0;
    int scale_mode_ = 0;
    std::vector<std::vector<double>> scale_map_;

    void loadScaleMap(const std::string& path);

    double getScaleFactor(int row, int col, int z_i, int zRef) const;

    /// Compute median Z from non-zero values.
    static int computeMedianZ(const std::vector<int>& zValues);

    /// Core implementation shared by stitch() and stitchWithPositions().
    cv::Mat stitchImpl(const std::vector<cv::Mat>& images,
                       const std::vector<std::pair<int, int>>& positions,
                       const std::vector<int>& zValues,
                       const cv::Size& gridSize);

    /// Compute output tile size based on current crop settings.
    void computeTileSize(const cv::Size& imgSize, int& tileW, int& tileH) const;

    /// Compute the crop ROI for a source image based on current settings.
    /// When centerCropSize_ > 0 and position info is provided, uses edge-aware
    /// cropping. Falls back to uniform center crop when row/col are -1.
    cv::Rect computeCropRoi(const cv::Size& imgSize, int tileW, int tileH,
                            int row = -1, int col = -1,
                            int gridRows = -1, int gridCols = -1,
                            int ox = 0, int oy = 0) const;

    /// Crop an image to the exact target tile size with edge-aware positioning.
    /// When row/col/gridSize are provided, boundary tiles preserve outward edges.
    /// If the image is smaller than the target, it is placed centered on a black canvas.
    cv::Mat cropTileToSize(const cv::Mat& src, int tileW, int tileH,
                           int row = -1, int col = -1,
                           int gridRows = -1, int gridCols = -1) const;
};

} // namespace stitch
