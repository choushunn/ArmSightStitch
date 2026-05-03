#pragma once

#include <QObject>
#include <QTimer>
#include <atomic>
#include <functional>

#include "core/arm/ModbusArmController.h"
#include "core/arm/SMovementController.h"
#include "core/camera/CameraHandler.h"
#include "core/detector/YoloDetector.h"
#include "core/stitch/ImageStitcher.h"

class WorkflowManager : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,
        ConnectingArm,
        ConnectingCamera,
        Zeroing,
        SMovement,
        Stitching,
        Error
    };

    explicit WorkflowManager(
        arm::ModbusArmController& arm,
        arm::SMovementController& s_movement,
        camera::CameraHandler& camera,
        detector::YoloDetector& detector,
        stitch::ImageStitcher& stitcher,
        QObject* parent = nullptr);
    ~WorkflowManager();

    State currentState() const { return state_; }

    // Manual control
    void startAutoWorkflow();
    void stopAutoWorkflow();
    void startSMovement(const cv::Size& grid_size,
                        const arm::SMovementPoint& start_pos,
                        const arm::SMovementPoint& end_pos);
    void stopSMovement();
    void startStitching(const std::vector<cv::Mat>& images, const cv::Size& grid_size);

signals:
    void stateChanged(WorkflowManager::State newState);
    void statusMessage(const QString& message);
    void workflowError(const QString& error);
    void stitchingFinished(const cv::Mat& result);

private slots:
    void onConnectArmStep();
    void onConnectCameraStep();
    void onZeroStep();
    void onSMovementFinished();

private:
    void setState(State s);
    void scheduleNext(int delayMs, std::function<void()> step);

    arm::ModbusArmController& arm_;
    arm::SMovementController& s_movement_;
    camera::CameraHandler& camera_;
    detector::YoloDetector& detector_;
    stitch::ImageStitcher& stitcher_;

    State state_ = State::Idle;
    std::atomic<bool> stop_requested_ = false;
    QTimer* step_timer_;
};
