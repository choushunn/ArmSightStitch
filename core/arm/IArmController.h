#pragma once

#include <string>
#include <vector>
#include <functional>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

namespace arm {

struct AxisConfig {
    int target_pos_reg;
    int current_pos_reg;
    int speed_reg;
    int pos_move_relay;
    int pos_move_abs_relay;
    int forward_relay;
    int backward_relay;
    std::string name;
};

struct AxisLimits {
    int32_t min_pos = -1000000;
    int32_t max_pos = 1000000;
};

struct ArmStatus {
    std::vector<int32_t> current_positions;
    std::vector<int32_t> target_positions;
    bool connected = false;
    std::string status_message;
};

struct SMovementPoint {
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t a;
    int32_t b;
    int row = 0;
    int col = 0;
};

struct SMovementStatus {
    bool running = false;
    bool paused = false;
    int current_point = 0;
    int total_points = 0;
    std::string status_message;
    std::string current_action;
    SMovementPoint current_position{};
};

class IArmController {
public:
    virtual ~IArmController() = default;

    virtual bool connect(const std::string& ip, int port) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual double readPosition(int axis_id) = 0;
    virtual bool setSpeed(int axis_id, int32_t speed) = 0;
    virtual int32_t readSpeed(int axis_id) = 0;

    virtual bool startContinuousMovement(int axis_id, bool direction) = 0;
    virtual bool stopContinuousMovement(int axis_id) = 0;
    virtual bool moveToPosition(int axis_id, double target_position) = 0;
    virtual bool stopAllMovements(int axis_id) = 0;

    virtual void setSafetyLimits(int axis_id, const AxisLimits& limits) = 0;
    virtual AxisLimits getSafetyLimits(int axis_id) const = 0;

    virtual void startAutoRead(float interval = 0.5f) = 0;
    virtual void stopAutoRead() = 0;

    virtual ArmStatus getStatus() const = 0;
    virtual void setStatusCallback(std::function<void(const ArmStatus&)> callback) = 0;
};

class ISMovementController {
public:
    virtual ~ISMovementController() = default;

    virtual bool initialize(const cv::Size& grid_size,
                           const SMovementPoint& start_pos,
                           const SMovementPoint& end_pos,
                           int step_size = 10000) = 0;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;

    virtual void setImageCaptureCallback(std::function<bool(cv::Mat&)> callback) = 0;
    virtual void setImageSaveCallback(std::function<bool(const cv::Mat&, const std::string&, int, int)> callback) = 0;
    virtual void setStatusCallback(std::function<void(const SMovementStatus&)> callback) = 0;

    virtual void setSaveDirectory(const std::string& save_dir) = 0;
    virtual void setPositionTolerance(double tolerance) = 0;
    virtual SMovementStatus getStatus() const = 0;
    virtual std::vector<SMovementPoint> getMovementPath() const = 0;
    virtual int getSavedImagesCount() const = 0;
};

} // namespace arm
