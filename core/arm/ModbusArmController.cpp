#include "ModbusArmController.h"

#include <spdlog/spdlog.h>
#include <chrono>

namespace arm {

ModbusArmController::ModbusArmController()
    : registerIO_(conn_) {
    // Initialize status
    current_status_.connected = false;
    current_status_.current_positions.resize(5, 0);
    current_status_.target_positions.resize(5, 0);
    current_status_.status_message = "Disconnected";

    auto_read_running_ = false;
}

ModbusArmController::~ModbusArmController() {
    stopAutoRead();
    // conn_ destructor handles modbus cleanup
}

bool ModbusArmController::connect(const std::string& ip, int port) {
    if (!conn_.connect(ip, port)) {
        return false;
    }

    // Update status
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        current_status_.connected = true;
        current_status_.status_message = "Connected to " + ip + ":" + std::to_string(port);
    }

    SPDLOG_INFO("[Arm] Testing connection by reading a register...");
    int16_t test_value;
    if (readRegisterUnsafe(7802, test_value)) {
        SPDLOG_INFO("[Arm] Test read successful, value: {}", test_value);
    } else {
        SPDLOG_WARN("[Arm] Test read failed");
    }

    SPDLOG_INFO("[Arm] Reading initial positions...");
    for (int i = 0; i < 5; ++i) {
        double pos = readPositionUnsafe(i);
        SPDLOG_INFO("[Arm] Axis {} initial position: {}", i, pos);
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            current_status_.current_positions[i] = static_cast<int32_t>(pos);
        }
    }

    SPDLOG_INFO("[Arm] Connect method completed successfully");
    return true;
}

void ModbusArmController::disconnect() {
    if (auto_read_running_) {
        stopAutoRead();
    }

    conn_.disconnect();
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        current_status_.connected = false;
        current_status_.status_message = "Disconnected";
    }

    if (status_callback_) {
        status_callback_(current_status_);
    }
}

double ModbusArmController::readPosition(int axis_id) {
    if (!conn_.isConnected() || axis_id < 0 || axis_id >= 5) {
        conn_.lastReadFailed() = true;
        return 0.0;
    }

    std::lock_guard<std::mutex> lock(conn_.mutex());

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        std::vector<int16_t> registers(2);

        conn_.lastReadFailed() = false;

        // Read the configured address (2 registers as in reference code)
        bool read_success = registerIO_.readRegisters(config.current_pos_reg, 2, registers);

        if (!read_success) {
            return 0.0;
        }

        // Combine registers into 32-bit value using unified conversion
        int32_t raw_position = convertTo32Bit(registers[0], registers[1]);

        // No scaling factor, use raw position directly
        double final_position = static_cast<double>(raw_position);

        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            current_status_.current_positions[axis_id] = static_cast<int32_t>(final_position);
        }

        return final_position;
    } catch (const std::exception& e) {
        conn_.lastReadFailed() = true;
        return 0.0;
    }
}

bool ModbusArmController::setSpeed(int axis_id, int32_t speed) {
    if (!conn_.isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(conn_.mutex());

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        int16_t low, high;
        convertTo16Bit(speed, low, high);

        std::vector<int16_t> values = {low, high};
        if (!registerIO_.writeRegisters(config.speed_reg, values)) {
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Arm] Error setting speed: {}", e.what());
        return false;
    }
}

int32_t ModbusArmController::readSpeed(int axis_id) {
    if (!conn_.isConnected() || axis_id < 0 || axis_id >= 5) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(conn_.mutex());

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        std::vector<int16_t> registers(2);

        SPDLOG_DEBUG("[Arm] Reading speed for axis {} ({})", axis_id, config.name);
        SPDLOG_DEBUG("[Arm] Speed register: {}", config.speed_reg);

        if (!registerIO_.readRegisters(config.speed_reg, 2, registers)) {
            SPDLOG_ERROR("[Arm] Failed to read speed registers");
            return -1;
        }

        SPDLOG_DEBUG("[Arm] Read speed registers - register[0]: {}, register[1]: {}", registers[0], registers[1]);

        int32_t speed = convertTo32Bit(registers[0], registers[1]);

        SPDLOG_DEBUG("[Arm] Speed: {}", speed);

        return speed;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Arm] Error reading speed: {}", e.what());
        return -1;
    }
}

bool ModbusArmController::startContinuousMovement(int axis_id, bool direction) {
    if (!conn_.isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(conn_.mutex());

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        int relay_address = direction ? config.forward_relay : config.backward_relay;
        int opposite_relay = direction ? config.backward_relay : config.forward_relay;

        SPDLOG_INFO("[Arm] Starting continuous movement for axis {} ({}) in direction {}", axis_id, config.name, direction ? "forward" : "backward");
        SPDLOG_DEBUG("[Arm] Relay addresses - forward: {}, backward: {}", config.forward_relay, config.backward_relay);
        SPDLOG_DEBUG("[Arm] Using relay: {}, opposite relay: {}", relay_address, opposite_relay);

        // Close absolute positioning relay for this axis only
        registerIO_.writeCoil(config.pos_move_abs_relay, false);
        // Close opposite direction relay for this axis only
        registerIO_.writeCoil(opposite_relay, false);
        // Wait a bit to ensure status is updated
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // Start movement for this axis only
        if (!registerIO_.writeCoil(relay_address, true)) {
            SPDLOG_ERROR("[Arm] Failed to start continuous movement");
            return false;
        }

        SPDLOG_INFO("[Arm] Continuous movement started successfully");
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Arm] Error starting continuous movement: {}", e.what());
        return false;
    }
}

bool ModbusArmController::stopContinuousMovement(int axis_id) {
    if (!conn_.isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(conn_.mutex());

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        // Stop both directions
        registerIO_.writeCoil(config.forward_relay, false);
        registerIO_.writeCoil(config.backward_relay, false);

        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Arm] Error stopping continuous movement: {}", e.what());
        return false;
    }
}

bool ModbusArmController::moveToPosition(int axis_id, double target_position) {
    if (!conn_.isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    int32_t target_pos_int = static_cast<int32_t>(target_position);
    const AxisLimits& limits = safety_limits_[axis_id];
    if (target_pos_int < limits.min_pos || target_pos_int > limits.max_pos) {
        SPDLOG_ERROR("[Arm] Safety limit exceeded for axis {} ({}): target={}, range=[{}, {}]",
                     axis_id, axis_configs_[axis_id].name, target_pos_int,
                     limits.min_pos, limits.max_pos);
        return false;
    }

    static constexpr int kMaxWaitCycles = 300;       // 300 * 20ms = 6 seconds max
    static constexpr double kArriveTolerance = 100.0; // position tolerance

    // ── Fast path: already at target position? ──
    {
        std::lock_guard<std::mutex> lock(conn_.mutex());
        double current = readPositionUnsafe(axis_id);
        if (std::abs(current - target_position) <= kArriveTolerance) {
            return true;
        }
    }

    // ── Command phase: stop, write target, trigger ──
    {
        std::lock_guard<std::mutex> lock(conn_.mutex());

        try {
            const AxisConfig& config = axis_configs_[axis_id];

            SPDLOG_INFO("[Arm] Moving to position - axis: {}, target: {} (int: {})", axis_id, target_position, target_pos_int);

            // Stop all movements first
            registerIO_.writeCoil(config.forward_relay, false);
            registerIO_.writeCoil(config.backward_relay, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));

            // Write target position
            int16_t low, high;
            convertTo16Bit(target_pos_int, low, high);
            std::vector<int16_t> values = {low, high};
            if (!registerIO_.writeRegisters(config.target_pos_reg, values)) {
                return false;
            }

            // Reset absolute positioning relay if still active
            bool relay_status;
            if (registerIO_.readCoil(config.pos_move_abs_relay, relay_status) && relay_status) {
                registerIO_.writeCoil(config.pos_move_abs_relay, false);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            // Trigger absolute positioning
            if (!registerIO_.writeCoil(config.pos_move_abs_relay, true)) {
                return false;
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[Arm] Error sending move command: {}", e.what());
            return false;
        }
    } // ── mutex released — other threads can use Modbus during the wait

    // ── Wait phase: poll until arrival (mutex NOT held) ──
    for (int wait = 0; wait < kMaxWaitCycles; ++wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        double current = readPosition(axis_id); // locks internally
        if (std::abs(current - target_position) <= kArriveTolerance) {
            SPDLOG_INFO("[Arm] Axis {} arrived at position {}", axis_id, current);
            break;
        }
        if (wait == kMaxWaitCycles - 1) {
            SPDLOG_WARN("[Arm] Axis {} move timeout: target={}, current={}", axis_id, target_position, current);
        }
    }

    // Update target position in status
    {
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        current_status_.target_positions[axis_id] = static_cast<int32_t>(target_position);
    }
    if (status_callback_) {
        ArmStatus status_copy = getStatus();
        status_callback_(status_copy);
    }

    return true;
}

bool ModbusArmController::moveXYAxes(double target_x, double target_y) {
    if (!conn_.isConnected()) {
        return false;
    }

    int32_t tx = static_cast<int32_t>(target_x);
    int32_t ty = static_cast<int32_t>(target_y);

    // Safety limits
    {
        const AxisLimits& limX = safety_limits_[0];
        const AxisLimits& limY = safety_limits_[1];
        if (tx < limX.min_pos || tx > limX.max_pos ||
            ty < limY.min_pos || ty > limY.max_pos) {
            SPDLOG_ERROR("[Arm] Safety limit: X={} range=[{},{}], Y={} range=[{},{}]",
                         tx, limX.min_pos, limX.max_pos, ty, limY.min_pos, limY.max_pos);
            return false;
        }
    }

    static constexpr double kArriveTolerance = 100.0;

    // Fast path: skip if target is 0 (readPosition error returns 0 → false positive)
    if (tx != 0 && ty != 0) {
        std::lock_guard<std::mutex> lock(conn_.mutex());
        double cx = readPositionUnsafe(0);
        double cy = readPositionUnsafe(1);
        if (std::abs(cx - target_x) <= kArriveTolerance &&
            std::abs(cy - target_y) <= kArriveTolerance) {
            return true;
        }
    }

    // ── Command phase: send both X and Y under one mutex lock ──
    {
        std::lock_guard<std::mutex> lock(conn_.mutex());
        const AxisConfig& cfgX = axis_configs_[0];
        const AxisConfig& cfgY = axis_configs_[1];

        // Stop X/Y relays
        registerIO_.writeCoil(cfgX.forward_relay, false);
        registerIO_.writeCoil(cfgX.backward_relay, false);
        registerIO_.writeCoil(cfgY.forward_relay, false);
        registerIO_.writeCoil(cfgY.backward_relay, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        // Write targets
        int16_t lx, hx, ly, hy;
        convertTo16Bit(tx, lx, hx);
        convertTo16Bit(ty, ly, hy);
        registerIO_.writeRegisters(cfgX.target_pos_reg, {lx, hx});
        registerIO_.writeRegisters(cfgY.target_pos_reg, {ly, hy});

        // Reset abs positioning relays
        bool relay_status;
        if (registerIO_.readCoil(cfgX.pos_move_abs_relay, relay_status) && relay_status) {
            registerIO_.writeCoil(cfgX.pos_move_abs_relay, false);
        }
        if (registerIO_.readCoil(cfgY.pos_move_abs_relay, relay_status) && relay_status) {
            registerIO_.writeCoil(cfgY.pos_move_abs_relay, false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Trigger both
        registerIO_.writeCoil(cfgX.pos_move_abs_relay, true);
        registerIO_.writeCoil(cfgY.pos_move_abs_relay, true);
    }

    // ── Wait phase: poll both axes concurrently (2-consecutive-read confirmation) ──
    static constexpr int kMaxWait = 300; // 300 * 20ms = 6 seconds
    int x_confirm = 0, y_confirm = 0;
    for (int wait = 0; wait < kMaxWait; ++wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        if (x_confirm < 2) {
            double cx = readPosition(0);
            if (std::abs(cx - target_x) <= kArriveTolerance) {
                x_confirm++;
                if (x_confirm == 2) SPDLOG_INFO("[Arm] X confirmed at {}", cx);
            } else {
                x_confirm = 0;
            }
        }
        if (y_confirm < 2) {
            double cy = readPosition(1);
            if (std::abs(cy - target_y) <= kArriveTolerance) {
                y_confirm++;
                if (y_confirm == 2) SPDLOG_INFO("[Arm] Y confirmed at {}", cy);
            } else {
                y_confirm = 0;
            }
        }
        if (x_confirm >= 2 && y_confirm >= 2) break;

        if (wait == kMaxWait - 1) {
            double cx = readPosition(0), cy = readPosition(1);
            SPDLOG_WARN("[Arm] XY move timeout: X={}→{}, Y={}→{}", cx, target_x, cy, target_y);
        }
    }

    // Update status
    {
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        current_status_.target_positions[0] = tx;
        current_status_.target_positions[1] = ty;
    }
    if (status_callback_) {
        ArmStatus status_copy = getStatus();
        status_callback_(status_copy);
    }

    return true;
}

bool ModbusArmController::moveAxesConcurrent(int x, int y, int z) {
    if (!conn_.isConnected()) return false;

    static constexpr int kAxes = 3; // X/Y/Z only; A/B disabled
    int32_t targets[kAxes] = {
        static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)
    };

    // Safety limits
    for (int i = 0; i < kAxes; ++i) {
        const AxisLimits& lim = safety_limits_[i];
        if (targets[i] < lim.min_pos || targets[i] > lim.max_pos) {
            SPDLOG_ERROR("[Arm] Safety limit axis {}: {} not in [{},{}]",
                         i, targets[i], lim.min_pos, lim.max_pos);
            return false;
        }
    }

    static constexpr double kArriveTolerance = 100.0;

    // Fast path: skip when any target is 0 (readPosition returns 0 on error,
    // indistinguishable from actual position 0, causing false positives)
    bool any_zero_target = false;
    for (int i = 0; i < kAxes; ++i) {
        if (targets[i] == 0) { any_zero_target = true; break; }
    }
    if (!any_zero_target) {
        std::lock_guard<std::mutex> lock(conn_.mutex());
        bool all_ok = true;
        for (int i = 0; i < kAxes; ++i) {
            if (std::abs(readPositionUnsafe(i) - targets[i]) > kArriveTolerance) {
                all_ok = false;
                break;
            }
        }
        if (all_ok) return true;
    }

    // ── Command phase: send all X/Y/Z under one mutex lock ──
    {
        std::lock_guard<std::mutex> lock(conn_.mutex());

        // Step 1: Stop all running movements
        for (int i = 0; i < kAxes; ++i) {
            registerIO_.writeCoil(axis_configs_[i].forward_relay, false);
            registerIO_.writeCoil(axis_configs_[i].backward_relay, false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Step 2: Write target positions for all axes
        for (int i = 0; i < kAxes; ++i) {
            int16_t low, high;
            convertTo16Bit(targets[i], low, high);
            registerIO_.writeRegisters(axis_configs_[i].target_pos_reg, {low, high});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Step 3: Unconditionally reset ALL abs relays to ensure clean rising edge.
        for (int i = 0; i < kAxes; ++i) {
            registerIO_.writeCoil(axis_configs_[i].pos_move_abs_relay, false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));

        // Step 4: Trigger axes one by one with 10ms gap
        for (int i = 0; i < kAxes; ++i) {
            registerIO_.writeCoil(axis_configs_[i].pos_move_abs_relay, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        SPDLOG_INFO("[Arm] All 3 axes (X/Y/Z) triggered concurrently");
    }

    // ── Wait phase: poll all 3 axes concurrently ──
    static constexpr int kMaxWait = 300; // 6 seconds
    int confirm_count[kAxes] = {0};
    for (int wait = 0; wait < kMaxWait; ++wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        bool all_confirmed = true;
        for (int i = 0; i < kAxes; ++i) {
            if (confirm_count[i] < 2) {
                double pos = readPosition(i);
                if (std::abs(pos - targets[i]) <= kArriveTolerance) {
                    confirm_count[i]++;
                    if (confirm_count[i] == 2) {
                        SPDLOG_INFO("[Arm] Axis {} confirmed at {}", i, static_cast<int>(pos));
                    }
                } else {
                    confirm_count[i] = 0;
                    all_confirmed = false;
                }
            }
        }
        if (all_confirmed) break;

        if (wait == kMaxWait - 1) {
            for (int i = 0; i < kAxes; ++i) {
                if (confirm_count[i] < 2) {
                    double pos = readPosition(i);
                    SPDLOG_WARN("[Arm] Axis {} move timeout: current={}, target={}", i, pos, targets[i]);
                }
            }
        }
    }

    // ── Fallback: retry any failed axis with proven sequential moveToPosition ──
    for (int i = 0; i < kAxes; ++i) {
        if (confirm_count[i] < 2) {
            SPDLOG_WARN("[Arm] Axis {} not confirmed after concurrent move, retrying sequentially", i);
            moveToPosition(i, targets[i]);
        }
    }

    // Update status
    {
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        for (int i = 0; i < kAxes; ++i)
            current_status_.target_positions[i] = targets[i];
    }
    if (status_callback_) {
        ArmStatus status_copy = getStatus();
        status_callback_(status_copy);
    }

    return true;
}

bool ModbusArmController::stopAllMovements(int axis_id) {
    if (!conn_.isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(conn_.mutex());

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        // Stop all movements
        registerIO_.writeCoil(config.forward_relay, false);
        registerIO_.writeCoil(config.backward_relay, false);
        registerIO_.writeCoil(config.pos_move_abs_relay, false);

        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Arm] Error stopping all movements: {}", e.what());
        return false;
    }
}

void ModbusArmController::startAutoRead(float interval) {
    if (auto_read_running_) {
        return;
    }

    // Ensure previous thread is joined
    if (auto_read_thread_.joinable()) {
        auto_read_thread_.join();
    }

    auto_read_interval_ = interval;
    auto_read_running_ = true;
    auto_read_thread_ = std::thread(&ModbusArmController::autoReadLoop, this);
}

void ModbusArmController::stopAutoRead() {
    auto_read_running_ = false;
    if (auto_read_thread_.joinable()) {
        auto_read_thread_.join();
    }
}

bool ModbusArmController::isConnected() const {
    return conn_.isConnected();
}

ArmStatus ModbusArmController::getStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return current_status_;
}

void ModbusArmController::setStatusCallback(std::function<void(const ArmStatus&)> callback) {
    status_callback_ = callback;
}

void ModbusArmController::autoReadLoop() {
    while (auto_read_running_) {
        if (!conn_.isConnected()) {
            if (!conn_.storedIp().empty() && auto_read_running_) {
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    current_status_.status_message = "Connection lost, reconnecting...";
                }
                if (status_callback_) { ArmStatus sc = getStatus(); status_callback_(sc); }

                // Retry loop with exponential backoff
                bool reconnected = false;
                for (int attempt = 0; attempt < ModbusConnection::kMaxReconnectAttempts; ++attempt) {
                    if (!auto_read_running_) break;

                    int delay_ms = std::min(1000 * (1 << attempt), 10000);
                    SPDLOG_WARN("[Arm] Reconnect attempt {}/{} in {}ms", attempt + 1, ModbusConnection::kMaxReconnectAttempts, delay_ms);
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

                    if (!auto_read_running_) break;

                    if (conn_.reconnect()) {
                        reconnected = true;
                        break;
                    }
                }

                if (reconnected) {
                    conn_.consecutiveFailures() = 0;
                    {
                        std::lock_guard<std::mutex> lock(status_mutex_);
                        current_status_.connected = true;
                        current_status_.status_message = "Reconnected to " + conn_.storedIp();
                    }
                    if (status_callback_) { ArmStatus sc = getStatus(); status_callback_(sc); }
                } else {
                    {
                        std::lock_guard<std::mutex> lock(status_mutex_);
                        current_status_.connected = false;
                        current_status_.status_message = "Reconnection failed";
                    }
                    if (status_callback_) { ArmStatus sc = getStatus(); status_callback_(sc); }
                    break;
                }
            } else {
                break;
            }
            continue;
        }

        bool any_read_failed = false;
        for (int axis_id = 0; axis_id < 5; ++axis_id) {
            if (auto_read_enabled_[axis_id]) {
                // Reset error flag before each read, readPosition sets it on failure
                conn_.lastReadFailed() = false;
                readPosition(axis_id);
                if (conn_.lastReadFailed()) {
                    any_read_failed = true;
                    break;  // Stop reading if one axis fails — connection likely broken
                }
            }
        }

        if (any_read_failed) {
            conn_.consecutiveFailures()++;
            if (conn_.consecutiveFailures() >= ModbusConnection::kMaxConsecutiveFailures) {
                SPDLOG_ERROR("[Arm] Too many consecutive failures ({}), disconnecting",
                             conn_.consecutiveFailures().load());
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    current_status_.status_message = "Connection unstable, attempting reconnect...";
                }
                if (status_callback_) { ArmStatus sc = getStatus(); status_callback_(sc); }

                conn_.disconnect();
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    current_status_.connected = false;
                    current_status_.status_message = "Disconnected";
                }
                continue;
            }
        } else {
            conn_.consecutiveFailures() = 0;
        }

        if (status_callback_) {
            ArmStatus status_copy = getStatus();
            status_callback_(status_copy);
        }

        std::this_thread::sleep_for(std::chrono::duration<float>(auto_read_interval_));
    }
}

bool ModbusArmController::readRegisterUnsafe(int address, int16_t& value) {
    return registerIO_.readRegister(address, value);
}

double ModbusArmController::readPositionUnsafe(int axis_id) {
    if (axis_id < 0 || axis_id >= 5) {
        conn_.lastReadFailed() = true;
        return 0.0;
    }

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        std::vector<int16_t> registers(2);

        conn_.lastReadFailed() = false;

        bool read_success = registerIO_.readRegisters(config.current_pos_reg, 2, registers);

        if (!read_success) {
            return 0.0;
        }

        // Combine registers into 32-bit value using unified conversion
        int32_t raw_position = convertTo32Bit(registers[0], registers[1]);

        double final_position = static_cast<double>(raw_position);
        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            current_status_.current_positions[axis_id] = static_cast<int32_t>(final_position);
        }

        return final_position;
    } catch (const std::exception& e) {
        conn_.lastReadFailed() = true;
        return 0.0;
    }
}

int32_t ModbusArmController::convertTo32Bit(int16_t low, int16_t high) {
    // Combine low and high words
    uint32_t combined = (static_cast<uint32_t>(static_cast<uint16_t>(high)) << 16) |
                       static_cast<uint32_t>(static_cast<uint16_t>(low));

    // Convert to signed integer
    return static_cast<int32_t>(combined);
}

void ModbusArmController::convertTo16Bit(int32_t value, int16_t& low, int16_t& high) {
    uint32_t u_value = static_cast<uint32_t>(value);
    low = static_cast<int16_t>(u_value & 0xFFFF);
    high = static_cast<int16_t>((u_value >> 16) & 0xFFFF);
}

void ModbusArmController::setSafetyLimits(int axis_id, const AxisLimits& limits) {
    if (axis_id >= 0 && axis_id < 5) {
        safety_limits_[axis_id] = limits;
    }
}

AxisLimits ModbusArmController::getSafetyLimits(int axis_id) const {
    if (axis_id >= 0 && axis_id < 5) {
        return safety_limits_[axis_id];
    }
    return AxisLimits{};
}

void ModbusArmController::setDebugEnabled(bool enabled) {
    conn_.setDebugEnabled(enabled);
}

std::string ModbusArmController::lastError() const {
    return conn_.getError();
}

} // namespace arm
