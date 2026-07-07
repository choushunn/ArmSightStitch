#include "ConfigManager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <cstdlib>
#include <spdlog/spdlog.h>

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

ConfigManager::ConfigManager() {
    loadDefaults();
    applyEnvironmentOverrides();
}

void ConfigManager::loadDefaults() {
    arm_ip_ = "192.168.0.1";
    arm_port_ = 502;
    default_speed_ = 35000;

    camera_width_ = 640;
    camera_height_ = 480;
    camera_exposure_ = 100.0f;
    camera_gain_ = 1.0f;

    model_param_path_ = "models/best-sim-opt.ncnn.param";
    model_bin_path_ = "models/best-sim-opt.ncnn.bin";

    grid_size_x_ = 10;
    grid_size_y_ = 10;
    step_size_ = 43000;
    z_height_ = 50000;

{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    image_save_base_path_ = (docs + "/ArmSightStitch/captured_images").toStdString();
    log_path_ = (docs + "/ArmSightStitch/logs/armsightstitch.log").toStdString();
}
}

void ConfigManager::applyEnvironmentOverrides() {
    auto env_override = [](const char* var, std::string& target) {
        const char* val = std::getenv(var);
        if (val && val[0] != '\0') {
            target = val;
            SPDLOG_INFO("Config overridden by env {} = {}", var, target);
        }
    };

    auto env_override_int = [](const char* var, int& target) {
        const char* val = std::getenv(var);
        if (val && val[0] != '\0') {
            target = std::atoi(val);
            SPDLOG_INFO("Config overridden by env {} = {}", var, target);
        }
    };

    env_override("ARM_SIGHT_STITCH_ARM_IP", arm_ip_);
    env_override_int("ARM_SIGHT_STITCH_ARM_PORT", arm_port_);
    env_override_int("ARM_SIGHT_STITCH_DEFAULT_SPEED", default_speed_);
    env_override("ARM_SIGHT_STITCH_MODEL_PARAM", model_param_path_);
    env_override("ARM_SIGHT_STITCH_MODEL_BIN", model_bin_path_);
    env_override("ARM_SIGHT_STITCH_SAVE_PATH", image_save_base_path_);
    env_override_int("ARM_SIGHT_STITCH_GRID_X", grid_size_x_);
    env_override_int("ARM_SIGHT_STITCH_GRID_Y", grid_size_y_);
    env_override_int("ARM_SIGHT_STITCH_STEP_SIZE", step_size_);
    env_override_int("ARM_SIGHT_STITCH_Z_HEIGHT", z_height_);
    env_override_int("ARM_SIGHT_STITCH_CENTER_CROP_SIZE", center_crop_size_);
}

bool ConfigManager::loadFromFile(const std::string& filepath) {
    QFile file(QString::fromStdString(filepath));
    if (!file.open(QIODevice::ReadOnly)) {
        last_error_ = "Cannot open config file: " + filepath;
        SPDLOG_ERROR(last_error_);
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        last_error_ = std::string("JSON parse error: ") + parseError.errorString().toStdString();
        SPDLOG_ERROR(last_error_);
        return false;
    }

    if (!doc.isObject()) {
        last_error_ = "Config file must contain a JSON object";
        SPDLOG_ERROR(last_error_);
        return false;
    }

    return loadFromJson(doc.object());
}

bool ConfigManager::loadFromJson(const QJsonObject& json) {
    auto readStr = [&](const QString& key, std::string& target) {
        if (json.contains(key) && json[key].isString()) {
            target = json[key].toString().toStdString();
        }
    };

    auto readInt = [&](const QString& key, int& target) {
        if (json.contains(key)) {
            if (json[key].isDouble()) {
                target = json[key].toInt();
            }
        }
    };

    auto readFloat = [&](const QString& key, float& target) {
        if (json.contains(key)) {
            if (json[key].isDouble()) {
                target = static_cast<float>(json[key].toDouble());
            }
        }
    };

    readStr("arm_ip", arm_ip_);
    readInt("arm_port", arm_port_);
    readInt("default_speed", default_speed_);

    readInt("camera_width", camera_width_);
    readInt("camera_height", camera_height_);
    readFloat("camera_exposure", camera_exposure_);
    readFloat("camera_gain", camera_gain_);

    readStr("model_param_path", model_param_path_);
    readStr("model_bin_path", model_bin_path_);

    readInt("grid_size_x", grid_size_x_);
    readInt("grid_size_y", grid_size_y_);
    readInt("step_size", step_size_);
    readInt("z_height", z_height_);
    readInt("center_crop_size", center_crop_size_);

    readInt("sphere_radius", sphere_radius_);
    readInt("sphere_cap_height", sphere_cap_height_);
    readInt("sphere_height_offset", sphere_height_offset_);
    readInt("z_base_height", z_base_height_);

    readStr("image_save_base_path", image_save_base_path_);
    readStr("log_path", log_path_);

    auto readIntArray = [&](const QString& key, int* target, int count) {
        if (json.contains(key) && json[key].isArray()) {
            QJsonArray arr = json[key].toArray();
            for (int i = 0; i < qMin(count, arr.size()); ++i) {
                if (arr[i].isDouble()) target[i] = arr[i].toInt();
            }
        }
    };
    readIntArray("axis_min_pos", axis_min_pos_, 5);
    readIntArray("axis_max_pos", axis_max_pos_, 5);

    readFloat("position_tolerance", position_tolerance_);

    if (json.contains("modbus_debug") && json["modbus_debug"].isBool()) {
        modbus_debug_ = json["modbus_debug"].toBool();
    }

    if (!validateConfig()) {
        return false;
    }

    SPDLOG_INFO("Configuration loaded successfully");
    return true;
}

bool ConfigManager::saveToFile(const std::string& filepath) const {
    QJsonObject json = toJson();
    QJsonDocument doc(json);

    QDir dir = QFileInfo(QString::fromStdString(filepath)).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QFile file(QString::fromStdString(filepath));
    if (!file.open(QIODevice::WriteOnly)) {
        last_error_ = "Cannot write config file: " + filepath;
        SPDLOG_ERROR(last_error_);
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    SPDLOG_INFO("Configuration saved to {}", filepath);
    return true;
}

QJsonObject ConfigManager::toJson() const {
    QJsonObject json;
    json["arm_ip"] = QString::fromStdString(arm_ip_);
    json["arm_port"] = arm_port_;
    json["default_speed"] = default_speed_;
    json["camera_width"] = camera_width_;
    json["camera_height"] = camera_height_;
    json["camera_exposure"] = static_cast<double>(camera_exposure_);
    json["camera_gain"] = static_cast<double>(camera_gain_);
    json["model_param_path"] = QString::fromStdString(model_param_path_);
    json["model_bin_path"] = QString::fromStdString(model_bin_path_);
    json["grid_size_x"] = grid_size_x_;
    json["grid_size_y"] = grid_size_y_;
    json["step_size"] = step_size_;
    json["z_height"] = z_height_;
    json["center_crop_size"] = center_crop_size_;
    json["sphere_radius"] = sphere_radius_;
    json["sphere_cap_height"] = sphere_cap_height_;
    json["sphere_height_offset"] = sphere_height_offset_;
    json["z_base_height"] = z_base_height_;
    json["image_save_base_path"] = QString::fromStdString(image_save_base_path_);
    json["log_path"] = QString::fromStdString(log_path_);

    QJsonArray minArr, maxArr;
    for (int i = 0; i < 5; ++i) {
        minArr.append(axis_min_pos_[i]);
        maxArr.append(axis_max_pos_[i]);
    }
    json["axis_min_pos"] = minArr;
    json["axis_max_pos"] = maxArr;
    json["position_tolerance"] = position_tolerance_;
    json["modbus_debug"] = modbus_debug_;

    return json;
}

bool ConfigManager::validateConfig() const {
    if (!isValidPort(arm_port_)) {
        last_error_ = "Invalid arm port: " + std::to_string(arm_port_);
        SPDLOG_ERROR(last_error_);
        return false;
    }
    if (default_speed_ <= 0 || default_speed_ > 200000) {
        last_error_ = "Invalid default speed: " + std::to_string(default_speed_);
        SPDLOG_ERROR(last_error_);
        return false;
    }
    if (camera_width_ <= 0 || camera_height_ <= 0) {
        last_error_ = "Invalid camera resolution";
        SPDLOG_ERROR(last_error_);
        return false;
    }
    if (grid_size_x_ <= 0 || grid_size_y_ <= 0) {
        last_error_ = "Grid size must be positive";
        SPDLOG_ERROR(last_error_);
        return false;
    }
    if (step_size_ <= 0) {
        last_error_ = "Step size must be positive";
        SPDLOG_ERROR(last_error_);
        return false;
    }
    return true;
}

bool ConfigManager::isValidPort(int port) {
    return port > 0 && port <= 65535;
}

bool ConfigManager::isValidFilePath(const std::string& path) {
    return !path.empty();
}

// Setters with validation

void ConfigManager::setArmIp(const std::string& ip) {
    if (ip.empty()) {
        SPDLOG_WARN("Attempted to set empty arm IP, ignored");
        return;
    }
    arm_ip_ = ip;
    SPDLOG_INFO("Arm IP set to {}", ip);
}

void ConfigManager::setArmPort(int port) {
    if (isValidPort(port)) {
        arm_port_ = port;
        SPDLOG_INFO("Arm port set to {}", port);
    } else {
        SPDLOG_WARN("Invalid arm port: {}", port);
    }
}

void ConfigManager::setDefaultSpeed(int speed) {
    if (speed > 0 && speed <= 200000) {
        default_speed_ = speed;
    } else {
        SPDLOG_WARN("Invalid speed value: {}", speed);
    }
}

void ConfigManager::setCameraWidth(int w) {
    if (w > 0) camera_width_ = w;
}

void ConfigManager::setCameraHeight(int h) {
    if (h > 0) camera_height_ = h;
}

void ConfigManager::setCameraExposure(float exp) {
    if (exp >= 0) camera_exposure_ = exp;
}

void ConfigManager::setCameraGain(float gain) {
    if (gain >= 0) camera_gain_ = gain;
}

void ConfigManager::setModelParamPath(const std::string& path) {
    if (isValidFilePath(path)) {
        model_param_path_ = path;
    }
}

void ConfigManager::setModelBinPath(const std::string& path) {
    if (isValidFilePath(path)) {
        model_bin_path_ = path;
    }
}

void ConfigManager::setGridSizeX(int x) {
    if (x > 0) grid_size_x_ = x;
}

void ConfigManager::setGridSizeY(int y) {
    if (y > 0) grid_size_y_ = y;
}

void ConfigManager::setStepSize(int s) {
    if (s > 0) step_size_ = s;
}

void ConfigManager::setZHeight(int z) {
    z_height_ = z;
}

void ConfigManager::setCenterCropSize(int size) {
    if (size >= 0) center_crop_size_ = size;
}

void ConfigManager::setSphereRadius(int r) {
    if (r > 0) sphere_radius_ = r;
}

void ConfigManager::setSphereCapHeight(int h) {
    if (h >= 0) sphere_cap_height_ = h;
}

void ConfigManager::setSphereHeightOffset(int d) {
    sphere_height_offset_ = d;
}

void ConfigManager::setZBaseHeight(int z) {
    z_base_height_ = z;
}

void ConfigManager::setImageSaveBasePath(const std::string& path) {
    if (isValidFilePath(path)) {
        image_save_base_path_ = path;
    }
}

void ConfigManager::setLogPath(const std::string& path) {
    if (isValidFilePath(path)) {
        log_path_ = path;
    }
}

int ConfigManager::axisMinPos(int axis) const {
    if (axis >= 0 && axis < 5) return axis_min_pos_[axis];
    return -1000000;
}

int ConfigManager::axisMaxPos(int axis) const {
    if (axis >= 0 && axis < 5) return axis_max_pos_[axis];
    return 1000000;
}

void ConfigManager::setAxisMinPos(int axis, int value) {
    if (axis >= 0 && axis < 5) axis_min_pos_[axis] = value;
}

void ConfigManager::setAxisMaxPos(int axis, int value) {
    if (axis >= 0 && axis < 5) axis_max_pos_[axis] = value;
}

void ConfigManager::setPositionTolerance(float t) {
    if (t > 0) position_tolerance_ = t;
}

void ConfigManager::setModbusDebug(bool enabled) {
    modbus_debug_ = enabled;
}
