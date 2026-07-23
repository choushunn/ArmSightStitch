#pragma once

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
    bool setNegative(bool enable) override;
    bool getNegative() const override;
    bool setGain(float gain);
    float getGain() const;
    void getGainRange(unsigned short& min_pct, unsigned short& max_pct, unsigned short& def_pct) const override;
    bool setSharpening(unsigned short strength) override;
    unsigned short getSharpening() const override;
    bool setResolution(int width, int height);
    void getResolution(int& width, int& height) const;
    std::vector<std::pair<int, int>> getSupportedResolutions() const;

signals:
    void frameReady(const cv::Mat& preview);
    void exposureChanged();
    void cameraDisconnected();
    void cameraError(const QString& msg);

private:
    // Push Mode: SDK pushes frame data (zero-copy, no PullImageV4 overhead)
    static void __stdcall pushDataCallback(const void* pData, const ToupcamFrameInfoV3* pInfo, int bSnap, void* ctx);
    static void __stdcall pushEventCallback(unsigned nEvent, void* ctx);
    void handlePushEvent(unsigned nEvent);

    void updateStatus(bool is_connected, const std::string& message);

    // Frame capture: pushDataCallback stores full-res copy, captureTriggerFrame reads it
    std::mutex frame_mutex_;
    cv::Mat latest_full_frame_;

    HToupcam hcam_ = nullptr;
    std::atomic<bool> connected_ = false;
    std::atomic<bool> capturing_ = false;
    std::atomic<bool> push_active_ = false;
    mutable std::mutex status_mutex_;
    std::function<void(const CameraStatus&)> status_callback_;
    CameraStatus current_status_;
    std::vector<CameraDevice> available_cameras_;

    int image_width_ = 0;
    int image_height_ = 0;
    int rotation_ = 0;  // software: 0/90/180/270
    bool negative_ = false;  // software: preview-only negative film
};

} // namespace camera
