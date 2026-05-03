#pragma once

#include <string>
#include <map>
#include <mutex>
#include <QString>
#include <QDir>

class ConfigManager {
public:
    static ConfigManager& instance();

    // Arm config
    std::string armIp() const { return arm_ip_; }
    int armPort() const { return arm_port_; }
    int defaultSpeed() const { return default_speed_; }

    // Camera config
    int cameraWidth() const { return camera_width_; }
    int cameraHeight() const { return camera_height_; }
    float cameraExposure() const { return camera_exposure_; }
    float cameraGain() const { return camera_gain_; }

    // Model paths
    std::string modelParamPath() const { return model_param_path_; }
    std::string modelBinPath() const { return model_bin_path_; }

    // S-movement default parameters
    int gridSizeX() const { return grid_size_x_; }
    int gridSizeY() const { return grid_size_y_; }
    int stepSize() const { return step_size_; }
    int zHeight() const { return z_height_; }

    // Image save path
    std::string imageSaveBasePath() const { return image_save_base_path_; }

    // Setters
    void setArmIp(const std::string& ip) { arm_ip_ = ip; }
    void setArmPort(int port) { arm_port_ = port; }
    void setDefaultSpeed(int speed) { default_speed_ = speed; }
    void setModelParamPath(const std::string& path) { model_param_path_ = path; }
    void setModelBinPath(const std::string& path) { model_bin_path_ = path; }
    void setImageSaveBasePath(const std::string& path) { image_save_base_path_ = path; }

private:
    ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    void loadDefaults();

    std::string arm_ip_;
    int arm_port_;
    int default_speed_;

    int camera_width_;
    int camera_height_;
    float camera_exposure_;
    float camera_gain_;

    std::string model_param_path_;
    std::string model_bin_path_;

    int grid_size_x_;
    int grid_size_y_;
    int step_size_;
    int z_height_;

    std::string image_save_base_path_;
};
