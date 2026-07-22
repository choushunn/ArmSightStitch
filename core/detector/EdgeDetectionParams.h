#pragma once

namespace detector {

/**
 * @brief Sobel 边缘检测（传统 CV 法）的可调参数。
 *
 * 纯 POD（仅 double/int，无第三方依赖），故可被 AppController.h 等头文件安全包含，
 * 不会把 OpenCV 类型泄漏进接口头。字段与 docs/det.py 的参数一一对应。
 */
struct EdgeDetectionParams {
    double claheClip         = 2.0;  ///< CLAHE 对比度限制
    int    claheTileGrid     = 8;    ///< CLAHE 分块大小（正方形）
    int    edgeThreshold     = 30;   ///< Sobel 幅值二值化阈值（0-255）
    int    sobelKSize        = 3;    ///< Sobel 核大小（内部强制取奇数）
    int    dilateIter        = 1;    ///< 膨胀迭代次数
    int    minBboxArea       = 25;   ///< 最小包围框面积（像素），小于此值视为噪声
    double nmsIouThresh     = 0.4;  ///< NMS IoU 阈值（0~1）
    double nmsContainThresh  = 0.5;  ///< NMS 包含率阈值（0~1），>
};

} // namespace detector
