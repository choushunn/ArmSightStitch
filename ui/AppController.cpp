#include "AppController.h"
#include "infra/config/ConfigManager.h"

#include <spdlog/spdlog.h>
#include <QDir>
#include <QFileInfo>
#include <filesystem>

AppController::AppController(QObject* parent)
    : QObject(parent)
    , s_movement_controller_(arm_controller_)
{
    auto& cfg = ConfigManager::instance();

    workflow_mgr_ = new WorkflowManager(
        arm_controller_, s_movement_controller_,
        camera_handler_, image_stitcher_, this);

    connect(workflow_mgr_, &WorkflowManager::statusMessage,
            this, &AppController::statusMessage);
    connect(workflow_mgr_, &WorkflowManager::workflowError,
            this, &AppController::errorMessage);
    connect(workflow_mgr_, &WorkflowManager::stitchingFinished,
            this, &AppController::stitchingFinished);

    SPDLOG_INFO("AppController initialized");
}

AppController::~AppController() {
    workflow_mgr_->stopAutoWorkflow();
    s_movement_controller_.stop();
    camera_handler_.stopCapture();
    camera_handler_.disconnect();
    arm_controller_.stopAutoRead();
    arm_controller_.disconnect();
    SPDLOG_INFO("AppController destroyed");
}

bool AppController::connectArm(const std::string& ip, int port) {
    bool ok = arm_controller_.connect(ip, port);
    if (ok) {
        int speed = ConfigManager::instance().defaultSpeed();
        for (int i = 0; i < 5; ++i) {
            arm_controller_.setSpeed(i, speed);
        }
        emit statusMessage(QString::fromStdString("Arm connected to " + ip));
    } else {
        emit errorMessage("Failed to connect arm");
    }
    return ok;
}

void AppController::disconnectArm() {
    arm_controller_.disconnect();
    emit statusMessage("Arm disconnected");
}

bool AppController::connectCamera(const std::string& deviceId) {
    bool ok = camera_handler_.connect(deviceId);
    emit statusMessage(ok ? "Camera connected" : "Camera connection failed");
    return ok;
}

void AppController::disconnectCamera() {
    camera_handler_.disconnect();
    emit statusMessage("Camera disconnected");
}

bool AppController::startCameraCapture() {
    bool ok = camera_handler_.startCapture();
    emit statusMessage(ok ? "Camera capture started" : "Camera capture failed");
    return ok;
}

void AppController::stopCameraCapture() {
    camera_handler_.stopCapture();
    emit statusMessage("Camera capture stopped");
}

bool AppController::captureImage(cv::Mat& frame) {
    return camera_handler_.captureSingleFrame(frame);
}

bool AppController::startSMovement(const cv::Size& gridSize, int stepSize, int zHeight) {
    auto& cfg = ConfigManager::instance();
    int endX = stepSize * (gridSize.width - 1);
    int endY = stepSize * (gridSize.height - 1);

    std::string basePath = cfg.imageSaveBasePath();
    int runNumber = 0;
    QDir baseDir(QString::fromStdString(basePath));
    if (baseDir.exists()) {
        for (const QString& dir : baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool ok;
            int num = dir.toInt(&ok);
            if (ok && num > runNumber) runNumber = num;
        }
    }
    runNumber++;
    std::string savePath = basePath + "/" + std::to_string(runNumber);

    QDir().mkpath(QString::fromStdString(savePath));
    s_movement_controller_.setSaveDirectory(savePath);
    s_movement_controller_.setRunNumber(runNumber);
    s_movement_controller_.setMovementSpeed(cfg.defaultSpeed());

    arm::SMovementPoint startPos = {0, 0, zHeight, 0, 0, 0, 0};
    arm::SMovementPoint endPos = {endX, endY, zHeight, 0, 0,
                                  gridSize.height - 1, gridSize.width - 1};

    if (!s_movement_controller_.initialize(gridSize, startPos, endPos)) {
        emit errorMessage("S-movement initialization failed");
        return false;
    }

    s_movement_controller_.setImageCaptureCallback([this](cv::Mat& frame) {
        return camera_handler_.captureSingleFrame(frame);
    });

    if (!s_movement_controller_.start()) {
        emit errorMessage("S-movement start failed");
        return false;
    }

    emit statusMessage("S-movement started");
    return true;
}

void AppController::stopSMovement() {
    s_movement_controller_.stop();
    emit statusMessage("S-movement stopped");
}

void AppController::pauseSMovement() {
    s_movement_controller_.pause();
    emit statusMessage("S-movement paused");
}

void AppController::resumeSMovement() {
    s_movement_controller_.resume();
    emit statusMessage("S-movement resumed");
}

void AppController::stitchImages(const std::vector<cv::Mat>& images, const cv::Size& gridSize) {
    std::vector<cv::Mat> sorted = image_stitcher_.sortImagesInSCurveOrder(images, gridSize);
    cv::Mat result = image_stitcher_.stitchImages(sorted, gridSize);
    if (!result.empty()) {
        emit stitchingFinished(result);
        emit statusMessage("Stitching completed");
    } else {
        emit errorMessage("Stitching failed");
    }
}

std::vector<cv::Mat> AppController::loadImages(const std::string& dir) {
    return image_stitcher_.loadImagesFromDirectory(dir);
}

bool AppController::loadDetectorModel(const std::string& paramPath, const std::string& binPath) {
    bool ok = yolo_detector_.loadModel(paramPath, binPath);
    emit statusMessage(ok ? "Model loaded" : "Model loading failed");
    return ok;
}

std::vector<detector::Detection> AppController::detect(const cv::Mat& image) {
    return yolo_detector_.detect(image);
}

cv::Mat AppController::drawDetections(const cv::Mat& image, const std::vector<detector::Detection>& detections) {
    return yolo_detector_.drawDetections(image, detections);
}
