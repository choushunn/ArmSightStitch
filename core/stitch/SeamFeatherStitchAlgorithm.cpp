#include "SeamFeatherStitchAlgorithm.h"

#include <spdlog/spdlog.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <algorithm>
#include <cmath>
#include <vector>

#ifdef HAVE_OPENCV_CUDA
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudawarping.hpp>
#endif

namespace stitch {

// ──────────────────────────────────────────────────────────────────
// Public setters
// ──────────────────────────────────────────────────────────────────

void SeamFeatherStitchAlgorithm::setFeatherWidth(int pixels) {
    featherWidth_ = pixels;
}

void SeamFeatherStitchAlgorithm::setCenterCropSize(int pixels) {
    centerCropSize_ = pixels;
}

void SeamFeatherStitchAlgorithm::setCropMargin(int pixels) {
    cropMargin_ = pixels;
}

void SeamFeatherStitchAlgorithm::setScaleMode(int mode) {
    scale_mode_ = (mode == 1) ? 1 : 0;
}

void SeamFeatherStitchAlgorithm::setScaleMapFile(const std::string& path) {
    loadScaleMap(path);
}

void SeamFeatherStitchAlgorithm::setZCorrectionCoef(double coef) {
    z_correction_coef_ = (coef > 0.0) ? coef : 1.0;
}

void SeamFeatherStitchAlgorithm::setCropOffsetFile(const std::string& path) {
    loadCropOffset(path);
}

void SeamFeatherStitchAlgorithm::setScaleStackMode(bool stack) {
    scale_stack_ = stack;
}

// ──────────────────────────────────────────────────────────────────
// stitch / stitchWithPositions
// ──────────────────────────────────────────────────────────────────

cv::Mat SeamFeatherStitchAlgorithm::stitch(const std::vector<cv::Mat>& images,
                                           const cv::Size& gridSize) {
    // No Z data — build positions and delegate
    std::vector<std::pair<int, int>> positions;
    positions.reserve(images.size());
    for (int i = 0; i < static_cast<int>(images.size()); ++i) {
        positions.emplace_back(i / gridSize.width, i % gridSize.width);
    }
    return stitchWithPositions(images, positions, {}, gridSize);
}

cv::Mat SeamFeatherStitchAlgorithm::stitchWithPositions(
    const std::vector<cv::Mat>& images,
    const std::vector<std::pair<int, int>>& positions,
    const cv::Size& gridSize) {
    return stitchWithPositions(images, positions, {}, gridSize);
}

cv::Mat SeamFeatherStitchAlgorithm::stitchWithPositions(
    const std::vector<cv::Mat>& images,
    const std::vector<std::pair<int, int>>& positions,
    const std::vector<int>& zValues,
    const cv::Size& gridSize) {
    if (images.empty() || (!positions.empty() && images.size() != positions.size())) {
        reportStatus("Invalid input: images and positions size mismatch");
        return {};
    }
    if (!zValues.empty() && images.size() != zValues.size()) {
        reportStatus("Invalid input: images and zValues size mismatch");
        return {};
    }

    // Validate all images share the same size and type — mismatch causes
    // cv::arithm_op failure in feather accumulation (ROI dimension mismatch).
    {
        cv::Size refSize = images[0].size();
        int refType = images[0].type();
        for (size_t i = 1; i < images.size(); ++i) {
            if (!images[i].empty()) {
                if (images[i].size() != refSize || images[i].type() != refType) {
                    reportStatus(
                        "Image size/type mismatch at index " + std::to_string(i) +
                        ": expected " + std::to_string(refSize.width) + "x" +
                        std::to_string(refSize.height) + " type=" + std::to_string(refType) +
                        ", got " + std::to_string(images[i].cols) + "x" +
                        std::to_string(images[i].rows) + " type=" +
                        std::to_string(images[i].type()));
                    return {};
                }
            } else {
                reportStatus("Empty image at index " + std::to_string(i));
                return {};
            }
        }
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
            SPDLOG_INFO("[Stitch] SeamFeather: zRef={} (median of {} values), coef={}, stack={}",
                        zRef, zValues.size(), z_correction_coef_, scale_stack_);
        }

        // Tile size is based on original image size.
        // After Z-scaling, each image is interpolated back to its original dimensions,
        // so all tiles have uniform pixel size and cover the same physical area.
        cv::Size refSize = images[0].size();
        SPDLOG_INFO("[Stitch] SeamFeather: refSize={}x{} type={} nChannels={}",
                    refSize.width, refSize.height, images[0].type(), images[0].channels());
        cv::Mat result = stitchImpl(images, positions, zValues, zRef, refSize, gridSize);
        reportProgress(100, 100);
        reportStatus("Seam-feather stitching completed");
        return result;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Stitch] SeamFeather exception: {} ({} images, grid={}x{}, ref={}x{})",
                     e.what(), images.size(), gridSize.width, gridSize.height,
                     images[0].cols, images[0].rows);
        reportStatus(std::string("Seam-feather stitching error: ") + e.what());
        reportProgress(100, 100);
        return {};
    }
}

// ──────────────────────────────────────────────────────────────────
// Private: loadScaleMap
// ──────────────────────────────────────────────────────────────────

void SeamFeatherStitchAlgorithm::loadScaleMap(const std::string& path) {
    scale_map_.clear();
    if (path.empty()) return;
    try {
        QFile file(QString::fromStdString(path));
        if (!file.open(QIODevice::ReadOnly)) {
            SPDLOG_ERROR("[Stitch] SeamFeather: cannot open scale-map file: {}", path);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isObject() || !doc["scale_values"].isArray()) {
            SPDLOG_ERROR("[Stitch] SeamFeather: scale-map missing 'scale_values' array");
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
        SPDLOG_INFO("[Stitch] SeamFeather: scale-map loaded {} x {}", scale_map_.size(),
                    scale_map_.empty() ? 0 : scale_map_[0].size());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Stitch] SeamFeather: failed to parse scale-map: {}", e.what());
        scale_map_.clear();
    }
}

// ──────────────────────────────────────────────────────────────────
// Private: loadCropOffset
// ──────────────────────────────────────────────────────────────────

void SeamFeatherStitchAlgorithm::loadCropOffset(const std::string& path) {
    ox_values_.clear();
    oy_values_.clear();
    if (path.empty()) return;
    try {
        QFile file(QString::fromStdString(path));
        if (!file.open(QIODevice::ReadOnly)) {
            SPDLOG_ERROR("[Stitch] SeamFeather: cannot open crop-offset file: {}", path);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isObject()) {
            SPDLOG_ERROR("[Stitch] SeamFeather: crop-offset file is not a JSON object");
            return;
        }
        QJsonObject root = doc.object();

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

        SPDLOG_INFO("[Stitch] SeamFeather: crop-offset loaded {}x{}, {}x{}",
                    ox_values_.size(), ox_values_.empty() ? 0 : ox_values_[0].size(),
                    oy_values_.size(), oy_values_.empty() ? 0 : oy_values_[0].size());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Stitch] SeamFeather: failed to parse crop-offset: {}", e.what());
        ox_values_.clear();
        oy_values_.clear();
    }
}

// ──────────────────────────────────────────────────────────────────
// Private: getScaleFactor
// ──────────────────────────────────────────────────────────────────

double SeamFeatherStitchAlgorithm::getScaleFactor(int row, int col, int z_i, int zRef) const {
    if (scale_mode_ == 1 && !scale_stack_) {
        // Non-stacking mode 1: pure scale_map lookup (algo3 original behavior)
        if (row >= 0 && row < static_cast<int>(scale_map_.size()) &&
            col >= 0 && col < static_cast<int>(scale_map_[row].size())) {
            return scale_map_[row][col];
        }
        SPDLOG_WARN("[Stitch] SeamFeather: scale-map missing entry [{},{}]", row, col);
        return 1.0;
    }

    // Compute Z-based base scale
    double zScale = 1.0;
    if (zRef > 0 && z_i > 0)
        zScale = static_cast<double>(zRef) / static_cast<double>(z_i);

    double result = zScale * z_correction_coef_;

    if (scale_mode_ == 1 && scale_stack_) {
        // Stacking mode 1: scale_map x Z-ratio x correction (algo4 behavior)
        double manual = 1.0;
        if (row >= 0 && row < static_cast<int>(scale_map_.size()) &&
            col >= 0 && col < static_cast<int>(scale_map_[row].size())) {
            manual = scale_map_[row][col];
        } else {
            SPDLOG_WARN("[Stitch] SeamFeather: scale-map missing entry [{},{}]", row, col);
        }
        result = manual * zScale * z_correction_coef_;
    }

    // Clamp to prevent extreme values
    if (result < 0.1) result = 0.1;
    if (result > 10.0) result = 10.0;
    return result;
}

// ──────────────────────────────────────────────────────────────────
// Private: computeMedianZ
// ──────────────────────────────────────────────────────────────────

int SeamFeatherStitchAlgorithm::computeMedianZ(const std::vector<int>& zValues) {
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

// ──────────────────────────────────────────────────────────────────
// Private: findClosestZIndex
// ──────────────────────────────────────────────────────────────────

int SeamFeatherStitchAlgorithm::findClosestZIndex(const std::vector<int>& zValues, int zRef) {
    int bestIdx = 0;
    int bestDiff = std::abs(zValues[0] - zRef);
    for (size_t i = 1; i < zValues.size(); ++i) {
        int diff = std::abs(zValues[i] - zRef);
        if (diff < bestDiff) { bestDiff = diff; bestIdx = static_cast<int>(i); }
    }
    return bestIdx;
}

// ──────────────────────────────────────────────────────────────────
// Private: scaleImage
// ──────────────────────────────────────────────────────────────────

cv::Mat SeamFeatherStitchAlgorithm::scaleImage(const cv::Mat& src, double scale, cv::Mat& dst) {
    if (std::abs(scale - 1.0) <= 1e-6) {
        dst = src;
        return src;
    }
    int sw = static_cast<int>(std::round(src.cols * scale));
    int sh = static_cast<int>(std::round(src.rows * scale));
    cv::resize(src, dst, cv::Size(sw, sh), 0, 0, cv::INTER_LINEAR);
    return dst;
}

// ──────────────────────────────────────────────────────────────────
// Private: scaleToUniform
// ──────────────────────────────────────────────────────────────────

cv::Mat SeamFeatherStitchAlgorithm::scaleToUniform(const cv::Mat& src,
                                                    int row, int col,
                                                    int z_i, int zRef) const {
    double s = getScaleFactor(row, col, z_i, zRef);
    if (std::abs(s - 1.0) <= 1e-6) return src;
    cv::Mat scaled, result;
    scaleImage(src, s, scaled);
    cv::resize(scaled, result, src.size(), 0, 0, cv::INTER_LINEAR);
    return result;
}

// ──────────────────────────────────────────────────────────────────
// Private: computeTileSize
// ──────────────────────────────────────────────────────────────────

void SeamFeatherStitchAlgorithm::computeTileSize(const cv::Size& imgSize,
                                                  int& tileW, int& tileH) const {
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

// ──────────────────────────────────────────────────────────────────
// Private: computeCropRoi
// ──────────────────────────────────────────────────────────────────

cv::Rect SeamFeatherStitchAlgorithm::computeCropRoi(const cv::Size& imgSize,
                                                     int tileW, int tileH,
                                                     int row, int col,
                                                     int gridRows, int gridCols,
                                                     int ox, int oy) const {
    if (centerCropSize_ > 0) {
        return report::computeEdgeAwareCropRoi(
            imgSize, tileW, row, col, gridRows, gridCols, ox, oy);
    } else if (cropMargin_ > 0) {
        int crop = cropMargin_;
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

// ──────────────────────────────────────────────────────────────────
// Private: expandEdgesWithPositions
// ──────────────────────────────────────────────────────────────────

cv::Mat SeamFeatherStitchAlgorithm::expandEdgesWithPositions(
    cv::Mat result,
    const std::vector<cv::Mat>& images,
    const std::vector<std::pair<int,int>>& positions,
    const std::vector<int>& zValues, int zRef, bool hasZ,
    const cv::Size& gridSize, const cv::Size& refSize,
    int tileW, int tileH) {
    if (centerCropSize_ <= 0 || images.empty()) return result;

    std::vector<cv::Mat> edgeImgs;
    std::vector<std::pair<int,int>> edgePos;
    const bool hasPositions = !positions.empty();
    int nRows = gridSize.height, nCols = gridSize.width;

    for (size_t i = 0; i < images.size(); ++i) {
        int row, col;
        if (hasPositions) {
            row = positions[i].first; col = positions[i].second;
        } else {
            row = static_cast<int>(i) / nCols;
            col = static_cast<int>(i) % nCols;
        }
        if (row < 0 || row >= nRows || col < 0 || col >= nCols) continue;
        if (row == 0 || row == nRows-1 || col == 0 || col == nCols-1) {
            cv::Mat uniform = scaleToUniform(images[i], row, col,
                (hasZ && i < zValues.size()) ? zValues[i] : 0, zRef);
            edgeImgs.push_back(uniform);
            edgePos.emplace_back(row, col);
        }
    }

    if (edgeImgs.empty()) return result;
    return report::applyEdgeExpansion(
        result, edgeImgs, edgePos, gridSize, refSize, tileW, tileH);
}

// ──────────────────────────────────────────────────────────────────
// Private: buildFeatherMask
// ──────────────────────────────────────────────────────────────────

cv::Mat SeamFeatherStitchAlgorithm::buildFeatherMask(int h, int w,
                                                     bool hasLeft, bool hasRight,
                                                     bool hasTop, bool hasBottom,
                                                     int fw) const {
    cv::Mat mask(h, w, CV_32FC1, cv::Scalar(1.0f));

    // Pre-compute 1D cosine fade: 0->1
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
        cv::Mat fade = makeFade(fw_h);          // 0->1
        cv::Mat roi = mask(cv::Rect(0, 0, fw_h, h));
        cv::Mat fade2D;
        cv::repeat(fade, h, 1, fade2D);
        cv::multiply(roi, fade2D, roi);
    }

    if (hasRight) {
        int fw_h = std::min(fw, w);
        cv::Mat fade = makeFade(fw_h);          // 0->1
        cv::flip(fade, fade, 1);                // 1->0
        cv::Mat roi = mask(cv::Rect(w - fw_h, 0, fw_h, h));
        cv::Mat fade2D;
        cv::repeat(fade, h, 1, fade2D);
        cv::multiply(roi, fade2D, roi);
    }

    if (hasTop) {
        int fw_v = std::min(fw, h);
        cv::Mat fade = makeFade(fw_v);          // 0->1
        cv::Mat roi = mask(cv::Rect(0, 0, w, fw_v));
        cv::Mat fade2D;
        cv::repeat(fade.t(), 1, w, fade2D);     // repeat horizontally (nx) to fill width
        cv::multiply(roi, fade2D, roi);
    }

    if (hasBottom) {
        int fw_v = std::min(fw, h);
        cv::Mat fade = makeFade(fw_v);          // 0->1
        cv::flip(fade, fade, 1);                // 1->0
        cv::Mat roi = mask(cv::Rect(0, h - fw_v, w, fw_v));
        cv::Mat fade2D;
        cv::repeat(fade.t(), 1, w, fade2D);     // repeat horizontally (nx) to fill width
        cv::multiply(roi, fade2D, roi);
    }

    return mask;
}

// ──────────────────────────────────────────────────────────────────
// Private: cropTile
// ──────────────────────────────────────────────────────────────────

cv::Mat SeamFeatherStitchAlgorithm::cropTile(const cv::Mat& src, int tileW, int tileH,
                                             int row, int col,
                                             int gridRows, int gridCols,
                                             int ox, int oy) const {
    cv::Rect roi = computeCropRoi(cv::Size(src.cols, src.rows),
                                   tileW, tileH, row, col, gridRows, gridCols, ox, oy);

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

// ──────────────────────────────────────────────────────────────────
// Private: stitchImpl
// ──────────────────────────────────────────────────────────────────

cv::Mat SeamFeatherStitchAlgorithm::stitchImpl(
    const std::vector<cv::Mat>& images,
    const std::vector<std::pair<int, int>>& positions,
    const std::vector<int>& zValues,
    int zRef,
    const cv::Size& refSize,
    const cv::Size& gridSize) {
    const int nRows = gridSize.height;
    const int nCols = gridSize.width;
    const int fw = featherWidth_;
    const bool hasZ = !zValues.empty() && zRef > 0;

    // ── Tile size from original image dimensions (uniform after interpolation) ──
    int tileW, tileH;
    computeTileSize(refSize, tileW, tileH);

    // ── Channel count (not hardcoded to 3 — images may be grayscale or BGRA) ──
    const int nChannels = images[0].channels();
    const int accType = CV_MAKETYPE(CV_32F, nChannels);

    // ── Canvas dimensions ──
    int canvasW = nCols * tileW;
    int canvasH = nRows * tileH;

    SPDLOG_INFO("[Stitch] SeamFeather: grid={}x{} tile={}x{} canvas={}x{} fw={} hasZ={} coef={} stack={} ch={}",
                nCols, nRows, tileW, tileH, canvasW, canvasH, fw, hasZ,
                z_correction_coef_, scale_stack_, nChannels);

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

        // ── Canvas expansion: extract boundary strips cut off by center crop ──
        result = expandEdgesWithPositions(result, images, positions,
                                          zValues, zRef, hasZ,
                                          gridSize, refSize, tileW, tileH);
        return result;
    }

#ifdef HAVE_OPENCV_CUDA
    // ── GPU weighted accumulation ──
    cv::cuda::GpuMat gpuAccSum(canvasH, canvasW, accType);
    cv::cuda::GpuMat gpuAccWeight(canvasH, canvasW, CV_32FC1);
    gpuAccSum.setTo(cv::Scalar::all(0));
    gpuAccWeight.setTo(cv::Scalar::all(0));

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
            SPDLOG_WARN("[Stitch] Position [{},{}] out of grid bounds, skipping", row, col);
            continue;
        }

        int ox, oy;
        getOffset(row, col, ox, oy);

        // ── 1. Scale -> interpolate to original size (uniform dimensions) ──
        cv::Mat uniform = scaleToUniform(images[i], row, col,
            hasZ ? zValues[i] : 0, zRef);

        // ── 2. Compute base crop ROI with center positioning ──
        cv::Rect baseCrop = computeCropRoi(cv::Size(uniform.cols, uniform.rows),
                                           tileW, tileH, ox, oy);

        // ── 3. Adjacency ──
        bool hasLeft   = (col > 0);
        bool hasRight  = (col < nCols - 1);
        bool hasTop    = (row > 0);
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
        gpuTile.convertTo(gpuTileFloat, accType);
        gpuMask.upload(maskCpu);

        // cv::cuda::merge may fail for nChannels==1 in some OpenCV builds;
        // skip the merge when there is only one channel.
        if (nChannels == 1) {
            cv::cuda::multiply(gpuTileFloat, gpuMask, gpuWeighted);
        } else {
            std::vector<cv::cuda::GpuMat> maskChs(nChannels, gpuMask);
            cv::cuda::merge(maskChs, gpuMask3);
            cv::cuda::multiply(gpuTileFloat, gpuMask3, gpuWeighted);
        }

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
    cv::cuda::GpuMat gpuResult;
    if (nChannels == 1) {
        cv::cuda::max(gpuAccWeight, 1e-8, gpuAccWeight);
        cv::cuda::divide(gpuAccSum, gpuAccWeight, gpuAccSum);
    } else {
        cv::cuda::GpuMat gpuWeight3;
        std::vector<cv::cuda::GpuMat> wChs(nChannels, gpuAccWeight);
        cv::cuda::merge(wChs, gpuWeight3);
        cv::cuda::max(gpuWeight3, 1e-8, gpuWeight3);
        cv::cuda::divide(gpuAccSum, gpuWeight3, gpuAccSum);
    }
    gpuAccSum.convertTo(gpuResult, images[0].type());

    cv::Mat result;
    gpuResult.download(result);
#else
    // ── CPU weighted accumulation (fallback when CUDA unavailable) ──
    cv::Mat accSum(canvasH, canvasW, accType, cv::Scalar::all(0));
    cv::Mat accWeight(canvasH, canvasW, CV_32FC1, cv::Scalar::all(0));

    for (size_t i = 0; i < images.size(); ++i) {
        int row, col;
        if (!positions.empty()) {
            row = positions[i].first; col = positions[i].second;
        } else {
            row = static_cast<int>(i) / nCols; col = static_cast<int>(i) % nCols;
        }
        if (row < 0 || row >= nRows || col < 0 || col >= nCols) {
            SPDLOG_WARN("[Stitch] Position [{},{}] out of grid bounds, skipping", row, col);
            continue;
        }

        int ox, oy;
        getOffset(row, col, ox, oy);

        // ── 1. Z-scale -> interpolate to original size ──
        cv::Mat uniform = scaleToUniform(images[i], row, col,
            hasZ ? zValues[i] : 0, zRef);

        // ── 2. Compute base crop ROI with center positioning ──
        cv::Rect baseCrop = computeCropRoi(cv::Size(uniform.cols, uniform.rows),
                                           tileW, tileH, ox, oy);

        // ── 3. Adjacency ──
        bool hasLeft   = (col > 0);
        bool hasRight  = (col < nCols - 1);
        bool hasTop    = (row > 0);
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

        cv::Mat tileFloat, weighted;
        tileCpu.convertTo(tileFloat, accType);
        // cv::merge may fail for nChannels==1 in some OpenCV builds
        if (nChannels == 1) {
            cv::multiply(tileFloat, maskCpu, weighted);
        } else {
            cv::Mat mask3;
            std::vector<cv::Mat> maskChs(static_cast<size_t>(nChannels), maskCpu);
            cv::merge(maskChs, mask3);
            cv::multiply(tileFloat, mask3, weighted);
        }

        cv::Mat accRoi = accSum(canvasRoi);
        cv::add(accRoi, weighted, accRoi);

        cv::Mat weightRoi = accWeight(canvasRoi);
        cv::add(weightRoi, maskCpu, weightRoi);

        int pct = 20 + static_cast<int>((i * 80) / images.size());
        reportProgress(pct, 100);
    }

    // ── CPU normalize ──
    cv::Mat resultFloat;
    if (nChannels == 1) {
        cv::max(accWeight, 1e-8, accWeight);
        cv::divide(accSum, accWeight, resultFloat);
    } else {
        cv::Mat weight3;
        std::vector<cv::Mat> wChs(static_cast<size_t>(nChannels), accWeight);
        cv::merge(wChs, weight3);
        cv::max(weight3, 1e-8, weight3);
        cv::divide(accSum, weight3, resultFloat);
    }

    cv::Mat result;
    resultFloat.convertTo(result, images[0].type());
#endif

    // ── Canvas expansion: extract boundary strips cut off by center crop ──
    result = expandEdgesWithPositions(result, images, positions,
                                      zValues, zRef, hasZ,
                                      gridSize, refSize, tileW, tileH);
    return result;
}

} // namespace stitch
