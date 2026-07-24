#include "ModbusConnection.h"

#include <modbus.h>
#include <spdlog/spdlog.h>

namespace arm {

ModbusConnection::ModbusConnection() {
    modbus_ = nullptr;
}

ModbusConnection::~ModbusConnection() {
    if (modbus_) {
        modbus_close(modbus_);
        modbus_free(modbus_);
        modbus_ = nullptr;
    }
}

bool ModbusConnection::connect(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(modbus_mutex_);
    SPDLOG_INFO("[Arm] Connecting to {}:{}", ip, port);

    if (isConnected()) {
        SPDLOG_INFO("[Arm] Already connected, disconnecting first...");
        if (modbus_) {
            modbus_close(modbus_);
            modbus_free(modbus_);
            modbus_ = nullptr;
        }
        connected_ = false;
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
            std::lock_guard<std::mutex> lock(error_mutex_);
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
            std::lock_guard<std::mutex> lock(error_mutex_);
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
            std::lock_guard<std::mutex> lock(error_mutex_);
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
            std::lock_guard<std::mutex> lock(error_mutex_);
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
    stored_ip_ = ip;
    stored_port_ = port;
    consecutive_failures_ = 0;

    return true;
}

void ModbusConnection::disconnect() {
    std::lock_guard<std::mutex> lock(modbus_mutex_);
    if (modbus_) {
        modbus_close(modbus_);
        modbus_free(modbus_);
        modbus_ = nullptr;
    }
    connected_ = false;
}

bool ModbusConnection::isConnected() const {
    return connected_ && modbus_;
}

bool ModbusConnection::reconnect() {
    std::lock_guard<std::mutex> lock(modbus_mutex_);

    if (modbus_) {
        modbus_free(modbus_);
        modbus_ = nullptr;
    }

    modbus_ = modbus_new_tcp(stored_ip_.c_str(), stored_port_);
    if (!modbus_) return false;

    modbus_set_slave(modbus_, 1);
    modbus_set_response_timeout(modbus_, 2, 0);

    if (modbus_connect(modbus_) == -1) {
        modbus_free(modbus_);
        modbus_ = nullptr;
        return false;
    }

    connected_ = true;
    SPDLOG_INFO("[Arm] Reconnected successfully");
    return true;
}

std::string ModbusConnection::getError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void ModbusConnection::setDebugEnabled(bool enabled) {
    debug_enabled_ = enabled;
}

} // namespace arm
