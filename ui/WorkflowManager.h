#pragma once

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <QObject>
#include <QTimer>
#include <QCoreApplication>
#include <atomic>
#include <functional>
#include <chrono>
#include <thread>

#include "core/arm/ModbusArmController.h"
#include "core/arm/SMovementController.h"
#include "core/camera/CameraHandler.h"
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
        stitch::ImageStitcher& stitcher,
        QObject* parent = nullptr);
    ~WorkflowManager();

    State currentState() const { return state_; }

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
    bool waitForPosition(const arm::SMovementPoint& target,
                         int timeoutMs = 10000, int checkIntervalMs = 100);

    arm::ModbusArmController& arm_;
    arm::SMovementController& s_movement_;
    camera::CameraHandler& camera_;
    stitch::ImageStitcher& stitcher_;

    State state_ = State::Idle;
    std::atomic<bool> stop_requested_ = false;
    QTimer* step_timer_;
    cv::Size grid_size_;
};
