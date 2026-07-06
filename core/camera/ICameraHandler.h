#pragma once

#include <string>
#include <vector>
#include <functional>
#include <opencv2/core/mat.hpp>

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
    /// Trigger-based still capture (independent of video stream, no mutex contention)
    virtual bool captureTriggerFrame(cv::Mat& frame) = 0;
    virtual void pauseStream() = 0;
    virtual void resumeStream() = 0;

    virtual void setImageCallback(std::function<void(const cv::Mat&)> callback) = 0;
    virtual void setStatusCallback(std::function<void(const CameraStatus&)> callback) = 0;

    virtual CameraStatus getStatus() const = 0;
    virtual bool isConnected() const = 0;
    virtual bool isCapturing() const = 0;

    virtual bool setExposure(float exposure) = 0;
    virtual float getExposure() const = 0;
    virtual float getRealExposure() const = 0;
    virtual void getExposureRange(float& min_ms, float& max_ms, float& def_ms) const = 0;
    virtual bool setAutoExposure(bool enable) = 0;
    virtual bool getAutoExposure() const = 0;
    virtual bool setRotation(int degrees) = 0;
    virtual int getRotation() const = 0;
    virtual bool setHFlip(bool flip) = 0;
    virtual bool getHFlip() const = 0;
    virtual bool setVFlip(bool flip) = 0;
    virtual bool getVFlip() const = 0;
    virtual bool setGain(float gain) = 0;
    virtual float getGain() const = 0;
    virtual bool setResolution(int width, int height) = 0;
    virtual void getResolution(int& width, int& height) const = 0;
    virtual std::vector<std::pair<int, int>> getSupportedResolutions() const = 0;
};

} // namespace camera
