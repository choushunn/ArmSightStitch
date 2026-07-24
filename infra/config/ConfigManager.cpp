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
    system_.armIp = "192.168.0.1";
    system_.armPort = 502;
    system_.defaultSpeed = 30000;

    camera_.width = 640;
    camera_.height = 480;
    camera_.exposure = 70.0f;
    camera_.gain = 1.0f;
    camera_.sharpening = 0;

    detection_.modelParamPath = "models/best-sim-opt.ncnn.param";
    detection_.modelBinPath = "models/best-sim-opt.ncnn.bin";

    scan_.gridSizeX = 10;
    scan_.gridSizeY = 10;
    scan_.stepSize = 43000;
    scan_.zHeight = 70000;
    zaxis_.mode = 0;
    {
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        zaxis_.zMapFile = (docs + "/ScannerData/z_map.json").toStdString();
        zaxis_.zRadialFile = (docs + "/ScannerData/z_radial.json").toStdString();
    }
    scan_.dwellTimeMs = 300;
    stitch_.centerCropSize = 1775;
    stitch_.algorithm = 3;
    stitch_.scaleMode = 0;
    {
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        stitch_.scaleMapFile = (docs + "/ScannerData/scale_map.json").toStdString();
        stitch_.cropOffsetFile = (docs + "/ScannerData/crop_offset.json").toStdString();
    }
    stitch_.featherWidth = 120;

    detection_.algorithm = 2;
    detection_.dustClaheClip = 2.0;
    detection_.dustBgBlur = 31;
    detection_.dustMinArea = 50;
    detection_.dustMaxArea = 20000;
    detection_.dustDilateIter = 0;
    detection_.dustMaxIter = 4;
    detection_.dustNmsIou = 0.1;

    detection_.edgeClaheClip = 2.0;
    detection_.edgeClaheTileGrid = 8;
    detection_.edgeThreshold = 30;
    detection_.edgeSobelKSize = 3;
    detection_.edgeDilateIter = 1;
    detection_.edgeMinBboxArea = 25;
    detection_.edgeNmsIouThresh = 0.4;
    detection_.edgeNmsContainThresh = 0.5;

    {
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        system_.imageSaveBasePath = (docs + "/ScannerData/captured_images").toStdString();
        system_.logPath = (docs + "/ScannerData/logs/scanner.log").toStdString();
    }
}

void ConfigManager::applyEnvironmentOverrides() {
    auto env_override = [](const char* var, std::string& target) {
        const char* val = std::getenv(var);
        if (val && val[0] != '\0') {
            target = val;
            SPDLOG_INFO("[Config] Config overridden by env {} = {}", var, target);
        }
    };

    auto env_override_int = [](const char* var, int& target) {
        const char* val = std::getenv(var);
        if (val && val[0] != '\0') {
            target = std::atoi(val);
            SPDLOG_INFO("[Config] Config overridden by env {} = {}", var, target);
        }
    };

    env_override("ARM_SIGHT_STITCH_ARM_IP", system_.armIp);
    env_override_int("ARM_SIGHT_STITCH_ARM_PORT", system_.armPort);
    env_override_int("ARM_SIGHT_STITCH_DEFAULT_SPEED", system_.defaultSpeed);
    env_override("ARM_SIGHT_STITCH_MODEL_PARAM", detection_.modelParamPath);
    env_override("ARM_SIGHT_STITCH_MODEL_BIN", detection_.modelBinPath);
    env_override("ARM_SIGHT_STITCH_SAVE_PATH", system_.imageSaveBasePath);
    env_override_int("ARM_SIGHT_STITCH_GRID_X", scan_.gridSizeX);
    env_override_int("ARM_SIGHT_STITCH_GRID_Y", scan_.gridSizeY);
    env_override_int("ARM_SIGHT_STITCH_STEP_SIZE", scan_.stepSize);
    env_override_int("ARM_SIGHT_STITCH_Z_HEIGHT", scan_.zHeight);
    env_override_int("ARM_SIGHT_STITCH_DWELL_TIME_MS", scan_.dwellTimeMs);
    env_override_int("ARM_SIGHT_STITCH_CENTER_CROP_SIZE", stitch_.centerCropSize);
    env_override_int("ARM_SIGHT_STITCH_ALGORITHM", stitch_.algorithm);
    env_override_int("ARM_SIGHT_STITCH_FEATHER_WIDTH", stitch_.featherWidth);
    {
        const char* val = std::getenv("ARM_SIGHT_STITCH_Z_CORRECTION_COEF");
        if (val && val[0] != '\0') {
            stitch_.zCorrectionCoef = std::atof(val);
            SPDLOG_INFO("[Config] Config overridden by env ARM_SIGHT_STITCH_Z_CORRECTION_COEF = {}",
                        stitch_.zCorrectionCoef);
        }
    }
    env_override("ARM_SIGHT_STITCH_CROP_OFFSET_FILE", stitch_.cropOffsetFile);
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

    auto readDouble = [&](const QString& key, double& target) {
        if (json.contains(key) && json[key].isDouble()) {
            target = json[key].toDouble();
        }
    };

    readStr("arm_ip", system_.armIp);
    readInt("arm_port", system_.armPort);
    readInt("default_speed", system_.defaultSpeed);

    readInt("camera_width", camera_.width);
    readInt("camera_height", camera_.height);
    readFloat("camera_exposure", camera_.exposure);
    readFloat("camera_gain", camera_.gain);
    readInt("camera_sharpening", camera_.sharpening);

    readStr("model_param_path", detection_.modelParamPath);
    readStr("model_bin_path", detection_.modelBinPath);

    readInt("grid_size_x", scan_.gridSizeX);
    readInt("grid_size_y", scan_.gridSizeY);
    readInt("step_size", scan_.stepSize);
    readInt("z_height", scan_.zHeight);
    readInt("dwell_time_ms", scan_.dwellTimeMs);
    readInt("center_crop_size", stitch_.centerCropSize);
    readInt("stitch_algorithm", stitch_.algorithm);
    readInt("feather_width", stitch_.featherWidth);
    readInt("scale_mode", stitch_.scaleMode);
    readStr("scale_map_file", stitch_.scaleMapFile);
    readDouble("z_correction_coef", stitch_.zCorrectionCoef);
    readStr("crop_offset_file", stitch_.cropOffsetFile);

    readInt("detection_algorithm", detection_.algorithm);
    readDouble("dust_clahe_clip", detection_.dustClaheClip);
    readInt("dust_bg_blur", detection_.dustBgBlur);
    readInt("dust_min_area", detection_.dustMinArea);
    readInt("dust_max_area", detection_.dustMaxArea);
    readInt("dust_dilate_iter", detection_.dustDilateIter);
    readInt("dust_max_iter", detection_.dustMaxIter);
    readDouble("dust_nms_iou", detection_.dustNmsIou);

    readDouble("edge_clahe_clip", detection_.edgeClaheClip);
    readInt("edge_clahe_tile_grid", detection_.edgeClaheTileGrid);
    readInt("edge_threshold", detection_.edgeThreshold);
    readInt("edge_sobel_ksize", detection_.edgeSobelKSize);
    readInt("edge_dilate_iter", detection_.edgeDilateIter);
    readInt("edge_min_bbox_area", detection_.edgeMinBboxArea);
    readDouble("edge_nms_iou_thresh", detection_.edgeNmsIouThresh);
    readDouble("edge_nms_contain_thresh", detection_.edgeNmsContainThresh);

    readInt("sphere_radius", zaxis_.sphereRadius);
    readInt("sphere_cap_height", zaxis_.sphereCapHeight);
    readInt("sphere_height_offset", zaxis_.sphereHeightOffset);
    readInt("z_base_height", zaxis_.zBaseHeight);
    readInt("z_mode", zaxis_.mode);
    readStr("z_map_file", zaxis_.zMapFile);
    readStr("z_radial_file", zaxis_.zRadialFile);

    readStr("image_save_base_path", system_.imageSaveBasePath);
    readStr("log_path", system_.logPath);

    auto readIntArray = [&](const QString& key, int* target, int count) {
        if (json.contains(key) && json[key].isArray()) {
            QJsonArray arr = json[key].toArray();
            for (int i = 0; i < qMin(count, arr.size()); ++i) {
                if (arr[i].isDouble()) target[i] = arr[i].toInt();
            }
        }
    };
    readIntArray("axis_min_pos", system_.axisMinPos, 5);
    readIntArray("axis_max_pos", system_.axisMaxPos, 5);

    readFloat("position_tolerance", system_.positionTolerance);

    if (json.contains("modbus_debug") && json["modbus_debug"].isBool()) {
        system_.modbusDebug = json["modbus_debug"].toBool();
    }

    if (!validateConfig()) {
        return false;
    }

    SPDLOG_INFO("[Config] Configuration loaded successfully");
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

    SPDLOG_INFO("[Config] Configuration saved to {}", filepath);
    return true;
}

QJsonObject ConfigManager::toJson() const {
    QJsonObject json;
    json["arm_ip"] = QString::fromStdString(system_.armIp);
    json["arm_port"] = system_.armPort;
    json["default_speed"] = system_.defaultSpeed;
    json["camera_width"] = camera_.width;
    json["camera_height"] = camera_.height;
    json["camera_exposure"] = static_cast<double>(camera_.exposure);
    json["camera_gain"] = static_cast<double>(camera_.gain);
    json["camera_sharpening"] = camera_.sharpening;
    json["model_param_path"] = QString::fromStdString(detection_.modelParamPath);
    json["model_bin_path"] = QString::fromStdString(detection_.modelBinPath);
    json["grid_size_x"] = scan_.gridSizeX;
    json["grid_size_y"] = scan_.gridSizeY;
    json["step_size"] = scan_.stepSize;
    json["z_height"] = scan_.zHeight;
    json["dwell_time_ms"] = scan_.dwellTimeMs;
    json["center_crop_size"] = stitch_.centerCropSize;
    json["stitch_algorithm"] = stitch_.algorithm;
    json["feather_width"] = stitch_.featherWidth;
    json["scale_mode"] = stitch_.scaleMode;
    json["scale_map_file"] = QString::fromStdString(stitch_.scaleMapFile);
    json["z_correction_coef"] = stitch_.zCorrectionCoef;
    json["crop_offset_file"] = QString::fromStdString(stitch_.cropOffsetFile);
    json["detection_algorithm"] = detection_.algorithm;
    json["dust_clahe_clip"] = detection_.dustClaheClip;
    json["dust_bg_blur"] = detection_.dustBgBlur;
    json["dust_min_area"] = detection_.dustMinArea;
    json["dust_max_area"] = detection_.dustMaxArea;
    json["dust_dilate_iter"] = detection_.dustDilateIter;
    json["dust_max_iter"] = detection_.dustMaxIter;
    json["dust_nms_iou"] = detection_.dustNmsIou;
    json["edge_clahe_clip"] = detection_.edgeClaheClip;
    json["edge_clahe_tile_grid"] = detection_.edgeClaheTileGrid;
    json["edge_threshold"] = detection_.edgeThreshold;
    json["edge_sobel_ksize"] = detection_.edgeSobelKSize;
    json["edge_dilate_iter"] = detection_.edgeDilateIter;
    json["edge_min_bbox_area"] = detection_.edgeMinBboxArea;
    json["edge_nms_iou_thresh"] = detection_.edgeNmsIouThresh;
    json["edge_nms_contain_thresh"] = detection_.edgeNmsContainThresh;
    json["sphere_radius"] = zaxis_.sphereRadius;
    json["sphere_cap_height"] = zaxis_.sphereCapHeight;
    json["sphere_height_offset"] = zaxis_.sphereHeightOffset;
    json["z_base_height"] = zaxis_.zBaseHeight;
    json["z_mode"] = zaxis_.mode;
    json["z_map_file"] = QString::fromStdString(zaxis_.zMapFile);
    json["z_radial_file"] = QString::fromStdString(zaxis_.zRadialFile);
    json["image_save_base_path"] = QString::fromStdString(system_.imageSaveBasePath);
    json["log_path"] = QString::fromStdString(system_.logPath);

    QJsonArray minArr, maxArr;
    for (int i = 0; i < 5; ++i) {
        minArr.append(system_.axisMinPos[i]);
        maxArr.append(system_.axisMaxPos[i]);
    }
    json["axis_min_pos"] = minArr;
    json["axis_max_pos"] = maxArr;
    json["position_tolerance"] = system_.positionTolerance;
    json["modbus_debug"] = system_.modbusDebug;

    return json;
}

bool ConfigManager::validateConfig() const {
    if (!isValidPort(system_.armPort)) {
        last_error_ = "Invalid arm port: " + std::to_string(system_.armPort);
        SPDLOG_ERROR(last_error_);
        return false;
    }
    if (system_.defaultSpeed <= 0 || system_.defaultSpeed > 200000) {
        last_error_ = "Invalid default speed: " + std::to_string(system_.defaultSpeed);
        SPDLOG_ERROR(last_error_);
        return false;
    }
    if (camera_.width <= 0 || camera_.height <= 0) {
        last_error_ = "Invalid camera resolution";
        SPDLOG_ERROR(last_error_);
        return false;
    }
    if (scan_.gridSizeX <= 0 || scan_.gridSizeY <= 0) {
        last_error_ = "Grid size must be positive";
        SPDLOG_ERROR(last_error_);
        return false;
    }
    if (scan_.stepSize <= 0) {
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
        SPDLOG_WARN("[Config] Attempted to set empty arm IP, ignored");
        return;
    }
    system_.armIp = ip;
    SPDLOG_INFO("[Config] Arm IP set to {}", ip);
}

void ConfigManager::setArmPort(int port) {
    if (isValidPort(port)) {
        system_.armPort = port;
        SPDLOG_INFO("[Config] Arm port set to {}", port);
    } else {
        SPDLOG_WARN("[Config] Invalid arm port: {}", port);
    }
}

void ConfigManager::setDefaultSpeed(int speed) {
    if (speed > 0 && speed <= 200000) {
        system_.defaultSpeed = speed;
    } else {
        SPDLOG_WARN("[Config] Invalid speed value: {}", speed);
    }
}

void ConfigManager::setCameraWidth(int w) {
    if (w > 0) camera_.width = w;
}

void ConfigManager::setCameraHeight(int h) {
    if (h > 0) camera_.height = h;
}

void ConfigManager::setCameraExposure(float exp) {
    if (exp >= 0) camera_.exposure = exp;
}

void ConfigManager::setCameraGain(float gain) {
    if (gain >= 0) camera_.gain = gain;
}

void ConfigManager::setCameraSharpening(int v) {
    if (v >= 0 && v <= 500) camera_.sharpening = v;
}

void ConfigManager::setModelParamPath(const std::string& path) {
    if (isValidFilePath(path)) {
        detection_.modelParamPath = path;
    }
}

void ConfigManager::setModelBinPath(const std::string& path) {
    if (isValidFilePath(path)) {
        detection_.modelBinPath = path;
    }
}

void ConfigManager::setGridSizeX(int x) {
    if (x > 0) scan_.gridSizeX = x;
}

void ConfigManager::setGridSizeY(int y) {
    if (y > 0) scan_.gridSizeY = y;
}

void ConfigManager::setStepSize(int s) {
    if (s > 0) scan_.stepSize = s;
}

void ConfigManager::setZHeight(int z) {
    scan_.zHeight = z;
}

void ConfigManager::setCenterCropSize(int size) {
    if (size >= 0) stitch_.centerCropSize = size;
}

void ConfigManager::setStitchAlgorithm(int algo) {
    if (algo >= 0 && algo <= 4) stitch_.algorithm = algo;
}

void ConfigManager::setFeatherWidth(int width) {
    if (width >= 0) stitch_.featherWidth = width;
}

void ConfigManager::setScaleMode(int mode) {
    stitch_.scaleMode = (mode == 1) ? 1 : 0;
}

void ConfigManager::setScaleMapFile(const std::string& path) {
    stitch_.scaleMapFile = path;
}

void ConfigManager::setZCorrectionCoef(double coef) {
    if (coef > 0.0) stitch_.zCorrectionCoef = coef;
}

void ConfigManager::setCropOffsetFile(const std::string& path) {
    stitch_.cropOffsetFile = path;
}

void ConfigManager::setDetectionAlgorithm(int algo) {
    if (algo >= 0 && algo <= 2) detection_.algorithm = algo;
}

void ConfigManager::setDustClaheClip(double v) {
    if (v > 0.0) detection_.dustClaheClip = v;
}
void ConfigManager::setDustBgBlur(int v) {
    if (v >= 3) detection_.dustBgBlur = v | 1;  // force odd kernel
}
void ConfigManager::setDustMinArea(int v) {
    if (v >= 0) detection_.dustMinArea = v;
}
void ConfigManager::setDustMaxArea(int v) {
    if (v >= 0) detection_.dustMaxArea = v;     // 0 = unlimited
}
void ConfigManager::setDustDilateIter(int v) {
    if (v >= 0) detection_.dustDilateIter = v;
}
void ConfigManager::setDustMaxIter(int v) {
    if (v >= 1) detection_.dustMaxIter = v;
}
void ConfigManager::setDustNmsIou(double v) {
    if (v >= 0.0 && v <= 1.0) detection_.dustNmsIou = v;
}

// Edge detection parameter setters
void ConfigManager::setEdgeClaheClip(double v) { if (v > 0.0) detection_.edgeClaheClip = v; }
void ConfigManager::setEdgeClaheTileGrid(int v) { if (v > 0) detection_.edgeClaheTileGrid = v; }
void ConfigManager::setEdgeThreshold(int v) { if (v >= 0 && v <= 255) detection_.edgeThreshold = v; }
void ConfigManager::setEdgeSobelKSize(int v) { if (v >= 1 && v % 2 == 1) detection_.edgeSobelKSize = v; }
void ConfigManager::setEdgeDilateIter(int v) { if (v >= 0 && v <= 10) detection_.edgeDilateIter = v; }
void ConfigManager::setEdgeMinBboxArea(int v) { if (v >= 0) detection_.edgeMinBboxArea = v; }
void ConfigManager::setEdgeNmsIouThresh(double v) { if (v >= 0.0 && v <= 1.0) detection_.edgeNmsIouThresh = v; }
void ConfigManager::setEdgeNmsContainThresh(double v) { if (v >= 0.0 && v <= 1.0) detection_.edgeNmsContainThresh = v; }

void ConfigManager::setSphereRadius(int r) {
    if (r > 0) zaxis_.sphereRadius = r;
}

void ConfigManager::setSphereCapHeight(int h) {
    if (h >= 0) zaxis_.sphereCapHeight = h;
}

void ConfigManager::setSphereHeightOffset(int d) {
    zaxis_.sphereHeightOffset = d;
}

void ConfigManager::setZBaseHeight(int z) {
    zaxis_.zBaseHeight = z;
}

void ConfigManager::setZMode(int mode) {
    zaxis_.mode = (mode >= 0 && mode <= 2) ? mode : 0;
}

void ConfigManager::setZMapFile(const std::string& path) {
    zaxis_.zMapFile = path;
}

void ConfigManager::setZRadialFile(const std::string& path) {
    zaxis_.zRadialFile = path;
}

void ConfigManager::setImageSaveBasePath(const std::string& path) {
    if (isValidFilePath(path)) {
        system_.imageSaveBasePath = path;
    }
}

void ConfigManager::setLogPath(const std::string& path) {
    if (isValidFilePath(path)) {
        system_.logPath = path;
    }
}

int ConfigManager::axisMinPos(int axis) const {
    if (axis >= 0 && axis < 5) return system_.axisMinPos[axis];
    return -1000000;
}

int ConfigManager::axisMaxPos(int axis) const {
    if (axis >= 0 && axis < 5) return system_.axisMaxPos[axis];
    return 1000000;
}

void ConfigManager::setAxisMinPos(int axis, int value) {
    if (axis >= 0 && axis < 5) system_.axisMinPos[axis] = value;
}

void ConfigManager::setAxisMaxPos(int axis, int value) {
    if (axis >= 0 && axis < 5) system_.axisMaxPos[axis] = value;
}

void ConfigManager::setPositionTolerance(float t) {
    if (t > 0) system_.positionTolerance = t;
}

void ConfigManager::setDwellTimeMs(int ms) {
    if (ms >= 0) scan_.dwellTimeMs = ms;
}

void ConfigManager::setModbusDebug(bool enabled) {
    system_.modbusDebug = enabled;
}
