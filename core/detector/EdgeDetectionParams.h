#pragma once

namespace detector {

/**
 * @brief Sobel 边缘检测算法的可调参数（对应 docs/det.py）。
 *
 * 纯 POD（仅 int/double，无第三方依赖），可被 AppController.h 安全包含。
 */
struct EdgeDetectionParams {
    double claheClip       = 2.0;   ///< CLAHE 对比度限制
    int    claheTileGrid   = 8;     ///< CLAHE 分块大小（正方形）
    int    edgeThreshold   = 30;    ///< Sobel 幅度二值化阈值（0-255）
    int    sobelKSize      = 3;     ///< Sobel 核大小（强制奇数）
    int    dilateIter      = 1;     ///< 膨胀迭代次数
    int    minBboxArea     = 25;    ///< 最小包围框面积（px），小于此值视为噪声
    double nmsIouThresh    = 0.4;   ///< NMS IoU 阈值
    double nmsContainThresh = 0.5;  ///< NMS 包含率阈值（inter/min_area）
};

} // namespace detector
