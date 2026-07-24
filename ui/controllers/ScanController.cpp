#ifdef _MSC_VER
#define NOMINMAX
#include <windows.h>
#endif

#include "ScanController.h"
#include "ui_MainWindow.h"
#include "ui/AppController.h"
#include "infra/config/ConfigManager.h"
#include "infra/report/EdgeCrop.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QtConcurrent/QtConcurrent>
#include <filesystem>
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

ScanController::ScanController(Ui::MainWindow* ui, AppController& ctrl,
                               QObject* parent)
    : QObject(parent)
    , ui_(ui)
    , ctrl_(ctrl)
{
}

void ScanController::applyCellBorder(int row, int col) {
    bool isScanning = (row == current_scan_row_ && col == current_scan_col_);
    bool isScanned = scanned_cells_.count({row, col}) > 0;

    auto* lbl = qobject_cast<QLabel*>(ui_->topCellsTable->cellWidget(row, col));
    if (lbl) {
        QString style;
        if (isScanning)
            style = QStringLiteral("border: 2px solid #0078D7;");
        else if (isScanned)
            style = QStringLiteral("border: 1px solid #28A745;");
        lbl->setStyleSheet(style);
        return;
    }
    auto* item = ui_->topCellsTable->item(row, col);
    if (item) {
        if (isScanning)
            item->setBackground(QColor(0, 120, 215, 40));
        else if (isScanned)
            item->setBackground(QColor(40, 167, 69, 25));
        else
            item->setBackground(QColor(0, 0, 0, 0));
    }
}

void ScanController::startScanSequence() {
    auto& cfg = ConfigManager::instance();
    cv::Size gs(cfg.gridSizeX(), cfg.gridSizeY());
    {
        std::lock_guard<std::mutex> lock(scan_state_mutex_);
        scan_base_dir_.clear();  // New scan starts, clear old directory
    }
    {
        std::lock_guard<std::mutex> lock(grid_thumbnails_mutex_);
        grid_thumbnails_.clear();
    }
    scanned_cells_.clear();
    current_scan_row_ = -1;
    current_scan_col_ = -1;
    {
        int gx = cfg.gridSizeX(), gy = cfg.gridSizeY();
        for (int r = 0; r < gy; ++r)
            for (int c = 0; c < gx; ++c)
                applyCellBorder(r, c);
    }

    ctrl_.movementController().setImageSaveCallback([this](const cv::Mat& frame, const std::string& path, int row, int col, int z) {
        // Track scan base directory for later stitching
        {
            std::lock_guard<std::mutex> lock(scan_state_mutex_);
            if (scan_base_dir_.empty()) scan_base_dir_ = path;
        }
        // Ensure original/ and negative/ subdirectories exist
        std::string origDir = path + "/original";
        std::string negDir  = path + "/negative";
        std::filesystem::create_directories(origDir);
        std::filesystem::create_directories(negDir);

        // Filename numbering 1-indexed; RTL table layout col=0 is on the right (scan start)
        int gx = ConfigManager::instance().gridSizeX();
        int dispRow = row + 1;
        int dispCol = col + 1;

        // Save full-resolution original (filename includes Z height for scale-aware stitching)
        std::string fn = origDir + "/" + std::to_string(dispRow) + "_" + std::to_string(dispCol) + "_" + std::to_string(z) + ".jpg";
        bool ok = cv::imwrite(fn, frame);
        // Save full-resolution negative (always)
        {
            cv::Mat negFrame;
            cv::bitwise_not(frame, negFrame);
            std::string negFn = negDir + "/" + std::to_string(dispRow) + "_" + std::to_string(dispCol) + "_" + std::to_string(z) + ".jpg";
            cv::imwrite(negFn, negFrame);
        }
        if (ok) {
            // tableCol == physical col; RTL: col=0 appears on right (scan start)
            int tableCol = col;
            {
                std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
                s_movement_images_[{row, tableCol}] = fn;
            }
            // Generate BOTH original and negative thumbnails for instant toggling
            if (row < 10 && tableCol < 10) {
                int cropSize = ConfigManager::instance().centerCropSize();
                cv::Mat cropped;
                if (cropSize > 0 && cropSize < frame.cols && cropSize < frame.rows) {
                    int gRows = ConfigManager::instance().gridSizeY();
                    int gCols = ConfigManager::instance().gridSizeX();
                    cv::Rect roi = report::computeEdgeAwareCropRoi(
                        cv::Size(frame.cols, frame.rows), cropSize,
                        row, gCols - 1 - tableCol, gRows, gCols);
                    cropped = frame(roi);
                } else {
                    cropped = frame;
                }
                // Original thumbnail
                cv::Mat thumbOrig;
                cv::resize(cropped, thumbOrig, cv::Size(50, 50), 0, 0, cv::INTER_AREA);
                QPixmap pixOrig = QPixmap::fromImage(cvMatToQImageStatic(thumbOrig));
                // Negative thumbnail
                cv::Mat thumbNeg;
                cv::bitwise_not(thumbOrig, thumbNeg);
                QPixmap pixNeg = QPixmap::fromImage(cvMatToQImageStatic(thumbNeg));
                // Store both
                {
                    std::lock_guard<std::mutex> lock(grid_thumbnails_mutex_);
                    grid_thumbnails_[{row, tableCol}] = {pixOrig, pixNeg};
                }
                QPixmap pix = scan_showing_negative_ ? pixNeg : pixOrig;
                QMetaObject::invokeMethod(this, [this, row, tableCol, pix]() {
                    auto* lbl = new QLabel();
                    lbl->setPixmap(pix);
                    lbl->setScaledContents(true);
                    lbl->setContentsMargins(0, 0, 0, 0);
                    lbl->setAlignment(Qt::AlignCenter);
                    lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                    ui_->topCellsTable->setCellWidget(row, tableCol, lbl);
                    // Thumbnail set + border mark atomically
                    scanned_cells_.insert({row, tableCol});
                    applyCellBorder(row, tableCol);
                }, Qt::QueuedConnection);
            }
        }
        return ok;
    });

    if (ctrl_.startSMovement(gs, cfg.stepSize(), cfg.zHeight())) {
        // Freeze preview during scan — captureTriggerFrame manages stream internally
        scanning_ = true;
        scan_stopped_by_user_ = false;
        ui_->cameraImageLabel->clear();
        ui_->cameraImageLabel->setText(QStringLiteral("扫描中..."));
        ui_->quickScanBtn->setText(QStringLiteral("停止扫描"));
        ui_->quickPauseBtn->setEnabled(true);
        ui_->quickPauseBtn->setText(QStringLiteral("暂停扫描"));
        emit logMessage(QStringLiteral("扫描已启动"), QStringLiteral("INFO"));
        emit scanStarted();
    }
}

void ScanController::onToggleScanStartStop() {
    if (ctrl_.movementController().getStatus().running) {
        scan_stopped_by_user_ = true;
        ctrl_.stopSMovement();
        ui_->quickScanBtn->setText(QStringLiteral("开始扫描"));
        ui_->quickPauseBtn->setEnabled(false);
        ui_->quickPauseBtn->setText(QStringLiteral("暂停扫描"));
        return;
    }

    if (!ctrl_.cameraHandler().isConnected()) {
        QMessageBox::warning(qobject_cast<QWidget*>(parent()), QStringLiteral("警告"),
                             QStringLiteral("请先连接相机"));
        return;
    }
    if (!ctrl_.armController().isConnected()) {
        QMessageBox::warning(qobject_cast<QWidget*>(parent()), QStringLiteral("警告"),
                             QStringLiteral("请先连接机械臂"));
        return;
    }

    // Check if arm is already at zero
    // We check via ArmPaneController's isArmAtZero equivalent
    auto& armCtrl = ctrl_.armController();
    bool atZero = true;
    for (int i = 0; i < 5; ++i) {
        if (std::abs(armCtrl.readPosition(i)) > 200.0) {
            atZero = false;
            break;
        }
    }

    if (atZero) {
        // Already zeroed — start scanning immediately
        ctrl_.startCameraCapture();
        startScanSequence();
        return;
    }

    // Not at zero — run zero sequence, then stop (user clicks scan again)
    ctrl_.startCameraCapture();

    ui_->quickScanBtn->setEnabled(false);
    ui_->quickScanBtn->setText(QStringLiteral("正在归零..."));

    auto* progress = new QProgressDialog(QStringLiteral("机械臂未归零，正在归零中...\n归零完成后请再次点击扫描"), QString(), 0, 5,
                                         qobject_cast<QWidget*>(parent()));
    progress->setWindowTitle(QStringLiteral("归零中"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    progress->setCancelButton(nullptr);
    progress->show();

    auto* ctrl = &ctrl_;
    QFuture<bool> zeroFuture = QtConcurrent::run([ctrl, progress]() {
        auto& arm = ctrl->armController();
        QMetaObject::invokeMethod(progress, "setLabelText", Qt::QueuedConnection,
            Q_ARG(QString, QStringLiteral("正在归零所有轴...")));
        QMetaObject::invokeMethod(progress, "setValue", Qt::QueuedConnection,
            Q_ARG(int, 1));
        arm.moveAxesConcurrent(0, 0, 0);
        QMetaObject::invokeMethod(progress, "setValue", Qt::QueuedConnection,
            Q_ARG(int, 5));
        return true;
    });

    // Connect the zero progress watcher - use a one-shot connection
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, progress]() {
        progress->close();
        progress->deleteLater();
        watcher->deleteLater();

        ui_->quickScanBtn->setEnabled(true);
        ui_->quickScanBtn->setText(QStringLiteral("开始扫描"));
        emit logMessage(QStringLiteral("归零完成，请再次点击扫描"), QStringLiteral("INFO"));
        QMessageBox::information(qobject_cast<QWidget*>(parent()), QStringLiteral("归零完成"),
                                 QStringLiteral("归零完成，请重新点击开始扫描"));
    });
    watcher->setFuture(zeroFuture);
}

void ScanController::onZeroProgressFinished() {
    // This slot is connected from ArmPaneController's zeroSequenceComplete signal.
    // Placeholder for any scan-specific zero completion logic.
}

void ScanController::onTogglePauseScan() {
    if (ctrl_.movementController().getStatus().paused) {
        ctrl_.resumeSMovement();
        ui_->quickPauseBtn->setText(QStringLiteral("暂停扫描"));
    } else {
        ctrl_.pauseSMovement();
        ui_->quickPauseBtn->setText(QStringLiteral("继续扫描"));
    }
}

void ScanController::onMovementStatus(const arm::SMovementStatus& status) {
    // Scan grid progress highlighting
    if (status.running) {
        int row = status.current_position.row;
        int col = status.current_position.col;
        if (row >= 0 && col >= 0) {
            if (current_scan_row_ >= 0 && current_scan_col_ >= 0
                && (current_scan_row_ != row || current_scan_col_ != col)) {
                scanned_cells_.insert({current_scan_row_, current_scan_col_});
                applyCellBorder(current_scan_row_, current_scan_col_);
            }
            current_scan_row_ = row;
            current_scan_col_ = col;
            applyCellBorder(row, col);
        }
    }
    if (!status.running && !status.paused && current_scan_row_ >= 0) {
        scanned_cells_.insert({current_scan_row_, current_scan_col_});
        applyCellBorder(current_scan_row_, current_scan_col_);
        current_scan_row_ = -1;
        current_scan_col_ = -1;
    }

    // Throttle per-point updates: only log meaningful status changes
    if (!status.status_message.empty()
        && status.status_message.find("Moving") == std::string::npos
        && status.status_message.find("Arrived") == std::string::npos) {
        emit logMessage(QString::fromStdString(status.status_message), QStringLiteral("INFO"));
    }
    // Throttle progress: update every 5 points or at completion
    if (status.total_points > 0
        && (status.current_point % 5 == 0 || status.current_point >= status.total_points)) {
        emit logMessage(QStringLiteral("采集进度: %1/%2").arg(status.current_point).arg(status.total_points), QStringLiteral("INFO"));
    }

    // Track whether the scan was actually running
    if (status.running) scan_was_running_ = true;

    // Reset UI when S-movement completes or is stopped (only if it was running)
    if (!status.running && !status.paused && scan_was_running_) {
        scan_was_running_ = false;
        scanning_ = false;
        if (ctrl_.cameraHandler().isConnected()) {
            ui_->cameraImageLabel->clear();
        }
        ui_->quickScanBtn->setText(QStringLiteral("开始扫描"));
        ui_->quickPauseBtn->setEnabled(false);
        ui_->quickPauseBtn->setText(QStringLiteral("暂停扫描"));

        if (scan_stopped_by_user_) {
            scan_stopped_by_user_ = false;
            emit logMessage(QStringLiteral("S型扫描已停止"), QStringLiteral("WARN"));
            ui_->statusLabel->setText(QStringLiteral("扫描已停止"));
            ui_->scanNegativeCheck->setEnabled(true);
            emit scanStopped();
        } else {
            emit logMessage(QStringLiteral("S型扫描完成"), QStringLiteral("INFO"));
            ui_->statusLabel->setText(QStringLiteral("扫描完成"));
            QMessageBox::information(qobject_cast<QWidget*>(parent()), QStringLiteral("扫描完成"),
                                     QStringLiteral("扫描已完成！"));
            ui_->scanNegativeCheck->setEnabled(true);
            emit scanFinished();
        }
    }
}
