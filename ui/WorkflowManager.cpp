#include "WorkflowManager.h"
#include "infra/config/ConfigManager.h"
#include "infra/config/PathUtils.h"
#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QDir>
#include <chrono>
#include <thread>

WorkflowManager::WorkflowManager(
    arm::IArmController& arm,
    arm::ISMovementController& s_movement,
    camera::ICameraHandler& camera,
    stitch::IStitcher& stitcher,
    QObject* parent)
    : QObject(parent)
    , arm_(arm)
    , s_movement_(s_movement)
    , camera_(camera)
    , stitcher_(stitcher)
{
    step_timer_ = new QTimer(this);
    step_timer_->setSingleShot(true);
    SPDLOG_INFO("WorkflowManager initialized");
}

WorkflowManager::~WorkflowManager() {
    stopAutoWorkflow();
    SPDLOG_INFO("WorkflowManager destroyed");
}

void WorkflowManager::setState(State s) {
    if (state_ != s) {
        State old = state_;
        state_ = s;
        SPDLOG_INFO("Workflow state: {} -> {}",
                     static_cast<int>(old), static_cast<int>(s));
        emit stateChanged(s);
    }
}

void WorkflowManager::scheduleNext(int delayMs, std::function<void()> step) {
    if (stop_requested_) return;

    step_timer_->stop();
    step_timer_->disconnect();

    connect(step_timer_, &QTimer::timeout, this, [this, step]() {
        step_timer_->disconnect();
        if (!stop_requested_) {
            try {
                step();
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Workflow step error: {}", e.what());
                emit workflowError(QString::fromStdString(e.what()));
                setState(State::Error);
            }
        }
    });

    step_timer_->start(delayMs);
}

bool WorkflowManager::waitForPosition(const arm::SMovementPoint& target,
                                       int timeoutMs, int checkIntervalMs) {
    auto start = std::chrono::steady_clock::now();
    const double tolerance = ConfigManager::instance().positionTolerance();

    while (!stop_requested_) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeoutMs) {
            SPDLOG_WARN("Position wait timed out after {}ms", timeoutMs);
            return false;
        }

        double cx = arm_.readPosition(0);
        double cy = arm_.readPosition(1);
        double cz = arm_.readPosition(2);

        if (std::abs(cx - target.x) <= tolerance &&
            std::abs(cy - target.y) <= tolerance &&
            std::abs(cz - target.z) <= tolerance) {
            return true;
        }

        QCoreApplication::processEvents(QEventLoop::AllEvents, checkIntervalMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
    }
    return false;
}

void WorkflowManager::startAutoWorkflow() {
    stop_requested_ = false;
    SPDLOG_INFO("Starting auto workflow");
    emit statusMessage("Starting auto workflow...");
    scheduleNext(100, [this]() { onConnectArmStep(); });
}

void WorkflowManager::stopAutoWorkflow() {
    SPDLOG_INFO("Stopping auto workflow");
    stop_requested_ = true;
    step_timer_->stop();
    step_timer_->disconnect();
    s_movement_.stop();
    if (state_ == State::SMovement) {
        emit statusMessage("Workflow stopped by user");
    }
    setState(State::Idle);
}

void WorkflowManager::startSMovement(const cv::Size& grid_size,
                                     const arm::SMovementPoint& start_pos,
                                     const arm::SMovementPoint& end_pos) {
    // Pass spherical cap parameters from config
    auto& cfg = ConfigManager::instance();
    s_movement_.setSphereParams(cfg.sphereRadius(), cfg.sphereCapHeight(),
                                cfg.sphereHeightOffset(), cfg.zBaseHeight());

    if (!s_movement_.initialize(grid_size, start_pos, end_pos)) {
        emit workflowError("S-movement initialization failed");
        return;
    }
    grid_size_ = grid_size;
    setState(State::SMovement);

    s_movement_.setStatusCallback([this](const arm::SMovementStatus& status) {
        if (!status.running && state_ == State::SMovement && !stop_requested_) {
            if (status.status_message.find("completed") != std::string::npos) {
                QMetaObject::invokeMethod(this, "onSMovementFinished", Qt::QueuedConnection);
            }
        }
    });

    s_movement_.start();
}

void WorkflowManager::stopSMovement() {
    s_movement_.stop();
    setState(State::Idle);
}

void WorkflowManager::onConnectArmStep() {
    if (stop_requested_) return;
    setState(State::ConnectingArm);
    emit statusMessage("Connecting to arm...");

    auto& cfg = ConfigManager::instance();
    if (arm_.connect(cfg.armIp(), cfg.armPort())) {
        for (int i = 0; i < 3; ++i) { // X/Y/Z only; A/B disabled
            arm_.setSpeed(i, cfg.defaultSpeed());
        }
        emit statusMessage("Arm connected, setting speed...");
        scheduleNext(200, [this]() { onConnectCameraStep(); });
    } else {
        QString err = "Failed to connect arm at " + QString::fromStdString(cfg.armIp()) +
                      ":" + QString::number(cfg.armPort());
        SPDLOG_ERROR(err.toStdString());
        emit workflowError(err);
        setState(State::Error);
    }
}

void WorkflowManager::onConnectCameraStep() {
    if (stop_requested_) return;
    setState(State::ConnectingCamera);
    emit statusMessage("Connecting to camera...");

    auto cameras = camera_.enumerateCameras();
    if (!cameras.empty()) {
        if (camera_.connect("")) {
            camera_.startCapture();
            emit statusMessage("Camera connected and capturing");
            scheduleNext(200, [this]() { onZeroStep(); });
        } else {
            emit workflowError("Failed to connect camera");
            setState(State::Error);
        }
    } else {
        emit workflowError("No camera found");
        setState(State::Error);
    }
}

void WorkflowManager::onZeroStep() {
    if (stop_requested_) return;
    setState(State::Zeroing);
    emit statusMessage("Zeroing all axes...");

    arm::SMovementPoint zero_pos = {0, 0, 0, 0, 0, 0, 0};

    arm_.moveAxesConcurrent(0, 0, 0);

    scheduleNext(500, [this, zero_pos]() {
        if (!stop_requested_) {
            bool arrived = waitForPosition(zero_pos, 10000, 100);
            if (arrived || stop_requested_) {
                emit statusMessage("Zeroing complete, starting S-movement...");
                auto& cfg = ConfigManager::instance();
                cv::Size grid_size(cfg.gridSizeX(), cfg.gridSizeY());
                int end = cfg.stepSize() * (cfg.gridSizeX() - 1);

                arm::SMovementPoint start_pos = {0, 0, cfg.zHeight(), 0, 0, 0, 0};
                arm::SMovementPoint end_pos = {end, end, cfg.zHeight(), 0, 0, cfg.gridSizeY() - 1, cfg.gridSizeX() - 1};

                startSMovement(grid_size, start_pos, end_pos);
            } else {
                emit workflowError("Zeroing timed out");
                setState(State::Error);
            }
        }
    });
}

void WorkflowManager::onSMovementFinished() {
    if (stop_requested_) return;
    emit statusMessage("S-movement completed, starting stitching...");
    setState(State::Stitching);

    auto& cfg = ConfigManager::instance();
    std::string basePath = cfg.imageSaveBasePath();

    std::string runDir = infra::findLatestRunDir(basePath);
    std::string loadPath = runDir.empty() ? basePath : runDir + "/original";

    // Use position-based loading: parse row_col from filenames, auto-detect grid
    cv::Size detected_grid = grid_size_;
    auto positioned = stitcher_.loadImagesWithPositions(loadPath, detected_grid);

    if (positioned.empty()) {
        // Fallback: try legacy sequential loading for backward compatibility
        auto images = stitcher_.loadImagesFromDirectory(loadPath);
        if (images.empty()) {
            emit workflowError("No images found for stitching");
            setState(State::Idle);
            return;
        }
        auto sorted = stitcher_.sortImagesInSCurveOrder(images, grid_size_);
        cv::Mat result = stitcher_.stitchImages(sorted, grid_size_);
        if (!result.empty()) {
            emit stitchingFinished(result);
            emit statusMessage("Workflow completed successfully");
        } else {
            emit workflowError("Stitching failed");
        }
        setState(State::Idle);
        return;
    }

    // Apply center crop setting from config
    stitcher_.setCenterCropSize(cfg.centerCropSize());
    stitcher_.setAlgorithm(cfg.stitchAlgorithm());

    // Direct position-based stitching (like docs/stitch.py)
    cv::Mat result = stitcher_.stitchImagesWithPositions(positioned, detected_grid);

    if (!result.empty()) {
        emit stitchingFinished(result);
        emit statusMessage("Workflow completed successfully");
    } else {
        emit workflowError("Stitching failed");
    }
    setState(State::Idle);
}

void WorkflowManager::startStitching(const std::vector<cv::Mat>& images, const cv::Size& grid_size) {
    setState(State::Stitching);
    emit statusMessage("Stitching images...");

    cv::Mat result = stitcher_.stitchImages(images, grid_size);

    if (!result.empty()) {
        emit stitchingFinished(result);
        emit statusMessage("Stitching completed");
    } else {
        emit workflowError("Stitching failed");
    }
    setState(State::Idle);
}
