#pragma once

#include "IStitchAlgorithm.h"
#include <opencv2/imgproc.hpp>
#ifdef HAVE_OPENCV_CUDA
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudawarping.hpp>
#endif
#include <spdlog/spdlog.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <cmath>
#include <vector>

namespace stitch {

/// Algorithm 4 — advanced grid stitching with Z correction coefficient,
/// per-cell crop offset, and cosine-weighted seam feathering.
///
/// Extends the Z-scale + feather-blend pipeline from SeamFeatherStitchAlgorithm
/// with two new features:
///   1. z_correction_coef_ : global multiplier on the Z-based scale factor
///   2. Per-cell (ox, oy) crop offset : shifts the center-crop ROI per grid cell
///
/// Scale mode behavior (DIFFERENT from algos 2/3):
///   Mode 0: scale = (zRef / z_i) × z_correction_coef_
///   Mode 1: scale = scale_map[row][col] × (zRef / z_i) × z_correction_coef_  (STACKING)
///
/// Crop offset is applied AFTER Z-scaling but BEFORE feather-blend expansion.
/// The tile remains at its fixed grid position on the canvas; only the content
/// inside the tile is shifted.
class AdvancedGridStitchAlgorithm : public IStitchAlgorithm {
public:
    void setFeatherWidth(int pixels)       { featherWidth_ = pixels; }
    void setCenterCropSize(int pixels)     { centerCropSize_ = pixels; }
    void setScaleMode(int mode)            { scale_mode_ = (mode == 1) ? 1 : 0; }
    void setScaleMapFile(const std::string& path) { loadScaleMap(path); }
    void setZCorrectionCoef(double coef)   { z_correction_coef_ = (coef > 0.0) ? coef : 1.0; }
    void setCropOffsetFile(const std::string& path) { loadCropOffset(path); }

    cv::Mat stitch(const std::vector<cv::Mat>& images,
                   const cv::Size& gridSize) override
    {
        // No Z data — build positions and delegate
        std::vector<std::pair<int, int>> positions;
        positions.reserve(images.size());
        for (int i = 0; i < static_cast<int>(images.size()); ++i) {
            positions.emplace_back(i / gridSize.width, i % gridSize.width);
        }
        return stitchWithPositions(images, positions, {}, gridSize);
    }

    /// Position-based stitching (no Z data).
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const cv::Size& gridSize)
    {
        return stitchWithPositions(images, positions, {}, gridSize);
    }

    /// Position-based stitching with Z-scale + crop offset + feather blending.
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const std::vector<int>& zValues,
                                const cv::Size& gridSize)
    {
        if (images.empty() || (!positions.empty() && images.size() != positions.size())) {
            reportStatus("Invalid input: images and positions size mismatch");
            return {};
        }
        if (!zValues.empty() && images.size() != zValues.size()) {
            reportStatus("Invalid input: images and zValues size mismatch");
            return {};
        }

        const bool hasZ = !zValues.empty();
        try {
            reportStatus("Advanced-grid stitching " + std::to_string(images.size()) +
                         " images (" + std::to_string(gridSize.width) + "x" +
                         std::to_string(gridSize.height) + ")" +
                         (hasZ ? " with Z-scale + crop-offset" : ""));
            reportProgress(0, 100);

            int zRef = 0;
            if (hasZ) {
                zRef = computeMedianZ(zValues);
                SPDLOG_INFO("AdvancedGrid: zRef={} (median of {} values), z_correction_coef={}",
                            zRef, zValues.size(), z_correction_coef_);
            }

            cv::Size refSize = images[0].size();
            cv::Mat result = stitchImpl(images, positions, zValues, zRef, refSize, gridSize);
            reportProgress(100, 100);
            reportStatus("Advanced-grid stitching completed");
            return result;
        } catch (const std::exception& e) {
            reportStatus(std::string("Advanced-grid stitching error: ") + e.what());
            reportProgress(100, 100);
            return {};
        }
    }

private:
    int featherWidth_ = 120;
    int centerCropSize_ = 0;
    int scale_mode_ = 0;                          // 0=Z-based, 1=manual scale-map (stacking)
    double z_correction_coef_ = 1.0;              // global multiplier on Z-based scale
    std::vector<std::vector<double>> scale_map_;  // manual scale values [row][col]
    std::vector<std::vector<int>> ox_values_;     // per-cell X crop offset (pixels)
    std::vector<std::vector<int>> oy_values_;     // per-cell Y crop offset (pixels)

    /// Load manual scale-map from JSON file (same format as algos 2/3).
    void loadScaleMap(const std::string& path) {
        scale_map_.clear();
        if (path.empty()) return;
        try {
            QFile file(QString::fromStdString(path));
            if (!file.open(QIODevice::ReadOnly)) {
                SPDLOG_ERROR("AdvancedGrid: cannot open scale-map file: {}", path);
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            if (!doc.isObject() || !doc["scale_values"].isArray()) {
                SPDLOG_ERROR("AdvancedGrid: scale-map missing 'scale_values' array");
                return;
            }
            QJsonArray rows = doc["scale_values"].toArray();
            scale_map_.reserve(rows.size());
            for (int r = 0; r < rows.size(); ++r) {
                if (!rows[r].isArray()) { scale_map_.clear(); return; }
                QJsonArray cols = rows[r].toArray();
                std::vector<double> rowVals;
                rowVals.reserve(cols.size());
                for (int c = 0; c < cols.size(); ++c)
                    rowVals.push_back(cols[c].toDouble());
                scale_map_.push_back(std::move(rowVals));
            }
            SPDLOG_INFO("AdvancedGrid: scale-map loaded {} x {}", scale_map_.size(),
                        scale_map_.empty() ? 0 : scale_map_[0].size());
        } catch (const std::exception& e) {
            SPDLOG_ERROR("AdvancedGrid: failed to parse scale-map: {}", e.what());
            scale_map_.clear();
        }
    }

    /// Load per-cell crop offset from JSON file.
    /// Format: {"ox_values": [[0,0,...],...], "oy_values": [[0,0,...],...]}
    /// This matches the existing z_values / scale_values 2D numeric array pattern.
    void loadCropOffset(const std::string& path) {
        ox_values_.clear();
        oy_values_.clear();
        if (path.empty()) return;
        try {
            QFile file(QString::fromStdString(path));
            if (!file.open(QIODevice::ReadOnly)) {
                SPDLOG_ERROR("AdvancedGrid: cannot open crop-offset file: {}", path);
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            if (!doc.isObject()) {
                SPDLOG_ERROR("AdvancedGrid: crop-offset file is not a JSON object");
                return;
            }
            QJsonObject root = doc.object();

            // Read ox_values
            if (root["ox_values"].isArray()) {
                QJsonArray rows = root["ox_values"].toArray();
                ox_values_.reserve(rows.size());
                for (int r = 0; r < rows.size(); ++r) {
                    if (!rows[r].isArray()) { ox_values_.clear(); return; }
                    QJsonArray cols = rows[r].toArray();
                    std::vector<int> rowVals;
                    rowVals.reserve(cols.size());
                    for (int c = 0; c < cols.size(); ++c)
                        rowVals.push_back(cols[c].toInt());
                    ox_values_.push_back(std::move(rowVals));
                }
            }

            // Read oy_values
            if (root["oy_values"].isArray()) {
                QJsonArray rows = root["oy_values"].toArray();
                oy_values_.reserve(rows.size());
                for (int r = 0; r < rows.size(); ++r) {
                    if (!rows[r].isArray()) { oy_values_.clear(); return; }
                    QJsonArray cols = rows[r].toArray();
                    std::vector<int> rowVals;
                    rowVals.reserve(cols.size());
                    for (int c = 0; c < cols.size(); ++c)
                        rowVals.push_back(cols[c].toInt());
                    oy_values_.push_back(std::move(rowVals));
                }
            }

            SPDLOG_INFO("AdvancedGrid: crop-offset loaded {}x{}, {}x{}",
                        ox_values_.size(), ox_values_.empty() ? 0 : ox_values_[0].size(),
                        oy_values_.size(), oy_values_.empty() ? 0 : oy_values_[0].size());
        } catch (const std::exception& e) {
            SPDLOG_ERROR("AdvancedGrid: failed to parse crop-offset: {}", e.what());
            ox_values_.clear();
            oy_values_.clear();
        }
    }

    /// Get the scale factor for image at (row, col).
    /// Mode 0: (zRef / z_i) × z_correction_coef_
    /// Mode 1: scale_map[row][col] × (zRef / z_i) × z_correction_coef_  (STACKING)
    double getScaleFactor(int row, int col, int z_i, int zRef) const {
        double baseScale = 1.0;
        if (zRef > 0 && z_i > 0)
            baseScale = static_cast<double>(zRef) / static_cast<double>(z_i);

        double result = baseScale * z_correction_coef_;

        if (scale_mode_ == 1) {
            // Manual scale-map: STACK (multiply) on top of Z-based scale
            if (row >= 0 && row < static_cast<int>(scale_map_.size()) &&
                col >= 0 && col < static_cast<int>(scale_map_[row].size())) {
                result *= scale_map_[row][col];
            } else {
                SPDLOG_WARN("AdvancedGrid: scale-map missing entry [{},{}]", row, col);
            }
        }

        // Clamp to prevent extreme values
        if (result < 0.1) result = 0.1;
        if (result > 10.0) result = 10.0;
        return result;
    }

    /// Compute median Z from non-zero values.
    static int computeMedianZ(const std::vector<int>& zValues) {
        if (zValues.empty()) return 0;
        std::vector<int> nonzero;
        nonzero.reserve(zValues.size());
        for (int z : zValues) {
            if (z > 0) nonzero.push_back(z);
        }
        if (nonzero.empty()) return 0;
        std::sort(nonzero.begin(), nonzero.end());
        return nonzero[nonzero.size() / 2];
    }

    /// Scale image by factor; returns the scaled Mat (or original if scale≈1).
    static cv::Mat scaleImage(const cv::Mat& src, double scale, cv::Mat& dst) {
        if (std::abs(scale - 1.0) <= 1e-6) {
            dst = src;
            return src;
        }
        int sw = static_cast<int>(std::round(src.cols * scale));
        int sh = static_cast<int>(std::round(src.rows * scale));
        cv::resize(src, dst, cv::Size(sw, sh), 0, 0, cv::INTER_LINEAR);
        return dst;
    }

    /// Scale by getScaleFactor, then interpolate back to original image size.
    /// This ensures all tiles have uniform pixel dimensions after Z-correction.
    cv::Mat scaleToUniform(const cv::Mat& src, int row, int col, int z_i, int zRef) const {
        double s = getScaleFactor(row, col, z_i, zRef);
        if (std::abs(s - 1.0) <= 1e-6) return src;
        cv::Mat scaled, result;
        scaleImage(src, s, scaled);
        cv::resize(scaled, result, src.size(), 0, 0, cv::INTER_LINEAR);
        return result;
    }

    /// Compute output tile size based on center_crop_size only.
    void computeTileSize(const cv::Size& imgSize, int& tileW, int& tileH) const {
        if (centerCropSize_ > 0) {
            int size = centerCropSize_;
            if (size >= imgSize.width || size >= imgSize.height) {
                size = std::min(imgSize.width, imgSize.height);
            }
            tileW = size;
            tileH = size;
        } else {
            tileW = imgSize.width;
            tileH = imgSize.height;
        }
    }

    /// Compute the crop ROI with per-cell offset applied.
    /// The offset shifts the center point; the ROI is then clamped to image bounds.
    cv::Rect computeCropRoi(const cv::Size& imgSize, int tileW, int tileH,
                            int ox, int oy) const
    {
        int left = (imgSize.width  - tileW) / 2 + ox;
        int top  = (imgSize.height - tileH) / 2 + oy;

        // Clamp to image bounds
        left = std::max(0, std::min(left, imgSize.width  - tileW));
        top  = std::max(0, std::min(top,  imgSize.height - tileH));

        return cv::Rect(left, top, tileW, tileH);
    }

    /// Build a cosine feather mask for a single tile.
    ///
    /// hasLeft  : left  edge fades 0→1 (fade in  from left neighbor)
    /// hasRight : right edge fades 1→0 (fade out to   right neighbor)
    /// hasTop   : top   edge fades 0→1
    /// hasBottom: bottom edge fades 1→0
    ///
    /// Adjacent tile masks are complementary, so seam weights sum to 1.0.
    cv::Mat buildFeatherMask(int h, int w,
                             bool hasLeft, bool hasRight,
                             bool hasTop, bool hasBottom,
                             int fw) const
    {
        cv::Mat mask(h, w, CV_32FC1, cv::Scalar(1.0f));

        auto makeFade = [](int len) -> cv::Mat {
            cv::Mat fade(1, len, CV_32FC1);
            float* p = fade.ptr<float>(0);
            if (len <= 1) {
                p[0] = 1.0f;
                return fade;
            }
            for (int i = 0; i < len; ++i) {
                float t = static_cast<float>(i) / (len - 1);
                p[i] = 0.5f - 0.5f * std::cos(static_cast<float>(CV_PI) * t);
            }
            return fade;
        };

        if (hasLeft) {
            int fw_h = std::min(fw, w);
            cv::Mat fade = makeFade(fw_h);
            cv::Mat roi = mask(cv::Rect(0, 0, fw_h, h));
            cv::Mat fade2D;
            cv::repeat(fade, h, 1, fade2D);
            cv::multiply(roi, fade2D, roi);
        }

        if (hasRight) {
            int fw_h = std::min(fw, w);
            cv::Mat fade = makeFade(fw_h);
            cv::flip(fade, fade, 1);
            cv::Mat roi = mask(cv::Rect(w - fw_h, 0, fw_h, h));
            cv::Mat fade2D;
            cv::repeat(fade, h, 1, fade2D);
            cv::multiply(roi, fade2D, roi);
        }

        if (hasTop) {
            int fw_v = std::min(fw, h);
            cv::Mat fade = makeFade(fw_v);
            cv::Mat roi = mask(cv::Rect(0, 0, w, fw_v));
            cv::Mat fade2D;
            cv::repeat(fade.t(), w, 1, fade2D);
            cv::multiply(roi, fade2D, roi);
        }

        if (hasBottom) {
            int fw_v = std::min(fw, h);
            cv::Mat fade = makeFade(fw_v);
            cv::flip(fade, fade, 1);
            cv::Mat roi = mask(cv::Rect(0, h - fw_v, w, fw_v));
            cv::Mat fade2D;
            cv::repeat(fade.t(), w, 1, fade2D);
            cv::multiply(roi, fade2D, roi);
        }

        return mask;
    }

    /// Center-crop (or pad) a source tile to exact dimensions.
    /// The source ROI is computed from a shifted center point.
    cv::Mat cropTile(const cv::Mat& src, int tileW, int tileH, int ox, int oy) const {
        cv::Rect roi = computeCropRoi(cv::Size(src.cols, src.rows), tileW, tileH, ox, oy);

        // Clamp to source bounds
        roi.x = std::max(0, roi.x);
        roi.y = std::max(0, roi.y);
        roi.width  = std::min(roi.width,  src.cols - roi.x);
        roi.height = std::min(roi.height, src.rows - roi.y);

        cv::Mat cropped;
        if (roi.width == tileW && roi.height == tileH) {
            cropped = src(roi).clone();
        } else {
            // Pad to target size, centered
            cropped = cv::Mat(tileH, tileW, src.type(), cv::Scalar(0, 0, 0));
            int px = (tileW - roi.width)  / 2;
            int py = (tileH - roi.height) / 2;
            src(roi).copyTo(cropped(cv::Rect(px, py, roi.width, roi.height)));
        }
        return cropped;
    }

    /// Core implementation: Z-scale → crop offset → feather blend → grid placement.
    ///
    /// Processing order per image:
    ///   1. Z-scale to uniform size (scaleToUniform)
    ///   2. Apply per-cell crop offset + center-crop to tileW×tileH
    ///   3. Expand crop by featherWidth for overlapping regions
    ///   4. Build cosine feather mask
    ///   5. Accumulate on canvas with weighted blending
    cv::Mat stitchImpl(const std::vector<cv::Mat>& images,
                       const std::vector<std::pair<int, int>>& positions,
                       const std::vector<int>& zValues,
                       int zRef,
                       const cv::Size& refSize,
                       const cv::Size& gridSize)
    {
        const int nRows = gridSize.height;
        const int nCols = gridSize.width;
        const int fw = featherWidth_;
        const bool hasZ = !zValues.empty() && zRef > 0;

        // ── Tile size from original image dimensions (uniform after interpolation) ──
        int tileW, tileH;
        computeTileSize(refSize, tileW, tileH);

        // ── Canvas dimensions ──
        int canvasW = nCols * tileW;
        int canvasH = nRows * tileH;

        SPDLOG_INFO("AdvancedGrid: grid={}x{} tile={}x{} canvas={}x{} fw={} hasZ={} coef={}",
                    nCols, nRows, tileW, tileH, canvasW, canvasH, fw, hasZ, z_correction_coef_);

        // ── Helper: get crop offset for a grid cell ──
        auto getOffset = [&](int row, int col, int& ox, int& oy) {
            ox = 0; oy = 0;
            if (row >= 0 && row < static_cast<int>(ox_values_.size()) &&
                col >= 0 && col < static_cast<int>(ox_values_[row].size())) {
                ox = ox_values_[row][col];
            }
            if (row >= 0 && row < static_cast<int>(oy_values_.size()) &&
                col >= 0 && col < static_cast<int>(oy_values_[row].size())) {
                oy = oy_values_[row][col];
            }
        };

        // If feather width is 0, fall back to simple copyTo
        if (fw <= 0) {
            cv::Mat result(canvasH, canvasW, images[0].type(), cv::Scalar(0, 0, 0));
            for (size_t i = 0; i < images.size(); ++i) {
                int row, col;
                if (!positions.empty()) {
                    row = positions[i].first; col = positions[i].second;
                } else {
                    row = static_cast<int>(i) / nCols; col = static_cast<int>(i) % nCols;
                }
                if (row < 0 || row >= nRows || col < 0 || col >= nCols) continue;

                int ox, oy;
                getOffset(row, col, ox, oy);

                cv::Mat uniform = scaleToUniform(images[i], row, col,
                    hasZ ? zValues[i] : 0, zRef);
                cv::Mat tile = cropTile(uniform, tileW, tileH, ox, oy);
                cv::Rect dest(col * tileW, row * tileH, tileW, tileH);
                tile.copyTo(result(dest));
                int pct = 20 + static_cast<int>((i * 80) / images.size());
                reportProgress(pct, 100);
            }
            return result;
        }

#ifdef HAVE_OPENCV_CUDA
        // ── GPU weighted accumulation ──
        cv::cuda::GpuMat gpuAccSum(canvasH, canvasW, CV_32FC3);
        cv::cuda::GpuMat gpuAccWeight(canvasH, canvasW, CV_32FC1);
        gpuAccSum.setTo(cv::Scalar(0, 0, 0));
        gpuAccWeight.setTo(cv::Scalar(0.0f));

        cv::cuda::GpuMat gpuTile, gpuTileFloat, gpuMask, gpuMask3, gpuWeighted;
        cv::cuda::GpuMat gpuTmpSum, gpuTmpW;

        for (size_t i = 0; i < images.size(); ++i) {
            int row, col;
            if (!positions.empty()) {
                row = positions[i].first; col = positions[i].second;
            } else {
                row = static_cast<int>(i) / nCols; col = static_cast<int>(i) % nCols;
            }
            if (row < 0 || row >= nRows || col < 0 || col >= nCols) {
                SPDLOG_WARN("AdvancedGrid: position [{},{}] out of grid bounds, skipping", row, col);
                continue;
            }

            int ox, oy;
            getOffset(row, col, ox, oy);

            // ── 1. Z-scale → interpolate to original size (uniform dimensions) ──
            cv::Mat uniform = scaleToUniform(images[i], row, col,
                hasZ ? zValues[i] : 0, zRef);

            // ── 2. Compute base crop ROI with per-cell offset ──
            cv::Rect baseCrop = computeCropRoi(cv::Size(uniform.cols, uniform.rows),
                                               tileW, tileH, ox, oy);

            // ── 3. Adjacency ──
            bool hasLeft   = (col > 0);
            bool hasRight  = (col < nCols - 1);
            bool hasTop     = (row > 0);
            bool hasBottom = (row < nRows - 1);

            // ── 4. Expanded crop (includes feather overlap zones) ──
            int cropLeft   = baseCrop.x - (hasLeft   ? fw : 0);
            int cropTop    = baseCrop.y - (hasTop    ? fw : 0);
            int cropRight  = baseCrop.x + baseCrop.width  + (hasRight  ? fw : 0);
            int cropBottom = baseCrop.y + baseCrop.height + (hasBottom ? fw : 0);

            // Clamp to source bounds
            cropLeft   = std::max(0, cropLeft);
            cropTop    = std::max(0, cropTop);
            cropRight  = std::min(uniform.cols, cropRight);
            cropBottom = std::min(uniform.rows, cropBottom);

            cv::Mat tile = uniform(cv::Rect(cropLeft, cropTop,
                                           cropRight - cropLeft,
                                           cropBottom - cropTop));
            int th = tile.rows;
            int tw = tile.cols;

            // ── 5. Build feather mask ──
            cv::Mat mask = buildFeatherMask(th, tw, hasLeft, hasRight, hasTop, hasBottom, fw);

            // ── 6. Canvas placement (overlap into neighbor cells) ──
            int x0 = col * tileW - (hasLeft   ? fw : 0);
            int y0 = row * tileH - (hasTop    ? fw : 0);
            int x1 = x0 + tw;
            int y1 = y0 + th;

            // Clip to canvas
            int sx0 = 0, sy0 = 0;
            if (x0 < 0) { sx0 = -x0; x0 = 0; }
            if (y0 < 0) { sy0 = -y0; y0 = 0; }
            if (x1 > canvasW) { tw -= x1 - canvasW; x1 = canvasW; }
            if (y1 > canvasH) { th -= y1 - canvasH; y1 = canvasH; }
            if (sx0 >= tw || sy0 >= th) continue;

            cv::Rect canvasRoi(x0, y0, tw - sx0, th - sy0);
            cv::Rect tileRoi(sx0, sy0, tw - sx0, th - sy0);

            // ── GPU accumulation ──
            cv::Mat tileCpu = tile(tileRoi);
            cv::Mat maskCpu = mask(tileRoi);

            gpuTile.upload(tileCpu);
            gpuTile.convertTo(gpuTileFloat, CV_32FC3);
            gpuMask.upload(maskCpu);

            std::vector<cv::cuda::GpuMat> maskChs{gpuMask, gpuMask, gpuMask};
            cv::cuda::merge(maskChs, gpuMask3);
            cv::cuda::multiply(gpuTileFloat, gpuMask3, gpuWeighted);

            cv::cuda::GpuMat gpuAccRoi = gpuAccSum(canvasRoi);
            cv::cuda::add(gpuAccRoi, gpuWeighted, gpuTmpSum);
            gpuTmpSum.copyTo(gpuAccRoi);

            cv::cuda::GpuMat gpuWeightRoi = gpuAccWeight(canvasRoi);
            cv::cuda::add(gpuWeightRoi, gpuMask, gpuTmpW);
            gpuTmpW.copyTo(gpuWeightRoi);

            int pct = 20 + static_cast<int>((i * 80) / images.size());
            reportProgress(pct, 100);
        }

        // ── GPU normalize ──
        cv::cuda::GpuMat gpuWeight3, gpuResult;
        std::vector<cv::cuda::GpuMat> wChs{gpuAccWeight, gpuAccWeight, gpuAccWeight};
        cv::cuda::merge(wChs, gpuWeight3);
        cv::cuda::max(gpuWeight3, 1e-8, gpuWeight3);
        cv::cuda::divide(gpuAccSum, gpuWeight3, gpuAccSum);
        gpuAccSum.convertTo(gpuResult, images[0].type());

        cv::Mat result;
        gpuResult.download(result);
#else
        // ── CPU weighted accumulation (fallback when CUDA unavailable) ──
        cv::Mat accSum(canvasH, canvasW, CV_32FC3, cv::Scalar(0, 0, 0));
        cv::Mat accWeight(canvasH, canvasW, CV_32FC1, cv::Scalar(0.0f));

        for (size_t i = 0; i < images.size(); ++i) {
            int row, col;
            if (!positions.empty()) {
                row = positions[i].first; col = positions[i].second;
            } else {
                row = static_cast<int>(i) / nCols; col = static_cast<int>(i) % nCols;
            }
            if (row < 0 || row >= nRows || col < 0 || col >= nCols) {
                SPDLOG_WARN("AdvancedGrid: position [{},{}] out of grid bounds, skipping", row, col);
                continue;
            }

            int ox, oy;
            getOffset(row, col, ox, oy);

            // ── 1. Z-scale → interpolate to original size ──
            cv::Mat uniform = scaleToUniform(images[i], row, col,
                hasZ ? zValues[i] : 0, zRef);

            // ── 2. Compute base crop ROI with per-cell offset ──
            cv::Rect baseCrop = computeCropRoi(cv::Size(uniform.cols, uniform.rows),
                                               tileW, tileH, ox, oy);

            // ── 3. Adjacency ──
            bool hasLeft   = (col > 0);
            bool hasRight  = (col < nCols - 1);
            bool hasTop     = (row > 0);
            bool hasBottom = (row < nRows - 1);

            // ── 4. Expanded crop ──
            int cropLeft   = baseCrop.x - (hasLeft   ? fw : 0);
            int cropTop    = baseCrop.y - (hasTop    ? fw : 0);
            int cropRight  = baseCrop.x + baseCrop.width  + (hasRight  ? fw : 0);
            int cropBottom = baseCrop.y + baseCrop.height + (hasBottom ? fw : 0);

            cropLeft   = std::max(0, cropLeft);
            cropTop    = std::max(0, cropTop);
            cropRight  = std::min(uniform.cols, cropRight);
            cropBottom = std::min(uniform.rows, cropBottom);

            cv::Mat tile = uniform(cv::Rect(cropLeft, cropTop,
                                           cropRight - cropLeft,
                                           cropBottom - cropTop));
            int th = tile.rows;
            int tw = tile.cols;

            // ── 5. Build feather mask ──
            cv::Mat mask = buildFeatherMask(th, tw, hasLeft, hasRight, hasTop, hasBottom, fw);

            // ── 6. Canvas placement ──
            int x0 = col * tileW - (hasLeft   ? fw : 0);
            int y0 = row * tileH - (hasTop    ? fw : 0);
            int x1 = x0 + tw;
            int y1 = y0 + th;

            int sx0 = 0, sy0 = 0;
            if (x0 < 0) { sx0 = -x0; x0 = 0; }
            if (y0 < 0) { sy0 = -y0; y0 = 0; }
            if (x1 > canvasW) { tw -= x1 - canvasW; x1 = canvasW; }
            if (y1 > canvasH) { th -= y1 - canvasH; y1 = canvasH; }
            if (sx0 >= tw || sy0 >= th) continue;

            cv::Rect canvasRoi(x0, y0, tw - sx0, th - sy0);
            cv::Rect tileRoi(sx0, sy0, tw - sx0, th - sy0);

            cv::Mat tileCpu = tile(tileRoi);
            cv::Mat maskCpu = mask(tileRoi);

            cv::Mat tileFloat, mask3, weighted;
            tileCpu.convertTo(tileFloat, CV_32FC3);
            std::vector<cv::Mat> maskChs{maskCpu, maskCpu, maskCpu};
            cv::merge(maskChs, mask3);
            cv::multiply(tileFloat, mask3, weighted);

            cv::Mat accRoi = accSum(canvasRoi);
            cv::add(accRoi, weighted, accRoi);

            cv::Mat weightRoi = accWeight(canvasRoi);
            cv::add(weightRoi, maskCpu, weightRoi);

            int pct = 20 + static_cast<int>((i * 80) / images.size());
            reportProgress(pct, 100);
        }

        // ── CPU normalize ──
        cv::Mat weight3, resultFloat;
        std::vector<cv::Mat> wChs{accWeight, accWeight, accWeight};
        cv::merge(wChs, weight3);
        cv::max(weight3, 1e-8, weight3);
        cv::divide(accSum, weight3, resultFloat);

        cv::Mat result;
        resultFloat.convertTo(result, images[0].type());
#endif

        return result;
    }
};

} // namespace stitch
