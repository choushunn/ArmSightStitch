#pragma once

#include <QObject>
#include <memory>

#include "core/arm/IArmController.h"
#include "core/arm/ModbusArmController.h"
#include "core/arm/SMovementController.h"
#include "core/camera/ICameraHandler.h"
#include "core/camera/CameraHandler.h"
#include "core/detector/IDetector.h"
#include "core/detector/YoloDetector.h"
#include "core/stitch/IStitcher.h"
#include "core/stitch/ImageStitcher.h"
#include "infra/workflow/WorkflowManager.h"

namespace arm { struct SMovementStatus; }
namespace camera { struct CameraStatus; }

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController();

    // Module access (read-only for MainWindow)
    arm::IArmController& armController() { return arm_controller_; }
    arm::ISMovementController& movementController() { return s_movement_controller_; }
    camera::ICameraHandler& cameraHandler() { return camera_handler_; }
    detector::IDetector& detector() { return yolo_detector_; }
    stitch::IStitcher& stitcher() { return image_stitcher_; }
    WorkflowManager& workflow() { return *workflow_mgr_; }

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
    std::vector<detector::Detection> detect(const cv::Mat& image);
    cv::Mat drawDetections(const cv::Mat& image, const std::vector<detector::Detection>& detections);

signals:
    void statusMessage(const QString& msg);
    void errorMessage(const QString& msg);
    void stitchingFinished(const cv::Mat& result);

private:
    arm::ModbusArmController arm_controller_;
    arm::SMovementController s_movement_controller_;
    camera::CameraHandler camera_handler_;
    detector::YoloDetector yolo_detector_;
    stitch::ImageStitcher image_stitcher_;
    WorkflowManager* workflow_mgr_ = nullptr;
};
