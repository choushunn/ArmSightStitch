#pragma once

#include <mutex>
#include <thread>
#include <atomic>

#include "IArmController.h"
#include "ModbusConnection.h"
#include "ModbusRegisterIO.h"

namespace arm {

class ModbusArmController : public IArmController {
public:
    ModbusArmController();
    ~ModbusArmController();

    /**
     * @brief Connect to modbus server
     * @param ip IP address of the arm
     * @param port Port number of the arm
     * @return true if connected successfully, false otherwise
     */
    bool connect(const std::string& ip, int port) override;

    /**
     * @brief Disconnect from modbus server
     */
    void disconnect() override;

    /**
     * @brief Read current position of specified axis
     * @param axis_id Axis ID (0-4: X, Y, Z, A, B)
     * @return Current position, or 0.0 if error
     */
    double readPosition(int axis_id) override;

    /**
     * @brief Set speed of specified axis
     * @param axis_id Axis ID (0-4: X, Y, Z, A, B)
     * @param speed Speed value
     * @return true if success, false otherwise
     */
    bool setSpeed(int axis_id, int32_t speed) override;

    /**
     * @brief Read speed of specified axis
     * @param axis_id Axis ID (0-4: X, Y, Z, A, B)
     * @return Speed value, or -1 if error
     */
    int32_t readSpeed(int axis_id) override;

    /**
     * @brief Start continuous movement
     * @param axis_id Axis ID (0-4: X, Y, Z, A, B)
     * @param direction Movement direction (true: forward, false: backward)
     * @return true if success, false otherwise
     */
    bool startContinuousMovement(int axis_id, bool direction) override;

    /**
     * @brief Stop continuous movement
     * @param axis_id Axis ID (0-4: X, Y, Z, A, B)
     * @return true if success, false otherwise
     */
    bool stopContinuousMovement(int axis_id) override;

    /**
     * @brief Move to absolute position
     * @param axis_id Axis ID (0-4: X, Y, Z, A, B)
     * @param target_position Target position
     * @return true if success, false otherwise
     */
    bool moveToPosition(int axis_id, double target_position) override;
    bool moveXYAxes(double target_x, double target_y) override;
    bool moveAxesConcurrent(int x, int y, int z) override;

    /**
     * @brief Stop all movements
     * @param axis_id Axis ID (0-4: X, Y, Z, A, B)
     * @return true if success, false otherwise
     */
    bool stopAllMovements(int axis_id) override;

    void setSafetyLimits(int axis_id, const AxisLimits& limits) override;
    AxisLimits getSafetyLimits(int axis_id) const override;
    void setDebugEnabled(bool enabled);
    std::string lastError() const override;

    /**
     * @brief Start auto reading of positions
     * @param interval Interval in seconds
     */
    void startAutoRead(float interval = 0.5f) override;

    /**
     * @brief Stop auto reading of positions
     */
    void stopAutoRead() override;

    /**
     * @brief Check if connected
     * @return true if connected, false otherwise
     */
    bool isConnected() const override;

    /**
     * @brief Get current arm status
     * @return Arm status
     */
    ArmStatus getStatus() const override;

    /**
     * @brief Status update callback
     * @param callback Callback function
     */
    void setStatusCallback(std::function<void(const ArmStatus&)> callback) override;

private:
    /**
     * @brief Auto read loop
     */
    void autoReadLoop();

    /**
     * @brief Read register without connected check (caller must verify connection first)
     * @param address Register address
     * @param value Output register value
     * @return true if success, false otherwise
     */
    bool readRegisterUnsafe(int address, int16_t& value);

    /**
     * @brief Read position without connected check (caller must verify connection first)
     * @param axis_id Axis ID (0-4: X, Y, Z, A, B)
     * @return Current position, or 0.0 if error
     */
    double readPositionUnsafe(int axis_id);

    /**
     * @brief Convert two 16-bit registers to 32-bit integer
     * @param low Low word
     * @param high High word
     * @return 32-bit integer
     */
    int32_t convertTo32Bit(int16_t low, int16_t high);

    /**
     * @brief Convert 32-bit integer to two 16-bit registers
     * @param value 32-bit integer
     * @param low Output low word
     * @param high Output high word
     */
    void convertTo16Bit(int32_t value, int16_t& low, int16_t& high);

private:
    ModbusConnection conn_;
    ModbusRegisterIO registerIO_;

    std::atomic<bool> auto_read_running_ = false;
    std::thread auto_read_thread_;
    float auto_read_interval_ = 0.5f;
    std::vector<bool> auto_read_enabled_ = {true, true, true, false, false}; // X/Y/Z only; A/B disabled
    std::function<void(const ArmStatus&)> status_callback_;
    ArmStatus current_status_;
    mutable std::mutex status_mutex_;

    // Axis configurations
    std::vector<AxisConfig> axis_configs_ = {
        {7800, 7802, 7804, 7504, 7505, 7502, 7503, "X轴"}, // X axis
        {7810, 7812, 7814, 7514, 7515, 7512, 7513, "Y轴"}, // Y axis
        {7820, 7822, 7824, 7524, 7525, 7522, 7523, "Z轴"}, // Z axis
        {7830, 7832, 7834, 7534, 7535, 7532, 7533, "A轴"}, // A axis
        {7840, 7842, 7844, 7544, 7545, 7542, 7543, "B轴"}  // B axis
    };

    std::vector<AxisLimits> safety_limits_ = {
        {0, 384000}, {0, 384000}, {0, 80000},
        {-180000, 180000}, {-180000, 180000}
    };
};

} // namespace arm
