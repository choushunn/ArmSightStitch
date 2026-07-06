#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <toupcam.h>
#include <QObject>

#include "ICameraHandler.h"

namespace camera {

class CameraHandler : public QObject, public ICameraHandler {
    Q_OBJECT

public:
    CameraHandler();
    ~CameraHandler();

    std::vector<CameraDevice> enumerateCameras();
    bool connect(const std::string& device_id = "");
    void disconnect();
    bool startCapture();
    void stopCapture();
    bool captureSingleFrame(cv::Mat& frame);
    bool captureTriggerFrame(cv::Mat& frame) override;
    void pauseStream() override;
    void resumeStream() override;

    void setStatusCallback(std::function<void(const CameraStatus&)> callback);
    CameraStatus getStatus() const;
    bool isConnected() const;
    bool isCapturing() const;

    bool setExposure(float exposure);
    float getExposure() const;
    float getRealExposure() const;
    void getExposureRange(float& min_ms, float& max_ms, float& def_ms) const;
    bool setAutoExposure(bool enable);
    bool getAutoExposure() const;
    bool setRotation(int degrees);
    int getRotation() const;
    bool setHFlip(bool flip);
    bool getHFlip() const;
    bool setVFlip(bool flip);
    bool getVFlip() const;
    bool setGain(float gain);
    float getGain() const;
    bool setResolution(int width, int height);
    void getResolution(int& width, int& height) const;
    std::vector<std::pair<int, int>> getSupportedResolutions() const;

signals:
    void frameReady(const cv::Mat& preview);
    void exposureChanged();
    void cameraDisconnected();
    void cameraError(const QString& msg);

private:
    static void __stdcall eventCallback(unsigned nEvent, void* ctx);
    void handleEvent(unsigned nEvent);
    void handleFrame();
    void updateFinalSize();
    void updateStatus(bool is_connected, const std::string& message);

    HToupcam hcam_ = nullptr;
    std::atomic<bool> connected_ = false;
    std::atomic<bool> capturing_ = false;
    mutable std::mutex status_mutex_;
    std::function<void(const CameraStatus&)> status_callback_;
    CameraStatus current_status_;
    std::vector<CameraDevice> available_cameras_;
    std::vector<uint8_t> pull_buffer_;
    std::mutex capture_mutex_;
    int image_width_ = 0;
    int image_height_ = 0;
    int final_width_ = 0;
    int final_height_ = 0;
};

} // namespace camera
