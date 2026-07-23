#pragma once

#include "IStitchAlgorithm.h"
#include "infra/report/EdgeCrop.h"
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>
#include <map>

namespace stitch {

/// Algorithm 0 — grid-based stitching with center cropping (like docs/stitch.py)
/// Images are placed at fixed grid positions. Center cropping removes overlap
/// and edge distortion between adjacent tiles.
class GridStitchAlgorithm : public IStitchAlgorithm {
public:
    /// Set crop margin in pixels (removed from each edge). 0 = no crop (full image).
    void setCropMargin(int pixels) { cropMargin_ = pixels; }

    /// Set center crop target size in pixels. Each tile is cropped to a square
    /// of this size from its center (like stitch.py crop_size=1775). 0 = disabled.
    void setCenterCropSize(int pixels) { centerCropSize_ = pixels; }

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

            reportStatus("Grid stitching " + std::to_string(images.size()) +
                         " images (" + std::to_string(gridSize.width) + "x" +
                         std::to_string(gridSize.height) + ")");
            reportProgress(0, 100);

            cv::Size imgSize = images[0].size();
            int tileW, tileH;
            computeTileSize(imgSize, tileW, tileH);

            int totalW = gridSize.width * tileW;
            int totalH = gridSize.height * tileH;

            cv::Mat result(totalH, totalW, images[0].type(), cv::Scalar(0, 0, 0));
            reportProgress(10, 100);

            // Uniform center crop — all tiles use the same ROI for seamless alignment
            int index = 0;
            for (int row = 0; row < gridSize.height; ++row) {
                reportProgress(20 + (row * 80) / gridSize.height, 100);
                for (int col = 0; col < gridSize.width; ++col) {
                    if (index >= static_cast<int>(images.size())) break;
                    cv::Mat tile = cropTile(images[index], tileW, tileH);
                    cv::Rect dest(col * tileW, row * tileH, tileW, tileH);
                    tile.copyTo(result(dest));
                    ++index;
                }
            }

            // ── 画布扩展：从边界图像提取被中心裁剪切掉的外沿条带 ──
            if (centerCropSize_ > 0) {
                std::vector<std::pair<int, int>> pos;
                int idx2 = 0;
                for (int r = 0; r < gridSize.height; ++r)
                    for (int c = 0; c < gridSize.width; ++c)
                        pos.emplace_back(r, c);
                result = report::applyEdgeExpansion(
                    result, images, pos, gridSize, imgSize, tileW, tileH);
            }

            reportProgress(100, 100);
            reportStatus("Grid stitching completed");
            return result;
        } catch (const std::exception& e) {
            reportStatus(std::string("Grid stitching error: ") + e.what());
            reportProgress(100, 100);
            return {};
        }
    }

    /// Direct position-based stitching (like docs/stitch.py).
    /// Each image is placed at its (row, col) grid position regardless of input order.
    cv::Mat stitchWithPositions(const std::vector<cv::Mat>& images,
                                const std::vector<std::pair<int, int>>& positions,
                                const cv::Size& gridSize)
    {
        if (images.empty() || images.size() != positions.size()) {
            reportStatus("Invalid input: images and positions size mismatch");
            return {};
        }

        try {
            reportStatus("Grid stitching " + std::to_string(images.size()) +
                         " images with positions (" + std::to_string(gridSize.width) +
                         "x" + std::to_string(gridSize.height) + ")");
            reportProgress(0, 100);

            cv::Size imgSize = images[0].size();
            int tileW, tileH;
            computeTileSize(imgSize, tileW, tileH);

            int totalW = gridSize.width * tileW;
            int totalH = gridSize.height * tileH;

            cv::Mat result(totalH, totalW, images[0].type(), cv::Scalar(0, 0, 0));
            reportProgress(10, 100);

            // Place each image directly at its grid position with uniform center crop
            for (size_t i = 0; i < images.size(); ++i) {
                int row = positions[i].first;
                int col = positions[i].second;

                if (row < 0 || row >= gridSize.height || col < 0 || col >= gridSize.width) {
                    SPDLOG_WARN("[Stitch] Position [{},{}] out of grid bounds, skipping", row, col);
                    continue;
                }

                cv::Mat tile = cropTile(images[i], tileW, tileH);
                cv::Rect dest(col * tileW, row * tileH, tileW, tileH);
                tile.copyTo(result(dest));

                int pct = 20 + static_cast<int>((i * 80) / images.size());
                reportProgress(pct, 100);
            }

            // ── 画布扩展：从边界图像提取被中心裁剪切掉的外沿条带 ──
            if (centerCropSize_ > 0) {
                result = report::applyEdgeExpansion(
                    result, images, positions, gridSize, imgSize, tileW, tileH);
            }

            reportProgress(100, 100);
            reportStatus("Grid stitching with positions completed");
            return result;
        } catch (const std::exception& e) {
            reportStatus(std::string("Grid stitching error: ") + e.what());
            reportProgress(100, 100);
            return {};
        }
    }

private:
    int cropMargin_ = 0;    // pixels to crop from each edge
    int centerCropSize_ = 0; // center-crop target size (0 = disabled)

    /// Compute output tile size based on current crop settings.
    /// Priority: centerCropSize_ > cropMargin_ > full image
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
    /// When centerCropSize_ > 0 and position info is provided, uses edge-aware
    /// cropping: boundary images preserve the outward-facing edge, interior
    /// images use center crop. Falls back to uniform center crop when row/col
    /// are -1 or grid size is unknown.
    cv::Rect computeCropRoi(const cv::Size& imgSize, int tileW, int tileH,
                            int row = -1, int col = -1,
                            int gridRows = -1, int gridCols = -1,
                            int ox = 0, int oy = 0) const {
        if (centerCropSize_ > 0) {
            return report::computeEdgeAwareCropRoi(
                imgSize, tileW, row, col, gridRows, gridCols, ox, oy);
        } else if (cropMargin_ > 0) {
            int crop = cropMargin_;
            if (crop * 2 >= imgSize.width || crop * 2 >= imgSize.height) {
                crop = 0;
            }
            int left = crop + ox;
            int top  = crop + oy;
            left = std::max(0, std::min(left, imgSize.width  - tileW));
            top  = std::max(0, std::min(top,  imgSize.height - tileH));
            return cv::Rect(left, top, tileW, tileH);
        }
        int left = std::max(0, std::min(ox, imgSize.width  - tileW));
        int top  = std::max(0, std::min(oy, imgSize.height - tileH));
        return cv::Rect(left, top, tileW, tileH);
    }

    /// Crop and optionally resize a tile to the target dimensions.
    /// When row/col/gridSize are provided, uses edge-aware cropping.
    cv::Mat cropTile(const cv::Mat& src, int tileW, int tileH,
                     int row = -1, int col = -1,
                     int gridRows = -1, int gridCols = -1,
                     int ox = 0, int oy = 0) const {
        cv::Rect roi = computeCropRoi(cv::Size(src.cols, src.rows),
                                       tileW, tileH, row, col, gridRows, gridCols, ox, oy);
        cv::Mat tile;
        if (roi.x != 0 || roi.y != 0 || roi.width != src.cols || roi.height != src.rows) {
            tile = src(roi);
        } else {
            tile = src;
        }
        if (tile.cols != tileW || tile.rows != tileH) {
            cv::resize(tile, tile, cv::Size(tileW, tileH));
        }
        return tile;
    }
};

} // namespace stitch
