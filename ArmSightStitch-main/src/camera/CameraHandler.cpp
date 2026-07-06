#include "CameraHandler.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <QString>
#include <windows.h>

namespace camera {

// 计算缓冲区宽度的宏，与参考项目一致
#define TDIBWIDTHBYTES(bits) ((DWORD)(((bits) + 31) & (~31)) / 8)

CameraHandler::CameraHandler() {
    std::cout << "CameraHandler: Constructor called" << std::endl;
    
    // Initialize status
    current_status_.connected = false;
    current_status_.status_message = "Disconnected";
    current_status_.width = 0;
    current_status_.height = 0;
    current_status_.fps = 0.0f;
    
    connected_ = false;
    capturing_ = false;
    is_callback_active_ = false;
    
    std::cout << "CameraHandler: Constructor completed" << std::endl;
}

CameraHandler::~CameraHandler() {
    disconnect();
}

std::vector<CameraDevice> CameraHandler::enumerateCameras() {
    std::cout << "CameraHandler: enumerateCameras called" << std::endl;
    available_cameras_.clear();
    
    try {
        std::cout << "CameraHandler: Creating devices array" << std::endl;
        ToupcamDeviceV2 devices[TOUPCAM_MAX];
        
        std::cout << "CameraHandler: Calling Toupcam_EnumV2..." << std::endl;
        unsigned count = Toupcam_EnumV2(devices);
        std::cout << "CameraHandler: Toupcam_EnumV2 returned " << count << " cameras" << std::endl;
        
        for (unsigned i = 0; i < count; ++i) {
            std::cout << "CameraHandler: Processing camera " << i << "..." << std::endl;
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
            
            std::cout << "CameraHandler: Camera " << i << ": ID=" << device.id << ", Name=" << device.display_name << ", Model=" << device.model_name << std::endl;
            available_cameras_.push_back(device);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error enumerating cameras: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error enumerating cameras" << std::endl;
    }
    
    std::cout << "CameraHandler: enumerateCameras completed, found " << available_cameras_.size() << " cameras" << std::endl;
    return available_cameras_;
}

bool CameraHandler::connect(const std::string& device_id) {
    disconnect();
    
    try {
        // 参考项目使用索引打开相机，忽略device_id参数
        std::cout << "CameraHandler: Attempting to open camera by index 0" << std::endl;
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
        
        std::cout << "CameraHandler: Camera resolution: " << image_width_ << "x" << image_height_ << std::endl;
        
        // 参考项目使用TDIBWIDTHBYTES计算缓冲区大小
        buffer_size_ = TDIBWIDTHBYTES(image_width_ * 24) * image_height_;
        std::cout << "CameraHandler: Buffer size: " << buffer_size_ << std::endl;
        image_buffer_.resize(buffer_size_);
        
        // 参考项目设置字节序为RGB
        std::cout << "CameraHandler: Setting byte order to RGB" << std::endl;
        Toupcam_put_Option(hcam_, TOUPCAM_OPTION_BYTEORDER, 0);
        
        // 禁用自动曝光（默认关闭）
        std::cout << "CameraHandler: Disabling auto exposure (default)" << std::endl;
        Toupcam_put_AutoExpoEnable(hcam_, 0);
        
        connected_ = true;
        capturing_ = false;
        is_callback_active_ = false;
        updateStatus(true, "Connected to camera");
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error connecting to camera: " << e.what() << std::endl;
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
    std::cout << "CameraHandler: startCapture called" << std::endl;
    
    if (!connected_) {
        std::cout << "CameraHandler: startCapture failed - camera not connected" << std::endl;
        return false;
    }
    
    try {
        // If already capturing, return true
        if (capturing_) {
            std::cout << "CameraHandler: startCapture skipped - already capturing" << std::endl;
            return true;
        }
        
        // Start pull mode with callback
        std::cout << "CameraHandler: Starting pull mode with callback" << std::endl;
        HRESULT result = Toupcam_StartPullModeWithCallback(hcam_, reinterpret_cast<PTOUPCAM_EVENT_CALLBACK>(cameraCallback), this);
        if (FAILED(result)) {
            std::cerr << "CameraHandler: Failed to start pull mode, HRESULT: " << result << std::endl;
            updateStatus(false, "Failed to start capture");
            return false;
        }
        
        std::cout << "CameraHandler: Pull mode started successfully" << std::endl;
        
        capturing_ = true;
        is_callback_active_ = true;
        
        updateStatus(true, "Capture started");
        std::cout << "CameraHandler: startCapture completed successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error starting capture: " << e.what() << std::endl;
        updateStatus(false, std::string("Failed to start capture: ") + e.what());
        return false;
    }
}

void CameraHandler::stopCapture() {
    std::cout << "CameraHandler: stopCapture called" << std::endl;
    
    if (!capturing_) {
        std::cout << "CameraHandler: stopCapture skipped - not capturing" << std::endl;
        return;
    }
    
    capturing_ = false;
    is_callback_active_ = false;
    
    if (hcam_) {
        std::cout << "CameraHandler: Stopping camera" << std::endl;
        Toupcam_Stop(hcam_);
    }
    
    updateStatus(true, "Capture stopped");
    std::cout << "CameraHandler: stopCapture completed" << std::endl;
}

bool CameraHandler::captureSingleFrame(cv::Mat& frame) {
    if (!connected_) {
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (!cached_frame_.empty()) {
            frame = cached_frame_.clone();
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "CameraHandler: Exception in captureSingleFrame: " << e.what() << std::endl;
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
        std::cerr << "Error setting exposure: " << e.what() << std::endl;
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
        std::cerr << "Error getting exposure: " << e.what() << std::endl;
        return 0.0f;
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
        std::cerr << "Error setting gain: " << e.what() << std::endl;
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
        std::cerr << "Error getting gain: " << e.what() << std::endl;
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
        std::cerr << "Error setting resolution: " << e.what() << std::endl;
        return false;
    }
}

void CameraHandler::getResolution(int& width, int& height) const {
    width = image_width_;
    height = image_height_;
}

bool CameraHandler::setAutoExposure(bool enable) {
    if (!connected_) {
        return false;
    }
    
    try {
        if (FAILED(Toupcam_put_AutoExpoEnable(hcam_, enable ? 1 : 0))) {
            std::cerr << "CameraHandler: Failed to set auto exposure" << std::endl;
            return false;
        }
        std::cout << "CameraHandler: Auto exposure " << (enable ? "enabled" : "disabled") << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error setting auto exposure: " << e.what() << std::endl;
        return false;
    }
}

bool CameraHandler::getAutoExposure() const {
    if (!connected_) {
        return false;
    }
    
    try {
        int enabled = 0;
        if (SUCCEEDED(Toupcam_get_AutoExpoEnable(hcam_, &enabled))) {
            return enabled != 0;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error getting auto exposure: " << e.what() << std::endl;
        return false;
    }
}

void CameraHandler::setImageRotation(int angle) {
    if (angle == 0 || angle == 90 || angle == 180 || angle == 270) {
        image_rotation_ = angle;
        std::cout << "CameraHandler: Image rotation set to " << angle << " degrees" << std::endl;
    } else {
        std::cerr << "CameraHandler: Invalid rotation angle: " << angle << ". Must be 0, 90, 180, or 270." << std::endl;
    }
}

int CameraHandler::getImageRotation() const {
    return image_rotation_;
}

void CameraHandler::setCropSize(int width, int height) {
    crop_width_ = width;
    crop_height_ = height;
    std::cout << "CameraHandler: Crop size set to " << width << "x" << height << std::endl;
}

void __stdcall CameraHandler::cameraCallback(unsigned nEvent, void* ctx) {
    CameraHandler* handler = static_cast<CameraHandler*>(ctx);
    if (!handler || !handler->is_callback_active_) {
        return;
    }
    
    switch (nEvent) {
        case TOUPCAM_EVENT_IMAGE: {
            try {
                std::vector<uint8_t> buffer(handler->buffer_size_);
                HRESULT result = Toupcam_PullImageV4(handler->hcam_, buffer.data(), 0, 24, 0, nullptr);
                if (SUCCEEDED(result)) {
                    int stride = TDIBWIDTHBYTES(handler->image_width_ * 24);
                    cv::Mat bgr(handler->image_height_, handler->image_width_, CV_8UC3, buffer.data(), stride);
                    
                    cv::Mat frame = bgr.clone();
                    
                    // 先进行中心裁剪（不跟随旋转）
                    if (handler->crop_width_ > 0 && handler->crop_height_ > 0) {
                        int crop_w = handler->crop_width_;
                        int crop_h = handler->crop_height_;
                        
                        // 计算中心裁剪的起始位置
                        int x_offset = (handler->image_width_ - crop_w) / 2;
                        int y_offset = (handler->image_height_ - crop_h) / 2;
                        
                        // 确保裁剪区域在图像范围内
                        if (x_offset >= 0 && y_offset >= 0 && 
                            x_offset + crop_w <= handler->image_width_ && 
                            y_offset + crop_h <= handler->image_height_) {
                            frame = frame(cv::Rect(x_offset, y_offset, crop_w, crop_h)).clone();
                        }
                    }
                    
                    // 然后进行旋转
                    if (handler->image_rotation_ != 0) {
                        int rotateCode = handler->image_rotation_ == 90 ? cv::ROTATE_90_CLOCKWISE :
                                        handler->image_rotation_ == 180 ? cv::ROTATE_180 :
                                        handler->image_rotation_ == 270 ? cv::ROTATE_90_COUNTERCLOCKWISE :
                                        cv::ROTATE_90_CLOCKWISE;
                        cv::rotate(frame, frame, rotateCode);
                    }
                    
                    std::lock_guard<std::mutex> lock(handler->frame_mutex_);
                    handler->cached_frame_ = frame;
                }
            } catch (const std::exception& e) {
                std::cerr << "CameraHandler: Exception in image callback: " << e.what() << std::endl;
            }
            break;
        }
        case TOUPCAM_EVENT_EXPOSURE:
            break;
        case TOUPCAM_EVENT_ERROR:
            handler->updateStatus(false, "Camera error");
            break;
        case TOUPCAM_EVENT_STILLIMAGE:
            break;
        case TOUPCAM_EVENT_DISCONNECTED:
            handler->updateStatus(false, "Camera disconnected");
            break;
        default:
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
