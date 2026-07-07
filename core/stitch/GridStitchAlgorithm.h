#pragma once

#include "IStitchAlgorithm.h"
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

            // Build crop ROIs for each image (same ROI for all images if sizes match)
            computeTileSize(imgSize, tileW, tileH);
            cv::Rect cropRoi = computeCropRoi(imgSize, tileW, tileH);

            int totalW = gridSize.width * tileW;
            int totalH = gridSize.height * tileH;

            cv::Mat result(totalH, totalW, images[0].type(), cv::Scalar(0, 0, 0));
            reportProgress(10, 100);

            // Place images in row-major order (left-to-right, top-to-bottom).
            // sortImagesInSCurveOrder already reordered the vector into S-curve,
            // so the grid placement here uses simple row-major indexing.
            int index = 0;
            for (int row = 0; row < gridSize.height; ++row) {
                reportProgress(20 + (row * 80) / gridSize.height, 100);
                for (int col = 0; col < gridSize.width; ++col) {
                    if (index >= static_cast<int>(images.size())) break;
                    cv::Mat tile = images[index];
                    tile = cropTile(tile, cropRoi, tileW, tileH);
                    cv::Rect dest(col * tileW, row * tileH, tileW, tileH);
                    tile.copyTo(result(dest));
                    ++index;
                }
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
            cv::Rect cropRoi = computeCropRoi(imgSize, tileW, tileH);

            int totalW = gridSize.width * tileW;
            int totalH = gridSize.height * tileH;

            cv::Mat result(totalH, totalW, images[0].type(), cv::Scalar(0, 0, 0));
            reportProgress(10, 100);

            // Place each image directly at its grid position
            for (size_t i = 0; i < images.size(); ++i) {
                int row = positions[i].first;
                int col = positions[i].second;

                if (row < 0 || row >= gridSize.height || col < 0 || col >= gridSize.width) {
                    SPDLOG_WARN("Position [{},{}] out of grid bounds, skipping", row, col);
                    continue;
                }

                cv::Mat tile = cropTile(images[i], cropRoi, tileW, tileH);
                cv::Rect dest(col * tileW, row * tileH, tileW, tileH);
                tile.copyTo(result(dest));

                int pct = 20 + static_cast<int>((i * 80) / images.size());
                reportProgress(pct, 100);
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

    /// Crop and optionally resize a tile to the target dimensions.
    cv::Mat cropTile(const cv::Mat& src, const cv::Rect& roi, int tileW, int tileH) const {
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
