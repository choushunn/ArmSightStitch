#pragma once

#include <QObject>
#include <QTimer>
#include <atomic>
#include <functional>

#include "core/arm/IArmController.h"
#include "core/camera/ICameraHandler.h"
#include "core/stitch/IStitcher.h"

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
        arm::IArmController& arm,
        arm::ISMovementController& s_movement,
        camera::ICameraHandler& camera,
        stitch::IStitcher& stitcher,
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

    arm::IArmController& arm_;
    arm::ISMovementController& s_movement_;
    camera::ICameraHandler& camera_;
    stitch::IStitcher& stitcher_;

    State state_ = State::Idle;
    std::atomic<bool> stop_requested_ = false;
    QTimer* step_timer_;
};
