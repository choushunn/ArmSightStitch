#pragma once

#include <net.h>
#include <vector>
#include <string>

#include "IDetector.h"

namespace detector {

class YoloDetector : public IDetector {
public:
    YoloDetector();
    ~YoloDetector();

    /**
     * @brief Load YOLO model from files
     * @param param_path Path to model param file
     * @param bin_path Path to model bin file
     * @return true if model loaded successfully, false otherwise
     */
    bool loadModel(const std::string& param_path, const std::string& bin_path);
    bool isModelLoaded() const override { return model_loaded_; }

    /**
     * @brief Detect objects in an image
     * @param image Input image
     * @return Vector of detections
     */
    std::vector<Detection> detect(const cv::Mat& image);

    /**
     * @brief Draw detections on image
     * @param image Input image
     * @param detections Vector of detections
     * @return Image with detections drawn
     */
    cv::Mat drawDetections(const cv::Mat& image, const std::vector<Detection>& detections);

    /**
     * @brief Set confidence threshold
     * @param threshold Confidence threshold (0.0-1.0)
     */
    void setConfidenceThreshold(float threshold);

    /**
     * @brief Set NMS threshold
     * @param threshold NMS threshold (0.0-1.0)
     */
    void setNmsThreshold(float threshold);

    /**
     * @brief Get confidence threshold
     * @return Confidence threshold
     */
    float getConfidenceThreshold() const;

    /**
     * @brief Get NMS threshold
     * @return NMS threshold
     */
    float getNmsThreshold() const;

    /**
     * @brief Set input size
     * @param width Input width
     * @param height Input height
     */
    void setInputSize(int width, int height);

    /**
     * @brief Get input width
     * @return Input width
     */
    int getInputWidth() const;

    /**
     * @brief Get input height
     * @return Input height
     */
    int getInputHeight() const;

private:
    /**
     * @brief Preprocess image for detection
     * @param image Input image
     * @return Preprocessed image as ncnn::Mat
     */
    ncnn::Mat preprocess(const cv::Mat& image);

    /**
     * @brief Apply non-maximum suppression
     * @param detections Vector of detections
     * @return Vector of suppressed detections
     */
    std::vector<Detection> applyNMS(std::vector<Detection>& detections);



private:
    ncnn::Net yolo_net_;
    float conf_threshold_ = 0.3f;
    float nms_threshold_ = 0.3f;
    int input_width_ = 640;
    int input_height_ = 640;
    std::vector<std::string> class_names_ = {"crack", "scratch", "dent"};
    std::vector<std::vector<float>> anchors_ = {
        {10.0f, 13.0f, 16.0f, 30.0f, 33.0f, 23.0f},
        {30.0f, 61.0f, 62.0f, 45.0f, 59.0f, 119.0f},
        {116.0f, 90.0f, 156.0f, 198.0f, 373.0f, 326.0f}
    };
    std::vector<int> strides_ = {8, 16, 32};
    bool model_loaded_ = false;
};

} // namespace detector
