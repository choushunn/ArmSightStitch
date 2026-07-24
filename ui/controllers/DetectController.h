#pragma once

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QPixmap>
#include <atomic>

#include <opencv2/opencv.hpp>

#include "core/detector/YoloDetector.h"

class AppController;
namespace Ui { class MainWindow; }

class DetectController : public QObject {
    Q_OBJECT

public:
    explicit DetectController(Ui::MainWindow* ui, AppController& ctrl,
                              QObject* parent = nullptr);
    ~DetectController() override;

public slots:
    void onDetSettings();
    void onManualDetect();
    void onDetectionFinished();

public:
    void runRealTimeDetection(const cv::Mat& frame);

signals:
    void logMessage(const QString& msg, const QString& level = QStringLiteral("INFO"));
    void modelLoadedChanged(bool loaded);

public:
    // Shared state accessors for MainWindow
    bool isModelLoaded() const { return model_loaded_; }
    void setModelLoaded(bool v) { model_loaded_ = v; }
    QPixmap& detectedPixmap() { return detected_pixmap_; }
    const QPixmap& detectedPixmap() const { return detected_pixmap_; }
    bool isDetectionBusy() const { return detection_busy_; }

private:
    struct DetectionResult {
        cv::Mat frame;
        std::vector<detector::Detection> detections;
    };

    Ui::MainWindow* ui_;
    AppController& ctrl_;

    bool model_loaded_ = false;
    QPixmap detected_pixmap_;

    QFuture<DetectionResult> detection_future_;
    QFutureWatcher<DetectionResult> detection_watcher_;
    std::atomic<bool> detection_busy_{false};

    // Manual (one-shot) detection
    QFuture<DetectionResult> manual_detect_future_;
    QFutureWatcher<DetectionResult> manual_detect_watcher_;
};
