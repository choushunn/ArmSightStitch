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
        
        // 参考项目启用自动曝光
        std::cout << "CameraHandler: Enabling auto exposure" << std::endl;
        Toupcam_put_AutoExpoEnable(hcam_, 1);
        
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

void __stdcall CameraHandler::cameraCallback(unsigned nEvent, void* ctx) {
    CameraHandler* handler = static_cast<CameraHandler*>(ctx);
    if (!handler || !handler->is_callback_active_) {
        return;
    }
    
    // Process camera events
    switch (nEvent) {
        case TOUPCAM_EVENT_IMAGE:
            std::cout << "CameraHandler: Received TOUPCAM_EVENT_IMAGE event" << std::endl;
            break; // Handled by pullImage thread
        case TOUPCAM_EVENT_EXPOSURE:
            std::cout << "CameraHandler: Received TOUPCAM_EVENT_EXPOSURE event" << std::endl;
            break;
        case TOUPCAM_EVENT_ERROR:
            std::cout << "CameraHandler: Received TOUPCAM_EVENT_ERROR event" << std::endl;
            handler->updateStatus(false, "Camera error");
            break;
        case TOUPCAM_EVENT_STILLIMAGE:
            std::cout << "CameraHandler: Received TOUPCAM_EVENT_STILLIMAGE event" << std::endl;
            break;
        case TOUPCAM_EVENT_DISCONNECTED:
            std::cout << "CameraHandler: Received TOUPCAM_EVENT_DISCONNECTED event" << std::endl;
            handler->updateStatus(false, "Camera disconnected");
            break;
        default:
            std::cout << "CameraHandler: Received unknown event: " << nEvent << std::endl;
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
