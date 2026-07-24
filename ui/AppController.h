#pragma once

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <QObject>
#include <memory>

#include "core/arm/ModbusArmController.h"
#include "core/arm/SMovementController.h"
#include "core/camera/CameraHandler.h"
#include "core/detector/YoloDetector.h"
#include "core/detector/DustDetectionParams.h"
#include "core/detector/EdgeDetectionParams.h"
#include "core/stitch/ImageStitcher.h"

class WorkflowManager;

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController();

    // Module access (read-only for MainWindow)
    arm::ModbusArmController& armController();
    arm::SMovementController& movementController();
    camera::CameraHandler& cameraHandler();
    detector::IDetector& detector();
    stitch::ImageStitcher& stitcher();
    WorkflowManager& workflow();

    // High-level operations
    bool connectArm(const std::string& ip, int port);
    void disconnectArm();

    bool connectCamera(const std::string& deviceId);
    void disconnectCamera();
    bool startCameraCapture();
    void stopCameraCapture();
    bool captureImage(cv::Mat& frame);

    bool startSMovement(const cv::Size& gridSize, int stepSize, int zHeight);
    void stopSMovement();
    void pauseSMovement();
    void resumeSMovement();

    void stitchImages(const std::vector<cv::Mat>& images, const cv::Size& gridSize);
    std::vector<cv::Mat> loadImages(const std::string& dir);
    std::vector<stitch::PositionedImage> loadImagesWithPositions(const std::string& dir,
                                                                  cv::Size& outGridSize);

    bool loadDetectorModel(const std::string& paramPath, const std::string& binPath);
    bool isModelLoaded() const;
    std::vector<detector::Detection> detect(const cv::Mat& image);
    cv::Mat drawDetections(const cv::Mat& image, const std::vector<detector::Detection>& detections);

    // Detector algorithm switching: 0=YOLO, 1=Dust, 2=Edge(Sobel)
    void setDetectorAlgorithm(int algo);
    int detectorAlgorithm() const;
    void setDustParams(const detector::DustDetectionParams& params);
    detector::DustDetectionParams dustParams() const;
    void setEdgeParams(const detector::EdgeDetectionParams& params);
    detector::EdgeDetectionParams edgeParams() const;

    // Scan directory + detection result export
    void setScanDir(const std::string& dir);
    std::string scanDir() const;
    /// Serialize detections (type/size/position) to <scanDir>/<sourceName>_detections.json.
    /// Returns the written path, or empty on failure.
    std::string saveDetectionsJson(const std::vector<detector::Detection>& detections,
                                   const cv::Mat& image,
                                   const std::string& sourceName);
    /// Same but writes to a specific directory with a given base name.
    std::string saveDetectionsJsonTo(const std::vector<detector::Detection>& detections,
                                     const cv::Mat& image,
                                     const std::string& outDir,
                                     const std::string& baseName);

signals:
    void statusMessage(const QString& msg);
    void errorMessage(const QString& msg);
    void stitchingFinished(const cv::Mat& result);
    void modelLoaded();

    void cameraFrameReady(const cv::Mat& frame);
    void cameraExposureChanged();
    void cameraDisconnected();
    void cameraError(const QString& msg);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
