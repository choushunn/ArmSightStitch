#include "ConfigManager.h"
#include <iostream>
#include <cstdlib>

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

ConfigManager::ConfigManager() {
    loadDefaults();
}

void ConfigManager::loadDefaults() {
    arm_ip_ = "192.168.0.1";
    arm_port_ = 502;
    default_speed_ = 70000;

    camera_width_ = 640;
    camera_height_ = 480;
    camera_exposure_ = 100.0f;
    camera_gain_ = 1.0f;

    model_param_path_ = "d:/ArmSightStitch/doc/NCNN/best-sim-opt.ncnn.param";
    model_bin_path_ = "d:/ArmSightStitch/doc/NCNN/best-sim-opt.ncnn.bin";

    grid_size_x_ = 10;
    grid_size_y_ = 10;
    step_size_ = 43000;
    z_height_ = 50000;

    image_save_base_path_ = "d:/ArmSightStitch/ArmLiteCPlusPlus/image";
}
