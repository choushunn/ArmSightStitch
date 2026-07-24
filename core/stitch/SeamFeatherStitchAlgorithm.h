#pragma once

#include "IStitchAlgorithm.h"
#include "infra/report/EdgeCrop.h"
#include <opencv2/imgproc.hpp>
#include <string>
#include <utility>
#include <vector>

namespace stitch {

/// Algorithm 3 — grid-based stitching with seam feathering (cosine-weighted blending)
/// + optional Z correction coefficient, per-cell crop offset, and scale stacking.
///
/// Like GridStitchAlgorithm, tiles are placed at fixed grid positions, but instead
/// of hard-edge copyTo(), adjacent tiles overlap by featherWidth_ pixels and are
/// blended with a cosine gradient mask so that seam weights sum to 1.0.
///
/// When Z values are provided, each image is pre-scaled by zRef/z_i to compensate
/// for magnification differences caused by varying Z-axis heights during scanning.
///
/// Optional advanced features (from former AdvancedGridStitchAlgorithm):
///   z_correction_coef_ : global multiplier on Z-based scale factor (default 1.0)
///   ox_values_/oy_values_ : per-cell crop offset loaded from JSON (empty = no offset)
///   scale_stack_ : when true, scale_mode=1 stacks manual scale-map on top of Z-based
///                  scale instead of replacing it (default false = replacement)
class SeamFeatherStitchAlgorithm : public IStitchAlgorithm {
public:
    void setFeatherWidth(int pixels);
    void setCenterCropSize(int pixels);
    void setCropMargin(int pixels);
    void setScaleMode(int mode);
    void setScaleMapFile(const std::string& path);

    /// Z correction coefficient: global multiplier on Z-based scale factor.
    /// Default 1.0 = no correction.  <1 reduces magnification, >1 increases it.
    void setZCorrectionCoef(double coef);

    /// Per-cell crop offset JSON file (format: {"ox_values":[[...],...], "oy_values":[[...],...]}).
    /// Empty string = no per-cell offset.
    void setCropOffsetFile(const std::string& path);

    /// When true, scale_mode=1 stacks manual scale-map on top of Z-based scale
    /// (scale = scale_map[row][col] x zRef/z_i x z_correction_coef_).
    /// When false (default), scale_mode=1 replaces Z-based scale entirely
    /// (scale = scale_map[row][col]).
    void setScaleStackMode(bool stack);

    cv::Mat stitch(const std::vector<cv::Mat>& images,
                   const cv::Size& gridSize) override;

    /// Position-based stitching with seam feathering (no Z data).
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const cv::Size& gridSize);

    /// Position-based stitching with Z-scale compensation + seam feathering
    /// + optional crop offset.
    /// Z values are per-image Z-axis heights in pulses; empty vector = no scaling.
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const std::vector<int>& zValues,
                                const cv::Size& gridSize);

private:
    int featherWidth_ = 120;
    int centerCropSize_ = 0;
    int cropMargin_ = 0;
    int scale_mode_ = 0;                          // 0=Z-based, 1=manual scale-map
    double z_correction_coef_ = 1.0;              // global multiplier on Z-based scale
    bool scale_stack_ = false;                    // true = scale_map stacks on Z-based; false = replaces
    std::vector<std::vector<double>> scale_map_;  // manual scale values [row][col]
    std::vector<std::vector<int>> ox_values_;     // per-cell X crop offset (pixels)
    std::vector<std::vector<int>> oy_values_;     // per-cell Y crop offset (pixels)

    /// Load manual scale-map from JSON file.
    void loadScaleMap(const std::string& path);

    /// Load per-cell crop offset from JSON file.
    /// Format: {"ox_values": [[0,0,...],...], "oy_values": [[0,0,...],...]}
    void loadCropOffset(const std::string& path);

    /// Get the scale factor for image at (row, col).
    /// Mode 0: zRef/z_i x z_correction_coef_
    /// Mode 1 (non-stacking): scale_map[row][col]  (replacement, backward compat with algo3)
    /// Mode 1 (stacking): scale_map[row][col] x zRef/z_i x z_correction_coef_  (algo4 behavior)
    double getScaleFactor(int row, int col, int z_i, int zRef) const;

    /// Compute median Z from non-zero values.
    static int computeMedianZ(const std::vector<int>& zValues);

    /// Find index of the Z value closest to zRef.
    static int findClosestZIndex(const std::vector<int>& zValues, int zRef);

    /// Scale image by factor; returns the scaled Mat (or original if scale ~= 1).
    static cv::Mat scaleImage(const cv::Mat& src, double scale, cv::Mat& dst);

    /// Scale by getScaleFactor, then interpolate back to original image size.
    /// This ensures all tiles have uniform pixel dimensions after Z-correction.
    cv::Mat scaleToUniform(const cv::Mat& src, int row, int col, int z_i, int zRef) const;

    /// Compute output tile size based on current crop settings.
    void computeTileSize(const cv::Size& imgSize, int& tileW, int& tileH) const;

    /// Compute the crop ROI with edge-aware positioning.
    /// When centerCropSize_ > 0, boundary images preserve outward edges.
    /// Falls back to uniform center crop when row/col are -1.
    /// Optional per-cell offset (ox, oy) from JSON is superimposed.
    cv::Rect computeCropRoi(const cv::Size& imgSize, int tileW, int tileH,
                            int row = -1, int col = -1,
                            int gridRows = -1, int gridCols = -1,
                            int ox = 0, int oy = 0) const;

    /// Re-scale boundary images to uniform size and apply edge expansion
    /// to append center-crop cut-off strips to the mosaic.
    cv::Mat expandEdgesWithPositions(
        cv::Mat result,
        const std::vector<cv::Mat>& images,
        const std::vector<std::pair<int,int>>& positions,
        const std::vector<int>& zValues, int zRef, bool hasZ,
        const cv::Size& gridSize, const cv::Size& refSize,
        int tileW, int tileH);

    /// Build a cosine feather mask for a single tile.
    ///
    /// hasLeft  : left  edge fades 0->1 (fade in  from left neighbor)
    /// hasRight : right edge fades 1->0 (fade out to   right neighbor)
    /// hasTop   : top   edge fades 0->1
    /// hasBottom: bottom edge fades 1->0
    ///
    /// Adjacent tile masks are complementary, so seam weights sum to 1.0.
    cv::Mat buildFeatherMask(int h, int w,
                             bool hasLeft, bool hasRight,
                             bool hasTop, bool hasBottom,
                             int fw) const;

    /// Center-crop (or pad) a source tile to exact dimensions for placement.
    /// Edge-aware: boundary images preserve outward edges when row/col provided.
    /// Optional per-cell offset (ox, oy) shifts the crop center.
    cv::Mat cropTile(const cv::Mat& src, int tileW, int tileH,
                     int row = -1, int col = -1,
                     int gridRows = -1, int gridCols = -1,
                     int ox = 0, int oy = 0) const;

    /// Core implementation: Z-scale -> crop offset -> feather blend -> grid placement.
    ///
    /// Processing order per image:
    ///   1. Z-scale to uniform size (scaleToUniform)
    ///   2. Apply per-cell crop offset (ox, oy) + center-crop to tileWxtileH
    ///   3. Expand crop by featherWidth for overlapping regions
    ///   4. Build cosine feather mask
    ///   5. Accumulate on canvas with weighted blending
    cv::Mat stitchImpl(const std::vector<cv::Mat>& images,
                       const std::vector<std::pair<int, int>>& positions,
                       const std::vector<int>& zValues,
                       int zRef,
                       const cv::Size& refSize,
                       const cv::Size& gridSize);
};

} // namespace stitch
