#pragma once

#include <string>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>

#include "ConfigSections.h"

class ConfigManager {
public:
    static ConfigManager& instance();

    bool loadFromFile(const std::string& filepath);
    bool loadFromJson(const QJsonObject& json);
    bool saveToFile(const std::string& filepath) const;
    QJsonObject toJson() const;

    std::string lastError() const { return last_error_; }

    // Arm config
    std::string armIp() const { return system_.armIp; }
    int armPort() const { return system_.armPort; }
    int defaultSpeed() const { return system_.defaultSpeed; }

    void setArmIp(const std::string& ip);
    void setArmPort(int port);
    void setDefaultSpeed(int speed);

    // Camera config
    int cameraWidth() const { return camera_.width; }
    int cameraHeight() const { return camera_.height; }
    float cameraExposure() const { return camera_.exposure; }
    float cameraGain() const { return camera_.gain; }
    int cameraSharpening() const { return camera_.sharpening; }

    void setCameraWidth(int w);
    void setCameraHeight(int h);
    void setCameraExposure(float exp);
    void setCameraGain(float gain);
    void setCameraSharpening(int v);

    // Model paths
    std::string modelParamPath() const { return detection_.modelParamPath; }
    std::string modelBinPath() const { return detection_.modelBinPath; }

    void setModelParamPath(const std::string& path);
    void setModelBinPath(const std::string& path);

    // S-movement / spherical cap parameters (Protocol Section 7.2)
    int gridSizeX() const { return scan_.gridSizeX; }
    int gridSizeY() const { return scan_.gridSizeY; }
    int stepSize() const { return scan_.stepSize; }
    int zHeight() const { return scan_.zHeight; }
    int sphereRadius() const { return zaxis_.sphereRadius; }
    int sphereCapHeight() const { return zaxis_.sphereCapHeight; }
    int sphereHeightOffset() const { return zaxis_.sphereHeightOffset; }
    int zBaseHeight() const { return zaxis_.zBaseHeight; }

    void setGridSizeX(int x);
    void setGridSizeY(int y);
    void setStepSize(int s);
    void setZHeight(int z);
    void setSphereRadius(int r);
    void setSphereCapHeight(int h);
    void setSphereHeightOffset(int d);
    void setZBaseHeight(int z);

    // Scanning timing
    int dwellTimeMs() const { return scan_.dwellTimeMs; }
    void setDwellTimeMs(int ms);

    // Stitching / crop
    int centerCropSize() const { return stitch_.centerCropSize; }
    void setCenterCropSize(int size);
    int stitchAlgorithm() const { return stitch_.algorithm; }
    void setStitchAlgorithm(int algo);
    int featherWidth() const { return stitch_.featherWidth; }
    void setFeatherWidth(int width);

    // Scale mode: 0=Z-based auto-scaling, 1=manual scale-map from file
    int scaleMode() const { return stitch_.scaleMode; }
    void setScaleMode(int mode);
    std::string scaleMapFile() const { return stitch_.scaleMapFile; }
    void setScaleMapFile(const std::string& path);

    // Z correction coefficient: global multiplier on Z-based scale (for AdvancedGridStitchAlgorithm)
    double zCorrectionCoef() const { return stitch_.zCorrectionCoef; }
    void setZCorrectionCoef(double coef);
    std::string cropOffsetFile() const {
        if (!stitch_.cropOffsetFile.empty()) return stitch_.cropOffsetFile;
        // fallback default path — consistent with loadDefaults()
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        return (docs + "/ScannerData/crop_offset.json").toStdString();
    }
    void setCropOffsetFile(const std::string& path);

    // Detection algorithm selection: 0=YOLO, 1=Dust, 2=Edge(Sobel)
    int detectionAlgorithm() const { return detection_.algorithm; }
    void setDetectionAlgorithm(int algo);

    // Dust detection parameters (mirror docs/dust_detection.py)
    double dustClaheClip() const { return detection_.dustClaheClip; }
    int dustBgBlur() const { return detection_.dustBgBlur; }
    int dustMinArea() const { return detection_.dustMinArea; }
    int dustMaxArea() const { return detection_.dustMaxArea; }
    int dustDilateIter() const { return detection_.dustDilateIter; }
    int dustMaxIter() const { return detection_.dustMaxIter; }
    double dustNmsIou() const { return detection_.dustNmsIou; }

    void setDustClaheClip(double v);
    void setDustBgBlur(int v);
    void setDustMinArea(int v);
    void setDustMaxArea(int v);
    void setDustDilateIter(int v);
    void setDustMaxIter(int v);
    void setDustNmsIou(double v);

    // Edge detection parameters (mirror docs/det.py)
    double edgeClaheClip() const { return detection_.edgeClaheClip; }
    int edgeClaheTileGrid() const { return detection_.edgeClaheTileGrid; }
    int edgeThreshold() const { return detection_.edgeThreshold; }
    int edgeSobelKSize() const { return detection_.edgeSobelKSize; }
    int edgeDilateIter() const { return detection_.edgeDilateIter; }
    int edgeMinBboxArea() const { return detection_.edgeMinBboxArea; }
    double edgeNmsIouThresh() const { return detection_.edgeNmsIouThresh; }
    double edgeNmsContainThresh() const { return detection_.edgeNmsContainThresh; }

    void setEdgeClaheClip(double v);
    void setEdgeClaheTileGrid(int v);
    void setEdgeThreshold(int v);
    void setEdgeSobelKSize(int v);
    void setEdgeDilateIter(int v);
    void setEdgeMinBboxArea(int v);
    void setEdgeNmsIouThresh(double v);
    void setEdgeNmsContainThresh(double v);

    // Z-axis mode: 0=spherical cap (default), 1=manual per-position Z-map, 2=radial Z-map
    int zMode() const { return zaxis_.mode; }
    void setZMode(int mode);
    std::string zMapFile() const { return zaxis_.zMapFile; }
    void setZMapFile(const std::string& path);
    std::string zRadialFile() const { return zaxis_.zRadialFile; }
    void setZRadialFile(const std::string& path);

    // Image save path
    std::string imageSaveBasePath() const { return system_.imageSaveBasePath; }
    void setImageSaveBasePath(const std::string& path);

    // Logging
    std::string logPath() const { return system_.logPath; }
    void setLogPath(const std::string& path);

    // Safety limits
    int axisMinPos(int axis) const;
    int axisMaxPos(int axis) const;
    void setAxisMinPos(int axis, int value);
    void setAxisMaxPos(int axis, int value);

    // Position tolerance
    float positionTolerance() const { return system_.positionTolerance; }
    void setPositionTolerance(float t);

    // Modbus debug
    bool modbusDebug() const { return system_.modbusDebug; }
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

    infra::config::CameraSection   camera_;
    infra::config::ScanSection     scan_;
    infra::config::StitchSection   stitch_;
    infra::config::ZAxisSection    zaxis_;
    infra::config::DetectionSection detection_;
    infra::config::SystemSection   system_;

    mutable std::string last_error_;
};
