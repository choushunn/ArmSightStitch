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
    int cameraSharpening() const { return camera_sharpening_; }

    void setCameraWidth(int w);
    void setCameraHeight(int h);
    void setCameraExposure(float exp);
    void setCameraGain(float gain);
    void setCameraSharpening(int v);

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

    // Scanning timing
    int dwellTimeMs() const { return dwell_time_ms_; }
    void setDwellTimeMs(int ms);

    // Stitching / crop
    int centerCropSize() const { return center_crop_size_; }
    void setCenterCropSize(int size);
    int stitchAlgorithm() const { return stitch_algorithm_; }
    void setStitchAlgorithm(int algo);
    int featherWidth() const { return feather_width_; }
    void setFeatherWidth(int width);

    // Scale mode: 0=Z-based auto-scaling, 1=manual scale-map from file
    int scaleMode() const { return scale_mode_; }
    void setScaleMode(int mode);
    std::string scaleMapFile() const { return scale_map_file_; }
    void setScaleMapFile(const std::string& path);

    // Z correction coefficient: global multiplier on Z-based scale (for AdvancedGridStitchAlgorithm)
    double zCorrectionCoef() const { return z_correction_coef_; }
    void setZCorrectionCoef(double coef);
    std::string cropOffsetFile() const { return crop_offset_file_; }
    void setCropOffsetFile(const std::string& path);

    // Detection algorithm selection: 0=YOLO, 1=Dust, 2=Edge(Sobel)
    int detectionAlgorithm() const { return detection_algorithm_; }
    void setDetectionAlgorithm(int algo);

    // Dust detection parameters (mirror docs/dust_detection.py)
    double dustClaheClip() const { return dust_clahe_clip_; }
    int dustBgBlur() const { return dust_bg_blur_; }
    int dustMinArea() const { return dust_min_area_; }
    int dustMaxArea() const { return dust_max_area_; }
    int dustDilateIter() const { return dust_dilate_iter_; }
    int dustMaxIter() const { return dust_max_iter_; }
    double dustNmsIou() const { return dust_nms_iou_; }

    void setDustClaheClip(double v);
    void setDustBgBlur(int v);
    void setDustMinArea(int v);
    void setDustMaxArea(int v);
    void setDustDilateIter(int v);
    void setDustMaxIter(int v);
    void setDustNmsIou(double v);

    // Edge detection parameters (mirror docs/det.py)
    double edgeClaheClip() const { return edge_clahe_clip_; }
    int edgeClaheTileGrid() const { return edge_clahe_tile_grid_; }
    int edgeThreshold() const { return edge_threshold_; }
    int edgeSobelKSize() const { return edge_sobel_ksize_; }
    int edgeDilateIter() const { return edge_dilate_iter_; }
    int edgeMinBboxArea() const { return edge_min_bbox_area_; }
    double edgeNmsIouThresh() const { return edge_nms_iou_thresh_; }
    double edgeNmsContainThresh() const { return edge_nms_contain_thresh_; }

    void setEdgeClaheClip(double v);
    void setEdgeClaheTileGrid(int v);
    void setEdgeThreshold(int v);
    void setEdgeSobelKSize(int v);
    void setEdgeDilateIter(int v);
    void setEdgeMinBboxArea(int v);
    void setEdgeNmsIouThresh(double v);
    void setEdgeNmsContainThresh(double v);

    // Z-axis mode: 0=spherical cap (default), 1=manual per-position Z-map, 2=radial Z-map
    int zMode() const { return z_mode_; }
    void setZMode(int mode);
    std::string zMapFile() const { return z_map_file_; }
    void setZMapFile(const std::string& path);
    std::string zRadialFile() const { return z_radial_file_; }
    void setZRadialFile(const std::string& path);

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
    int default_speed_ = 30000;

    int camera_width_ = 640;
    int camera_height_ = 480;
    float camera_exposure_ = 70.0f;
    float camera_gain_ = 1.0f;
    int camera_sharpening_ = 0;

    std::string model_param_path_;
    std::string model_bin_path_;

    int grid_size_x_ = 10;
    int grid_size_y_ = 10;
    int step_size_ = 43000;
    int z_height_ = 80000;
    int center_crop_size_ = 1775;  // center crop like docs/stitch.py
    int stitch_algorithm_ = 0;     // 0=Grid,1=Feature,2=ZScale,3=SeamFeather,4=AdvancedGrid
    int feather_width_ = 120;      // seam feather width in pixels
    int scale_mode_ = 0;           // 0=Z-based auto, 1=manual scale-map from file
    std::string scale_map_file_;   // path to scale-map JSON file when scale_mode_ == 1
    double z_correction_coef_ = 1.0;    // Z-scale correction multiplier (for AdvancedGridStitchAlgorithm)
    std::string crop_offset_file_;       // path to crop-offset JSON (per-cell ox/oy values)

    int detection_algorithm_ = 1;  // 0=YOLO, 1=Dust, 2=Edge
    double dust_clahe_clip_ = 2.0;
    int dust_bg_blur_ = 31;
    int dust_min_area_ = 50;
    int dust_max_area_ = 20000;    // 0 = no upper limit
    int dust_dilate_iter_ = 0;
    int dust_max_iter_ = 4;
    double dust_nms_iou_ = 0.1;    // 0 = disabled

    double edge_clahe_clip_ = 2.0;
    int edge_clahe_tile_grid_ = 8;
    int edge_threshold_ = 30;
    int edge_sobel_ksize_ = 3;
    int edge_dilate_iter_ = 1;
    int edge_min_bbox_area_ = 25;
    double edge_nms_iou_thresh_ = 0.4;
    double edge_nms_contain_thresh_ = 0.5;

    int sphere_radius_ = 230000;
    int sphere_cap_height_ = 50000;
    int sphere_height_offset_ = 0;
    int z_base_height_ = 80000;
    int z_mode_ = 0;              // 0=spherical cap, 1=manual per-position Z-map, 2=radial Z-map
    std::string z_map_file_;      // path to Z-map JSON file when z_mode_ == 1
    std::string z_radial_file_;   // path to radial Z-map JSON when z_mode_ == 2

    int dwell_time_ms_ = 300;

    std::string image_save_base_path_;
    std::string log_path_;
    mutable std::string last_error_;

    int axis_min_pos_[5] = {0, 0, 0, -180000, -180000};
    int axis_max_pos_[5] = {384000, 384000, 80000, 180000, 180000};
    float position_tolerance_ = 100.0f;
    bool modbus_debug_ = false;
};
