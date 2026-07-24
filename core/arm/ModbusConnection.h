#pragma once

#include <modbus.h>
#include <mutex>
#include <atomic>
#include <string>

namespace arm {

class ModbusConnection {
public:
    ModbusConnection();
    ~ModbusConnection();

    /// Establish TCP connection to the Modbus server.
    /// @return true on success, false on failure (call getError() for details).
    bool connect(const std::string& ip, int port);

    /// Close and free the modbus context.
    void disconnect();

    /// @return true if modbus context exists and is marked connected.
    bool isConnected() const;

    /// Single reconnect attempt using the stored IP/port.
    /// Caller is responsible for retry loop and backoff policy.
    /// @return true if reconnected successfully.
    bool reconnect();

    /// @return last error message (thread-safe).
    std::string getError() const;

    /// Set debug flag; takes effect on the next connect() / reconnect().
    void setDebugEnabled(bool enabled);

    // ---- Internal accessors for sister classes in the same namespace ----
    modbus_t* modbusContext() const { return modbus_; }
    std::mutex& mutex() { return modbus_mutex_; }

    std::atomic<int>& consecutiveFailures() { return consecutive_failures_; }
    std::atomic<bool>& lastReadFailed() { return last_read_failed_; }
    const std::string& storedIp() const { return stored_ip_; }
    int storedPort() const { return stored_port_; }

    static constexpr int kMaxConsecutiveFailures = 5;
    static constexpr int kMaxReconnectAttempts = 10;

private:
    modbus_t* modbus_ = nullptr;
    std::mutex modbus_mutex_;
    std::atomic<bool> connected_ = false;
    std::string stored_ip_;
    int stored_port_ = 502;
    std::atomic<bool> debug_enabled_ = false;
    std::atomic<int> consecutive_failures_ = 0;
    mutable std::string last_error_;
    mutable std::mutex error_mutex_;
    std::atomic<bool> last_read_failed_ = false;
};

} // namespace arm
