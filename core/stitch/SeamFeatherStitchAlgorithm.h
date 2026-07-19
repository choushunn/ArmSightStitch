#pragma once

#include "IStitchAlgorithm.h"
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <cmath>
#include <vector>

namespace stitch {

/// Algorithm 3 — grid-based stitching with seam feathering (cosine-weighted blending)
///
/// Like GridStitchAlgorithm, tiles are placed at fixed grid positions, but instead
/// of hard-edge copyTo(), adjacent tiles overlap by featherWidth_ pixels and are
/// blended with a cosine gradient mask so that seam weights sum to 1.0.
///
/// When Z values are provided, each image is pre-scaled by zRef/z_i to compensate
/// for magnification differences caused by varying Z-axis heights during scanning.
/// This combines the benefits of ZScaleGridStitchAlgorithm (magnification matching)
/// with seam feathering (smooth transitions).
class SeamFeatherStitchAlgorithm : public IStitchAlgorithm {
public:
    void setFeatherWidth(int pixels) { featherWidth_ = pixels; }
    void setCenterCropSize(int pixels) { centerCropSize_ = pixels; }
    void setCropMargin(int pixels) { cropMargin_ = pixels; }
    void setScaleMode(int mode) { scale_mode_ = (mode == 1) ? 1 : 0; }
    void setScaleMapFile(const std::string& path) { loadScaleMap(path); }

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

    /// Position-based stitching with seam feathering (no Z data).
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const cv::Size& gridSize)
    {
        return stitchWithPositions(images, positions, {}, gridSize);
    }

    /// Position-based stitching with Z-scale compensation + seam feathering.
    /// Z values are per-image Z-axis heights in pulses; empty vector = no scaling.
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
            reportStatus("Seam-feather stitching " + std::to_string(images.size()) +
                         " images (" + std::to_string(gridSize.width) + "x" +
                         std::to_string(gridSize.height) + ")" +
                         (hasZ ? " with Z-scale compensation" : ""));
            reportProgress(0, 100);

            // Compute reference Z for magnification compensation
            int zRef = 0;
            if (hasZ) {
                zRef = computeMedianZ(zValues);
                SPDLOG_INFO("SeamFeather: zRef={} (median of {} values)", zRef, zValues.size());
            }

            // Tile size is based on original image size.
            // After Z-scaling, each image is interpolated back to its original dimensions,
            // so all tiles have uniform pixel size and cover the same physical area.
            cv::Size refSize = images[0].size();
            cv::Mat result = stitchImpl(images, positions, zValues, zRef, refSize, gridSize);
            reportProgress(100, 100);
            reportStatus("Seam-feather stitching completed");
            return result;
        } catch (const std::exception& e) {
            reportStatus(std::string("Seam-feather stitching error: ") + e.what());
            reportProgress(100, 100);
            return {};
        }
    }

private:
    int featherWidth_ = 120;
    int centerCropSize_ = 0;
    int cropMargin_ = 0;
    int scale_mode_ = 0;                          // 0=Z-based, 1=manual scale-map
    std::vector<std::vector<double>> scale_map_;  // manual scale values [row][col]

    /// Load manual scale-map from JSON file.
    void loadScaleMap(const std::string& path) {
        scale_map_.clear();
        if (path.empty()) return;
        try {
            QFile file(QString::fromStdString(path));
            if (!file.open(QIODevice::ReadOnly)) {
                SPDLOG_ERROR("SeamFeather: cannot open scale-map file: {}", path);
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            if (!doc.isObject() || !doc["scale_values"].isArray()) {
                SPDLOG_ERROR("SeamFeather: scale-map missing 'scale_values' array");
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
            SPDLOG_INFO("SeamFeather: scale-map loaded {} x {}", scale_map_.size(),
                        scale_map_.empty() ? 0 : scale_map_[0].size());
        } catch (const std::exception& e) {
            SPDLOG_ERROR("SeamFeather: failed to parse scale-map: {}", e.what());
            scale_map_.clear();
        }
    }

    /// Get the scale factor for image at (row, col).
    /// Returns from manual scale-map if available, otherwise computes Z-based scale.
    double getScaleFactor(int row, int col, int z_i, int zRef) const {
        if (scale_mode_ == 1) {
            // Manual scale-map lookup
            if (row >= 0 && row < static_cast<int>(scale_map_.size()) &&
                col >= 0 && col < static_cast<int>(scale_map_[row].size())) {
                return scale_map_[row][col];
            }
            SPDLOG_WARN("SeamFeather: scale-map missing entry [{},{}]", row, col);
            return 1.0;
        }
        // Z-based auto-scaling
        if (zRef > 0 && z_i > 0)
            return static_cast<double>(zRef) / static_cast<double>(z_i);
        return 1.0;
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

    /// Find index of the Z value closest to zRef.
    static int findClosestZIndex(const std::vector<int>& zValues, int zRef) {
        int bestIdx = 0;
        int bestDiff = std::abs(zValues[0] - zRef);
        for (size_t i = 1; i < zValues.size(); ++i) {
            int diff = std::abs(zValues[i] - zRef);
            if (diff < bestDiff) { bestDiff = diff; bestIdx = static_cast<int>(i); }
        }
        return bestIdx;
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

    /// Compute output tile size based on current crop settings.
    void computeTileSize(const cv::Size& imgSize, int& tileW, int& tileH) const {
        if (centerCropSize_ > 0) {
            int size = centerCropSize_;
            if (size >= imgSize.width || size >= imgSize.height) {
                size = std::min(imgSize.width, imgSize.height);
            }
            tileW = size;
            tileH = size;
        } else if (cropMargin_ > 0) {
            int crop = cropMargin_;
            if (crop * 2 >= imgSize.width || crop * 2 >= imgSize.height) {
                crop = 0;
            }
            tileW = crop > 0 ? imgSize.width - crop * 2 : imgSize.width;
            tileH = crop > 0 ? imgSize.height - crop * 2 : imgSize.height;
        } else {
            tileW = imgSize.width;
            tileH = imgSize.height;
        }
    }

    /// Compute the crop ROI for a source image based on current settings.
    cv::Rect computeCropRoi(const cv::Size& imgSize, int tileW, int tileH) const {
        if (centerCropSize_ > 0 || cropMargin_ > 0) {
            if (centerCropSize_ > 0) {
                int left = (imgSize.width - tileW) / 2;
                int top  = (imgSize.height - tileH) / 2;
                return cv::Rect(left, top, tileW, tileH);
            } else {
                int crop = cropMargin_;
                return cv::Rect(crop, crop, tileW, tileH);
            }
        }
        return cv::Rect(0, 0, tileW, tileH);
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

        // Pre-compute 1D cosine fade: 0→1
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
            cv::Mat fade = makeFade(fw_h);          // 0→1
            cv::Mat roi = mask(cv::Rect(0, 0, fw_h, h));
            cv::Mat fade2D;
            cv::repeat(fade, h, 1, fade2D);
            cv::multiply(roi, fade2D, roi);
        }

        if (hasRight) {
            int fw_h = std::min(fw, w);
            cv::Mat fade = makeFade(fw_h);          // 0→1
            cv::flip(fade, fade, 1);                // 1→0
            cv::Mat roi = mask(cv::Rect(w - fw_h, 0, fw_h, h));
            cv::Mat fade2D;
            cv::repeat(fade, h, 1, fade2D);
            cv::multiply(roi, fade2D, roi);
        }

        if (hasTop) {
            int fw_v = std::min(fw, h);
            cv::Mat fade = makeFade(fw_v);          // 0→1
            cv::Mat roi = mask(cv::Rect(0, 0, w, fw_v));
            cv::Mat fade2D;
            cv::repeat(fade.t(), w, 1, fade2D);
            cv::multiply(roi, fade2D, roi);
        }

        if (hasBottom) {
            int fw_v = std::min(fw, h);
            cv::Mat fade = makeFade(fw_v);          // 0→1
            cv::flip(fade, fade, 1);                // 1→0
            cv::Mat roi = mask(cv::Rect(0, h - fw_v, w, fw_v));
            cv::Mat fade2D;
            cv::repeat(fade.t(), w, 1, fade2D);
            cv::multiply(roi, fade2D, roi);
        }

        return mask;
    }

    /// Center-crop (or pad) a source tile to exact dimensions for placement.
    /// Used as the base crop before feather expansion.
    cv::Mat cropTile(const cv::Mat& src, int tileW, int tileH) const {
        cv::Rect roi = computeCropRoi(cv::Size(src.cols, src.rows), tileW, tileH);

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
            int px = (tileW - roi.width) / 2;
            int py = (tileH - roi.height) / 2;
            src(roi).copyTo(cropped(cv::Rect(px, py, roi.width, roi.height)));
        }
        return cropped;
    }

    /// Core implementation: Z-scale → interpolate to original size → grid placement + feather.
    ///
    /// Each image is Z-scaled to match the reference magnification, then interpolated
    /// back to its original pixel dimensions. This ensures all tiles have the same pixel
    /// size and cover the same physical area — a prerequisite for uniform grid placement.
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

        SPDLOG_INFO("SeamFeather: grid={}x{} tile={}x{} canvas={}x{} fw={} hasZ={}",
                    nCols, nRows, tileW, tileH, canvasW, canvasH, fw, hasZ);

        // Base crop ROI on the uniform original size
        cv::Rect baseCrop = computeCropRoi(refSize, tileW, tileH);

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

                cv::Mat uniform = scaleToUniform(images[i], row, col,
                    hasZ ? zValues[i] : 0, zRef);
                cv::Mat tile = cropTile(uniform, tileW, tileH);
                cv::Rect dest(col * tileW, row * tileH, tileW, tileH);
                tile.copyTo(result(dest));
                int pct = 20 + static_cast<int>((i * 80) / images.size());
                reportProgress(pct, 100);
            }
            return result;
        }

        // ── Weighted accumulation ──
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
                SPDLOG_WARN("Position [{},{}] out of grid bounds, skipping", row, col);
                continue;
            }

            // ── Scale → interpolate to original size (uniform dimensions) ──
            cv::Mat uniform = scaleToUniform(images[i], row, col,
                hasZ ? zValues[i] : 0, zRef);

            // ── Adjacency ──
            bool hasLeft   = (col > 0);
            bool hasRight  = (col < nCols - 1);
            bool hasTop    = (row > 0);
            bool hasBottom = (row < nRows - 1);

            // ── Expanded crop in uniform-sized source image ──
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

            // ── Build feather mask ──
            cv::Mat mask = buildFeatherMask(th, tw, hasLeft, hasRight, hasTop, hasBottom, fw);

            // ── Canvas placement (overlap into neighbor cells) ──
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

            // ── Accumulate ──
            cv::Mat tileFloat;
            tile(tileRoi).convertTo(tileFloat, CV_32FC3);
            cv::Mat maskCrop = mask(tileRoi);

            cv::Mat mask3;
            cv::merge(std::vector<cv::Mat>{maskCrop.clone(), maskCrop.clone(), maskCrop.clone()}, mask3);
            cv::Mat weighted;
            cv::multiply(tileFloat, mask3, weighted);
            cv::add(accSum(canvasRoi), weighted, accSum(canvasRoi));
            cv::add(accWeight(canvasRoi), maskCrop, accWeight(canvasRoi));

            int pct = 20 + static_cast<int>((i * 80) / images.size());
            reportProgress(pct, 100);
        }

        // ── Normalize ──
        cv::Mat result(canvasH, canvasW, images[0].type());
        cv::Mat weight3;
        cv::merge(std::vector<cv::Mat>{
            accWeight.clone(), accWeight.clone(), accWeight.clone()
        }, weight3);
        cv::max(weight3, 1e-8, weight3);
        cv::divide(accSum, weight3, accSum);
        accSum.convertTo(result, images[0].type());

        return result;
    }
};

} // namespace stitch
