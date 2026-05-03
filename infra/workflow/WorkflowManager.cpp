#include "WorkflowManager.h"
#include "infra/config/ConfigManager.h"
#include <iostream>

WorkflowManager::WorkflowManager(
    arm::ModbusArmController& arm,
    arm::SMovementController& s_movement,
    camera::CameraHandler& camera,
    detector::YoloDetector& detector,
    stitch::ImageStitcher& stitcher,
    QObject* parent)
    : QObject(parent)
    , arm_(arm)
    , s_movement_(s_movement)
    , camera_(camera)
    , detector_(detector)
    , stitcher_(stitcher)
{
    step_timer_ = new QTimer(this);
    step_timer_->setSingleShot(true);
}

WorkflowManager::~WorkflowManager() {
    stopAutoWorkflow();
}

void WorkflowManager::setState(State s) {
    if (state_ != s) {
        state_ = s;
        emit stateChanged(s);
    }
}

void WorkflowManager::scheduleNext(int delayMs, std::function<void()> step) {
    if (stop_requested_) return;
    step_timer_->stop();
    connect(step_timer_, &QTimer::timeout, this, [this, step]() {
        step_timer_->disconnect();
        if (!stop_requested_) step();
    });
    step_timer_->start(delayMs);
}

void WorkflowManager::startAutoWorkflow() {
    stop_requested_ = false;
    emit statusMessage("Starting auto workflow...");
    scheduleNext(100, [this]() { onConnectArmStep(); });
}

void WorkflowManager::stopAutoWorkflow() {
    stop_requested_ = true;
    step_timer_->stop();
    s_movement_.stop();
    if (state_ == State::SMovement) {
        emit statusMessage("Workflow stopped by user");
    }
    setState(State::Idle);
}

void WorkflowManager::startSMovement(const cv::Size& grid_size,
                                     const arm::SMovementPoint& start_pos,
                                     const arm::SMovementPoint& end_pos) {
    if (!s_movement_.initialize(grid_size, start_pos, end_pos)) {
        emit workflowError("S-movement initialization failed");
        return;
    }
    setState(State::SMovement);
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
        for (int i = 0; i < 5; ++i) {
            arm_.setSpeed(i, cfg.defaultSpeed());
        }
        emit statusMessage("Arm connected, setting speed...");
        scheduleNext(500, [this]() { onConnectCameraStep(); });
    } else {
        emit workflowError("Failed to connect arm");
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
            scheduleNext(500, [this]() { onZeroStep(); });
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

    arm_.moveToPosition(0, 0);
    arm_.moveToPosition(1, 0);
    arm_.moveToPosition(2, 0);
    arm_.moveToPosition(3, 0);
    arm_.moveToPosition(4, 0);

    // Wait for zeroing to complete
    scheduleNext(3000, [this]() {
        if (!stop_requested_) {
            emit statusMessage("Zeroing complete, starting S-movement...");
            auto& cfg = ConfigManager::instance();
            cv::Size grid_size(cfg.gridSizeX(), cfg.gridSizeY());
            int end = cfg.stepSize() * (cfg.gridSizeX() - 1);

            arm::SMovementPoint start_pos = {0, 0, cfg.zHeight(), 0, 0, 0, 0};
            arm::SMovementPoint end_pos = {end, end, cfg.zHeight(), 0, 0, cfg.gridSizeY() - 1, cfg.gridSizeX() - 1};

            startSMovement(grid_size, start_pos, end_pos);
        }
    });
}

void WorkflowManager::onSMovementFinished() {
    if (stop_requested_) return;
    emit statusMessage("S-movement completed, starting stitching...");
    setState(State::Stitching);
    // Stitching is triggered externally via collected images
    setState(State::Idle);
    emit statusMessage("Workflow completed");
}

void WorkflowManager::startStitching(const std::vector<cv::Mat>& images, const cv::Size& grid_size) {
    setState(State::Stitching);
    emit statusMessage("Stitching images...");

    // Fix: pass images directly without double-sorting.
    // The images have already been collected in S-curve order from SMovementController.
    // stitchImages will place them in the grid in the order received.
    cv::Mat result = stitcher_.stitchImages(images, grid_size);

    if (!result.empty()) {
        emit stitchingFinished(result);
        emit statusMessage("Stitching completed");
    } else {
        emit workflowError("Stitching failed");
    }
    setState(State::Idle);
}
