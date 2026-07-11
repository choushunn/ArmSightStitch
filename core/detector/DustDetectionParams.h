#pragma once

namespace detector {

/**
 * @brief 灰尘颗粒检测（传统 CV 迭代法）的可调参数。
 *
 * 纯 POD（仅 int/double，无第三方依赖），故可被 AppController.h 等头文件安全包含，
 * 不会把 OpenCV 类型泄漏进接口头。字段与 docs/dust_detection.py 的 detect_dust 参数一一对应。
 */
struct DustDetectionParams {
    double claheClip  = 2.0;    ///< CLAHE 对比度限制
    int    bgBlurSize = 31;     ///< 背景估计高斯核（内部强制取奇数）
    int    minArea    = 50;     ///< 连通域最小面积（像素），小于此值视为噪声
    int    maxArea    = 20000;  ///< 面积上限（像素），>= 此值被过滤；0 = 不限
    int    dilateIter = 0;      ///< mask 膨胀次数
    int    maxIter    = 4;      ///< 最大迭代检测次数
    double nmsIou     = 0.1;    ///< NMS 合并阈值（0~1），0 = 不合并
};

} // namespace detector
