#include "ModbusArmController.h"

#include <modbus.h>
#include <iostream>
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
    std::cout << "ModbusArmController: Connecting to " << ip << ":" << port << std::endl;
    
    if (isConnected()) {
        std::cout << "ModbusArmController: Already connected, disconnecting first..." << std::endl;
        disconnect();
    }

    // Free existing modbus context if it exists
    if (modbus_) {
        std::cout << "ModbusArmController: Freeing existing modbus context..." << std::endl;
        modbus_free(modbus_);
        modbus_ = nullptr;
    }

    // Create new modbus context with provided IP and port
    std::cout << "ModbusArmController: Creating new modbus context with IP: " << ip << ", port: " << port << std::endl;
    modbus_ = modbus_new_tcp(ip.c_str(), port);
    if (!modbus_) {
        std::cerr << "ModbusArmController: Failed to create modbus context: " << modbus_strerror(errno) << std::endl;
        return false;
    }

    // Set debug mode
    std::cout << "ModbusArmController: Setting debug mode..." << std::endl;
    modbus_set_debug(modbus_, TRUE);

    // Set slave ID (根据协议文档，站号固定为01)
    std::cout << "ModbusArmController: Setting slave ID to 1..." << std::endl;
    modbus_set_slave(modbus_, 1);

    // Set response timeout
    std::cout << "ModbusArmController: Setting response timeout to 5 seconds..." << std::endl;
    modbus_set_response_timeout(modbus_, 5, 0); // 增加超时时间到5秒

    // Connect to modbus server
    std::cout << "ModbusArmController: Attempting to connect to modbus server..." << std::endl;
    if (modbus_connect(modbus_) == -1) {
        std::cerr << "ModbusArmController: Failed to connect to modbus server " << ip << ":" << port << ": " << modbus_strerror(errno) << std::endl;
        modbus_free(modbus_);
        modbus_ = nullptr;
        return false;
    }

    std::cout << "ModbusArmController: Connection successful!" << std::endl;
    connected_ = true;
    current_status_.connected = true;
    current_status_.status_message = "Connected to " + ip + ":" + std::to_string(port);

    // Test reading a single register to verify connection
    std::cout << "ModbusArmController: Testing connection by reading a register..." << std::endl;
    int16_t test_value;
    if (readRegister(7802, test_value)) {
        std::cout << "ModbusArmController: Test read successful, value: " << test_value << std::endl;
    } else {
        std::cerr << "ModbusArmController: Test read failed" << std::endl;
    }

    // Read initial positions
    std::cout << "ModbusArmController: Reading initial positions..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        double pos = readPosition(i);
        std::cout << "ModbusArmController: Axis " << i << " initial position: " << pos << std::endl;
        current_status_.current_positions[i] = static_cast<int32_t>(pos);
    }

    std::cout << "ModbusArmController: Connect method completed successfully" << std::endl;
    return true;
}

void ModbusArmController::disconnect() {
    if (auto_read_running_) {
        stopAutoRead();
    }

    if (modbus_) {
        modbus_close(modbus_);
    }

    connected_ = false;
    current_status_.connected = false;
    current_status_.status_message = "Disconnected";

    if (status_callback_) {
        status_callback_(current_status_);
    }
}

double ModbusArmController::readPosition(int axis_id) {
    if (!isConnected() || axis_id < 0 || axis_id >= 5) {
        return 0.0;
    }

    std::lock_guard<std::mutex> lock(modbus_mutex_);

    try {
        const AxisConfig& config = axis_configs_[axis_id];
        std::vector<int16_t> registers(2);

        // Read the configured address (2 registers as in reference code)
        bool read_success = readRegisters(config.current_pos_reg, 2, registers);
        
        if (!read_success) {
            return 0.0;
        }

        // Combine registers into 32-bit value (low-high byte order as in reference code)
        uint16_t reg0 = static_cast<uint16_t>(registers[0]);
        uint16_t reg1 = static_cast<uint16_t>(registers[1]);
        uint32_t combined = (static_cast<uint32_t>(reg1) << 16) | reg0;
        
        // Convert to signed 32-bit integer (as in reference code)
        int32_t raw_position = 0;
        if (combined & 0x80000000) {
            raw_position = static_cast<int32_t>(combined - 0x100000000);
        } else {
            raw_position = static_cast<int32_t>(combined);
        }
        
        // No scaling factor, use raw position directly
        double final_position = static_cast<double>(raw_position);

        current_status_.current_positions[axis_id] = static_cast<int32_t>(final_position);

        return final_position;
    } catch (const std::exception& e) {
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
        std::cerr << "Error setting speed: " << e.what() << std::endl;
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

        std::cout << "ModbusArmController: Reading speed for axis " << axis_id << " (" << config.name << ")" << std::endl;
        std::cout << "ModbusArmController: Speed register: " << config.speed_reg << std::endl;

        if (!readRegisters(config.speed_reg, 2, registers)) {
            std::cerr << "ModbusArmController: Failed to read speed registers" << std::endl;
            return -1;
        }

        std::cout << "ModbusArmController: Read speed registers - register[0]: " << registers[0] << ", register[1]: " << registers[1] << std::endl;

        // Try both byte orders to find the correct one
        int32_t speed1 = convertTo32Bit(registers[0], registers[1]); // high, low
        int32_t speed2 = convertTo32Bit(registers[1], registers[0]); // low, high
        
        std::cout << "ModbusArmController: Speed (high-low): " << speed1 << ", Speed (low-high): " << speed2 << std::endl;
        
        // Choose the more reasonable value (assuming speed shouldn't be too large)
        int32_t speed = (abs(speed1) < abs(speed2)) ? speed1 : speed2;
        
        std::cout << "ModbusArmController: Selected speed: " << speed << std::endl;

        return speed;
    } catch (const std::exception& e) {
        std::cerr << "Error reading speed: " << e.what() << std::endl;
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

        std::cout << "ModbusArmController: Starting continuous movement for axis " << axis_id << " (" << config.name << ") in direction " << (direction ? "forward" : "backward") << std::endl;
        std::cout << "ModbusArmController: Relay addresses - forward: " << config.forward_relay << ", backward: " << config.backward_relay << std::endl;
        std::cout << "ModbusArmController: Using relay: " << relay_address << ", opposite relay: " << opposite_relay << std::endl;

        // Close absolute positioning relay for this axis only
        writeCoil(config.pos_move_abs_relay, false);
        // Close opposite direction relay for this axis only
        writeCoil(opposite_relay, false);
        // Wait a bit to ensure status is updated
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // Start movement for this axis only
        if (!writeCoil(relay_address, true)) {
            std::cerr << "ModbusArmController: Failed to start continuous movement" << std::endl;
            return false;
        }

        std::cout << "ModbusArmController: Continuous movement started successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error starting continuous movement: " << e.what() << std::endl;
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
        std::cerr << "Error stopping continuous movement: " << e.what() << std::endl;
        return false;
    }
}

bool ModbusArmController::moveToPosition(int axis_id, double target_position) {
    if (!isConnected() || axis_id < 0 || axis_id >= 5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(modbus_mutex_);

    try {
        const AxisConfig& config = axis_configs_[axis_id];

        // No scaling factor, use target position directly
        int32_t target_pos_int = static_cast<int32_t>(target_position);
        std::cout << "ModbusArmController: Moving to position - axis: " << axis_id << ", target: " << target_position << " (int: " << target_pos_int << ")" << std::endl;

        // Stop all movements first
        writeCoil(config.forward_relay, false);
        writeCoil(config.backward_relay, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Write target position
        int16_t low, high;
        convertTo16Bit(target_pos_int, low, high);
        std::vector<int16_t> values = {low, high};
        if (!writeRegisters(config.target_pos_reg, values)) {
            return false;
        }

        // Check if absolute positioning relay is on
        bool relay_status;
        if (readCoil(config.pos_move_abs_relay, relay_status) && relay_status) {
            writeCoil(config.pos_move_abs_relay, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Trigger absolute positioning
        if (!writeCoil(config.pos_move_abs_relay, true)) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Update target position in status
        current_status_.target_positions[axis_id] = static_cast<int32_t>(target_position);
        if (status_callback_) {
            status_callback_(current_status_);
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error moving to position: " << e.what() << std::endl;
        return false;
    }
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
        std::cerr << "Error stopping all movements: " << e.what() << std::endl;
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
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(status_mutex_));
    return current_status_;
}

void ModbusArmController::setStatusCallback(std::function<void(const ArmStatus&)> callback) {
    status_callback_ = callback;
}

void ModbusArmController::autoReadLoop() {
    while (auto_read_running_ && isConnected()) {
        {
            std::lock_guard<std::mutex> lock(modbus_mutex_);
            for (int axis_id = 0; axis_id < 5; ++axis_id) {
                if (auto_read_enabled_[axis_id]) {
                    readPosition(axis_id);
                }
            }
        }

        // Notify status change once per cycle
        if (status_callback_) {
            status_callback_(current_status_);
        }

        // Sleep for interval
        std::this_thread::sleep_for(std::chrono::duration<float>(auto_read_interval_));
    }
}

bool ModbusArmController::writeCoil(int address, bool value) {
    if (!isConnected()) {
        return false;
    }

    int ret = modbus_write_bit(modbus_, address, value ? 1 : 0);
    if (ret != 1) {
        std::cerr << "Failed to write coil " << address << ": " << modbus_strerror(errno) << std::endl;
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
        std::cerr << "Failed to read coil " << address << ": " << modbus_strerror(errno) << std::endl;
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
        std::cerr << "Failed to write register " << address << ": " << modbus_strerror(errno) << std::endl;
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
        std::cerr << "Failed to write registers starting at " << address << ": " << modbus_strerror(errno) << std::endl;
        return false;
    }

    return true;
}

bool ModbusArmController::readRegister(int address, int16_t& value) {
    if (!isConnected()) {
        return false;
    }

    uint16_t reg_value;
    int ret = modbus_read_registers(modbus_, address, 1, &reg_value);
    if (ret != 1) {
        std::cerr << "Failed to read register " << address << ": " << modbus_strerror(errno) << std::endl;
        return false;
    }

    value = static_cast<int16_t>(reg_value);
    return true;
}

bool ModbusArmController::readRegisters(int address, int count, std::vector<int16_t>& values) {
    if (!isConnected()) {
        return false;
    }

    values.resize(count);
    int ret = modbus_read_registers(modbus_, address, count, reinterpret_cast<uint16_t*>(values.data()));
    
    if (ret != count) {
        return false;
    }

    return true;
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

} // namespace arm
