#include "EdgeCrop.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace report {

cv::Rect computeEdgeAwareCropRoi(
    const cv::Size& imgSize, int cropSize,
    int row, int col, int gridRows, int gridCols,
    int ox, int oy)
{
    // ── 有效性检查 ──
    if (cropSize <= 0) return cv::Rect(0, 0, imgSize.width, imgSize.height);

    int cropW = cropSize;
    int cropH = cropSize;
    if (cropW > imgSize.width)  cropW = imgSize.width;
    if (cropH > imgSize.height) cropH = imgSize.height;

    // ── 无位置信息时回退到居中裁剪 ──
    if (row < 0 || col < 0 || gridRows <= 0 || gridCols <= 0) {
        int left = (imgSize.width  - cropW) / 2 + ox;
        int top  = (imgSize.height - cropH) / 2 + oy;
        left = std::max(0, std::min(left, imgSize.width  - cropW));
        top  = std::max(0, std::min(top,  imgSize.height - cropH));
        return cv::Rect(left, top, cropW, cropH);
    }

    // ── 水平位置 ──
    int left;
    if (col == 0)
        left = 0;                             // 左边缘保留
    else if (col == gridCols - 1)
        left = imgSize.width - cropW;         // 右边缘保留
    else
        left = (imgSize.width - cropW) / 2;   // 居中

    // ── 垂直位置 ──
    int top;
    if (row == 0)
        top = 0;                              // 顶部保留
    else if (row == gridRows - 1)
        top = imgSize.height - cropH;         // 底部保留
    else
        top = (imgSize.height - cropH) / 2;   // 居中

    // ── 叠加手动偏移 ──
    left += ox;
    top  += oy;

    // ── Clamp 到图像边界内 ──
    left = std::max(0, std::min(left, imgSize.width  - cropW));
    top  = std::max(0, std::min(top,  imgSize.height - cropH));

    return cv::Rect(left, top, cropW, cropH);
}

cv::Mat applyEdgeExpansion(
    const cv::Mat& mosaic,
    const std::vector<cv::Mat>& images,
    const std::vector<std::pair<int, int>>& positions,
    const cv::Size& gridSize,
    const cv::Size& refSize,
    int tileW, int tileH)
{
    if (mosaic.empty() || images.empty()) return mosaic;
    int iw = refSize.width, ih = refSize.height;
    int cx = (iw - tileW) / 2;  // 居中裁剪左侧切掉的像素
    int cy = (ih - tileH) / 2;  // 居中裁剪上侧切掉的像素

    SPDLOG_INFO("[Stitch] EdgeExpansion: mosaic={}x{} ref={}x{} tile={}x{} cx={} cy={} grid={}x{} nImages={}",
                mosaic.cols, mosaic.rows, iw, ih, tileW, tileH, cx, cy,
                gridSize.width, gridSize.height, images.size());

    if (cx <= 0 && cy <= 0) {
        SPDLOG_INFO("[Stitch] EdgeExpansion: skipped (no margins)");
        return mosaic;
    }

    int gx = gridSize.width, gy = gridSize.height;
    int rw = iw - cx - tileW;   // 右侧切掉的像素
    int bh = ih - cy - tileH;   // 下侧切掉的像素
    int newW = gx * tileW + cx + rw;
    int newH = gy * tileH + cy + bh;

    cv::Mat result(newH, newW, mosaic.type(), cv::Scalar(0, 0, 0));
    mosaic.copyTo(result(cv::Rect(cx, cy, mosaic.cols, mosaic.rows)));

    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].empty()) continue;
        int row = positions[i].first;
        int col = positions[i].second;
        if (row < 0 || row >= gy || col < 0 || col >= gx) continue;

        // ── 四角 ──
        if (row == 0   && col == 0    && cx > 0 && cy > 0)
            images[i](cv::Rect(0,         0,          cx,         cy))
                .copyTo(result(cv::Rect(0,         0,          cx,         cy)));
        if (row == 0   && col == gx-1 && rw > 0 && cy > 0)
            images[i](cv::Rect(cx + tileW, 0,          rw,         cy))
                .copyTo(result(cv::Rect(cx + gx*tileW, 0,          rw,         cy)));
        if (row == gy-1 && col == 0    && cx > 0 && bh > 0)
            images[i](cv::Rect(0,         cy + tileH, cx,         bh))
                .copyTo(result(cv::Rect(0,         cy + gy*tileH, cx,         bh)));
        if (row == gy-1 && col == gx-1 && rw > 0 && bh > 0)
            images[i](cv::Rect(cx + tileW, cy + tileH, rw,         bh))
                .copyTo(result(cv::Rect(cx + gx*tileW, cy + gy*tileH, rw,         bh)));

        // ── 四条边（不含角）──
        if (row == 0   && cy > 0)   // 上边
            images[i](cv::Rect(cx,          0,          tileW, cy))
                .copyTo(result(cv::Rect(cx + col*tileW, 0,          tileW, cy)));
        if (row == gy-1 && bh > 0)  // 下边
            images[i](cv::Rect(cx,          cy+tileH,   tileW, bh))
                .copyTo(result(cv::Rect(cx + col*tileW, cy+gy*tileH, tileW, bh)));
        if (col == 0   && cx > 0)   // 左边
            images[i](cv::Rect(0,           cy,         cx,    tileH))
                .copyTo(result(cv::Rect(0,           cy+row*tileH, cx,    tileH)));
        if (col == gx-1 && rw > 0)  // 右边
            images[i](cv::Rect(cx + tileW,  cy,         rw,    tileH))
                .copyTo(result(cv::Rect(cx + gx*tileW, cy+row*tileH, rw,    tileH)));
    }
    SPDLOG_INFO("[Stitch] EdgeExpansion: done → {}x{} (added L={} R={} T={} B={})",
                newW, newH, cx, rw, cy, bh);
    return result;
}

} // namespace report
