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
#include "EdgeDetectionParams.h"

namespace detector {

/**
 * @brief Sobel 边缘检测器（传统 CV，YOLO/Dust 之外的第三种算法）。
 *
 * 头文件式实现（仿 DustDetector），通过 #include 引入即可，无需加入 CMake SOURCES。
 * 算法移植自 docs/det.py 的流水线：
 * 灰度 + CLAHE → Sobel 幅值 → 阈值二值化 → 膨胀 → 轮廓查找 →
 * 面积过滤 → 自定义 NMS（IoU + 包含率双判据）。
 *
 * 无需模型：loadModel/isModelLoaded 恒为成功/true，可直接检测。
 * 使用原始分辨率，不缩放。
 */
class EdgeDetector : public IDetector {
public:
    EdgeDetector() = default;
    ~EdgeDetector() override = default;

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

            cv::Mat binary;  // 最终二值图（CPU 或 GPU download 结果）

#ifdef HAVE_OPENCV_CUDAFILTERS
            // ── GPU 加速路径：CLAHE → Sobel → magnitude → min/clip → threshold → dilate ──
            {
                // 上传灰度图到 GPU
                cv::cuda::GpuMat gpuGray, gpuClahe;
                gpuGray.upload(gray);

                // CLAHE 增强
                auto clahe = cv::cuda::createCLAHE(params_.claheClip,
                    cv::Size(params_.claheTileGrid, params_.claheTileGrid));
                clahe->apply(gpuGray, gpuClahe);

                // Sobel X + Y（CV_32F 输出）
                cv::cuda::GpuMat gpuSobelX, gpuSobelY;
                auto sobelX = cv::cuda::createSobelFilter(gpuClahe.type(), CV_32F,
                    1, 0, params_.sobelKSize);
                auto sobelY = cv::cuda::createSobelFilter(gpuClahe.type(), CV_32F,
                    0, 1, params_.sobelKSize);
                sobelX->apply(gpuClahe, gpuSobelX);
                sobelY->apply(gpuClahe, gpuSobelY);

                // 幅值 = sqrt(gx² + gy²)
                cv::cuda::GpuMat gpuMag;
                cv::cuda::magnitude(gpuSobelX, gpuSobelY, gpuMag);

                // min(mag, 255) + 转 CV_8U
                cv::cuda::GpuMat gpuMagClipped, gpuMagU8;
                cv::cuda::min(gpuMag, 255.0, gpuMagClipped);
                gpuMagClipped.convertTo(gpuMagU8, CV_8U);

                // 二值化：mag > edgeThreshold
                cv::cuda::GpuMat gpuBin;
                cv::cuda::threshold(gpuMagU8, gpuBin,
                    params_.edgeThreshold, 255, cv::THRESH_BINARY);

                // 膨胀
                cv::Mat dk = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
                cv::cuda::GpuMat gpuDilated;
                auto dilateF = cv::cuda::createMorphologyFilter(
                    cv::MORPH_DILATE, gpuBin.type(), dk,
                    cv::Point(-1, -1), params_.dilateIter);
                dilateF->apply(gpuBin, gpuDilated);

                // 下载回 CPU
                gpuDilated.download(binary);
            }
#else
            // ── CPU 路径 ──
            // CLAHE 增强
            cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(params_.claheClip,
                cv::Size(params_.claheTileGrid, params_.claheTileGrid));
            cv::Mat grayClahe;
            clahe->apply(gray, grayClahe);

            // Sobel X + Y
            cv::Mat sobelX, sobelY;
            cv::Sobel(grayClahe, sobelX, CV_32F, 1, 0, params_.sobelKSize);
            cv::Sobel(grayClahe, sobelY, CV_32F, 0, 1, params_.sobelKSize);

            // 幅值 + clip + 转 uint8
            cv::Mat mag;
            cv::magnitude(sobelX, sobelY, mag);
            cv::Mat magClipped;
            cv::min(mag, 255.0, magClipped);
            cv::Mat magU8;
            magClipped.convertTo(magU8, CV_8U);

            // 二值化
            cv::threshold(magU8, binary,
                params_.edgeThreshold, 255, cv::THRESH_BINARY);

            // 膨胀
            if (params_.dilateIter > 0) {
                cv::Mat dk = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
                cv::dilate(binary, binary, dk, cv::Point(-1, -1), params_.dilateIter);
            }
#endif

            // ---- 8. 查找轮廓（CPU only）----
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            // ---- 9. 过滤 + 提取包围框 + 面积作为 score ----
            std::vector<Comp> comps;
            for (const auto& cnt : contours) {
                cv::Rect bbox = cv::boundingRect(cnt);
                if (bbox.width * bbox.height < params_.minBboxArea)
                    continue;
                double area = cv::contourArea(cnt);
                comps.push_back({bbox, area});
            }

            if (comps.empty()) return detections;

            // ---- 10. NMS（双判据：IoU + 包含率）----
            std::vector<int> keep = edgeNms(comps);

            // ---- 11. 组装 Detection ----
            const double imageArea = static_cast<double>(image.cols) * image.rows;
            const double normFactor = imageArea * 0.01;  // 1% = confidence 1.0
            for (int idx : keep) {
                Detection d;
                d.bounding_box = comps[idx].bbox;
                d.class_id = 2;
                d.class_name = "defect";
                d.confidence = static_cast<float>(
                    std::min(1.0, comps[idx].score / normFactor));
                detections.push_back(d);
            }
            SPDLOG_DEBUG("EdgeDetector: {} defects", detections.size());
        } catch (const std::exception& e) {
            SPDLOG_ERROR("EdgeDetector::detect exception: {}", e.what());
            detections.clear();
        }
        return detections;
    }

    cv::Mat drawDetections(const cv::Mat& image, const std::vector<Detection>& detections) override {
        cv::Mat result = image.clone();
        for (const auto& d : detections)
            cv::rectangle(result, d.bounding_box, cv::Scalar(0, 255, 0), 2);
        std::string label = "edge: " + std::to_string(detections.size());
        cv::putText(result, label, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                    cv::Scalar(0, 255, 0), 2);
        return result;
    }

    // ---- 参数存取 ----
    void setParams(const EdgeDetectionParams& p) {
        params_ = p;
        params_.sobelKSize |= 1;               // 强制 Sobel 核为奇数
        if (params_.sobelKSize < 1) params_.sobelKSize = 3;
        if (params_.dilateIter < 0) params_.dilateIter = 0;
        if (params_.minBboxArea < 0) params_.minBboxArea = 0;
    }
    EdgeDetectionParams params() const { return params_; }

    // ---- IDetector 其余接口（存值满足接口）----
    void setConfidenceThreshold(float threshold) override { conf_threshold_ = threshold; }
    void setNmsThreshold(float threshold) override { nms_threshold_ = threshold; }
    float getConfidenceThreshold() const override { return conf_threshold_; }
    float getNmsThreshold() const override { return nms_threshold_; }
    void setInputSize(int width, int height) override { input_width_ = width; input_height_ = height; }
    int getInputWidth() const override { return input_width_; }
    int getInputHeight() const override { return input_height_; }

private:
    struct Comp { cv::Rect bbox; double score; };

    /**
     * @brief 自定义 NMS（复现 docs/det.py 的 nms 函数）。
     *
     * 与标准 NMS 不同，此函数使用双判据抑制：
     *   - IoU = inter / (area_i + area_j - inter)  （标准交并比）
     *   - containment = inter / min(area_i, area_j) （包含率）
     * 任一超过阈值即抑制，比标准 NMS 更激进。
     *
     * @param comps 候选框列表（含 bbox 和面积分数）
     * @return 保留的索引列表
     */
    std::vector<int> edgeNms(std::vector<Comp>& comps) const {
        if (comps.size() <= 1) {
            // 只有一个框，直接返回
            std::vector<int> keep(comps.size());
            for (size_t i = 0; i < comps.size(); ++i) keep[i] = static_cast<int>(i);
            return keep;
        }

        // 按 score 降序排序（保留原始索引）
        std::vector<int> indices(comps.size());
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<int>(i);
        std::sort(indices.begin(), indices.end(),
            [&comps](int a, int b) { return comps[a].score > comps[b].score; });

        std::vector<char> suppressed(comps.size(), 0);
        std::vector<int> keep;

        for (size_t si = 0; si < indices.size(); ++si) {
            int i = indices[si];
            if (suppressed[i]) continue;
            keep.push_back(i);

            cv::Rect& ri = comps[i].bbox;
            double area_i = static_cast<double>(ri.width) * ri.height;

            for (size_t sj = si + 1; sj < indices.size(); ++sj) {
                int j = indices[sj];
                if (suppressed[j]) continue;

                cv::Rect& rj = comps[j].bbox;
                double area_j = static_cast<double>(rj.width) * rj.height;

                // 交集
                int ix1 = std::max(ri.x, rj.x);
                int iy1 = std::max(ri.y, rj.y);
                int ix2 = std::min(ri.x + ri.width, rj.x + rj.width);
                int iy2 = std::min(ri.y + ri.height, rj.y + rj.height);
                int iw = ix2 - ix1;
                int ih = iy2 - iy1;
                if (iw <= 0 || ih <= 0) continue;

                double inter = static_cast<double>(iw) * ih;

                // IoU = inter / union
                double iou = inter / (area_i + area_j - inter + 1e-8);

                // containment = inter / min(area_i, area_j)
                double minArea = std::min(area_i, area_j);
                double containment = (minArea > 0) ? inter / minArea : 0.0;

                if (iou > params_.nmsIouThresh || containment > params_.nmsContainThresh)
                    suppressed[j] = 1;
            }
        }
        return keep;
    }

    EdgeDetectionParams params_;
    float conf_threshold_ = 0.3f;
    float nms_threshold_ = 0.3f;
    int input_width_ = 640;
    int input_height_ = 640;
};

} // namespace detector
