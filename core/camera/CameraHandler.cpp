#include "CameraHandler.h"

#include <spdlog/spdlog.h>
#include <opencv2/imgproc.hpp>
#include <thread>
#include <chrono>
#include <windows.h>

namespace camera {

static std::string wcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

// TDIBWIDTHBYTES is defined in toupcam.h

CameraHandler::CameraHandler() {
    current_status_.connected = false;
    current_status_.status_message = "Disconnected";
    current_status_.width = 0;
    current_status_.height = 0;
    current_status_.fps = 0.0f;
    connected_ = false;
    capturing_ = false;
}

CameraHandler::~CameraHandler() { disconnect(); }

std::vector<CameraDevice> CameraHandler::enumerateCameras() {
    available_cameras_.clear();
    try {
        ToupcamDeviceV2 devices[TOUPCAM_MAX];
        unsigned count = Toupcam_EnumV2(devices);
        for (unsigned i = 0; i < count; ++i) {
            CameraDevice device;
#if defined(_WIN32)
            device.id = wcharToUtf8(devices[i].id);
            device.display_name = wcharToUtf8(devices[i].displayname);
            device.model_name = wcharToUtf8(devices[i].model->name);
#else
            device.id = devices[i].id;
            device.display_name = devices[i].displayname;
            device.model_name = devices[i].model->name;
#endif
            device.width = devices[i].model->res[0].width;
            device.height = devices[i].model->res[0].height;
            available_cameras_.push_back(device);
        }
    } catch (...) {}
    return available_cameras_;
}

bool CameraHandler::connect(const std::string& device_id) {
    disconnect();
    try {
        if (device_id.empty()) {
            hcam_ = Toupcam_Open(nullptr);
        } else {
#if defined(_WIN32)
            std::wstring wid(device_id.begin(), device_id.end());
            hcam_ = Toupcam_Open(wid.c_str());
#else
            hcam_ = Toupcam_Open(device_id.c_str());
#endif
        }
        if (!hcam_) { updateStatus(false, "Failed to open camera"); return false; }

        int width = 0, height = 0;
        if (SUCCEEDED(Toupcam_get_Size(hcam_, &width, &height))) {
            image_width_ = width; image_height_ = height;
        } else {
            image_width_ = 1920; image_height_ = 1080;
        }

        Toupcam_put_Option(hcam_, TOUPCAM_OPTION_BYTEORDER, 0);
        Toupcam_put_AutoExpoEnable(hcam_, 0);
        Toupcam_put_ExpoTime(hcam_, 20000);

        connected_ = true;
        capturing_ = false;
        updateFinalSize();
        updateStatus(true, "Connected to camera");
        return true;
    } catch (const std::exception& e) {
        updateStatus(false, std::string("Connection error: ") + e.what());
        return false;
    }
}

void CameraHandler::disconnect() {
    if (hcam_) {
        capturing_ = false;
        Toupcam_Stop(hcam_);
        Toupcam_Close(hcam_);
        hcam_ = nullptr;
    }
    connected_ = false;
    capturing_ = false;
    pull_buffer_.clear();
    updateStatus(false, "Disconnected from camera");
}

bool CameraHandler::startCapture() {
    if (!connected_) return false;
    if (capturing_) return true;
    try {
        updateFinalSize();
        HRESULT hr = Toupcam_StartPullModeWithCallback(hcam_, eventCallback, this);
        if (FAILED(hr)) {
            pull_buffer_.clear();
            updateStatus(false, "Failed to start capture");
            return false;
        }
        capturing_ = true;
        updateStatus(true, "Capture started (pull mode)");
        return true;
    } catch (const std::exception& e) {
        pull_buffer_.clear();
        updateStatus(false, std::string("Failed to start capture: ") + e.what());
        return false;
    }
}

void CameraHandler::stopCapture() {
    if (!capturing_) return;
    capturing_ = false;
    if (hcam_) Toupcam_Stop(hcam_);
    pull_buffer_.clear();
    updateStatus(true, "Capture stopped");
}

bool CameraHandler::captureSingleFrame(cv::Mat& frame) {
    if (!connected_) return false;
    try {
        std::lock_guard<std::mutex> lock(capture_mutex_);
        int final_w = 0, final_h = 0;
        if (FAILED(Toupcam_get_FinalSize(hcam_, &final_w, &final_h))) {
            final_w = image_width_; final_h = image_height_;
        }
        int buf_size = TDIBWIDTHBYTES(final_w * 24) * final_h;
        std::vector<uint8_t> buf(buf_size);
        HRESULT result = E_FAIL;
        for (int a = 0; a < 3; ++a) {
            result = Toupcam_PullImageV4(hcam_, buf.data(), 0, 24, 0, nullptr);
            if (SUCCEEDED(result)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (FAILED(result)) return false;
        int stride = TDIBWIDTHBYTES(final_w * 24);
        cv::Mat bgr(final_h, final_w, CV_8UC3, buf.data(), stride);
        frame = bgr.clone();
        return true;
    } catch (const std::exception&) { return false; }
}

bool CameraHandler::captureTriggerFrame(cv::Mat& frame) {
    if (!connected_) return false;
    if (capturing_) Toupcam_Pause(hcam_, 1);
    bool ok = false;
    int final_w = 0, final_h = 0;
    if (SUCCEEDED(Toupcam_get_FinalSize(hcam_, &final_w, &final_h))) {
        int stride = TDIBWIDTHBYTES(final_w * 24);
        std::vector<uint8_t> buffer(stride * final_h);
        ToupcamFrameInfoV4 info = {};
        HRESULT hr = Toupcam_TriggerSyncV4(hcam_, 3000, buffer.data(), 24, stride, &info);
        if (SUCCEEDED(hr)) {
            cv::Mat bgr(final_h, final_w, CV_8UC3, buffer.data(), stride);
            frame = bgr.clone();
            ok = true;
        }
    }
    if (!ok && connected_) {
        int fw = 0, fh = 0;
        if (FAILED(Toupcam_get_FinalSize(hcam_, &fw, &fh))) { fw = image_width_; fh = image_height_; }
        int stride2 = TDIBWIDTHBYTES(fw * 24);
        std::vector<uint8_t> buf2(stride2 * fh);
        for (int a = 0; a < 3; ++a) {
            if (SUCCEEDED(Toupcam_PullImageV4(hcam_, buf2.data(), 0, 24, 0, nullptr))) {
                cv::Mat bgr(fh, fw, CV_8UC3, buf2.data(), stride2);
                frame = bgr.clone();
                ok = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    if (capturing_) Toupcam_Pause(hcam_, 0);
    return ok;
}

void CameraHandler::pauseStream() {
    if (hcam_ && capturing_) Toupcam_Pause(hcam_, 1);
}
void CameraHandler::resumeStream() {
    if (hcam_ && capturing_) Toupcam_Pause(hcam_, 0);
}

void CameraHandler::setStatusCallback(std::function<void(const CameraStatus&)> callback) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_callback_ = callback;
}

CameraStatus CameraHandler::getStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return current_status_;
}
bool CameraHandler::isConnected() const { return connected_; }
bool CameraHandler::isCapturing() const { return capturing_; }

bool CameraHandler::setExposure(float exposure) {
    if (!connected_) return false;
    unsigned us = static_cast<unsigned>(exposure * 1000);
    return SUCCEEDED(Toupcam_put_ExpoTime(hcam_, us));
}
float CameraHandler::getExposure() const {
    if (!connected_) return 0;
    unsigned us = 0;
    if (SUCCEEDED(Toupcam_get_ExpoTime(hcam_, &us))) return us / 1000.0f;
    return 0;
}
float CameraHandler::getRealExposure() const {
    if (!connected_) return 0;
    unsigned us = 0;
    if (SUCCEEDED(Toupcam_get_RealExpoTime(hcam_, &us))) return us / 1000.0f;
    return 0;
}
void CameraHandler::getExposureRange(float& min_ms, float& max_ms, float& def_ms) const {
    if (!connected_) { min_ms = 0.01f; max_ms = 100.0f; def_ms = 10.0f; return; }
    unsigned nMin, nMax, nDef;
    if (SUCCEEDED(Toupcam_get_ExpTimeRange(hcam_, &nMin, &nMax, &nDef))) {
        min_ms = nMin / 1000.0f; max_ms = nMax / 1000.0f; def_ms = nDef / 1000.0f;
    }
}
bool CameraHandler::setAutoExposure(bool enable) {
    if (!connected_) return false;
    return SUCCEEDED(Toupcam_put_AutoExpoEnable(hcam_, enable ? 1 : 0));
}
bool CameraHandler::getAutoExposure() const {
    if (!connected_) return false;
    int mode = 0;
    if (SUCCEEDED(Toupcam_get_AutoExpoEnable(hcam_, &mode))) return mode != 0;
    return false;
}
bool CameraHandler::setRotation(int degrees) {
    if (!connected_) return false;
    int v = ((degrees % 360) + 360) % 360;
    if (v % 90 != 0) return false;
    if (FAILED(Toupcam_put_Option(hcam_, TOUPCAM_OPTION_ROTATE, v))) return false;
    updateFinalSize();
    return true;
}
int CameraHandler::getRotation() const {
    if (!connected_) return 0;
    int val = 0;
    Toupcam_get_Option(hcam_, TOUPCAM_OPTION_ROTATE, &val);
    return val;
}
bool CameraHandler::setHFlip(bool flip) {
    if (!connected_) return false;
    return SUCCEEDED(Toupcam_put_HFlip(hcam_, flip ? 1 : 0));
}
bool CameraHandler::getHFlip() const {
    if (!connected_) return false;
    int val = 0;
    return SUCCEEDED(Toupcam_get_HFlip(hcam_, &val)) && val != 0;
}
bool CameraHandler::setVFlip(bool flip) {
    if (!connected_) return false;
    return SUCCEEDED(Toupcam_put_VFlip(hcam_, flip ? 1 : 0));
}
bool CameraHandler::getVFlip() const {
    if (!connected_) return false;
    int val = 0;
    return SUCCEEDED(Toupcam_get_VFlip(hcam_, &val)) && val != 0;
}
bool CameraHandler::setGain(float gain) {
    if (!connected_) return false;
    return SUCCEEDED(Toupcam_put_ExpoAGain(hcam_, static_cast<unsigned short>(gain * 100)));
}
float CameraHandler::getGain() const {
    if (!connected_) return 0;
    unsigned short g = 0;
    if (SUCCEEDED(Toupcam_get_ExpoAGain(hcam_, &g))) return g / 100.0f;
    return 0;
}
bool CameraHandler::setResolution(int width, int height) {
    if (!connected_) return false;
    if (FAILED(Toupcam_put_Size(hcam_, width, height))) return false;
    image_width_ = width; image_height_ = height;
    updateFinalSize();
    CameraStatus snap;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        current_status_.width = width; current_status_.height = height;
        snap = current_status_;
    }
    if (status_callback_) status_callback_(snap);
    return true;
}
void CameraHandler::getResolution(int& w, int& h) const { w = image_width_; h = image_height_; }

std::vector<std::pair<int, int>> CameraHandler::getSupportedResolutions() const {
    std::vector<std::pair<int, int>> res;
    if (!connected_ || !hcam_) return res;
    HRESULT hr = Toupcam_get_ResolutionNumber(hcam_);
    if (FAILED(hr)) return res;
    unsigned count = static_cast<unsigned>(hr);
    for (unsigned i = 0; i < count; ++i) {
        int w = 0, h = 0;
        if (SUCCEEDED(Toupcam_get_Resolution(hcam_, i, &w, &h))) res.emplace_back(w, h);
    }
    return res;
}

// ── Pull-Mode Event Callback (SDK thread → UI thread) ──

void __stdcall CameraHandler::eventCallback(unsigned nEvent, void* ctx) {
    CameraHandler* handler = static_cast<CameraHandler*>(ctx);
    if (!handler || !handler->capturing_) return;
    QMetaObject::invokeMethod(handler, [handler, nEvent]() {
        handler->handleEvent(nEvent);
    }, Qt::QueuedConnection);
}

void CameraHandler::handleEvent(unsigned nEvent) {
    switch (nEvent) {
    case TOUPCAM_EVENT_IMAGE:    handleFrame(); break;
    case TOUPCAM_EVENT_EXPOSURE: emit exposureChanged(); break;
    case TOUPCAM_EVENT_ERROR:    emit cameraError(QStringLiteral("Camera error")); updateStatus(false, "Camera error"); break;
    case TOUPCAM_EVENT_DISCONNECTED: emit cameraDisconnected(); updateStatus(false, "Camera disconnected"); break;
    default: break;
    }
}

// ── Final Size Cache ──

void CameraHandler::updateFinalSize() {
    if (!hcam_) return;
    int fw = 0, fh = 0;
    if (SUCCEEDED(Toupcam_get_FinalSize(hcam_, &fw, &fh))) {
        final_width_ = fw; final_height_ = fh;
    } else {
        final_width_ = image_width_; final_height_ = image_height_;
    }
    int stride = TDIBWIDTHBYTES(final_width_ * 24);
    pull_buffer_.resize(stride * final_height_);
}

// ── Frame Handler (UI thread, hot path) ──

void CameraHandler::handleFrame() {
    if (pull_buffer_.empty() || !hcam_ || !capturing_) return;
    HRESULT hr;
    {
        std::lock_guard<std::mutex> lock(capture_mutex_);
        hr = Toupcam_PullImageV4(hcam_, pull_buffer_.data(), 0, 24, 0, nullptr);
    }
    if (FAILED(hr)) return;
    int stride = TDIBWIDTHBYTES(final_width_ * 24);
    cv::Mat frame(final_height_, final_width_, CV_8UC3, pull_buffer_.data(), stride);
    cv::Mat preview;
    int max_w = 640;
    if (final_width_ > max_w) {
        double s = static_cast<double>(max_w) / final_width_;
        cv::resize(frame, preview, cv::Size(), s, s, cv::INTER_LINEAR);
    } else {
        preview = frame.clone();
    }
    emit frameReady(preview);
}

void CameraHandler::updateStatus(bool is_connected, const std::string& message) {
    std::function<void(const CameraStatus&)> cb;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        current_status_.connected = is_connected;
        current_status_.status_message = message;
        current_status_.width = image_width_;
        current_status_.height = image_height_;
        cb = status_callback_;
    }
    if (cb) cb(current_status_);
}

} // namespace camera
