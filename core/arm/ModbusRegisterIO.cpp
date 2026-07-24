#include "ModbusRegisterIO.h"
#include "ModbusConnection.h"

#include <modbus.h>
#include <spdlog/spdlog.h>

namespace arm {

ModbusRegisterIO::ModbusRegisterIO(ModbusConnection& conn)
    : conn_(conn) {}

bool ModbusRegisterIO::writeCoil(int address, bool value) {
    if (!conn_.isConnected()) {
        return false;
    }

    int ret = modbus_write_bit(conn_.modbusContext(), address, value ? 1 : 0);
    if (ret != 1) {
        SPDLOG_ERROR("[Arm] Failed to write coil {}: {}", address, modbus_strerror(errno));
        return false;
    }

    return true;
}

bool ModbusRegisterIO::readCoil(int address, bool& value) {
    if (!conn_.isConnected()) {
        return false;
    }

    uint8_t bit;
    int ret = modbus_read_bits(conn_.modbusContext(), address, 1, &bit);
    if (ret != 1) {
        SPDLOG_ERROR("[Arm] Failed to read coil {}: {}", address, modbus_strerror(errno));
        return false;
    }

    value = (bit == 1);
    return true;
}

bool ModbusRegisterIO::writeRegister(int address, int16_t value) {
    if (!conn_.isConnected()) {
        return false;
    }

    int ret = modbus_write_register(conn_.modbusContext(), address, value);
    if (ret != 1) {
        SPDLOG_ERROR("[Arm] Failed to write register {}: {}", address, modbus_strerror(errno));
        return false;
    }

    return true;
}

bool ModbusRegisterIO::writeRegisters(int address, const std::vector<int16_t>& values) {
    if (!conn_.isConnected()) {
        return false;
    }

    std::vector<uint16_t> uint_values(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        uint_values[i] = static_cast<uint16_t>(values[i]);
    }
    int ret = modbus_write_registers(conn_.modbusContext(), address, uint_values.size(), uint_values.data());
    if (ret != static_cast<int>(values.size())) {
        SPDLOG_ERROR("[Arm] Failed to write registers starting at {}: {}", address, modbus_strerror(errno));
        return false;
    }

    return true;
}

bool ModbusRegisterIO::readRegister(int address, int16_t& value) {
    if (!conn_.isConnected()) {
        conn_.lastReadFailed() = true;
        return false;
    }

    uint16_t reg_value;
    int ret = modbus_read_registers(conn_.modbusContext(), address, 1, &reg_value);
    if (ret != 1) {
        SPDLOG_ERROR("[Arm] Failed to read register {}: {}", address, modbus_strerror(errno));
        conn_.lastReadFailed() = true;
        return false;
    }

    value = static_cast<int16_t>(reg_value);
    return true;
}

bool ModbusRegisterIO::readRegisters(int address, int count, std::vector<int16_t>& values) {
    if (!conn_.isConnected()) {
        conn_.lastReadFailed() = true;
        return false;
    }

    std::vector<uint16_t> temp_values(count);
    int ret = modbus_read_registers(conn_.modbusContext(), address, count, temp_values.data());

    if (ret != count) {
        SPDLOG_ERROR("[Arm] Failed to read {} registers at address {}: {} (got {})",
                     count, address, modbus_strerror(errno), ret);
        conn_.lastReadFailed() = true;
        return false;
    }

    values.resize(count);
    for (int i = 0; i < count; ++i) {
        values[i] = static_cast<int16_t>(temp_values[i]);
    }

    return true;
}

} // namespace arm
