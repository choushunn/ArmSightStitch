#pragma once

#include "IStitchAlgorithm.h"
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
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
    void setCropMargin(int pixels) { cropMargin_ = pixels; }
    void setCenterCropSize(int pixels) { centerCropSize_ = pixels; }
    void setScaleMode(int mode) { scale_mode_ = (mode == 1) ? 1 : 0; }
    void setScaleMapFile(const std::string& path) { loadScaleMap(path); }

    /// Standard stitch (no Z info) — falls back to plain grid tiling.
    cv::Mat stitch(const std::vector<cv::Mat>& images,
                   const cv::Size& gridSize) override
    {
        return stitchImpl(images, {}, {}, gridSize);
    }

    /// Z-aware position-based stitching.
    /// Each image is scaled by Z_ref / Z_i, center-cropped, and placed at its grid cell.
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const std::vector<int>& zValues,
                                const cv::Size& gridSize)
    {
        return stitchImpl(images, positions, zValues, gridSize);
    }

private:
    int cropMargin_ = 0;
    int centerCropSize_ = 0;
    int scale_mode_ = 0;
    std::vector<std::vector<double>> scale_map_;

    void loadScaleMap(const std::string& path) {
        scale_map_.clear();
        if (path.empty()) return;
        try {
            QFile file(QString::fromStdString(path));
            if (!file.open(QIODevice::ReadOnly)) return;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            if (!doc.isObject() || !doc["scale_values"].isArray()) return;
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
        } catch (...) {}
    }

    double getScaleFactor(int row, int col, int z_i, int zRef) const {
        if (scale_mode_ == 1) {
            if (row >= 0 && row < static_cast<int>(scale_map_.size()) &&
                col >= 0 && col < static_cast<int>(scale_map_[row].size()))
                return scale_map_[row][col];
            return 1.0;
        }
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

    /// Core implementation shared by stitch() and stitchWithPositions().
    cv::Mat stitchImpl(const std::vector<cv::Mat>& images,
                       const std::vector<std::pair<int, int>>& positions,
                       const std::vector<int>& zValues,
                       const cv::Size& gridSize)
    {
        if (images.empty()) {
            reportStatus("No images to stitch");
            return {};
        }

        const bool hasPositions = !positions.empty();
        const bool hasZ = !zValues.empty() && hasPositions;

        try {
            int expected = gridSize.width * gridSize.height;
            if (static_cast<int>(images.size()) < expected) {
                reportStatus("Not enough images. Expected " +
                    std::to_string(expected) + ", got " + std::to_string(images.size()));
                reportProgress(100, 100);
                return {};
            }

            reportStatus("Z-scale grid stitching " + std::to_string(images.size()) +
                         " images (" + std::to_string(gridSize.width) + "x" +
                         std::to_string(gridSize.height) + ")" +
                         (hasZ ? " with Z-scale compensation" : " (no Z data, no scaling)"));
            reportProgress(0, 100);

            // ── Compute reference Z and per-image scale factors ──
            int zRef = hasZ ? computeMedianZ(zValues) : 0;
            SPDLOG_INFO("Z-scale stitch: zRef={}, hasZ={}, imageCount={}", zRef, hasZ, images.size());

            // ── Determine tile size from original image dimensions ──
            // After Z-scaling + interpolation, all images are back to original size,
            // so the tile size is based on the original (uniform) image dimensions.
            cv::Size refSize = images[0].size();

            int tileW, tileH;
            computeTileSize(refSize, tileW, tileH);
            cv::Rect refCropRoi = computeCropRoi(refSize, tileW, tileH);

            SPDLOG_INFO("Z-scale: zRef={}, tileSize={}x{}", zRef, tileW, tileH);

            int totalW = gridSize.width * tileW;
            int totalH = gridSize.height * tileH;

            cv::Mat result(totalH, totalW, images[0].type(), cv::Scalar(0, 0, 0));
            reportProgress(10, 100);

            // ── Process each image ──
            for (size_t i = 0; i < images.size(); ++i) {
                int row, col;
                if (hasPositions) {
                    row = positions[i].first;
                    col = positions[i].second;
                    if (row < 0 || row >= gridSize.height || col < 0 || col >= gridSize.width) {
                        SPDLOG_WARN("Position [{},{}] out of grid bounds, skipping", row, col);
                        continue;
                    }
                } else {
                    row = static_cast<int>(i) / gridSize.width;
                    col = static_cast<int>(i) % gridSize.width;
                }

                cv::Mat processed = images[i];

                // ── Scale compensation → interpolate to original size ──
                {
                    double scale = getScaleFactor(row, col,
                        hasZ ? zValues[i] : 0, zRef);
                    if (std::abs(scale - 1.0) > 0.001) {
                        cv::Mat scaled;
                        int sw = static_cast<int>(std::round(images[i].cols * scale));
                        int sh = static_cast<int>(std::round(images[i].rows * scale));
                        cv::resize(images[i], scaled, cv::Size(sw, sh), 0, 0, cv::INTER_LINEAR);
                        // Interpolate back to original size for uniform tile dimensions
                        cv::resize(scaled, processed, images[i].size(), 0, 0, cv::INTER_LINEAR);
                        SPDLOG_DEBUG("Image[{}] Z={} scale={:.4f} -> {}x{} -> uniform {}x{}",
                                     i, hasZ ? zValues[i] : 0, scale, sw, sh,
                                     images[i].cols, images[i].rows);
                    }
                }

                // ── Center-crop to uniform tile size ──
                processed = cropTileToSize(processed, tileW, tileH);

                // ── Place in canvas ──
                cv::Rect dest(col * tileW, row * tileH, tileW, tileH);
                processed.copyTo(result(dest));

                int pct = 20 + static_cast<int>((i * 80) / images.size());
                reportProgress(pct, 100);
            }

            reportProgress(100, 100);
            reportStatus("Z-scale grid stitching completed");
            return result;

        } catch (const std::exception& e) {
            reportStatus(std::string("Z-scale stitching error: ") + e.what());
            reportProgress(100, 100);
            return {};
        }
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
        if (centerCropSize_ > 0) {
            int left = (imgSize.width - tileW) / 2;
            int top = (imgSize.height - tileH) / 2;
            return cv::Rect(left, top, tileW, tileH);
        } else if (cropMargin_ > 0) {
            int crop = cropMargin_;
            if (crop * 2 >= imgSize.width || crop * 2 >= imgSize.height) {
                crop = 0;
            }
            return cv::Rect(crop, crop, tileW, tileH);
        }
        return cv::Rect(0, 0, tileW, tileH);
    }

    /// Center-crop an image to the exact target tile size.
    /// If the image is smaller than the target, it is placed centered on a black canvas.
    cv::Mat cropTileToSize(const cv::Mat& src, int tileW, int tileH) const {
        if (src.cols == tileW && src.rows == tileH) {
            return src;
        }

        cv::Rect roi;
        if (centerCropSize_ > 0 || cropMargin_ > 0) {
            roi = computeCropRoi(cv::Size(src.cols, src.rows), tileW, tileH);
        } else {
            // No crop config — if scaled image differs from tile size,
            // center-crop or pad to match
            roi = computeCropRoi(cv::Size(src.cols, src.rows), tileW, tileH);
        }

        // Clamp ROI to source bounds
        roi.x = std::max(0, roi.x);
        roi.y = std::max(0, roi.y);
        roi.width = std::min(roi.width, src.cols - roi.x);
        roi.height = std::min(roi.height, src.rows - roi.y);

        cv::Mat tile;
        if (roi.width == tileW && roi.height == tileH) {
            tile = src(roi).clone();
        } else {
            // Need padding: create tile-sized canvas and place extracted region centered
            tile = cv::Mat(tileH, tileW, src.type(), cv::Scalar(0, 0, 0));
            int pasteX = (tileW - roi.width) / 2;
            int pasteY = (tileH - roi.height) / 2;
            cv::Rect pasteRoi(pasteX, pasteY, roi.width, roi.height);
            src(roi).copyTo(tile(pasteRoi));
        }

        return tile;
    }
};

} // namespace stitch
