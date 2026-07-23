#include "CameraHandler.h"

#include <spdlog/spdlog.h>
#include <opencv2/imgproc.hpp>
#include <QPointer>
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

CameraHandler::CameraHandler() {
    current_status_.connected = false;
    current_status_.status_message = "Disconnected";
    connected_ = false; capturing_ = false; push_active_ = false;
}

CameraHandler::~CameraHandler() { disconnect(); }

// ── Enumeration / Connect / Disconnect ──────────────────────────────────

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
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Camera] enumerateCameras exception: {}", e.what());
    } catch (...) {
        SPDLOG_ERROR("[Camera] enumerateCameras: unknown exception");
    }
    return available_cameras_;
}

bool CameraHandler::connect(const std::string& device_id) {
    disconnect();
    try {
        if (device_id.empty()) hcam_ = Toupcam_Open(nullptr);
        else {
#if defined(_WIN32)
            std::wstring wid(device_id.begin(), device_id.end());
            hcam_ = Toupcam_Open(wid.c_str());
#else
            hcam_ = Toupcam_Open(device_id.c_str());
#endif
        }
        if (!hcam_) {
            SPDLOG_ERROR("[Camera] Failed to open camera, device='{}'", device_id.empty() ? "default" : device_id);
            updateStatus(false, "Failed to open camera");
            return false;
        }

        int w = 0, h = 0;
        if (SUCCEEDED(Toupcam_get_Size(hcam_, &w, &h)))
            { image_width_ = w; image_height_ = h; }
        else { image_width_ = 1920; image_height_ = 1080; }

        // No hardware rotation — software rotation on preview (after resize, ~0.1ms)
        Toupcam_put_Option(hcam_, TOUPCAM_OPTION_BYTEORDER, 0);
        Toupcam_put_AutoExpoEnable(hcam_, 0);
        Toupcam_put_ExpoTime(hcam_, 70000);  // 70ms
        Toupcam_put_HFlip(hcam_, 1);   // 默认水平翻转
        Toupcam_put_VFlip(hcam_, 1);   // 默认垂直翻转

        SPDLOG_INFO("[Camera] Camera connected: {}x{}", image_width_, image_height_);
        connected_ = true; capturing_ = false;
        updateStatus(true, "Connected to camera");
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Camera] Camera connection error: {}", e.what());
        updateStatus(false, std::string("Connection error: ") + e.what());
        return false;
    }
}

void CameraHandler::disconnect() {
    if (hcam_) { capturing_ = false; push_active_ = false; Toupcam_Stop(hcam_); Toupcam_Close(hcam_); hcam_ = nullptr; }
    connected_ = false; capturing_ = false;
    // Note: negative_ is a user preview preference — not reset on disconnect.
    updateStatus(false, "Disconnected from camera");
}

// ── Capture Control ─────────────────────────────────────────────────────

bool CameraHandler::startCapture() {
    if (!connected_) return false;
    if (capturing_) return true;
    try {
        push_active_ = true;
        HRESULT hr = Toupcam_StartPushModeV4(hcam_, pushDataCallback, this, pushEventCallback, this);
        if (FAILED(hr)) {
            SPDLOG_ERROR("[Camera] Toupcam_StartPushModeV4 failed, hr=0x{:08X}", static_cast<unsigned>(hr));
            push_active_ = false;
            updateStatus(false, "Failed to start capture");
            return false;
        }
        SPDLOG_INFO("[Camera] Camera capture started (push mode)");
        capturing_ = true;
        updateStatus(true, "Capture started (push mode)");
        return true;
    } catch (const std::exception& e) {
        push_active_ = false; updateStatus(false, std::string("Failed to start capture: ") + e.what()); return false;
    }
}

void CameraHandler::stopCapture() {
    if (!capturing_) return;
    capturing_ = false; push_active_ = false;
    if (hcam_) Toupcam_Stop(hcam_);
    updateStatus(true, "Capture stopped");
}

void CameraHandler::pauseStream()  { if (hcam_ && capturing_) Toupcam_Pause(hcam_, 1); }
void CameraHandler::resumeStream() { if (hcam_ && capturing_) Toupcam_Pause(hcam_, 0); }

// ── Push Mode Callbacks ─────────────────────────────────────────────────
// pData is only valid during the callback — we deep-copy via cv::resize
// on the SDK thread before posting to UI thread. The preview (max 640px)
// is small enough that both the copy and rotation are fast (~1ms total).

void __stdcall CameraHandler::pushDataCallback(const void* pData, const ToupcamFrameInfoV3* pInfo, int bSnap, void* ctx) {
    CameraHandler* handler = static_cast<CameraHandler*>(ctx);
    if (!handler || !handler->push_active_ || !pData || !pInfo || bSnap) return;

    int w = static_cast<int>(pInfo->width);
    int h = static_cast<int>(pInfo->height);
    int stride = TDIBWIDTHBYTES(w * 24);
    cv::Mat frame(h, w, CV_8UC3, const_cast<void*>(pData), stride);

    // Store full-resolution copy for scanning capture (captureTriggerFrame)
    {
        std::lock_guard<std::mutex> lock(handler->frame_mutex_);
        handler->latest_full_frame_ = frame.clone();
    }

    // Pre-scale on SDK thread (deep-copies to preview — safe after callback returns)
    cv::Mat preview;
    int max_w = 640;
    if (w > max_w) {
        double s = static_cast<double>(max_w) / w;
        cv::resize(frame, preview, cv::Size(), s, s, cv::INTER_LINEAR);
    } else {
        preview = frame.clone();
    }

    // Software rotation on SDK thread (on small preview, ~0.1ms)
    int rot = handler->rotation_;
    if (rot == 90)       cv::rotate(preview, preview, cv::ROTATE_90_CLOCKWISE);
    else if (rot == 180) cv::rotate(preview, preview, cv::ROTATE_180);
    else if (rot == 270) cv::rotate(preview, preview, cv::ROTATE_90_COUNTERCLOCKWISE);

    // Software negative (preview-only — does not affect latest_full_frame_ or captures)
    if (handler->negative_) {
        cv::bitwise_not(preview, preview);
    }

    // Post to UI thread — preview is already deep-copied and rotated.
    // Use QPointer to guard against CameraHandler destruction before the
    // queued lambda executes (prevents use-after-free).
    QPointer<CameraHandler> guard(handler);
    QMetaObject::invokeMethod(handler, [guard, preview]() {
        if (guard) emit guard->frameReady(preview);
    }, Qt::QueuedConnection);
}

void __stdcall CameraHandler::pushEventCallback(unsigned nEvent, void* ctx) {
    CameraHandler* handler = static_cast<CameraHandler*>(ctx);
    if (!handler) return;
    QPointer<CameraHandler> guard(handler);
    QMetaObject::invokeMethod(handler, [guard, nEvent]() {
        if (guard) guard->handlePushEvent(nEvent);
    }, Qt::QueuedConnection);
}

void CameraHandler::handlePushEvent(unsigned nEvent) {
    switch (nEvent) {
    case TOUPCAM_EVENT_EXPOSURE:    emit exposureChanged(); break;
    case TOUPCAM_EVENT_ERROR:       emit cameraError(QStringLiteral("Camera error")); updateStatus(false, "Camera error"); break;
    case TOUPCAM_EVENT_DISCONNECTED: emit cameraDisconnected(); updateStatus(false, "Camera disconnected"); break;
    default: break;
    }
}

// ── Still / Single / Trigger Capture ────────────────────────────────────

bool CameraHandler::captureSingleFrame(cv::Mat& frame) {
    if (!connected_) return false;
    try {
        int stride = TDIBWIDTHBYTES(image_width_ * 24);
        std::vector<uint8_t> buf(stride * image_height_);
        HRESULT r = E_FAIL;
        for (int a = 0; a < 3; ++a) {
            r = Toupcam_PullImageV4(hcam_, buf.data(), 0, 24, 0, nullptr);
            if (SUCCEEDED(r)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (FAILED(r)) {
            SPDLOG_ERROR("[Camera] captureSingleFrame failed after 3 retries, hr=0x{:08X}", static_cast<unsigned>(r));
            return false;
        }

        cv::Mat bgr(image_height_, image_width_, CV_8UC3, buf.data(), stride);
        if (rotation_ == 90) cv::rotate(bgr, bgr, cv::ROTATE_90_CLOCKWISE);
        else if (rotation_ == 180) cv::rotate(bgr, bgr, cv::ROTATE_180);
        else if (rotation_ == 270) cv::rotate(bgr, bgr, cv::ROTATE_90_COUNTERCLOCKWISE);
        frame = bgr.clone();
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Camera] captureSingleFrame exception: {}", e.what());
        return false;
    }
}

bool CameraHandler::captureTriggerFrame(cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (latest_full_frame_.empty()) {
        SPDLOG_WARN("[Camera] captureTriggerFrame: no frame available (push active={})", push_active_.load());
        return false;
    }
    frame = latest_full_frame_.clone();
    if (rotation_ == 90) cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);
    else if (rotation_ == 180) cv::rotate(frame, frame, cv::ROTATE_180);
    else if (rotation_ == 270) cv::rotate(frame, frame, cv::ROTATE_90_COUNTERCLOCKWISE);
    return true;
}

// ── Camera Controls ─────────────────────────────────────────────────────

void CameraHandler::setStatusCallback(std::function<void(const CameraStatus&)> cb) {
    std::lock_guard<std::mutex> lock(status_mutex_); status_callback_ = cb;
}
CameraStatus CameraHandler::getStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_); return current_status_;
}
bool CameraHandler::isConnected() const { return connected_; }
bool CameraHandler::isCapturing() const { return capturing_; }

bool CameraHandler::setExposure(float expo) {
    if (!connected_) return false;
    return SUCCEEDED(Toupcam_put_ExpoTime(hcam_, static_cast<unsigned>(expo * 1000)));
}
float CameraHandler::getExposure() const {
    if (!connected_) return 0; unsigned us = 0;
    return SUCCEEDED(Toupcam_get_ExpoTime(hcam_, &us)) ? us / 1000.0f : 0;
}
float CameraHandler::getRealExposure() const {
    if (!connected_) return 0; unsigned us = 0;
    return SUCCEEDED(Toupcam_get_RealExpoTime(hcam_, &us)) ? us / 1000.0f : 0;
}
void CameraHandler::getExposureRange(float& mn, float& mx, float& df) const {
    if (!connected_) { mn = 0.01f; mx = 100.0f; df = 10.0f; return; }
    unsigned a,b,c; if (SUCCEEDED(Toupcam_get_ExpTimeRange(hcam_,&a,&b,&c))) { mn=a/1000.f; mx=b/1000.f; df=c/1000.f; }
}
bool CameraHandler::setAutoExposure(bool en) { return connected_ && SUCCEEDED(Toupcam_put_AutoExpoEnable(hcam_, en?1:0)); }
bool CameraHandler::getAutoExposure() const { if(!connected_)return false; int m=0; return SUCCEEDED(Toupcam_get_AutoExpoEnable(hcam_,&m))&&m; }

bool CameraHandler::setRotation(int deg) {
    int v = ((deg % 360) + 360) % 360;
    if (v % 90 != 0) return false;
    rotation_ = v;  // software rotation, no ToupCam hardware rotation
    return true;
}
int CameraHandler::getRotation() const { return rotation_; }

bool CameraHandler::setHFlip(bool f) { return connected_ && SUCCEEDED(Toupcam_put_HFlip(hcam_,f?1:0)); }
bool CameraHandler::getHFlip() const { if(!connected_)return false; int v=0; return SUCCEEDED(Toupcam_get_HFlip(hcam_,&v))&&v; }
bool CameraHandler::setVFlip(bool f) { return connected_ && SUCCEEDED(Toupcam_put_VFlip(hcam_,f?1:0)); }
bool CameraHandler::getVFlip() const { if(!connected_)return false; int v=0; return SUCCEEDED(Toupcam_get_VFlip(hcam_,&v))&&v; }
bool CameraHandler::setNegative(bool enable) { negative_ = enable; return true; }
bool CameraHandler::getNegative() const { return negative_; }
bool CameraHandler::setGain(float g) { return connected_ && SUCCEEDED(Toupcam_put_ExpoAGain(hcam_,static_cast<unsigned short>(g*100))); }
float CameraHandler::getGain() const { if(!connected_)return 0; unsigned short g=0; return SUCCEEDED(Toupcam_get_ExpoAGain(hcam_,&g))?g/100.f:0; }

void CameraHandler::getGainRange(unsigned short& min_pct, unsigned short& max_pct, unsigned short& def_pct) const {
    if (!connected_) { min_pct = 100; max_pct = 500; def_pct = 100; return; }
    if (FAILED(Toupcam_get_ExpoAGainRange(hcam_, &min_pct, &max_pct, &def_pct))) {
        min_pct = 100; max_pct = 500; def_pct = 100;
    }
}

bool CameraHandler::setSharpening(unsigned short strength) {
    if (!connected_) return false;
    // Pack: (threshold << 24) | (radius << 16) | strength
    // Use defaults: radius=2, threshold=0 per SDK defines
    int value = (0 << 24) | (2 << 16) | strength;
    return SUCCEEDED(Toupcam_put_Option(hcam_, TOUPCAM_OPTION_SHARPENING, value));
}

unsigned short CameraHandler::getSharpening() const {
    if (!connected_) return 0;
    int value = 0;
    if (FAILED(Toupcam_get_Option(hcam_, TOUPCAM_OPTION_SHARPENING, &value))) return 0;
    return static_cast<unsigned short>(value & 0xFFFF);  // extract strength from low 16 bits
}

bool CameraHandler::setResolution(int w, int h) {
    if (!connected_ || FAILED(Toupcam_put_Size(hcam_, w, h))) return false;
    image_width_ = w; image_height_ = h;
    CameraStatus snap;
    { std::lock_guard<std::mutex> lk(status_mutex_); current_status_.width=w; current_status_.height=h; snap=current_status_; }
    if (status_callback_) status_callback_(snap);
    return true;
}
void CameraHandler::getResolution(int& w, int& h) const { w=image_width_; h=image_height_; }
std::vector<std::pair<int,int>> CameraHandler::getSupportedResolutions() const {
    std::vector<std::pair<int,int>> res;
    if (!connected_||!hcam_) return res;
    HRESULT hr = Toupcam_get_ResolutionNumber(hcam_); if (FAILED(hr)) return res;
    for (unsigned i=0; i<static_cast<unsigned>(hr); ++i) { int w=0,h=0; if (SUCCEEDED(Toupcam_get_Resolution(hcam_,i,&w,&h))) res.emplace_back(w,h); }
    return res;
}

void CameraHandler::updateStatus(bool is_connected, const std::string& message) {
    std::function<void(const CameraStatus&)> cb;
    { std::lock_guard<std::mutex> lk(status_mutex_); current_status_.connected=is_connected; current_status_.status_message=message; current_status_.width=image_width_; current_status_.height=image_height_; cb=status_callback_; }
    if (cb) cb(current_status_);
}

} // namespace camera
