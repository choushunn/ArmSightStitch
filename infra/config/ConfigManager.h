#pragma once

#include <string>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>

class ConfigManager {
public:
    static ConfigManager& instance();

    bool loadFromFile(const std::string& filepath);
    bool loadFromJson(const QJsonObject& json);
    bool saveToFile(const std::string& filepath) const;
    QJsonObject toJson() const;

    std::string lastError() const { return last_error_; }

    // Arm config
    std::string armIp() const { return arm_ip_; }
    int armPort() const { return arm_port_; }
    int defaultSpeed() const { return default_speed_; }

    void setArmIp(const std::string& ip);
    void setArmPort(int port);
    void setDefaultSpeed(int speed);

    // Camera config
    int cameraWidth() const { return camera_width_; }
    int cameraHeight() const { return camera_height_; }
    float cameraExposure() const { return camera_exposure_; }
    float cameraGain() const { return camera_gain_; }

    void setCameraWidth(int w);
    void setCameraHeight(int h);
    void setCameraExposure(float exp);
    void setCameraGain(float gain);

    // Model paths
    std::string modelParamPath() const { return model_param_path_; }
    std::string modelBinPath() const { return model_bin_path_; }

    void setModelParamPath(const std::string& path);
    void setModelBinPath(const std::string& path);

    // S-movement / spherical cap parameters (Protocol Section 7.2)
    int gridSizeX() const { return grid_size_x_; }
    int gridSizeY() const { return grid_size_y_; }
    int stepSize() const { return step_size_; }
    int zHeight() const { return z_height_; }
    int sphereRadius() const { return sphere_radius_; }
    int sphereCapHeight() const { return sphere_cap_height_; }
    int sphereHeightOffset() const { return sphere_height_offset_; }
    int zBaseHeight() const { return z_base_height_; }

    void setGridSizeX(int x);
    void setGridSizeY(int y);
    void setStepSize(int s);
    void setZHeight(int z);
    void setSphereRadius(int r);
    void setSphereCapHeight(int h);
    void setSphereHeightOffset(int d);
    void setZBaseHeight(int z);

    // Image save path
    std::string imageSaveBasePath() const { return image_save_base_path_; }
    void setImageSaveBasePath(const std::string& path);

    // Logging
    std::string logPath() const { return log_path_; }
    void setLogPath(const std::string& path);

    // Safety limits
    int axisMinPos(int axis) const;
    int axisMaxPos(int axis) const;
    void setAxisMinPos(int axis, int value);
    void setAxisMaxPos(int axis, int value);

    // Position tolerance
    float positionTolerance() const { return position_tolerance_; }
    void setPositionTolerance(float t);

    // Modbus debug
    bool modbusDebug() const { return modbus_debug_; }
    void setModbusDebug(bool enabled);

private:
    ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    void loadDefaults();
    void applyEnvironmentOverrides();
    bool validateConfig() const;

    static bool isValidPort(int port);
    static bool isValidFilePath(const std::string& path);

    std::string arm_ip_;
    int arm_port_ = 502;
    int default_speed_ = 70000;

    int camera_width_ = 640;
    int camera_height_ = 480;
    float camera_exposure_ = 100.0f;
    float camera_gain_ = 1.0f;

    std::string model_param_path_;
    std::string model_bin_path_;

    int grid_size_x_ = 10;
    int grid_size_y_ = 10;
    int step_size_ = 43000;
    int z_height_ = 50000;

    int sphere_radius_ = 230000;
    int sphere_cap_height_ = 50000;
    int sphere_height_offset_ = 0;
    int z_base_height_ = 80000;

    std::string image_save_base_path_;
    std::string log_path_;
    mutable std::string last_error_;

    int axis_min_pos_[5] = {-1000000, -1000000, -1000000, -180000, -180000};
    int axis_max_pos_[5] = {1000000, 1000000, 1000000, 180000, 180000};
    float position_tolerance_ = 100.0f;
    bool modbus_debug_ = false;
};
