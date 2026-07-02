#include "CameraHandler.h"

#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <QString>
#include <windows.h>

namespace camera {

// 计算缓冲区宽度的宏，与参考项目一致
#define TDIBWIDTHBYTES(bits) ((DWORD)(((bits) + 31) & (~31)) / 8)

CameraHandler::CameraHandler() {
    SPDLOG_INFO("Constructor called");
    
    // Initialize status
    current_status_.connected = false;
    current_status_.status_message = "Disconnected";
    current_status_.width = 0;
    current_status_.height = 0;
    current_status_.fps = 0.0f;
    
    connected_ = false;
    capturing_ = false;
    is_callback_active_ = false;
    
    SPDLOG_INFO("Constructor completed");
}

CameraHandler::~CameraHandler() {
    disconnect();
}

std::vector<CameraDevice> CameraHandler::enumerateCameras() {
    SPDLOG_DEBUG("enumerateCameras called");
    available_cameras_.clear();
    
    try {
        SPDLOG_DEBUG("Creating devices array");
        ToupcamDeviceV2 devices[TOUPCAM_MAX];
        
        SPDLOG_DEBUG("Calling Toupcam_EnumV2...");
        unsigned count = Toupcam_EnumV2(devices);
        SPDLOG_DEBUG("Toupcam_EnumV2 returned {} cameras", count);
        
        for (unsigned i = 0; i < count; ++i) {
            SPDLOG_DEBUG("Processing camera {}...", i);
            CameraDevice device;
            #if defined(_WIN32)
            device.id = QString::fromWCharArray(devices[i].id).toStdString();
            device.display_name = QString::fromWCharArray(devices[i].displayname).toStdString();
            device.model_name = QString::fromWCharArray(devices[i].model->name).toStdString();
            #else
            device.id = devices[i].id;
            device.display_name = devices[i].displayname;
            device.model_name = devices[i].model->name;
            #endif
            device.width = devices[i].model->res[0].width;
            device.height = devices[i].model->res[0].height;
            
            SPDLOG_DEBUG("Camera {}: ID={}, Name={}, Model={}", i, device.id, device.display_name, device.model_name);
            available_cameras_.push_back(device);
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error enumerating cameras: {}", e.what());
    } catch (...) {
        SPDLOG_ERROR("Unknown error enumerating cameras");
    }
    
    SPDLOG_DEBUG("enumerateCameras completed, found {} cameras", available_cameras_.size());
    return available_cameras_;
}

bool CameraHandler::connect(const std::string& device_id) {
    disconnect();
    
    try {
        // 参考项目使用索引打开相机，忽略device_id参数
        SPDLOG_INFO("Attempting to open camera by index 0");
        hcam_ = Toupcam_OpenByIndex(0);
        
        if (!hcam_) {
            updateStatus(false, "Failed to open camera");
            return false;
        }
        
        // Get camera information
        int width = 0, height = 0;
        if (SUCCEEDED(Toupcam_get_Size(hcam_, &width, &height))) {
            image_width_ = width;
            image_height_ = height;
        }
        else {
            // Use default resolution
            image_width_ = 1920;
            image_height_ = 1080;
        }
        
        SPDLOG_INFO("Camera resolution: {}x{}", image_width_, image_height_);
        
        // 参考项目使用TDIBWIDTHBYTES计算缓冲区大小
        buffer_size_ = TDIBWIDTHBYTES(image_width_ * 24) * image_height_;
        SPDLOG_DEBUG("Buffer size: {}", buffer_size_);
        image_buffer_.resize(buffer_size_);
        
        // 参考项目设置字节序为RGB
        SPDLOG_DEBUG("Setting byte order to RGB");
        Toupcam_put_Option(hcam_, TOUPCAM_OPTION_BYTEORDER, 0);
        
        // 参考项目启用自动曝光
        SPDLOG_DEBUG("Enabling auto exposure");
        Toupcam_put_AutoExpoEnable(hcam_, 1);
        
        connected_ = true;
        capturing_ = false;
        is_callback_active_ = false;
        updateStatus(true, "Connected to camera");
        
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error connecting to camera: {}", e.what());
        updateStatus(false, std::string("Connection error: ") + e.what());
        return false;
    }
}

void CameraHandler::disconnect() {
    if (hcam_) {
        capturing_ = false;
        is_callback_active_ = false;
        
        Toupcam_Stop(hcam_);
        Toupcam_Close(hcam_);
        hcam_ = nullptr;
    }
    
    connected_ = false;
    capturing_ = false;
    updateStatus(false, "Disconnected from camera");
}

bool CameraHandler::startCapture() {
    SPDLOG_DEBUG("startCapture called");
    
    if (!connected_) {
        SPDLOG_DEBUG("startCapture failed - camera not connected");
        return false;
    }
    
    try {
        // If already capturing, return true
        if (capturing_) {
            SPDLOG_DEBUG("startCapture skipped - already capturing");
            return true;
        }
        
        // Start pull mode with callback
        SPDLOG_DEBUG("Starting pull mode with callback");
        HRESULT result = Toupcam_StartPullModeWithCallback(hcam_, reinterpret_cast<PTOUPCAM_EVENT_CALLBACK>(cameraCallback), this);
        if (FAILED(result)) {
            SPDLOG_ERROR("Failed to start pull mode, HRESULT: {}", result);
            updateStatus(false, "Failed to start capture");
            return false;
        }
        
        SPDLOG_DEBUG("Pull mode started successfully");
        
        capturing_ = true;
        is_callback_active_ = true;
        
        updateStatus(true, "Capture started");
        SPDLOG_DEBUG("startCapture completed successfully");
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error starting capture: {}", e.what());
        updateStatus(false, std::string("Failed to start capture: ") + e.what());
        return false;
    }
}

void CameraHandler::stopCapture() {
    SPDLOG_DEBUG("stopCapture called");
    
    if (!capturing_) {
        SPDLOG_DEBUG("stopCapture skipped - not capturing");
        return;
    }
    
    capturing_ = false;
    is_callback_active_ = false;
    
    if (hcam_) {
        SPDLOG_DEBUG("Stopping camera");
        Toupcam_Stop(hcam_);
    }
    
    updateStatus(true, "Capture stopped");
    SPDLOG_DEBUG("stopCapture completed");
}

bool CameraHandler::captureSingleFrame(cv::Mat& frame) {
    if (!connected_) {
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(capture_mutex_);

        std::vector<uint8_t> single_frame_buffer(buffer_size_);
        
        // 尝试捕获图像，最多尝试3次
        int attempts = 0;
        const int max_attempts = 3;
        HRESULT result;
        
        while (attempts < max_attempts) {
            attempts++;
            result = Toupcam_PullImageV4(hcam_, single_frame_buffer.data(), 0, 24, 0, nullptr);
            if (SUCCEEDED(result)) {
                break;
            }
            // 等待50ms后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        if (FAILED(result)) {
            return false;
        }
        
        // 计算实际的步长（考虑内存对齐）
        int stride = TDIBWIDTHBYTES(image_width_ * 24);
        
        // 参考项目使用QImage::Format_BGR888，所以我们直接使用BGR格式
        cv::Mat bgr(image_height_, image_width_, CV_8UC3, single_frame_buffer.data(), stride);
        frame = bgr.clone();
        
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

void CameraHandler::setImageCallback(std::function<void(const cv::Mat&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    image_callback_ = callback;
}

void CameraHandler::setStatusCallback(std::function<void(const CameraStatus&)> callback) {
    status_callback_ = callback;
}

CameraStatus CameraHandler::getStatus() const {
    return current_status_;
}

bool CameraHandler::isConnected() const {
    return connected_;
}

bool CameraHandler::isCapturing() const {
    return capturing_;
}

bool CameraHandler::setExposure(float exposure) {
    if (!connected_) {
        return false;
    }
    
    try {
        unsigned exposure_us = static_cast<unsigned>(exposure * 1000); // Convert ms to us
        if (FAILED(Toupcam_put_ExpoTime(hcam_, exposure_us))) {
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error setting exposure: {}", e.what());
        return false;
    }
}

float CameraHandler::getExposure() const {
    if (!connected_) {
        return 0.0f;
    }
    
    try {
        unsigned exposure_us = 0;
        if (SUCCEEDED(Toupcam_get_ExpoTime(hcam_, &exposure_us))) {
            return static_cast<float>(exposure_us) / 1000.0f; // Convert us to ms
        }
        return 0.0f;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error getting exposure: {}", e.what());
        return 0.0f;
    }
}

bool CameraHandler::setAutoExposure(bool enable) {
    if (!connected_) {
        return false;
    }

    try {
        if (FAILED(Toupcam_put_AutoExpoEnable(hcam_, enable ? 1 : 0))) {
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error setting auto exposure: {}", e.what());
        return false;
    }
}

bool CameraHandler::getAutoExposure() const {
    if (!connected_) {
        return false;
    }

    try {
        int mode = 0;
        if (SUCCEEDED(Toupcam_get_AutoExpoEnable(hcam_, &mode))) {
            return mode != 0;
        }
        return false;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error getting auto exposure: {}", e.what());
        return false;
    }
}

bool CameraHandler::setGain(float gain) {
    if (!connected_) {
        return false;
    }
    
    try {
        unsigned short gain_value = static_cast<unsigned short>(gain * 100); // Gain in percentage
        if (FAILED(Toupcam_put_ExpoAGain(hcam_, gain_value))) {
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error setting gain: {}", e.what());
        return false;
    }
}

float CameraHandler::getGain() const {
    if (!connected_) {
        return 0.0f;
    }
    
    try {
        unsigned short gain_value = 0;
        if (SUCCEEDED(Toupcam_get_ExpoAGain(hcam_, &gain_value))) {
            return static_cast<float>(gain_value) / 100.0f;
        }
        return 0.0f;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error getting gain: {}", e.what());
        return 0.0f;
    }
}

bool CameraHandler::setResolution(int width, int height) {
    if (!connected_) {
        return false;
    }
    
    try {
        // Set resolution
        if (FAILED(Toupcam_put_Size(hcam_, width, height))) {
            return false;
        }
        
        // Update internal variables
        image_width_ = width;
        image_height_ = height;
        buffer_size_ = TDIBWIDTHBYTES(width * 24) * height;
        image_buffer_.resize(buffer_size_);
        
        // Update status
        current_status_.width = width;
        current_status_.height = height;
        if (status_callback_) {
            status_callback_(current_status_);
        }
        
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error setting resolution: {}", e.what());
        return false;
    }
}

void CameraHandler::getResolution(int& width, int& height) const {
    width = image_width_;
    height = image_height_;
}

void __stdcall CameraHandler::cameraCallback(unsigned nEvent, void* ctx) {
    CameraHandler* handler = static_cast<CameraHandler*>(ctx);
    if (!handler || !handler->is_callback_active_) {
        return;
    }
    
    // Process camera events
    switch (nEvent) {
        case TOUPCAM_EVENT_IMAGE:
            SPDLOG_DEBUG("Received TOUPCAM_EVENT_IMAGE event");
            break; // Handled by pullImage thread
        case TOUPCAM_EVENT_EXPOSURE:
            SPDLOG_DEBUG("Received TOUPCAM_EVENT_EXPOSURE event");
            break;
        case TOUPCAM_EVENT_ERROR:
            SPDLOG_ERROR("Received TOUPCAM_EVENT_ERROR event");
            handler->updateStatus(false, "Camera error");
            break;
        case TOUPCAM_EVENT_STILLIMAGE:
            SPDLOG_DEBUG("Received TOUPCAM_EVENT_STILLIMAGE event");
            break;
        case TOUPCAM_EVENT_DISCONNECTED:
            SPDLOG_DEBUG("Received TOUPCAM_EVENT_DISCONNECTED event");
            handler->updateStatus(false, "Camera disconnected");
            break;
        default:
            SPDLOG_DEBUG("Received unknown event: {}", nEvent);
            break;
    }
}



void CameraHandler::updateStatus(bool is_connected, const std::string& message) {
    current_status_.connected = is_connected;
    current_status_.status_message = message;
    current_status_.width = image_width_;
    current_status_.height = image_height_;
    
    if (status_callback_) {
        status_callback_(current_status_);
    }
}

} // namespace camera
