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
 * @brief Sobel 边缘检测器（传统 CV，无模型，原始分辨率）。
 *
 * 移植自 docs/det.py 的流水线：
 *   灰度 + CLAHE → Sobel 梯度幅度 → 阈值二值化 → 膨胀 → 轮廓查找 →
 *   面积过滤 → 双判据 NMS（IoU 或包含率 任一超标即抑制）。
 *
 * CUDA 加速（HAVE_OPENCV_CUDAFILTERS）：CLAHE、Sobel、magnitude、
 * min/convertTo、threshold、dilate 在 GPU 上执行，findContours 及
 * 之后为 CPU。否则全程 CPU。
 *
 * 头文件式实现（仿 DustDetector），无需加入 CMake SOURCES。
 */
class EdgeDetector : public IDetector {
public:
    EdgeDetector() = default;
    ~EdgeDetector() override = default;

    // 无模型算法
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
            if (image.channels() == 3)
                cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
            else if (image.channels() == 4)
                cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
            else
                gray = image;

            cv::Mat binary;

#ifdef HAVE_OPENCV_CUDAFILTERS
            // ── GPU 流水线：CLAHE → Sobel → magnitude → min/convertTo → threshold → dilate ──
            {
                // upload
                cv::cuda::GpuMat gpuGray;
                gpuGray.upload(gray);

                // CLAHE
                cv::Ptr<cv::cuda::CLAHE> clahe = cv::cuda::createCLAHE(
                    params_.claheClip, cv::Size(params_.claheTileGrid, params_.claheTileGrid));
                cv::cuda::GpuMat gpuClahe;
                clahe->apply(gpuGray, gpuClahe);

                // Sobel X
                cv::Ptr<cv::cuda::Filter> sobelX = cv::cuda::createSobelFilter(
                    gpuClahe.type(), CV_32F, 1, 0, params_.sobelKSize);
                cv::cuda::GpuMat gpuSobelX;
                sobelX->apply(gpuClahe, gpuSobelX);

                // Sobel Y
                cv::Ptr<cv::cuda::Filter> sobelY = cv::cuda::createSobelFilter(
                    gpuClahe.type(), CV_32F, 0, 1, params_.sobelKSize);
                cv::cuda::GpuMat gpuSobelY;
                sobelY->apply(gpuClahe, gpuSobelY);

                // magnitude = sqrt(sobelX^2 + sobelY^2)
                cv::cuda::GpuMat gpuMag;
                cv::cuda::magnitude(gpuSobelX, gpuSobelY, gpuMag);

                // clip to [0, 255]
                cv::cuda::GpuMat gpuClipped;
                cv::cuda::min(gpuMag, 255.0, gpuClipped);

                // convert to uint8
                cv::cuda::GpuMat gpuU8;
                gpuClipped.convertTo(gpuU8, CV_8U);

                // threshold
                cv::cuda::GpuMat gpuBin;
                cv::cuda::threshold(gpuU8, gpuBin, params_.edgeThreshold,
                                    255.0, cv::THRESH_BINARY);

                // dilate (3x3 rect kernel)
                cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
                cv::Ptr<cv::cuda::Filter> dilateF = cv::cuda::createMorphologyFilter(
                    cv::MORPH_DILATE, gpuBin.type(), kernel,
                    cv::Point(-1, -1), params_.dilateIter);

                cv::cuda::GpuMat gpuDilated;
                dilateF->apply(gpuBin, gpuDilated);

                // download result to CPU for findContours
                gpuDilated.download(binary);
            }
#else
            // ── CPU 流水线 ──
            // CLAHE
            cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(
                params_.claheClip, cv::Size(params_.claheTileGrid, params_.claheTileGrid));
            cv::Mat grayClahe;
            clahe->apply(gray, grayClahe);

            // Sobel X + Y
            cv::Mat sobelX, sobelY;
            cv::Sobel(grayClahe, sobelX, CV_32F, 1, 0, params_.sobelKSize);
            cv::Sobel(grayClahe, sobelY, CV_32F, 0, 1, params_.sobelKSize);

            // magnitude
            cv::Mat mag;
            cv::magnitude(sobelX, sobelY, mag);

            // clip to [0,255] + convert to uint8
            cv::Mat magClipped, magU8;
            cv::min(mag, 255.0, magClipped);
            magClipped.convertTo(magU8, CV_8U);

            // threshold
            cv::Mat bin;
            cv::threshold(magU8, bin, params_.edgeThreshold, 255, cv::THRESH_BINARY);

            // dilate
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
            cv::dilate(bin, binary, kernel, cv::Point(-1, -1), params_.dilateIter);
#endif

            // ---- 2. findContours (CPU only) ----
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            // ---- 3. 过滤 + 构建 Detection ----
            const double img_area = static_cast<double>(image.cols) * image.rows;
            const double score_divisor = img_area * 0.01; // 1% 面积 → confidence=1.0

            for (const auto& cnt : contours) {
                cv::Rect bbox = cv::boundingRect(cnt);
                if (bbox.width * bbox.height < params_.minBboxArea)
                    continue;

                double area = cv::contourArea(cnt);
                Detection d;
                d.bounding_box = bbox;
                d.class_id = 2;
                d.class_name = "defect";
                d.confidence = static_cast<float>(
                    std::min(1.0, area / score_divisor));
                detections.push_back(d);
            }

            // ---- 4. 按置信度降序排序 ----
            std::sort(detections.begin(), detections.end(),
                      [](const Detection& a, const Detection& b) {
                          return a.confidence > b.confidence;
                      });

            // ---- 5. 双判据 NMS（IoU 或包含率）----
            detections = edgeNms(detections);

            SPDLOG_DEBUG("[Detect] EdgeDetector: {} defects", detections.size());
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[Detect] EdgeDetector::detect exception: {}", e.what());
            detections.clear();
        }
        return detections;
    }

    cv::Mat drawDetections(const cv::Mat& image, const std::vector<Detection>& detections) override {
        cv::Mat result = image.clone();
        for (const auto& d : detections)
            cv::rectangle(result, d.bounding_box, cv::Scalar(0, 255, 0), 2); // 绿色（与 det.py 一致）
        std::string label = "defect: " + std::to_string(detections.size());
        cv::putText(result, label, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                    cv::Scalar(0, 255, 0), 2);
        return result;
    }

    // ---- 参数存取 ----
    void setParams(const EdgeDetectionParams& p) {
        params_ = p;
        params_.sobelKSize |= 1;                // 强制奇数核
        if (params_.sobelKSize < 1) params_.sobelKSize = 3;
        if (params_.dilateIter < 0) params_.dilateIter = 0;
        if (params_.minBboxArea < 0) params_.minBboxArea = 0;
    }
    EdgeDetectionParams params() const { return params_; }

    // ---- IDetector 存值接口 ----
    void setConfidenceThreshold(float threshold) override { conf_threshold_ = threshold; }
    void setNmsThreshold(float threshold) override { nms_threshold_ = threshold; }
    float getConfidenceThreshold() const override { return conf_threshold_; }
    float getNmsThreshold() const override { return nms_threshold_; }
    void setInputSize(int w, int h) override { input_width_ = w; input_height_ = h; }
    int getInputWidth() const override { return input_width_; }
    int getInputHeight() const override { return input_height_; }

private:
    /// 双判据 NMS（对应 det.py 第 44-74 行）。
    /// 已按 confidence 降序的 detections 中，对每个保留框 i，
    /// 检查后续框 j 的 IoU（inter/union）或包含率（inter/min_area），
    /// 任一超阈值即抑制 j。不合并框。
    std::vector<Detection> edgeNms(const std::vector<Detection>& detections) const {
        const size_t n = detections.size();
        if (n <= 1) return detections;

        std::vector<char> suppressed(n, 0);
        std::vector<Detection> kept;

        for (size_t i = 0; i < n; ++i) {
            if (suppressed[i]) continue;
            kept.push_back(detections[i]);

            const cv::Rect& ri = detections[i].bounding_box;
            int ix1 = ri.x, iy1 = ri.y;
            int ix2 = ri.x + ri.width, iy2 = ri.y + ri.height;
            double area_i = static_cast<double>(ri.width) * ri.height;

            for (size_t j = i + 1; j < n; ++j) {
                if (suppressed[j]) continue;

                const cv::Rect& rj = detections[j].bounding_box;
                int jx1 = rj.x, jy1 = rj.y;
                int jx2 = rj.x + rj.width, jy2 = rj.y + rj.height;

                int xx1 = std::max(ix1, jx1), yy1 = std::max(iy1, jy1);
                int xx2 = std::min(ix2, jx2), yy2 = std::min(iy2, jy2);

                if (xx2 <= xx1 || yy2 <= yy1) continue; // 无交集

                double inter = static_cast<double>(xx2 - xx1 + 1) * (yy2 - yy1 + 1);
                double area_j = static_cast<double>(rj.width) * rj.height;
                double iou = inter / (area_i + area_j - inter + 1e-8);
                double containment = inter / (std::min(area_i, area_j) + 1e-8);

                if (iou > params_.nmsIouThresh || containment > params_.nmsContainThresh)
                    suppressed[j] = 1;
            }
        }
        return kept;
    }

    EdgeDetectionParams params_;
    float conf_threshold_ = 0.3f;
    float nms_threshold_ = 0.3f;
    int input_width_ = 640;
    int input_height_ = 640;
};

} // namespace detector
