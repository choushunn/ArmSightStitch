#ifdef _MSC_VER
#define NOMINMAX
#include <windows.h>
#endif

#include "CameraPaneController.h"
#include "ui_MainWindow.h"
#include "ui/AppController.h"
#include "infra/config/ConfigManager.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFuture>
#include <QFutureWatcher>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>
#include <spdlog/spdlog.h>

namespace {
QImage cvMatToQImageStatic(const cv::Mat& mat) {
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
} // namespace

CameraPaneController::CameraPaneController(Ui::MainWindow* ui, AppController& ctrl,
                                           QObject* parent)
    : QObject(parent)
    , ui_(ui)
    , ctrl_(ctrl)
{
}

void CameraPaneController::onConnectCamera() {
    QString id = ui_->cameraCombo->currentData().toString();
    if (ctrl_.connectCamera(id.toStdString())) {
        ui_->cameraToggleButton->setText(QStringLiteral("断开"));
        ui_->captureImageButton->setEnabled(true);
        ui_->scanNegativeCheck->setChecked(true);
        ui_->quickDetectBtn->setEnabled(true);
        ui_->enableDetectionCheck->setEnabled(true);
        // Clear placeholder text so preview area shows immediately
        ui_->cameraImageLabel->setText({});

        // Default rotation 270 degrees BEFORE capture (ToupCam SDK requirement)
        ctrl_.cameraHandler().setRotation(270);
        {
            QSignalBlocker blocker(ui_->rotationCombo);
            ui_->rotationCombo->setCurrentIndex(3); // 270 degrees
        }
        ui_->rotationCombo->setEnabled(true);

        // Sync flip states from camera hardware
        if (ui_->hflipCheck) ui_->hflipCheck->setChecked(ctrl_.cameraHandler().getHFlip());
        if (ui_->vflipCheck) ui_->vflipCheck->setChecked(ctrl_.cameraHandler().getVFlip());
        if (ui_->negativeCheck) ui_->negativeCheck->setChecked(ctrl_.cameraHandler().getNegative());

        // Populate resolution combo from camera's supported resolutions
        // (also before capture — Toupcam_put_Size must be called before start)
        auto* res_combo = ui_->resolutionCombo;
        res_combo->clear();
        res_combo->setEnabled(true);
        auto resolutions = ctrl_.cameraHandler().getSupportedResolutions();
        int cur_w = 0, cur_h = 0;
        ctrl_.cameraHandler().getResolution(cur_w, cur_h);
        int select_idx = 0;
        for (size_t i = 0; i < resolutions.size(); ++i) {
            auto [w, h] = resolutions[i];
            QString label = QStringLiteral("%1x%2").arg(w).arg(h);
            res_combo->addItem(label, QVariant(QSize(w, h)));
            if (w == cur_w && h == cur_h) {
                select_idx = static_cast<int>(i);
            }
        }
        res_combo->setCurrentIndex(select_idx);

        ctrl_.startCameraCapture();
        // First frame arrives via signal on next event-loop iteration,
        // after Qt layouts settle — no manual defer needed
        ui_->statusLabel->setText(QStringLiteral("相机已连接"));

        // Restore camera params from config (persists across sessions)
        {
            auto& cfg = ConfigManager::instance();
            bool hw_auto = ctrl_.cameraHandler().getAutoExposure();
            if (!hw_auto) {
                ctrl_.cameraHandler().setExposure(cfg.cameraExposure());
                ctrl_.cameraHandler().setGain(cfg.cameraGain());
            }
            ctrl_.cameraHandler().setSharpening(static_cast<unsigned short>(cfg.cameraSharpening()));
        }

        // Sync exposure UI state from camera
        bool auto_on = ctrl_.cameraHandler().getAutoExposure();
        ui_->autoExposureCheck->setChecked(auto_on);
        auto& slider = ui_->exposureSlider;
        auto& label = ui_->exposureValueLabel;

        // Configure slider range from camera's actual exposure range (SDK: Toupcam_get_ExpTimeRange)
        slider->setMinimum(10);     // 0.1ms
        slider->setMaximum(35000);   // 350ms

        // Set current value from camera (use RealExpoTime for actual hardware value)
        float expo = auto_on ? ctrl_.cameraHandler().getRealExposure()
                             : ctrl_.cameraHandler().getExposure();
        int slider_val = qBound(slider->minimum(), static_cast<int>(expo * 100.0f), slider->maximum());
        slider->setValue(slider_val);
        label->setText(QString::number(expo, 'f', 2) + QStringLiteral(" ms"));
        slider->setEnabled(!auto_on);
        label->setEnabled(!auto_on);
        ui_->autoExposureCheck->setEnabled(true);

        // Sync gain UI state from camera
        {
            unsigned short gMin, gMax, gDef;
            ctrl_.cameraHandler().getGainRange(gMin, gMax, gDef);
            auto& gslider = ui_->gainSlider;
            gslider->setMinimum(static_cast<int>(gMin));
            gslider->setMaximum(static_cast<int>(gMax));
            float current_gain = ctrl_.cameraHandler().getGain();
            int gval = qBound(gslider->minimum(), static_cast<int>(current_gain * 100.0f), gslider->maximum());
            gslider->setValue(gval);
            ui_->gainValueLabel->setText(QString::number(current_gain, 'f', 2) + QStringLiteral("x"));
            gslider->setEnabled(!auto_on);
            ui_->gainValueLabel->setEnabled(!auto_on);
        }

        // Sync sharpening UI state from camera
        {
            unsigned short current_sharp = ctrl_.cameraHandler().getSharpening();
            QSignalBlocker sb(ui_->sharpeningSlider);
            ui_->sharpeningSlider->setMinimum(0);
            ui_->sharpeningSlider->setMaximum(500);
            ui_->sharpeningSlider->setValue(static_cast<int>(current_sharp));
            ui_->sharpeningValueLabel->setText(current_sharp == 0 ? QStringLiteral("关")
                                                : QString::number(current_sharp));
            ui_->sharpeningSlider->setEnabled(true);
            ui_->sharpeningValueLabel->setEnabled(true);
        }

        if (ui_->hflipCheck) ui_->hflipCheck->setEnabled(true);
        if (ui_->vflipCheck) ui_->vflipCheck->setEnabled(true);
        if (ui_->negativeCheck) ui_->negativeCheck->setEnabled(true);

        emit cameraConnected();
    }
}

void CameraPaneController::onDisconnectCamera() {
    ctrl_.stopCameraCapture();
    ctrl_.disconnectCamera();
    ui_->cameraToggleButton->setText(QStringLiteral("连接"));
    ui_->captureImageButton->setEnabled(false);
    ui_->quickDetectBtn->setEnabled(false);
    ui_->enableDetectionCheck->setChecked(false);
    ui_->enableDetectionCheck->setEnabled(false);
    ui_->cameraImageLabel->setText(QStringLiteral("相机预览"));
    ui_->cameraImageLabel->setPixmap({});
    ui_->statusLabel->setText(QStringLiteral("相机已断开"));
    ui_->autoExposureCheck->setEnabled(false);
    ui_->exposureSlider->setEnabled(false);
    ui_->exposureValueLabel->setEnabled(false);
    ui_->gainSlider->setEnabled(false);
    ui_->gainValueLabel->setEnabled(false);
    ui_->sharpeningSlider->setEnabled(false);
    ui_->sharpeningValueLabel->setEnabled(false);
    ui_->rotationCombo->setEnabled(false);
    ui_->resolutionCombo->setEnabled(false);
    if (ui_->hflipCheck) ui_->hflipCheck->setEnabled(false);
    if (ui_->vflipCheck) ui_->vflipCheck->setEnabled(false);
    if (ui_->negativeCheck) ui_->negativeCheck->setEnabled(false);

    emit cameraDisconnected();
}

void CameraPaneController::onEnumerateCameras() {
    ui_->cameraCombo->clear();
    for (const auto& cam : ctrl_.cameraHandler().enumerateCameras()) {
        ui_->cameraCombo->addItem(cam.display_name.c_str(), cam.id.c_str());
    }
}

void CameraPaneController::onCaptureImage() {
    cv::Mat frame;
    if (ctrl_.captureImage(frame)) {
        auto* mw = qobject_cast<QMainWindow*>(parent());
        // Display on camera preview
        QPixmap pix = QPixmap::fromImage(cvMatToQImageStatic(frame));
        ui_->cameraImageLabel->setPixmap(pix.scaled(ui_->cameraImageLabel->size(),
                                                      Qt::KeepAspectRatio, Qt::FastTransformation));

        // Save capture to Documents with timestamp (imwrite async)
        QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      + QStringLiteral("/ScannerData/captures");
        QDir().mkpath(dir);
        auto now = QDateTime::currentDateTime();
        QString ts = now.toString(QStringLiteral("yyyyMMdd_HHmmss"));
        QString filename = dir + QStringLiteral("/capture_") + ts + QStringLiteral(".jpg");
        QString negDir = dir + QStringLiteral("/negative");
        QDir().mkpath(negDir);
        QString negFn = negDir + QStringLiteral("/capture_") + ts + QStringLiteral(".jpg");
        emit logMessage(QStringLiteral("捕获已保存: %1").arg(filename), QStringLiteral("INFO"));

        // Async imwrite with error logging
        QFuture<bool> saveFuture = QtConcurrent::run([frame, filename, negFn]() -> bool {
            bool ok = cv::imwrite(filename.toStdString(), frame);
            cv::Mat neg;
            cv::bitwise_not(frame, neg);
            ok = cv::imwrite(negFn.toStdString(), neg) && ok;
            return ok;
        });
        auto* saveWatcher = new QFutureWatcher<bool>(this);
        connect(saveWatcher, &QFutureWatcher<bool>::finished, this, [saveWatcher, filename]() {
            if (!saveWatcher->result())
                SPDLOG_WARN("[App] Failed to save capture: {}", filename.toStdString());
            saveWatcher->deleteLater();
        });
        saveWatcher->setFuture(saveFuture);
    }
}
