#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#ifdef HAVE_OPENCV_CUDAFILTERS
#include <opencv2/cudafilters.hpp>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudaimgproc.hpp>
#endif
#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>
#include <vector>

#include "IDetector.h"
#include "DustDetectionParams.h"

namespace detector {

/**
 * @brief 灰尘颗粒检测器（传统 CV，YOLO 之外的备选算法）。
 *
 * 头文件式实现（仿 core/stitch/*StitchAlgorithm.h），通过 #include 引入即可，
 * 无需加入 CMake SOURCES。算法移植自 docs/dust_detection.py 的 detect_dust 迭代流水线：
 * 灰度 + CLAHE → 背景减除提取暗斑 → Otsu → 形态学去噪 → 连通域面积过滤 →
 * 用背景填充已检出区域后反复迭代 → 最终面积过滤 → 可选 NMS 合并。
 *
 * 无需模型：loadModel/isModelLoaded 恒为成功/true，可直接检测。
 */
class DustDetector : public IDetector {
public:
    DustDetector() = default;
    ~DustDetector() override = default;

    // 无模型算法：加载即成功，始终就绪
    bool loadModel(const std::string& /*param_path*/, const std::string& /*bin_path*/) override {
        return true;
    }
    bool isModelLoaded() const override { return true; }

    std::vector<Detection> detect(const cv::Mat& image) override {
        std::vector<Detection> detections;
        if (image.empty()) return detections;

        try {
            // ---- 1. 转灰度 ----
            cv::Mat gray;
            if (image.channels() == 3)      cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
            else if (image.channels() == 4) cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
            else                            gray = image;

            // ---- 2. CLAHE 对比度增强 ----
            cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(params_.claheClip, cv::Size(8, 8));
            cv::Mat gray_enh;
            clahe->apply(gray, gray_enh);

            // ---- 3. 迭代检测 ----
            cv::Mat gray_current;
            gray_enh.convertTo(gray_current, CV_32F);
            cv::Mat final_mask = cv::Mat::zeros(gray_current.size(), CV_8U);

            const bool need_labels = params_.maxArea > 0;
            const int max_iter = std::max(1, params_.maxIter);

            for (int iter = 1; iter <= max_iter; ++iter) {
                cv::Mat bg, labels, stats;
                int num_labels = 0;
                cv::Mat mask = singlePass(gray_current, bg, need_labels, num_labels, labels, stats);

                // ---- 本轮 max_area 过滤（复用 singlePass 返回的连通域信息）----
                cv::Mat mask_accepted;
                if (params_.maxArea > 0) {
                    mask_accepted = cv::Mat::zeros(mask.size(), CV_8U);
                    for (int i = 1; i < num_labels; ++i) {
                        if (stats.at<int>(i, cv::CC_STAT_AREA) < params_.maxArea)
                            mask_accepted.setTo(255, labels == i);
                    }
                } else {
                    mask_accepted = mask;
                }

                if (cv::countNonZero(mask_accepted) == 0) break;

                // 只保留本轮新增部分（排除已检出区域）
                cv::Mat new_mask = mask_accepted.clone();
                new_mask.setTo(0, final_mask > 0);
                if (cv::countNonZero(new_mask) == 0) break;

                cv::bitwise_or(final_mask, mask_accepted, final_mask);

                // ---- 用背景估计值填充已检出区域（用原始 mask，覆盖所有检出区域）----
                cv::Mat fill_kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
                cv::Mat fill_mask;
                cv::dilate(mask, fill_mask, fill_kernel, cv::Point(-1, -1), 1);

                cv::Mat bg_fill;                        // clip(bg, 0, 255)
                cv::max(bg, 0.0, bg_fill);
                cv::min(bg_fill, 255.0, bg_fill);
                bg_fill.copyTo(gray_current, fill_mask);  // where(fill_mask>0, bg_fill, gray_current)

                // 填充边界平滑过渡（减小模糊核，避免过度侵蚀邻近颗粒）
                cv::Mat fill_mask_f, blur_mask;
                fill_mask.convertTo(fill_mask_f, CV_32F);
                cv::GaussianBlur(fill_mask_f, blur_mask, cv::Size(7, 7), 3);
                blur_mask = blur_mask / 255.0;            // alpha ∈ [0,1]
                cv::Mat inv = 1.0 - blur_mask;
                cv::multiply(gray_current, inv, gray_current);
                cv::Mat weighted;
                cv::multiply(bg_fill, blur_mask, weighted);
                gray_current += weighted;
            }

            // ---- 4. 最终面积过滤：跨轮粘连的连通域仍需再次过滤 ----
            std::vector<Comp> comps;
            {
                cv::Mat labels, stats, centroids;
                int n = cv::connectedComponentsWithStats(final_mask, labels, stats, centroids, 8);
                for (int i = 1; i < n; ++i) {
                    long long area = stats.at<int>(i, cv::CC_STAT_AREA);
                    if (params_.maxArea > 0 && area >= params_.maxArea) continue;
                    comps.push_back({cv::Rect(stats.at<int>(i, cv::CC_STAT_LEFT),
                                              stats.at<int>(i, cv::CC_STAT_TOP),
                                              stats.at<int>(i, cv::CC_STAT_WIDTH),
                                              stats.at<int>(i, cv::CC_STAT_HEIGHT)),
                                     area});
                }
            }

            // ---- 4.5 NMS 合并重叠框（贪心，累加真实像素面积）----
            if (params_.nmsIou > 0)
                comps = mergeBoxes(comps, params_.nmsIou);

            // ---- 5. 组装 Detection ----
            const float ref = params_.maxArea > 0 ? static_cast<float>(params_.maxArea) : 1.0f;
            for (const auto& c : comps) {
                Detection d;
                d.bounding_box = c.bbox;
                d.class_id = 1;
                d.class_name = "dust";
                d.confidence = params_.maxArea > 0
                                   ? std::min(1.0f, static_cast<float>(c.area) / ref)
                                   : 1.0f;
                detections.push_back(d);
            }
            SPDLOG_DEBUG("DustDetector: {} particles", detections.size());
        } catch (const std::exception& e) {
            SPDLOG_ERROR("DustDetector::detect exception: {}", e.what());
            detections.clear();
        }
        return detections;
    }

    cv::Mat drawDetections(const cv::Mat& image, const std::vector<Detection>& detections) override {
        cv::Mat result = image.clone();
        for (const auto& d : detections)
            cv::rectangle(result, d.bounding_box, cv::Scalar(0, 0, 255), 2);
        std::string label = "dust: " + std::to_string(detections.size());
        cv::putText(result, label, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                    cv::Scalar(0, 0, 255), 2);
        return result;
    }

    // ---- 参数存取 ----
    void setParams(const DustDetectionParams& p) {
        params_ = p;
        params_.bgBlurSize |= 1;                 // GaussianBlur 要求奇数核
        if (params_.maxIter < 1) params_.maxIter = 1;
    }
    DustDetectionParams params() const { return params_; }

    // ---- IDetector 其余接口（灰尘算法用不到，存值满足接口）----
    void setConfidenceThreshold(float threshold) override { conf_threshold_ = threshold; }
    void setNmsThreshold(float threshold) override { nms_threshold_ = threshold; }
    float getConfidenceThreshold() const override { return conf_threshold_; }
    float getNmsThreshold() const override { return nms_threshold_; }
    void setInputSize(int width, int height) override { input_width_ = width; input_height_ = height; }
    int getInputWidth() const override { return input_width_; }
    int getInputHeight() const override { return input_height_; }

private:
    /// 单个检出颗粒：包围框 + 真实像素面积。
    struct Comp { cv::Rect bbox; long long area; };

    /**
     * @brief 单次检测：背景减除 → Otsu → 形态学去噪 → 连通域 min_area 过滤 → 可选膨胀。
     * @param gray          CV_32F 灰度图
     * @param bg_out        [out] 背景估计（CV_32F），供调用方回填
     * @param compute_labels 是否对结果 mask 重算连通域（仅 max_area>0 时需要）
     * @return mask（CV_8U，0/255）
     */
    cv::Mat singlePass(const cv::Mat& gray, cv::Mat& bg_out, bool compute_labels,
                       int& num_labels, cv::Mat& labels, cv::Mat& stats) const {
        const int blur = params_.bgBlurSize | 1;

        cv::Mat bg;
#ifdef HAVE_OPENCV_CUDAFILTERS
        {
            cv::cuda::GpuMat gpuGray, gpuBg;
            gpuGray.upload(gray);
            auto gaussian = cv::cuda::createGaussianFilter(gray.type(), -1,
                cv::Size(blur, blur), blur / 2);
            gaussian->apply(gpuGray, gpuBg);
            gpuBg.download(bg);
        }
#else
        cv::GaussianBlur(gray, bg, cv::Size(blur, blur), blur / 2);
#endif
        bg_out = bg;

        // dark_spots = clip(bg - gray, 0, None) 转 uint8（暗斑：比背景更暗的区域）
        cv::Mat dark_f;
        cv::subtract(bg, gray, dark_f);
        cv::threshold(dark_f, dark_f, 0.0, 0.0, cv::THRESH_TOZERO);
        cv::Mat dark;
        dark_f.convertTo(dark, CV_8U);

        // Otsu 阈值
        cv::Mat spots_bin;
        cv::threshold(dark, spots_bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

        // 形态学去噪
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
        cv::Mat clean;
        cv::morphologyEx(spots_bin, clean, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 2);
        cv::morphologyEx(clean, clean, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 1);

        // 连通域 min_area 过滤
        cv::Mat lbl, st, ct;
        int n = cv::connectedComponentsWithStats(clean, lbl, st, ct, 8);
        cv::Mat mask = cv::Mat::zeros(clean.size(), CV_8U);
        for (int i = 1; i < n; ++i) {
            if (st.at<int>(i, cv::CC_STAT_AREA) >= params_.minArea)
                mask.setTo(255, lbl == i);
        }

        // 仅在需要时对 mask 重算连通域（供 max_area 过滤）
        if (compute_labels) {
            num_labels = cv::connectedComponentsWithStats(mask, labels, stats, ct, 8);
        } else {
            num_labels = 0;
        }

        // 膨胀（iterations=0 时跳过，保持紧凑边界避免跨颗粒粘连）
        if (params_.dilateIter > 0) {
            cv::Mat dk = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
#ifdef HAVE_OPENCV_CUDAFILTERS
            {
                cv::cuda::GpuMat gpuMask, gpuOut;
                gpuMask.upload(mask);
                auto dilateF = cv::cuda::createMorphologyFilter(
                    cv::MORPH_DILATE, mask.type(), dk,
                    cv::Point(-1, -1), params_.dilateIter);
                dilateF->apply(gpuMask, gpuOut);
                gpuOut.download(mask);
            }
#else
            cv::dilate(mask, mask, dk, cv::Point(-1, -1), params_.dilateIter);
#endif
        }
        return mask;
    }

    /// 对检测框做贪心合并：IoU（分母取小框面积）> 阈值则并为包围框，累加真实像素面积。
    std::vector<Comp> mergeBoxes(std::vector<Comp> comps, double iou_threshold) const {
        if (comps.size() <= 1 || iou_threshold <= 0) return comps;

        std::sort(comps.begin(), comps.end(),
                  [](const Comp& a, const Comp& b) { return a.area > b.area; });
        std::vector<char> suppressed(comps.size(), 0);
        std::vector<Comp> merged;

        for (size_t i = 0; i < comps.size(); ++i) {
            if (suppressed[i]) continue;
            int bx1 = comps[i].bbox.x, by1 = comps[i].bbox.y;
            int bx2 = bx1 + comps[i].bbox.width, by2 = by1 + comps[i].bbox.height;
            long long pix = comps[i].area;

            for (size_t j = i + 1; j < comps.size(); ++j) {
                if (suppressed[j]) continue;
                int x2 = comps[j].bbox.x, y2 = comps[j].bbox.y;
                int w2 = comps[j].bbox.width, h2 = comps[j].bbox.height;

                int ix1 = std::max(bx1, x2), iy1 = std::max(by1, y2);
                int ix2 = std::min(bx2, x2 + w2), iy2 = std::min(by2, y2 + h2);
                if (ix2 > ix1 && iy2 > iy1) {
                    double inter = static_cast<double>(ix2 - ix1) * (iy2 - iy1);
                    double area_a = static_cast<double>(bx2 - bx1) * (by2 - by1);
                    double area_b = static_cast<double>(w2) * h2;
                    double iou = inter / std::min(area_a, area_b);
                    if (iou > iou_threshold) {
                        bx1 = std::min(bx1, x2); by1 = std::min(by1, y2);
                        bx2 = std::max(bx2, x2 + w2); by2 = std::max(by2, y2 + h2);
                        pix += comps[j].area;
                        suppressed[j] = 1;
                    }
                }
            }
            merged.push_back({cv::Rect(bx1, by1, bx2 - bx1, by2 - by1), pix});
        }
        return merged;
    }

    DustDetectionParams params_;
    float conf_threshold_ = 0.3f;
    float nms_threshold_ = 0.3f;
    int input_width_ = 640;
    int input_height_ = 640;
};

} // namespace detector
