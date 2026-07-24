#ifdef _MSC_VER
#define NOMINMAX
#include <windows.h>
#endif

#include "DetectController.h"
#include "ui_MainWindow.h"
#include "ui/MainWindow.h"
#include "ui/AppController.h"
#include "ui/DetectionSettingsDialog.h"
#include "infra/config/ConfigManager.h"

#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <spdlog/spdlog.h>

static QImage cvMatToQImageStatic(const cv::Mat& mat) {
    if (mat.empty()) return {};
    if (mat.type() == CV_8UC3) {
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888)
            .rgbSwapped();
    }
    if (mat.type() == CV_8UC1) {
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
    }
    cv::Mat rgb;
    mat.convertTo(rgb, CV_8UC3);
    return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888)
        .rgbSwapped()
        .copy();
}

static void displayImageFullQualityHelper(const cv::Mat& image, QLabel* label, QPixmap& storage) {
    if (image.empty()) return;
    storage = QPixmap::fromImage(cvMatToQImageStatic(image));
    label->setPixmap(storage.scaled(label->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

DetectController::DetectController(Ui::MainWindow* ui, AppController& ctrl,
                                   QObject* parentObj)
    : QObject(parentObj)
    , ui_(ui)
    , ctrl_(ctrl)
{
    connect(&detection_watcher_, &QFutureWatcher<DetectionResult>::finished,
            this, &DetectController::onDetectionFinished);
    connect(&manual_detect_watcher_, &QFutureWatcher<DetectionResult>::finished,
            this, [this]() {
        DetectionResult result = manual_detect_future_.result();
        if (!result.frame.empty()) {
            cv::Mat annotated = ctrl_.drawDetections(result.frame, result.detections);
            displayImageFullQualityHelper(annotated, ui_->detectImageLabel, detected_pixmap_);
            emit logMessage(QStringLiteral("手动检测完成: %1 个目标").arg(result.detections.size()), QStringLiteral("INFO"));
            // Export detection results (type/size/position) to scan directory as JSON
            auto* mw = static_cast<MainWindow*>(parent());
            std::string jsonPath = ctrl_.saveDetectionsJson(
                result.detections, result.frame, mw->currentImageSource());
            if (!jsonPath.empty())
                emit logMessage(QStringLiteral("检测结果已导出: %1").arg(QString::fromStdString(jsonPath)), QStringLiteral("INFO"));
        }
    });
}

DetectController::~DetectController() {
    detection_watcher_.waitForFinished();
    manual_detect_watcher_.waitForFinished();
}

void DetectController::onDetSettings() {
    auto& cfg = ConfigManager::instance();

    // Snapshot current state; rollback on cancel for live-test changes to detector
    int prevAlgo = ctrl_.detectorAlgorithm();
    detector::DustDetectionParams prevDust = ctrl_.dustParams();
    detector::EdgeDetectionParams prevEdge = ctrl_.edgeParams();
    float prevConf = ctrl_.detector().getConfidenceThreshold();
    float prevNms  = ctrl_.detector().getNmsThreshold();

    DetectionSettingsDialog dlg(&ctrl_, qobject_cast<QWidget*>(parent()));
    dlg.setDetectionAlgorithm(cfg.detectionAlgorithm());
    dlg.setParamPath(QString::fromStdString(cfg.modelParamPath()));
    dlg.setBinPath(QString::fromStdString(cfg.modelBinPath()));
    dlg.setConfidenceThreshold(ctrl_.detector().getConfidenceThreshold());
    dlg.setNmsThreshold(ctrl_.detector().getNmsThreshold());
    dlg.setDustParams(prevDust);
    dlg.setEdgeParams(prevEdge);

    if (dlg.exec() == QDialog::Accepted) {
        int algo = dlg.detectionAlgorithm();
        detector::DustDetectionParams dp = dlg.dustParams();
        detector::EdgeDetectionParams ep = dlg.edgeParams();

        // Persist to ConfigManager
        cfg.setDetectionAlgorithm(algo);
        cfg.setDustClaheClip(dp.claheClip);
        cfg.setDustBgBlur(dp.bgBlurSize);
        cfg.setDustMinArea(dp.minArea);
        cfg.setDustMaxArea(dp.maxArea);
        cfg.setDustDilateIter(dp.dilateIter);
        cfg.setDustMaxIter(dp.maxIter);
        cfg.setDustNmsIou(dp.nmsIou);

        cfg.setEdgeClaheClip(ep.claheClip);
        cfg.setEdgeClaheTileGrid(ep.claheTileGrid);
        cfg.setEdgeThreshold(ep.edgeThreshold);
        cfg.setEdgeSobelKSize(ep.sobelKSize);
        cfg.setEdgeDilateIter(ep.dilateIter);
        cfg.setEdgeMinBboxArea(ep.minBboxArea);
        cfg.setEdgeNmsIouThresh(ep.nmsIouThresh);
        cfg.setEdgeNmsContainThresh(ep.nmsContainThresh);

        // Apply to runtime detector
        ctrl_.setDustParams(dp);
        ctrl_.setEdgeParams(ep);
        ctrl_.setDetectorAlgorithm(algo);
        ctrl_.detector().setConfidenceThreshold(dlg.confidenceThreshold());
        ctrl_.detector().setNmsThreshold(dlg.nmsThreshold());
        model_loaded_ = ctrl_.isModelLoaded();
        emit modelLoadedChanged(model_loaded_);
    } else {
        // Rollback live-test changes
        ctrl_.setDetectorAlgorithm(prevAlgo);
        ctrl_.setDustParams(prevDust);
        ctrl_.setEdgeParams(prevEdge);
        ctrl_.detector().setConfidenceThreshold(prevConf);
        ctrl_.detector().setNmsThreshold(prevNms);
        model_loaded_ = ctrl_.isModelLoaded();
        emit modelLoadedChanged(model_loaded_);
    }
}

void DetectController::onManualDetect() {
    if (!model_loaded_) {
        QMessageBox::warning(qobject_cast<QWidget*>(parent()), QStringLiteral("警告"),
                             QStringLiteral("请先加载检测模型"));
        return;
    }

    // Access current_image_ from MainWindow (controllers are friends)
    auto* mw = static_cast<MainWindow*>(parent());
    if (mw->currentImage().empty()) {
        QMessageBox::warning(qobject_cast<QWidget*>(parent()), QStringLiteral("警告"),
                             QStringLiteral("请先打开或拍摄一张图像"));
        return;
    }

    // Single-frame detection: clone current frame, run async, show result once
    cv::Mat img = mw->currentImage().clone();
    auto* ctrl = &ctrl_;
    manual_detect_future_ = QtConcurrent::run([ctrl, img]() -> DetectionResult {
        DetectionResult result;
        result.frame = img;
        result.detections = ctrl->detect(img);
        return result;
    });
    manual_detect_watcher_.setFuture(manual_detect_future_);
}

void DetectController::runRealTimeDetection(const cv::Mat& frame) {
    if (!model_loaded_ || detection_busy_) return;
    detection_busy_ = true;
    auto* ctrl = &ctrl_;
    detection_future_ = QtConcurrent::run([ctrl, frame]() -> DetectionResult {
        DetectionResult result;
        result.frame = frame;
        result.detections = ctrl->detect(frame);
        return result;
    });
    detection_watcher_.setFuture(detection_future_);
}

void DetectController::onDetectionFinished() {
    detection_busy_ = false;
    DetectionResult result = detection_future_.result();
    if (!result.frame.empty()) {
        cv::Mat annotated = ctrl_.drawDetections(result.frame, result.detections);
        // Detection overlay ONLY on the detection preview panel (bottom-right),
        // NOT on the camera preview (left) — camera stays clean
        displayImageFullQualityHelper(annotated, ui_->detectImageLabel, detected_pixmap_);

        // Detection fullscreen live update deferred to MainWindow via signal
    }
}
