#include "ZScaleGridStitchAlgorithm.h"
#include "infra/report/EdgeCrop.h"
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace stitch {

void ZScaleGridStitchAlgorithm::setCropMargin(int pixels)
{
    cropMargin_ = pixels;
}

void ZScaleGridStitchAlgorithm::setCenterCropSize(int pixels)
{
    centerCropSize_ = pixels;
}

void ZScaleGridStitchAlgorithm::setScaleMode(int mode)
{
    scale_mode_ = (mode == 1) ? 1 : 0;
}

void ZScaleGridStitchAlgorithm::setScaleMapFile(const std::string& path)
{
    loadScaleMap(path);
}

cv::Mat ZScaleGridStitchAlgorithm::stitch(const std::vector<cv::Mat>& images,
                                          const cv::Size& gridSize)
{
    return stitchImpl(images, {}, {}, gridSize);
}

cv::Mat ZScaleGridStitchAlgorithm::stitchWithPositions(
    const std::vector<cv::Mat>& images,
    const std::vector<std::pair<int, int>>& positions,
    const std::vector<int>& zValues,
    const cv::Size& gridSize)
{
    return stitchImpl(images, positions, zValues, gridSize);
}

void ZScaleGridStitchAlgorithm::loadScaleMap(const std::string& path)
{
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

double ZScaleGridStitchAlgorithm::getScaleFactor(int row, int col,
                                                  int z_i, int zRef) const
{
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

int ZScaleGridStitchAlgorithm::computeMedianZ(const std::vector<int>& zValues)
{
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

cv::Mat ZScaleGridStitchAlgorithm::stitchImpl(
    const std::vector<cv::Mat>& images,
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
        SPDLOG_INFO("[Stitch] Z-scale stitch: zRef={}, hasZ={}, imageCount={}", zRef, hasZ, images.size());

        // ── Determine tile size from original image dimensions ──
        // After Z-scaling + interpolation, all images are back to original size,
        // so the tile size is based on the original (uniform) image dimensions.
        cv::Size refSize = images[0].size();

        int tileW, tileH;
        computeTileSize(refSize, tileW, tileH);

        SPDLOG_INFO("[Stitch] Z-scale: zRef={}, tileSize={}x{}", zRef, tileW, tileH);

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
                    SPDLOG_WARN("[Stitch] Position [{},{}] out of grid bounds, skipping", row, col);
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
                    SPDLOG_DEBUG("[Stitch] Image[{}] Z={} scale={:.4f} -> {}x{} -> uniform {}x{}",
                                 i, hasZ ? zValues[i] : 0, scale, sw, sh,
                                 images[i].cols, images[i].rows);
                }
            }

            // ── Uniform center crop to tile size ──
            processed = cropTileToSize(processed, tileW, tileH);

            // ── Place in canvas ──
            cv::Rect dest(col * tileW, row * tileH, tileW, tileH);
            processed.copyTo(result(dest));

            int pct = 20 + static_cast<int>((i * 80) / images.size());
            reportProgress(pct, 100);
        }

        // ── 画布扩展：从边界图像提取被中心裁剪切掉的外沿条带 ──
        if (centerCropSize_ > 0) {
            std::vector<std::pair<int, int>> pos;
            if (hasPositions)
                pos = positions;
            else
                for (size_t j = 0; j < images.size(); ++j)
                    pos.emplace_back(static_cast<int>(j) / gridSize.width,
                                     static_cast<int>(j) % gridSize.width);
            result = report::applyEdgeExpansion(
                result, images, pos, gridSize, refSize, tileW, tileH);
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

void ZScaleGridStitchAlgorithm::computeTileSize(const cv::Size& imgSize,
                                                 int& tileW, int& tileH) const
{
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

cv::Rect ZScaleGridStitchAlgorithm::computeCropRoi(const cv::Size& imgSize,
                                                     int tileW, int tileH,
                                                     int row, int col,
                                                     int gridRows, int gridCols,
                                                     int ox, int oy) const
{
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

cv::Mat ZScaleGridStitchAlgorithm::cropTileToSize(const cv::Mat& src,
                                                    int tileW, int tileH,
                                                    int row, int col,
                                                    int gridRows, int gridCols) const
{
    if (src.cols == tileW && src.rows == tileH) {
        return src;
    }

    cv::Rect roi = computeCropRoi(cv::Size(src.cols, src.rows),
                                   tileW, tileH, row, col, gridRows, gridCols);

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

} // namespace stitch
