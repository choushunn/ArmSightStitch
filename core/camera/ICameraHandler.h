#pragma once

#include <string>
#include <vector>
#include <functional>
#include <opencv2/opencv.hpp>

namespace camera {

struct CameraDevice {
    std::string id;
    std::string display_name;
    std::string model_name;
    int width = 0;
    int height = 0;
};

struct CameraStatus {
    bool connected = false;
    std::string status_message;
    int width = 0;
    int height = 0;
    float fps = 0.0f;
};

class ICameraHandler {
public:
    virtual ~ICameraHandler() = default;

    virtual std::vector<CameraDevice> enumerateCameras() = 0;
    virtual bool connect(const std::string& device_id = "") = 0;
    virtual void disconnect() = 0;

    virtual bool startCapture() = 0;
    virtual void stopCapture() = 0;
    virtual bool captureSingleFrame(cv::Mat& frame) = 0;

    virtual void setImageCallback(std::function<void(const cv::Mat&)> callback) = 0;
    virtual void setStatusCallback(std::function<void(const CameraStatus&)> callback) = 0;

    virtual CameraStatus getStatus() const = 0;
    virtual bool isConnected() const = 0;
    virtual bool isCapturing() const = 0;

    virtual bool setExposure(float exposure) = 0;
    virtual float getExposure() const = 0;
    virtual bool setGain(float gain) = 0;
    virtual float getGain() const = 0;
    virtual bool setResolution(int width, int height) = 0;
    virtual void getResolution(int& width, int& height) const = 0;
};

} // namespace camera
