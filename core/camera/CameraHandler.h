#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <toupcam.h>

#include "ICameraHandler.h"

namespace camera {

class CameraHandler : public ICameraHandler {
public:
    CameraHandler();
    ~CameraHandler();

    /**
     * @brief Enumerate available cameras
     * @return Vector of camera devices
     */
    std::vector<CameraDevice> enumerateCameras();

    /**
     * @brief Connect to a camera
     * @param device_id Camera device ID
     * @return true if connected successfully, false otherwise
     */
    bool connect(const std::string& device_id = "");

    /**
     * @brief Disconnect from camera
     */
    void disconnect();

    /**
     * @brief Start continuous image capture
     * @return true if started successfully, false otherwise
     */
    bool startCapture();

    /**
     * @brief Stop continuous image capture
     */
    void stopCapture();

    /**
     * @brief Capture a single frame
     * @param frame Output frame
     * @return true if captured successfully, false otherwise
     */
    bool captureSingleFrame(cv::Mat& frame);

    /**
     * @brief Set image callback function
     * @param callback Callback function to be called when a new frame is available
     */
    void setImageCallback(std::function<void(const cv::Mat&)> callback);

    /**
     * @brief Set camera status callback function
     * @param callback Callback function to be called when camera status changes
     */
    void setStatusCallback(std::function<void(const CameraStatus&)> callback);

    /**
     * @brief Get current camera status
     * @return Camera status
     */
    CameraStatus getStatus() const;

    /**
     * @brief Check if camera is connected
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Check if camera is capturing
     * @return true if capturing, false otherwise
     */
    bool isCapturing() const;

    /**
     * @brief Set camera exposure time
     * @param exposure Exposure time in milliseconds
     * @return true if success, false otherwise
     */
    bool setExposure(float exposure);

    /**
     * @brief Get camera exposure time
     * @return Exposure time in milliseconds
     */
    float getExposure() const;

    /**
     * @brief Get actual exposure time from the camera hardware
     * @return actual exposure time in milliseconds
     */
    float getRealExposure() const;

    /**
     * @brief Get the camera's valid exposure time range
     * @param min_ms output minimum exposure in ms
     * @param max_ms output maximum exposure in ms
     * @param def_ms output default exposure in ms
     */
    void getExposureRange(float& min_ms, float& max_ms, float& def_ms) const;

    /**
     * @brief Enable or disable auto exposure
     * @param enable true to enable, false to disable
     * @return true if success, false otherwise
     */
    bool setAutoExposure(bool enable);

    /**
     * @brief Check if auto exposure is enabled
     * @return true if auto exposure is enabled, false otherwise
     */
    bool getAutoExposure() const;

    /**
     * @brief Set camera rotation angle
     * @param degrees Rotation angle (0, 90, 180, 270)
     * @return true if success, false otherwise
     */
    bool setRotation(int degrees);

    /**
     * @brief Get current camera rotation
     * @return Rotation angle in degrees (0, 90, 180, 270)
     */
    int getRotation() const;

    /**
     * @brief Set camera gain
     * @param gain Gain value
     * @return true if success, false otherwise
     */
    bool setGain(float gain);

    /**
     * @brief Get camera gain
     * @return Gain value
     */
    float getGain() const;

    /**
     * @brief Set camera resolution
     * @param width Width in pixels
     * @param height Height in pixels
     * @return true if success, false otherwise
     */
    bool setResolution(int width, int height);

    /**
     * @brief Get camera resolution
     * @param width Output width in pixels
     * @param height Output height in pixels
     */
    void getResolution(int& width, int& height) const;

    /**
     * @brief Get list of all supported resolutions from the camera
     * @return vector of (width, height) pairs
     */
    std::vector<std::pair<int, int>> getSupportedResolutions() const;

private:
    /**
     * @brief Camera callback function for ToupCam SDK
     * @param nEvent Event type
     * @param ctx Context pointer
     */
    static void __stdcall cameraCallback(unsigned nEvent, void* ctx);



    /**
     * @brief Update camera status
     * @param is_connected Connection status
     * @param message Status message
     */
    void updateStatus(bool is_connected, const std::string& message);

private:
    HToupcam hcam_ = nullptr;
    std::atomic<bool> connected_ = false;
    std::atomic<bool> capturing_ = false;
    std::atomic<bool> is_callback_active_ = false;
    std::mutex callback_mutex_;
    std::function<void(const cv::Mat&)> image_callback_;
    std::function<void(const CameraStatus&)> status_callback_;
    CameraStatus current_status_;
    std::vector<CameraDevice> available_cameras_;
    std::vector<uint8_t> image_buffer_;
    mutable std::mutex image_buffer_mutex_;
    std::mutex capture_mutex_;  // Protects Toupcam_PullImageV4 calls
    int image_width_ = 0;
    int image_height_ = 0;
    int buffer_size_ = 0;
    int pixel_format_ = 24; // RGB24 format (bits=24 in Toupcam_PullImageV4)
};

} // namespace camera
