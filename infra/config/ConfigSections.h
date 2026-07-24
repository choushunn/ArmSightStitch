#pragma once

#include <string>
#include <QString>
#include <QStandardPaths>

namespace infra::config {

// ── Camera configuration ──
struct CameraSection {
    int width = 640;
    int height = 480;
    float exposure = 70.0f;
    float gain = 1.0f;
    int sharpening = 0;
};

// ── Scan / grid configuration ──
struct ScanSection {
    int gridSizeX = 10;
    int gridSizeY = 10;
    int stepSize = 43000;       // pulses per step
    int zHeight = 80000;        // default Z position
    int dwellTimeMs = 300;      // settle time at each cell
};

// ── Stitching configuration ──
struct StitchSection {
    int algorithm = 3;          // 0=Grid, 1=Feature, 2=ZScale, 3=SeamFeather
    int centerCropSize = 1775;
    int featherWidth = 120;
    int scaleMode = 0;          // 0=Z-based auto, 1=manual scale-map
    double zCorrectionCoef = 1.0;
    std::string scaleMapFile;
    std::string cropOffsetFile;
};

// ── Z-axis configuration ──
struct ZAxisSection {
    int mode = 0;               // 0=spherical cap, 1=manual Z-map, 2=radial Z-map
    int sphereRadius = 230000;
    int sphereCapHeight = 50000;
    int sphereHeightOffset = 0;
    int zBaseHeight = 80000;
    std::string zMapFile;
    std::string zRadialFile;
};

// ── Detection configuration ──
struct DetectionSection {
    int algorithm = 1;          // 0=YOLO, 1=Dust, 2=Edge
    std::string modelParamPath;
    std::string modelBinPath;

    // Dust params
    double dustClaheClip = 2.0;
    int dustBgBlur = 31;
    int dustMinArea = 50;
    int dustMaxArea = 20000;
    int dustDilateIter = 0;
    int dustMaxIter = 4;
    double dustNmsIou = 0.1;

    // Edge params
    double edgeClaheClip = 2.0;
    int edgeClaheTileGrid = 8;
    int edgeThreshold = 30;
    int edgeSobelKSize = 3;
    int edgeDilateIter = 1;
    int edgeMinBboxArea = 25;
    double edgeNmsIouThresh = 0.4;
    double edgeNmsContainThresh = 0.5;
};

// ── System configuration ──
struct SystemSection {
    std::string armIp;
    int armPort = 502;
    int defaultSpeed = 30000;
    int axisMinPos[5] = {0, 0, 0, -180000, -180000};
    int axisMaxPos[5] = {384000, 384000, 80000, 180000, 180000};
    float positionTolerance = 100.0f;
    bool modbusDebug = false;
    std::string logPath;
    std::string imageSaveBasePath;
};

}  // namespace infra::config
