#pragma once

#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

namespace detector {

struct Detection {
    cv::Rect bounding_box;
    float confidence = 0.0f;
    int class_id = 0;
    std::string class_name;
};

class IDetector {
public:
    virtual ~IDetector() = default;

    virtual bool loadModel(const std::string& param_path, const std::string& bin_path) = 0;
    virtual std::vector<Detection> detect(const cv::Mat& image) = 0;
    virtual cv::Mat drawDetections(const cv::Mat& image, const std::vector<Detection>& detections) = 0;

    virtual void setConfidenceThreshold(float threshold) = 0;
    virtual void setNmsThreshold(float threshold) = 0;
    virtual float getConfidenceThreshold() const = 0;
    virtual float getNmsThreshold() const = 0;

    virtual void setInputSize(int width, int height) = 0;
    virtual int getInputWidth() const = 0;
    virtual int getInputHeight() const = 0;
};

} // namespace detector
