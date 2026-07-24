#pragma once

#include <cstdint>
#include <vector>

namespace arm {

class ModbusConnection;

class ModbusRegisterIO {
public:
    explicit ModbusRegisterIO(ModbusConnection& conn);

    /// Write a single coil.
    bool writeCoil(int address, bool value);

    /// Read a single coil.
    bool readCoil(int address, bool& value);

    /// Write a single holding register.
    bool writeRegister(int address, int16_t value);

    /// Write multiple consecutive holding registers.
    bool writeRegisters(int address, const std::vector<int16_t>& values);

    /// Read a single holding register.
    bool readRegister(int address, int16_t& value);

    /// Read multiple consecutive holding registers.
    bool readRegisters(int address, int count, std::vector<int16_t>& values);

private:
    ModbusConnection& conn_;
};

} // namespace arm
