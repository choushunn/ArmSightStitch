#include "AppController.h"
#include "infra/config/ConfigManager.h"
#include "infra/config/PathUtils.h"

#include "core/arm/ModbusArmController.h"
#include "core/arm/SMovementController.h"
#include "core/camera/CameraHandler.h"
#include "core/detector/YoloDetector.h"
#include "core/detector/DustDetector.h"
#include "core/detector/EdgeDetector.h"
#include "core/stitch/ImageStitcher.h"
#include "ui/WorkflowManager.h"

#include <spdlog/spdlog.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <filesystem>
#include <cmath>

// ── pimpl: concrete implementations are hidden from the header ────────
struct AppController::Impl {
    arm::ModbusArmController arm_controller_;
    arm::SMovementController s_movement_controller_;
    camera::CameraHandler camera_handler_;
    detector::YoloDetector yolo_detector_;
    detector::DustDetector dust_detector_;
    detector::EdgeDetector edge_detector_;
    detector::IDetector* current_detector_ = &yolo_detector_;  // active algorithm
    stitch::ImageStitcher image_stitcher_;
    WorkflowManager* workflow_mgr_ = nullptr;
    std::string last_scan_dir_;  // most recent scan run / working directory for exports

    Impl() : s_movement_controller_(arm_controller_) {}
};

// ── Module accessors ──────────────────────────────────────────────────
arm::IArmController& AppController::armController() {
    return pimpl_->arm_controller_;
}
arm::ISMovementController& AppController::movementController() {
    return pimpl_->s_movement_controller_;
}
camera::ICameraHandler& AppController::cameraHandler() {
    return pimpl_->camera_handler_;
}
detector::IDetector& AppController::detector() {
    return *pimpl_->current_detector_;
}
stitch::IStitcher& AppController::stitcher() {
    return pimpl_->image_stitcher_;
}
WorkflowManager& AppController::workflow() {
    return *pimpl_->workflow_mgr_;
}

// ── Constructor / Destructor ──────────────────────────────────────────
AppController::AppController(QObject* parent)
    : QObject(parent)
    , pimpl_(std::make_unique<Impl>())
{
    auto& cfg = ConfigManager::instance();

    pimpl_->workflow_mgr_ = new WorkflowManager(
        pimpl_->arm_controller_, pimpl_->s_movement_controller_,
        pimpl_->camera_handler_, pimpl_->image_stitcher_, this);

    connect(pimpl_->workflow_mgr_, &WorkflowManager::statusMessage,
            this, &AppController::statusMessage);
    connect(pimpl_->workflow_mgr_, &WorkflowManager::workflowError,
            this, &AppController::errorMessage);
    connect(pimpl_->workflow_mgr_, &WorkflowManager::stitchingFinished,
            this, &AppController::stitchingFinished);

    // Camera preview relay
    connect(&pimpl_->camera_handler_, &camera::CameraHandler::frameReady,
            this, &AppController::cameraFrameReady);
    connect(&pimpl_->camera_handler_, &camera::CameraHandler::exposureChanged,
            this, &AppController::cameraExposureChanged);
    connect(&pimpl_->camera_handler_, &camera::CameraHandler::cameraDisconnected,
            this, &AppController::cameraDisconnected);
    connect(&pimpl_->camera_handler_, &camera::CameraHandler::cameraError,
            this, &AppController::cameraError);

    // Auto-load default model from config paths
    std::string paramPath = cfg.modelParamPath();
    std::string binPath = cfg.modelBinPath();
    if (!paramPath.empty() && !binPath.empty()) {
        QString fullParam = QDir(QCoreApplication::applicationDirPath())
                            .absoluteFilePath(QString::fromStdString(paramPath));
        QString fullBin = QDir(QCoreApplication::applicationDirPath())
                          .absoluteFilePath(QString::fromStdString(binPath));
        if (QFileInfo::exists(fullParam) && QFileInfo::exists(fullBin)) {
            bool ok = pimpl_->yolo_detector_.loadModel(
                fullParam.toStdString(), fullBin.toStdString());
            if (ok) {
                emit modelLoaded();
                SPDLOG_INFO("Auto-loaded default model");
            }
        }
    }

    // Initialize dust detector params + active algorithm from config
    detector::DustDetectionParams dp;
    dp.claheClip  = cfg.dustClaheClip();
    dp.bgBlurSize = cfg.dustBgBlur();
    dp.minArea    = cfg.dustMinArea();
    dp.maxArea    = cfg.dustMaxArea();
    dp.dilateIter = cfg.dustDilateIter();
    dp.maxIter    = cfg.dustMaxIter();
    dp.nmsIou     = cfg.dustNmsIou();
    pimpl_->dust_detector_.setParams(dp);
    setDetectorAlgorithm(cfg.detectionAlgorithm());

    // Initialize edge detector params from config
    detector::EdgeDetectionParams ep;
    ep.claheClip        = cfg.edgeClaheClip();
    ep.claheTileGrid    = cfg.edgeClaheTileGrid();
    ep.edgeThreshold    = cfg.edgeThreshold();
    ep.sobelKSize       = cfg.edgeSobelKSize();
    ep.dilateIter       = cfg.edgeDilateIter();
    ep.minBboxArea      = cfg.edgeMinBboxArea();
    ep.nmsIouThresh     = cfg.edgeNmsIouThresh();
    ep.nmsContainThresh = cfg.edgeNmsContainThresh();
    pimpl_->edge_detector_.setParams(ep);

    SPDLOG_INFO("AppController initialized");
}

AppController::~AppController() {
    pimpl_->workflow_mgr_->stopAutoWorkflow();
    pimpl_->s_movement_controller_.stop();
    pimpl_->camera_handler_.stopCapture();
    pimpl_->camera_handler_.disconnect();
    pimpl_->arm_controller_.stopAutoRead();
    pimpl_->arm_controller_.disconnect();
    SPDLOG_INFO("AppController destroyed");
}

// ── High-level operations ─────────────────────────────────────────────
bool AppController::connectArm(const std::string& ip, int port) {
    auto& arm = pimpl_->arm_controller_;
    arm.setDebugEnabled(ConfigManager::instance().modbusDebug());
    bool ok = arm.connect(ip, port);
    if (ok) {
        int speed = ConfigManager::instance().defaultSpeed();
        for (int i = 0; i < 5; ++i) {
            arm.setSpeed(i, speed);
            arm::AxisLimits limits;
            limits.min_pos = ConfigManager::instance().axisMinPos(i);
            limits.max_pos = ConfigManager::instance().axisMaxPos(i);
            arm.setSafetyLimits(i, limits);
        }
        arm.startAutoRead();
        ConfigManager::instance().setArmIp(ip);
        ConfigManager::instance().setArmPort(port);
        ConfigManager::instance().saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
        emit statusMessage(QString::fromStdString("Arm connected to " + ip));
    } else {
        std::string errMsg = arm.lastError();
        if (errMsg.empty()) errMsg = "Failed to connect arm";
        emit errorMessage(QString::fromStdString(errMsg));
    }
    return ok;
}

void AppController::disconnectArm() {
    pimpl_->arm_controller_.disconnect();
    emit statusMessage("Arm disconnected");
}

bool AppController::connectCamera(const std::string& deviceId) {
    bool ok = pimpl_->camera_handler_.connect(deviceId);
    emit statusMessage(ok ? "Camera connected" : "Camera connection failed");
    return ok;
}

void AppController::disconnectCamera() {
    pimpl_->camera_handler_.disconnect();
    emit statusMessage("Camera disconnected");
}

bool AppController::startCameraCapture() {
    bool ok = pimpl_->camera_handler_.startCapture();
    emit statusMessage(ok ? "Camera capture started" : "Camera capture failed");
    return ok;
}

void AppController::stopCameraCapture() {
    pimpl_->camera_handler_.stopCapture();
    emit statusMessage("Camera capture stopped");
}

bool AppController::captureImage(cv::Mat& frame) {
    return pimpl_->camera_handler_.captureSingleFrame(frame);
}

bool AppController::startSMovement(const cv::Size& gridSize, int stepSize, int zHeight) {
    auto& cfg = ConfigManager::instance();
    auto& sm = pimpl_->s_movement_controller_;
    int endX = stepSize * (gridSize.width - 1);
    int endY = stepSize * (gridSize.height - 1);

    // Pass spherical cap / Z-axis parameters from config to controller
    sm.setSphereParams(cfg.sphereRadius(), cfg.sphereCapHeight(),
                       cfg.sphereHeightOffset(), cfg.zBaseHeight());
    sm.setZMode(cfg.zMode());
    if (cfg.zMode() == 1 && !cfg.zMapFile().empty()) {
        sm.setZMapFile(cfg.zMapFile());
    }
    if (cfg.zMode() == 2 && !cfg.zRadialFile().empty()) {
        sm.setZRadialFile(cfg.zRadialFile());
    }

    std::string savePath = infra::createNextRunDir(cfg.imageSaveBasePath());
    sm.setSaveDirectory(savePath);
    pimpl_->last_scan_dir_ = savePath;  // 供检测结果 JSON 导出定位扫描目录
    // Extract run number from path for the controller (cross-platform)
    QString qSavePath = QString::fromStdString(savePath);
    QString dirName = QDir(qSavePath).dirName();
    bool ok = false;
    int runNumber = dirName.toInt(&ok);
    sm.setRunNumber(ok ? runNumber : 0);
    sm.setMovementSpeed(cfg.defaultSpeed());
    sm.setPositionTolerance(cfg.positionTolerance());
    sm.setDwellTimeMs(cfg.dwellTimeMs());

    arm::SMovementPoint startPos = {0, 0, zHeight, 0, 0, 0, 0};
    arm::SMovementPoint endPos = {endX, endY, zHeight, 0, 0,
                                  gridSize.height - 1, gridSize.width - 1};

    if (!sm.initialize(gridSize, startPos, endPos)) {
        emit errorMessage("S-movement initialization failed");
        return false;
    }

    sm.setImageCaptureCallback([this](cv::Mat& frame) {
        return pimpl_->camera_handler_.captureTriggerFrame(frame);
    });

    if (!sm.start()) {
        emit errorMessage("S-movement start failed");
        return false;
    }

    emit statusMessage("S-movement started");
    return true;
}

void AppController::stopSMovement() {
    pimpl_->s_movement_controller_.stop();
    emit statusMessage("S-movement stopped");
}

void AppController::pauseSMovement() {
    pimpl_->s_movement_controller_.pause();
    emit statusMessage("S-movement paused");
}

void AppController::resumeSMovement() {
    pimpl_->s_movement_controller_.resume();
    emit statusMessage("S-movement resumed");
}

void AppController::stitchImages(const std::vector<cv::Mat>& images, const cv::Size& gridSize) {
    auto& st = pimpl_->image_stitcher_;
    std::vector<cv::Mat> sorted = st.sortImagesInSCurveOrder(images, gridSize);
    cv::Mat result = st.stitchImages(sorted, gridSize);
    if (!result.empty()) {
        emit stitchingFinished(result);
        emit statusMessage("Stitching completed");
    } else {
        emit errorMessage("Stitching failed");
    }
}

std::vector<cv::Mat> AppController::loadImages(const std::string& dir) {
    return pimpl_->image_stitcher_.loadImagesFromDirectory(dir);
}

std::vector<stitch::PositionedImage> AppController::loadImagesWithPositions(const std::string& dir,
                                                                             cv::Size& outGridSize) {
    return pimpl_->image_stitcher_.loadImagesWithPositions(dir, outGridSize);
}

bool AppController::loadDetectorModel(const std::string& paramPath, const std::string& binPath) {
    bool ok = pimpl_->yolo_detector_.loadModel(paramPath, binPath);
    emit statusMessage(ok ? "Model loaded" : "Model loading failed");
    return ok;
}

bool AppController::isModelLoaded() const {
    return pimpl_->current_detector_->isModelLoaded();
}

std::vector<detector::Detection> AppController::detect(const cv::Mat& image) {
    return pimpl_->current_detector_->detect(image);
}

cv::Mat AppController::drawDetections(const cv::Mat& image, const std::vector<detector::Detection>& detections) {
    return pimpl_->current_detector_->drawDetections(image, detections);
}

void AppController::setDetectorAlgorithm(int algo) {
    if (algo == 2)
        pimpl_->current_detector_ = static_cast<detector::IDetector*>(&pimpl_->edge_detector_);
    else if (algo == 1)
        pimpl_->current_detector_ = static_cast<detector::IDetector*>(&pimpl_->dust_detector_);
    else
        pimpl_->current_detector_ = static_cast<detector::IDetector*>(&pimpl_->yolo_detector_);
    const char* name = (algo == 2) ? "Edge" : (algo == 1) ? "Dust" : "YOLO";
    SPDLOG_INFO("Detector algorithm set to {}", name);
}

int AppController::detectorAlgorithm() const {
    if (pimpl_->current_detector_ == &pimpl_->edge_detector_) return 2;
    return (pimpl_->current_detector_ == &pimpl_->dust_detector_) ? 1 : 0;
}

void AppController::setDustParams(const detector::DustDetectionParams& params) {
    pimpl_->dust_detector_.setParams(params);
}

detector::DustDetectionParams AppController::dustParams() const {
    return pimpl_->dust_detector_.params();
}

void AppController::setEdgeParams(const detector::EdgeDetectionParams& params) {
    pimpl_->edge_detector_.setParams(params);
}

detector::EdgeDetectionParams AppController::edgeParams() const {
    return pimpl_->edge_detector_.params();
}

void AppController::setScanDir(const std::string& dir) {
    pimpl_->last_scan_dir_ = dir;
}

std::string AppController::scanDir() const {
    // 1) 显式记录的扫描目录（S 扫描运行目录 / on_stitchRun 选择目录）
    if (!pimpl_->last_scan_dir_.empty()) {
        QString d = QString::fromStdString(pimpl_->last_scan_dir_);
        if (QDir(d).exists()) return pimpl_->last_scan_dir_;
    }
    // 2) 回退：工作目录下编号最大的运行目录
    QString base = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
    QDir baseDir(base);
    if (baseDir.exists()) {
        int best = -1;
        QString bestName;
        const auto names = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& name : names) {
            bool ok = false;
            int n = name.toInt(&ok);
            if (ok && n > best) { best = n; bestName = name; }
        }
        if (best >= 0) return baseDir.absoluteFilePath(bestName).toStdString();
    }
    // 3) 回退：工作目录根
    if (!base.isEmpty()) {
        baseDir.mkpath(".");
        return base.toStdString();
    }
    return {};
}

std::string AppController::saveDetectionsJson(const std::vector<detector::Detection>& detections,
                                              const cv::Mat& image,
                                              const std::string& sourceName) {
    std::string dirStd = scanDir();
    if (dirStd.empty()) {
        SPDLOG_WARN("saveDetectionsJson: no scan directory available");
        return {};
    }
    QDir dir(QString::fromStdString(dirStd));
    dir.mkpath(".");

    // 输出文件名：<源图基名>_detections.json；无源图时用时间戳
    QString baseName;
    if (!sourceName.empty())
        baseName = QFileInfo(QString::fromStdString(sourceName)).completeBaseName();
    if (baseName.isEmpty())
        baseName = "detect_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outPath = dir.absoluteFilePath(baseName + "_detections.json");

    QJsonObject root;
    root["source"] = QString::fromStdString(sourceName);
    root["algorithm"] = (detectorAlgorithm() == 2) ? "edge"
                      : (detectorAlgorithm() == 1) ? "dust"
                      : "yolo";
    root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QJsonObject size;
    size["width"] = image.cols;
    size["height"] = image.rows;
    root["image_size"] = size;
    root["count"] = static_cast<int>(detections.size());

    QJsonArray arr;
    int id = 1;
    for (const auto& d : detections) {
        QJsonObject o;
        o["id"] = id++;
        o["type"] = QString::fromStdString(d.class_name);   // 类型
        o["class_id"] = d.class_id;
        o["confidence"] = std::round(d.confidence * 10000.0) / 10000.0;
        QJsonObject bbox;                                    // 位置 + 尺寸
        bbox["x"] = d.bounding_box.x;
        bbox["y"] = d.bounding_box.y;
        bbox["width"] = d.bounding_box.width;
        bbox["height"] = d.bounding_box.height;
        o["bbox"] = bbox;
        QJsonObject center;
        center["x"] = d.bounding_box.x + d.bounding_box.width / 2;
        center["y"] = d.bounding_box.y + d.bounding_box.height / 2;
        o["center"] = center;
        o["area"] = d.bounding_box.width * d.bounding_box.height;  // 尺寸（框面积）
        arr.append(o);
    }
    root["detections"] = arr;

    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly)) {
        SPDLOG_ERROR("saveDetectionsJson: cannot write {}", outPath.toStdString());
        emit errorMessage(QString("检测结果导出失败: %1").arg(outPath));
        return {};
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    SPDLOG_INFO("Detection JSON saved: {} ({} items)", outPath.toStdString(), detections.size());
    return outPath.toStdString();
}
