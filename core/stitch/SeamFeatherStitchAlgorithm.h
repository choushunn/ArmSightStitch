#pragma once

#include "IStitchAlgorithm.h"
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <vector>

namespace stitch {

/// Algorithm 3 — grid-based stitching with seam feathering (cosine-weighted blending)
///
/// Like GridStitchAlgorithm, tiles are placed at fixed grid positions, but instead
/// of hard-edge copyTo(), adjacent tiles overlap by featherWidth_ pixels and are
/// blended with a cosine gradient mask so that seam weights sum to 1.0.
///
/// Reference: toupview_ref/stitch_seam_feather.py
class SeamFeatherStitchAlgorithm : public IStitchAlgorithm {
public:
    void setFeatherWidth(int pixels) { featherWidth_ = pixels; }
    void setCenterCropSize(int pixels) { centerCropSize_ = pixels; }
    void setCropMargin(int pixels) { cropMargin_ = pixels; }

    cv::Mat stitch(const std::vector<cv::Mat>& images,
                   const cv::Size& gridSize) override
    {
        if (images.empty()) {
            reportStatus("No images to stitch");
            return {};
        }

        try {
            int expected = gridSize.width * gridSize.height;
            if (static_cast<int>(images.size()) < expected) {
                reportStatus("Not enough images. Expected " +
                    std::to_string(expected) + ", got " + std::to_string(images.size()));
                reportProgress(100, 100);
                return {};
            }

            reportStatus("Seam-feather stitching " + std::to_string(images.size()) +
                         " images (" + std::to_string(gridSize.width) + "x" +
                         std::to_string(gridSize.height) + ")");
            reportProgress(0, 100);

            // Build position array for row-major placement (S-curve reordering is
            // already done by the caller, so we use simple row-major indexing)
            std::vector<std::pair<int, int>> positions;
            positions.reserve(images.size());
            for (int i = 0; i < static_cast<int>(images.size()); ++i) {
                int row = i / gridSize.width;
                int col = i % gridSize.width;
                positions.emplace_back(row, col);
            }

            cv::Mat result = stitchImpl(images, positions, gridSize);
            reportProgress(100, 100);
            reportStatus("Seam-feather stitching completed");
            return result;
        } catch (const std::exception& e) {
            reportStatus(std::string("Seam-feather stitching error: ") + e.what());
            reportProgress(100, 100);
            return {};
        }
    }

    /// Position-based stitching with seam feathering.
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const cv::Size& gridSize)
    {
        if (images.empty() || images.size() != positions.size()) {
            reportStatus("Invalid input: images and positions size mismatch");
            return {};
        }

        try {
            reportStatus("Seam-feather stitching " + std::to_string(images.size()) +
                         " images with positions (" + std::to_string(gridSize.width) +
                         "x" + std::to_string(gridSize.height) + ")");
            reportProgress(0, 100);

            cv::Mat result = stitchImpl(images, positions, gridSize);
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

    /// Core implementation: grid placement with cosine feather blending.
    cv::Mat stitchImpl(const std::vector<cv::Mat>& images,
                       const std::vector<std::pair<int, int>>& positions,
                       const cv::Size& gridSize)
    {
        const int nRows = gridSize.height;
        const int nCols = gridSize.width;
        const int fw = featherWidth_;

        // ── Determine tile size from first image ──
        cv::Size imgSize = images[0].size();
        int tileW, tileH;
        computeTileSize(imgSize, tileW, tileH);

        // ── Canvas dimensions ──
        int canvasW = nCols * tileW;
        int canvasH = nRows * tileH;

        SPDLOG_INFO("SeamFeather: grid={}x{} tile={}x{} canvas={}x{} fw={}",
                    nCols, nRows, tileW, tileH, canvasW, canvasH, fw);

        // If feather width is 0, fall back to simple copyTo (no blending overhead)
        if (fw <= 0) {
            cv::Mat result(canvasH, canvasW, images[0].type(), cv::Scalar(0, 0, 0));
            for (size_t i = 0; i < images.size(); ++i) {
                int row = positions[i].first;
                int col = positions[i].second;
                if (row < 0 || row >= nRows || col < 0 || col >= nCols) continue;
                cv::Mat tile = cropTile(images[i], tileW, tileH);
                cv::Rect dest(col * tileW, row * tileH, tileW, tileH);
                tile.copyTo(result(dest));
                int pct = 20 + static_cast<int>((i * 80) / images.size());
                reportProgress(pct, 100);
            }
            return result;
        }

        // ── Weighted accumulation ──
        // Use float accumulators for precision; final normalize to 8UC3
        cv::Mat accSum(canvasH, canvasW, CV_32FC3, cv::Scalar(0, 0, 0));
        cv::Mat accWeight(canvasH, canvasW, CV_32FC1, cv::Scalar(0.0f));

        // Base crop region in source image (before feather expansion)
        cv::Rect baseCrop = computeCropRoi(imgSize, tileW, tileH);

        for (size_t i = 0; i < images.size(); ++i) {
            int row = positions[i].first;
            int col = positions[i].second;
            if (row < 0 || row >= nRows || col < 0 || col >= nCols) {
                SPDLOG_WARN("Position [{},{}] out of grid bounds, skipping", row, col);
                continue;
            }

            // ── Adjacency ──
            bool hasLeft   = (col > 0);
            bool hasRight  = (col < nCols - 1);
            bool hasTop    = (row > 0);
            bool hasBottom = (row < nRows - 1);

            // ── Expanded crop in source image ──
            int cropLeft   = baseCrop.x - (hasLeft   ? fw : 0);
            int cropTop    = baseCrop.y - (hasTop    ? fw : 0);
            int cropRight  = baseCrop.x + baseCrop.width  + (hasRight  ? fw : 0);
            int cropBottom = baseCrop.y + baseCrop.height + (hasBottom ? fw : 0);

            // Clamp to source bounds
            cropLeft   = std::max(0, cropLeft);
            cropTop    = std::max(0, cropTop);
            cropRight  = std::min(images[i].cols, cropRight);
            cropBottom = std::min(images[i].rows, cropBottom);

            cv::Mat tile = images[i](cv::Rect(cropLeft, cropTop,
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

            // Broadcast mask to 3 channels: accSum += tile * mask3
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
