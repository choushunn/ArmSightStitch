#pragma once

#include <QObject>
#include <memory>

#include "core/arm/IArmController.h"
#include "core/camera/ICameraHandler.h"
#include "core/detector/IDetector.h"
#include "core/stitch/IStitcher.h"

namespace arm { struct SMovementStatus; }
namespace camera { struct CameraStatus; }
class WorkflowManager;

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController();

    // Module access (read-only for MainWindow)
    arm::IArmController& armController();
    arm::ISMovementController& movementController();
    camera::ICameraHandler& cameraHandler();
    detector::IDetector& detector();
    stitch::IStitcher& stitcher();
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

    bool loadDetectorModel(const std::string& paramPath, const std::string& binPath);
    bool isModelLoaded() const;
    std::vector<detector::Detection> detect(const cv::Mat& image);
    cv::Mat drawDetections(const cv::Mat& image, const std::vector<detector::Detection>& detections);

signals:
    void statusMessage(const QString& msg);
    void errorMessage(const QString& msg);
    void stitchingFinished(const cv::Mat& result);
    void modelLoaded();

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
