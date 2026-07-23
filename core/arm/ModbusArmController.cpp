#include "ModbusArmController.h"

#include <modbus.h>
#include <spdlog/spdlog.h>
#include <chrono>

namespace arm {

ModbusArmController::ModbusArmController() {
    // Initialize modbus context to nullptr
    modbus_ = nullptr;

    // Initialize status
    current_status_.connected = false;
    current_status_.current_positions.resize(5, 0);
    current_status_.target_positions.resize(5, 0);
    current_status_.status_message = "Disconnected";

    auto_read_running_ = false;
}

ModbusArmController::~ModbusArmController() {
    stopAutoRead();
    if (modbus_) {
        modbus_close(modbus_);
        modbus_free(modbus_);
        modbus_ = nullptr;
    }
    connected_ = false;
}

bool ModbusArmController::connect(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(modbus_mutex_);
    SPDLOG_INFO("[Arm] Connecting to {}:{}", ip, port);
    
    if (isConnected()) {
        SPDLOG_INFO("[Arm] Already connected, disconnecting first...");
        disconnectInternal();
    }

    if (modbus_) {
        SPDLOG_INFO("[Arm] Freeing existing modbus context...");
        modbus_free(modbus_);
        modbus_ = nullptr;
    }

    SPDLOG_INFO("[Arm] Creating new modbus context with IP: {}, port: {}", ip, port);
    modbus_ = modbus_new_tcp(ip.c_str(), port);
    if (!modbus_) {
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            last_error_ = std::string("无法创建Modbus连接: ") + modbus_strerror(errno);
        }
        SPDLOG_ERROR("[Arm] {}", last_error_);
        return false;
    }

    SPDLOG_INFO("[Arm] Setting debug mode to {}...", debug_enabled_ ? "ON" : "OFF");
    if (modbus_set_debug(modbus_, debug_enabled_ ? TRUE : FALSE) == -1) {
        SPDLOG_WARN("[Arm] modbus_set_debug failed: {}", modbus_strerror(errno));
    }

    SPDLOG_INFO("[Arm] Setting slave ID to 1...");
    if (modbus_set_slave(modbus_, 1) == -1) {
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            last_error_ = std::string("设置从站ID失败: ") + modbus_strerror(errno);
        }
        SPDLOG_ERROR("[Arm] {}", last_error_);
        modbus_free(modbus_);
        modbus_ = nullptr;
        return false;
    }

    SPDLOG_INFO("[Arm] Setting response timeout to 2 seconds...");
    if (modbus_set_response_timeout(modbus_, 2, 0) == -1) {
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            last_error_ = std::string("设置超时失败: ") + modbus_strerror(errno);
        }
        SPDLOG_ERROR("[Arm] {}", last_error_);
        modbus_free(modbus_);
        modbus_ = nullptr;
        return false;
    }

    SPDLOG_INFO("[Arm] Attempting to connect to modbus server at {}:{}...", ip, port);
    if (modbus_connect(modbus_) == -1) {
        int saved_errno = errno;
#ifdef _WIN32
        int wsa_err = WSAGetLastError();
#endif
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            last_error_ = std::string("无法连接到 ") + ip + ":" + std::to_string(port)
                          + " — errno=" + std::to_string(saved_errno)
                          + " (" + modbus_strerror(saved_errno) + ")"
#ifdef _WIN32
                          + " wsa=" + std::to_string(wsa_err)
#endif
                          ;
        }
        SPDLOG_ERROR("[Arm] {}", last_error_);
        modbus_free(modbus_);
        modbus_ = nullptr;
        return false;
    }

    SPDLOG_INFO("[Arm] Connection successful!");
    connected_ = true;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        current_status_.connected = true;
        current_status_.status_message = "Connected to " + ip + ":" + std::to_string(port);
    }
    stored_ip_ = ip;
    stored_port_ = port;
    consecutive_failures_ = 0;

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

void ModbusArmController::disconnectInternal() {
    if (modbus_) {
        modbus_close(modbus_);
        modbus_free(modbus_);
        modbus_ = nullptr;
    }
    connected_ = false;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        current_status_.connected = false;
        current_status_.status_message = "Disconnected";
    }
}

void ModbusArmController::disconnect() {
    if (auto_read_running_) {
        stopAutoRead();
    }

    {
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        disconnectInternal();
    }

    if (status_callback_) {
        status_callback_(current_status_);
    }
}

double ModbusArmController::readPosition(int axis_id) {
    if (!isConnected() || axis_id < 0 || axis_id >= 5) {
        last_read_failed_ = true;
        return 0.0;
    }

    std::lock_guard<std::mutex> lock(modbus_mutex_);

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        std::vector<int16_t> registers(2);

        last_read_failed_ = false;

        // Read the configured address (2 registers as in reference code)
        bool read_success = readRegisters(config.current_pos_reg, 2, registers);

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
        last_read_failed_ = true;
        return 0.0;
    }
}

bool ModbusArmController::setSpeed(int axis_id, int32_t speed) {
    if (!isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(modbus_mutex_);

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        int16_t low, high;
        convertTo16Bit(speed, low, high);

        std::vector<int16_t> values = {low, high};
        if (!writeRegisters(config.speed_reg, values)) {
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Arm] Error setting speed: {}", e.what());
        return false;
    }
}

int32_t ModbusArmController::readSpeed(int axis_id) {
    if (!isConnected() || axis_id < 0 || axis_id >= 5) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(modbus_mutex_);

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        std::vector<int16_t> registers(2);

        SPDLOG_DEBUG("[Arm] Reading speed for axis {} ({})", axis_id, config.name);
        SPDLOG_DEBUG("[Arm] Speed register: {}", config.speed_reg);

        if (!readRegisters(config.speed_reg, 2, registers)) {
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
    if (!isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(modbus_mutex_);

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        int relay_address = direction ? config.forward_relay : config.backward_relay;
        int opposite_relay = direction ? config.backward_relay : config.forward_relay;

        SPDLOG_INFO("[Arm] Starting continuous movement for axis {} ({}) in direction {}", axis_id, config.name, direction ? "forward" : "backward");
        SPDLOG_DEBUG("[Arm] Relay addresses - forward: {}, backward: {}", config.forward_relay, config.backward_relay);
        SPDLOG_DEBUG("[Arm] Using relay: {}, opposite relay: {}", relay_address, opposite_relay);

        // Close absolute positioning relay for this axis only
        writeCoil(config.pos_move_abs_relay, false);
        // Close opposite direction relay for this axis only
        writeCoil(opposite_relay, false);
        // Wait a bit to ensure status is updated
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // Start movement for this axis only
        if (!writeCoil(relay_address, true)) {
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
    if (!isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(modbus_mutex_);

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        // Stop both directions
        writeCoil(config.forward_relay, false);
        writeCoil(config.backward_relay, false);

        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Arm] Error stopping continuous movement: {}", e.what());
        return false;
    }
}

bool ModbusArmController::moveToPosition(int axis_id, double target_position) {
    if (!isConnected() || axis_id < 0 || axis_id >= 5) {
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
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        double current = readPositionUnsafe(axis_id);
        if (std::abs(current - target_position) <= kArriveTolerance) {
            return true;
        }
    }

    // ── Command phase: stop, write target, trigger ──
    {
        std::lock_guard<std::mutex> lock(modbus_mutex_);

        try {
            const AxisConfig& config = axis_configs_[axis_id];

            SPDLOG_INFO("[Arm] Moving to position - axis: {}, target: {} (int: {})", axis_id, target_position, target_pos_int);

            // Stop all movements first
            writeCoil(config.forward_relay, false);
            writeCoil(config.backward_relay, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));

            // Write target position
            int16_t low, high;
            convertTo16Bit(target_pos_int, low, high);
            std::vector<int16_t> values = {low, high};
            if (!writeRegisters(config.target_pos_reg, values)) {
                return false;
            }

            // Reset absolute positioning relay if still active
            bool relay_status;
            if (readCoil(config.pos_move_abs_relay, relay_status) && relay_status) {
                writeCoil(config.pos_move_abs_relay, false);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            // Trigger absolute positioning
            if (!writeCoil(config.pos_move_abs_relay, true)) {
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
    if (!isConnected()) {
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
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        double cx = readPositionUnsafe(0);
        double cy = readPositionUnsafe(1);
        if (std::abs(cx - target_x) <= kArriveTolerance &&
            std::abs(cy - target_y) <= kArriveTolerance) {
            return true;
        }
    }

    // ── Command phase: send both X and Y under one mutex lock ──
    {
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        const AxisConfig& cfgX = axis_configs_[0];
        const AxisConfig& cfgY = axis_configs_[1];

        // Stop X/Y relays
        writeCoil(cfgX.forward_relay, false);
        writeCoil(cfgX.backward_relay, false);
        writeCoil(cfgY.forward_relay, false);
        writeCoil(cfgY.backward_relay, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        // Write targets
        int16_t lx, hx, ly, hy;
        convertTo16Bit(tx, lx, hx);
        convertTo16Bit(ty, ly, hy);
        writeRegisters(cfgX.target_pos_reg, {lx, hx});
        writeRegisters(cfgY.target_pos_reg, {ly, hy});

        // Reset abs positioning relays
        bool relay_status;
        if (readCoil(cfgX.pos_move_abs_relay, relay_status) && relay_status) {
            writeCoil(cfgX.pos_move_abs_relay, false);
        }
        if (readCoil(cfgY.pos_move_abs_relay, relay_status) && relay_status) {
            writeCoil(cfgY.pos_move_abs_relay, false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Trigger both
        writeCoil(cfgX.pos_move_abs_relay, true);
        writeCoil(cfgY.pos_move_abs_relay, true);
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
    if (!isConnected()) return false;

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
        std::lock_guard<std::mutex> lock(modbus_mutex_);
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
        std::lock_guard<std::mutex> lock(modbus_mutex_);

        // Step 1: Stop all running movements
        for (int i = 0; i < kAxes; ++i) {
            writeCoil(axis_configs_[i].forward_relay, false);
            writeCoil(axis_configs_[i].backward_relay, false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Step 2: Write target positions for all axes
        for (int i = 0; i < kAxes; ++i) {
            int16_t low, high;
            convertTo16Bit(targets[i], low, high);
            writeRegisters(axis_configs_[i].target_pos_reg, {low, high});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Step 3: Unconditionally reset ALL abs relays to ensure clean rising edge.
        for (int i = 0; i < kAxes; ++i) {
            writeCoil(axis_configs_[i].pos_move_abs_relay, false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));

        // Step 4: Trigger axes one by one with 10ms gap
        for (int i = 0; i < kAxes; ++i) {
            writeCoil(axis_configs_[i].pos_move_abs_relay, true);
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
    if (!isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(modbus_mutex_);

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        // Stop all movements
        writeCoil(config.forward_relay, false);
        writeCoil(config.backward_relay, false);
        writeCoil(config.pos_move_abs_relay, false);

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
    return connected_ && modbus_;
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
        if (!isConnected()) {
            if (!stored_ip_.empty() && auto_read_running_) {
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    current_status_.status_message = "Connection lost, reconnecting...";
                }
                if (status_callback_) { ArmStatus sc = getStatus(); status_callback_(sc); }
                if (attemptReconnect()) {
                    consecutive_failures_ = 0;
                    {
                        std::lock_guard<std::mutex> lock(status_mutex_);
                        current_status_.connected = true;
                        current_status_.status_message = "Reconnected to " + stored_ip_;
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
                last_read_failed_ = false;
                readPosition(axis_id);
                if (last_read_failed_) {
                    any_read_failed = true;
                    break;  // Stop reading if one axis fails — connection likely broken
                }
            }
        }

        if (any_read_failed) {
            consecutive_failures_++;
            if (consecutive_failures_ >= kMaxConsecutiveFailures) {
                SPDLOG_ERROR("[Arm] Too many consecutive failures ({}), disconnecting",
                             consecutive_failures_.load());
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    current_status_.status_message = "Connection unstable, attempting reconnect...";
                }
                if (status_callback_) { ArmStatus sc = getStatus(); status_callback_(sc); }

                {
                    std::lock_guard<std::mutex> lock(modbus_mutex_);
                    disconnectInternal();
                }
                continue;
            }
        } else {
            consecutive_failures_ = 0;
        }

        if (status_callback_) {
            ArmStatus status_copy = getStatus();
            status_callback_(status_copy);
        }

        std::this_thread::sleep_for(std::chrono::duration<float>(auto_read_interval_));
    }
}

bool ModbusArmController::writeCoil(int address, bool value) {
    if (!isConnected()) {
        return false;
    }

    int ret = modbus_write_bit(modbus_, address, value ? 1 : 0);
    if (ret != 1) {
        SPDLOG_ERROR("[Arm] Failed to write coil {}: {}", address, modbus_strerror(errno));
        return false;
    }

    return true;
}

bool ModbusArmController::readCoil(int address, bool& value) {
    if (!isConnected()) {
        return false;
    }

    uint8_t bit;
    int ret = modbus_read_bits(modbus_, address, 1, &bit);
    if (ret != 1) {
        SPDLOG_ERROR("[Arm] Failed to read coil {}: {}", address, modbus_strerror(errno));
        return false;
    }

    value = (bit == 1);
    return true;
}

bool ModbusArmController::writeRegister(int address, int16_t value) {
    if (!isConnected()) {
        return false;
    }

    int ret = modbus_write_register(modbus_, address, value);
    if (ret != 1) {
        SPDLOG_ERROR("[Arm] Failed to write register {}: {}", address, modbus_strerror(errno));
        return false;
    }

    return true;
}

bool ModbusArmController::writeRegisters(int address, const std::vector<int16_t>& values) {
    if (!isConnected()) {
        return false;
    }

    std::vector<uint16_t> uint_values(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        uint_values[i] = static_cast<uint16_t>(values[i]);
    }
    int ret = modbus_write_registers(modbus_, address, uint_values.size(), uint_values.data());
    if (ret != static_cast<int>(values.size())) {
        SPDLOG_ERROR("[Arm] Failed to write registers starting at {}: {}", address, modbus_strerror(errno));
        return false;
    }

    return true;
}

bool ModbusArmController::readRegister(int address, int16_t& value) {
    if (!isConnected()) {
        last_read_failed_ = true;
        return false;
    }

    uint16_t reg_value;
    int ret = modbus_read_registers(modbus_, address, 1, &reg_value);
    if (ret != 1) {
        SPDLOG_ERROR("[Arm] Failed to read register {}: {}", address, modbus_strerror(errno));
        last_read_failed_ = true;
        return false;
    }

    value = static_cast<int16_t>(reg_value);
    return true;
}

bool ModbusArmController::readRegisters(int address, int count, std::vector<int16_t>& values) {
    if (!isConnected()) {
        last_read_failed_ = true;
        return false;
    }

    std::vector<uint16_t> temp_values(count);
    int ret = modbus_read_registers(modbus_, address, count, temp_values.data());

    if (ret != count) {
        SPDLOG_ERROR("[Arm] Failed to read {} registers at address {}: {} (got {})",
                     count, address, modbus_strerror(errno), ret);
        last_read_failed_ = true;
        return false;
    }

    values.resize(count);
    for (int i = 0; i < count; ++i) {
        values[i] = static_cast<int16_t>(temp_values[i]);
    }

    return true;
}

bool ModbusArmController::readRegisterUnsafe(int address, int16_t& value) {
    uint16_t reg_value;
    int ret = modbus_read_registers(modbus_, address, 1, &reg_value);
    if (ret != 1) {
        SPDLOG_ERROR("[Arm] Failed to read register {}: {}", address, modbus_strerror(errno));
        last_read_failed_ = true;
        return false;
    }
    value = static_cast<int16_t>(reg_value);
    return true;
}

double ModbusArmController::readPositionUnsafe(int axis_id) {
    if (axis_id < 0 || axis_id >= 5) {
        last_read_failed_ = true;
        return 0.0;
    }

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        std::vector<int16_t> registers(2);

        last_read_failed_ = false;

        bool read_success = readRegisters(config.current_pos_reg, 2, registers);

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
        last_read_failed_ = true;
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
    debug_enabled_ = enabled;
}

bool ModbusArmController::attemptReconnect() {
    for (int attempt = 0; attempt < kMaxReconnectAttempts; ++attempt) {
        if (!auto_read_running_) return false;

        int delay_ms = std::min(1000 * (1 << attempt), 10000);
        SPDLOG_WARN("[Arm] Reconnect attempt {}/{} in {}ms", attempt + 1, kMaxReconnectAttempts, delay_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

        if (!auto_read_running_) return false;

        std::lock_guard<std::mutex> lock(modbus_mutex_);

        if (modbus_) {
            modbus_free(modbus_);
            modbus_ = nullptr;
        }

        modbus_ = modbus_new_tcp(stored_ip_.c_str(), stored_port_);
        if (!modbus_) continue;

        modbus_set_slave(modbus_, 1);
        modbus_set_response_timeout(modbus_, 2, 0);

        if (modbus_connect(modbus_) == -1) {
            modbus_free(modbus_);
            modbus_ = nullptr;
            continue;
        }

        connected_ = true;
        SPDLOG_INFO("[Arm] Reconnected successfully");
        return true;
    }
    return false;
}

} // namespace arm
