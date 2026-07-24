#pragma once

// ── EdgeCrop — 边缘感知裁剪 ──────────────────────────────────
//
// 替代统一的中心裁剪：网格边界图像贴边裁剪（保留外沿信息），
// 内部图像居中裁剪。支持逐格手动偏移叠加。
//
// 坐标约定：row/col 为 拼接 canvas 坐标系
//   - row: 0-indexed, row=0 = 顶部, row=N-1 = 底部
//   - col: 0-indexed, col=0 = 左边缘(显示第10列), col=N-1 = 右边缘(显示第1列)
//
// 边界行为：
//   - row=0:   top=0               (保留顶部)
//   - row=N-1: top=imgH - cropH    (保留底部)
//   - col=0:   left=0              (保留左侧)
//   - col=N-1: left=imgW - cropW   (保留右侧)
//   - 内部:    居中裁剪
//   - row/col = -1 或 gridSize <= 0: 回退到旧版居中裁剪
// ─────────────────────────────────────────────────────────────

#include <opencv2/core/types.hpp>
#include <opencv2/core/mat.hpp>
#include <vector>

namespace report {

cv::Rect computeEdgeAwareCropRoi(
    const cv::Size& imgSize, int cropSize,
    int row, int col, int gridRows, int gridCols,
    int ox = 0, int oy = 0);

/// 将居中裁剪拼接的 mosaic 四周扩展——从边界图像的原图提取被切掉的
/// 边缘条带，贴到 mosaic 外侧。用于网格拼接的后处理步骤。
///
/// @param mosaic     已拼接的主体（居中裁剪，gx*tw × gy*th）
/// @param images     边界图像原始全图（与 positions 一一对应）
/// @param positions  每张图的 (row, col) 网格位置，仅边界图像需要有效
/// @param gridSize   网格 (cols, rows)
/// @param refSize    原始图像尺寸（所有图像同尺寸）
/// @param tileW,tileH  tile 尺寸（= centerCropSize 正方形边长）
cv::Mat applyEdgeExpansion(
    const cv::Mat& mosaic,
    const std::vector<cv::Mat>& images,
    const std::vector<std::pair<int, int>>& positions,
    const cv::Size& gridSize,
    const cv::Size& refSize,
    int tileW, int tileH);

} // namespace report
