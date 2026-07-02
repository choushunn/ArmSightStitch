#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/AppController.h"
#include "ui/DetectionSettingsDialog.h"
#include "ui/StitchingSettingsDialog.h"
#include "infra/config/ConfigManager.h"

#include <spdlog/spdlog.h>
#include <QEvent>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QStatusBar>
#include <QHeaderView>
#include <QDateTime>
#include <QTimer>
#include <QDir>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <filesystem>

MainWindow::MainWindow(AppController& ctrl, QWidget *parent)
    : QMainWindow(parent)
    , ctrl_(ctrl)
    , ui_(new Ui::MainWindow)
{
    ui_->setupUi(this);

    // Main splitter ratio: sidebar (1) : work area (5.4)
    ui_->mainSplitter->setStretchFactor(0, 10);
    ui_->mainSplitter->setStretchFactor(1, 27);

    // Work splitter ratio: table (2) : right preview (1)
    ui_->workSplitter->setStretchFactor(0, 2);
    ui_->workSplitter->setStretchFactor(1, 1);

    // Camera row: dropdown (3) : connect button (2)
    ui_->cameraConnTopRow->setStretch(0, 3);
    ui_->cameraConnTopRow->setStretch(1, 2);

    // Position buttons: evenly distributed
    ui_->posBtnLayout->setStretch(0, 1);
    ui_->posBtnLayout->setStretch(1, 1);
    ui_->posBtnLayout->setStretch(2, 1);

    camera_update_timer_ = new QTimer(this);
    connect(camera_update_timer_, &QTimer::timeout, this, &MainWindow::updateCameraImage);
    connect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished, this, &MainWindow::onStitchingFinished);
    connect(&detection_watcher_, &QFutureWatcher<DetectionResult>::finished, this, &MainWindow::onDetectionFinished);
    connect(&arm_connect_watcher_, &QFutureWatcher<bool>::finished, this, &MainWindow::onArmConnectFinished);
    connect(&arm_move_watcher_, &QFutureWatcher<void>::finished, this, &MainWindow::onArmMoveFinished);

    // Reference log panel from .ui (placed in left sidebar's hardware section)
    status_log_edit_ = ui_->statusLogEdit;

    initGridTables();
    setupMenuNavigation();
    connectSignals();

    // Wire module callbacks
    ctrl_.cameraHandler().setStatusCallback([this](const camera::CameraStatus&) {});
    ctrl_.armController().setStatusCallback([this](const arm::ArmStatus& s) {
        QMetaObject::invokeMethod(this, "onArmStatusChanged", Q_ARG(const arm::ArmStatus&, s));
    });
    ctrl_.movementController().setStatusCallback([this](const arm::SMovementStatus& s) {
        QMetaObject::invokeMethod(this, "onMovementStatus", Q_ARG(const arm::SMovementStatus&, s));
    });
    ctrl_.stitcher().setProgressCallback([this](int cur, int total) {
        appendLog(QString("拼接进度: %1/%2").arg(cur).arg(total), "INFO");
    });
    ctrl_.stitcher().setStatusCallback([this](const std::string& msg) {
        appendLog(QString::fromStdString(msg), "INFO");
    });

    // Auto-scan cameras on startup
    on_enumerateCameras();

    appendLog("Application initialized", "INFO");
    SPDLOG_INFO("MainWindow initialized");
}

MainWindow::~MainWindow() {
    camera_update_timer_->stop();
    ctrl_.stopCameraCapture();
    delete ui_;
}

void MainWindow::setupMenuNavigation() {
    // Set initial state
    ui_->controlStack->setCurrentIndex(0);
}

void MainWindow::onMenuButtonClicked(int id) {
    ui_->controlStack->setCurrentIndex(id);
    QString pageNames[] = {"扫描", "拼接", "检测"};
    if (id >= 0 && id < 3) {
        ui_->statusLabel->setText(QString("当前页面: %1").arg(pageNames[id]));
    }
}

void MainWindow::connectSignals() {
    // ---- 文件 ----
    connect(ui_->actionOpenImage, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "打开图像", "", "图像 (*.jpg *.png *.bmp)");
        if (!path.isEmpty()) {
            current_image_ = cv::imread(path.toStdString());
            if (!current_image_.empty()) displayImage(current_image_, ui_->cameraImageLabel);
        }
    });
    connect(ui_->actionSaveStitch, &QAction::triggered, this, &MainWindow::on_saveStitch);
    connect(ui_->actionImportConfig, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "导入配置", "", "JSON (*.json)");
        if (!path.isEmpty()) {
            ConfigManager::instance().loadFromFile(path.toStdString());
            ui_->statusLabel->setText("配置已导入: " + path);
        }
    });
    connect(ui_->actionExportConfig, &QAction::triggered, this, [this]() {
        ConfigManager::instance().saveToFile("config_export.json");
        ui_->statusLabel->setText("配置已导出");
    });
    connect(ui_->actionExit, &QAction::triggered, this, &QWidget::close);

    // ---- 视图: 模式切换 ----
    connect(ui_->actionViewScan, &QAction::triggered, this, [this]() { onMenuButtonClicked(0); });
    connect(ui_->actionViewStitch, &QAction::triggered, this, [this]() { onMenuButtonClicked(1); });
    connect(ui_->actionViewDetect, &QAction::triggered, this, [this]() { onMenuButtonClicked(2); });
    connect(ui_->actionToggleSidebar, &QAction::toggled, this, [this](bool visible) { ui_->menuPanel->setVisible(visible); });
    connect(ui_->actionToggleLog, &QAction::toggled, this, [this](bool visible) { ui_->statusLogEdit->setVisible(visible); });

    // ---- 设置 ----
    connect(ui_->actionDetSettings, &QAction::triggered, this, &MainWindow::on_detSettings);
    connect(ui_->actionStitchSettings, &QAction::triggered, this, &MainWindow::on_stitchSettings);
    connect(ui_->actionArmZero, &QAction::triggered, this, &MainWindow::on_zeroArm);
    connect(ui_->actionLoadModel, &QAction::triggered, this, &MainWindow::on_loadModel);

    // ---- 帮助 ----
    connect(ui_->actionUserGuide, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "使用说明",
            "ArmSightStitch 明场显微成像验证组件\n\n"
            "工作流程:\n"
            "1. 连接相机和机械臂 (硬件连接区)\n"
            "2. 在扫描模式下设置位置参数，执行S型扫描采集图像\n"
            "3. 切换到拼接模式，执行图像拼接并保存结果\n"
            "4. 切换到检测模式，加载模型执行目标检测\n\n"
            "快捷键:\n"
            "F5 - 开始/停止扫描  F6 - 暂停/继续\n"
            "Ctrl+1/2/3 - 切换扫描/拼接/检测模式\n"
            "Ctrl+P - 拍照  Ctrl+O - 打开图像  Ctrl+S - 保存结果");
    });
    connect(ui_->actionAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "关于 ArmSightStitch",
            "ArmSightStitch v2.0\n\n"
            "明场显微成像验证组件\n"
            "基于机械臂Modbus TCP控制\n"
            "支持YOLO目标检测与图像拼接");
    });

    // ---- 扫描页面: 相机 ----
    connect(ui_->scanCamerasButton, &QPushButton::clicked, this, &MainWindow::on_enumerateCameras);
    connect(ui_->cameraToggleButton, &QPushButton::clicked, this, [this]() {
        if (ctrl_.cameraHandler().isConnected()) {
            on_disconnectCamera();
        } else {
            on_connectCamera();
        }
    });
    connect(ui_->captureImageButton, &QPushButton::clicked, this, &MainWindow::on_captureImage);

    // ---- 相机曝光控制 ----
    connect(ui_->autoExposureCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s) {
        bool auto_on = (s == Qt::Checked);
        ctrl_.cameraHandler().setAutoExposure(auto_on);
        ui_->exposureSlider->setEnabled(!auto_on);
        ui_->exposureValueLabel->setEnabled(!auto_on);
        if (auto_on) {
            // When switching to auto, immediately sync real exposure value from camera hardware
            float expo = ctrl_.cameraHandler().getRealExposure();
            int slider_val = qBound(ui_->exposureSlider->minimum(),
                                    static_cast<int>(expo * 100.0f),
                                    ui_->exposureSlider->maximum());
            ui_->exposureSlider->setValue(slider_val);
            ui_->exposureValueLabel->setText(QString::number(expo, 'f', 2) + " ms");
        }
    });
    connect(ui_->exposureSlider, &QSlider::valueChanged, this, [this](int val) {
        float exposure = static_cast<float>(val) / 100.0f;
        ui_->exposureValueLabel->setText(QString::number(exposure, 'f', 2) + " ms");
        // Only apply to camera when not in auto-exposure mode, to avoid overriding auto exposure
        if (!ctrl_.cameraHandler().getAutoExposure()) {
            ctrl_.cameraHandler().setExposure(exposure);
        }
    });

    // ---- 相机旋转控制 ----
    connect(ui_->rotationCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        int degrees = idx * 90;
        ctrl_.cameraHandler().setRotation(degrees);
    });

    // ---- 相机分辨率控制 ----
    connect(ui_->resolutionCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx < 0) return;
        QVariant data = ui_->resolutionCombo->currentData();
        if (!data.isValid()) return;
        QSize res = data.toSize();
        ctrl_.cameraHandler().setResolution(res.width(), res.height());
    });

    // ---- Quick toolbar ----
    connect(ui_->quickCaptureBtn, &QPushButton::clicked, this, &MainWindow::on_captureImage);
    connect(ui_->quickScanBtn, &QPushButton::clicked, this, &MainWindow::on_toggleStartStopSMovement);
    connect(ui_->quickPauseBtn, &QPushButton::clicked, this, &MainWindow::on_togglePauseSMovement);
    connect(ui_->quickStitchBtn, &QPushButton::clicked, this, &MainWindow::on_stitchRun);
    connect(ui_->quickSaveBtn, &QPushButton::clicked, this, &MainWindow::on_saveStitch);
    connect(ui_->quickDetectBtn, &QPushButton::clicked, this, [this]() {
        if (!current_image_.empty()) {
            auto dets = ctrl_.detect(current_image_);
            displayImage(ctrl_.drawDetections(current_image_, dets), ui_->detectImageLabel);
        } else {
            QMessageBox::warning(this, "警告", "请先打开或拍摄一张图像");
        }
    });
    connect(ui_->enableDetectionCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s) {
        image_detection_enabled_ = (s == Qt::Checked);
    });

    // ---- 扫描页面: 机械臂 ----
    connect(ui_->armToggleButton, &QPushButton::clicked, this, [this]() {
        if (ctrl_.armController().isConnected()) {
            on_disconnectArm();
        } else {
            on_connectArm();
        }
    });
    connect(ui_->moveToPosButton, &QPushButton::clicked, this, &MainWindow::on_moveToPosition);
    connect(ui_->readPosButton, &QPushButton::clicked, this, &MainWindow::on_readPosition);
    connect(ui_->zeroButton, &QPushButton::clicked, this, &MainWindow::on_zeroArm);

    // ---- 扫描页面: 网格 ----
    connect(ui_->topCellsTable, &QTableWidget::cellClicked, this, &MainWindow::on_topCellsTable_cellClicked);

    // ---- 拼接页面 ----
    connect(ui_->stitchRunButton, &QPushButton::clicked, this, &MainWindow::on_stitchRun);
    connect(ui_->stitchSettingsButton, &QPushButton::clicked, this, &MainWindow::on_stitchSettings);
    connect(ui_->stitchSaveButton, &QPushButton::clicked, this, &MainWindow::on_saveStitch);

    // ---- 拼接预览: 双击全屏 ----
    ui_->stitchImageLabel->installEventFilter(this);

    // ---- 检测页面 ----
    connect(ui_->detectModelButton, &QPushButton::clicked, this, &MainWindow::on_loadModel);
    connect(ui_->detectSettingsButton, &QPushButton::clicked, this, &MainWindow::on_detSettings);
    connect(ui_->detectRunButton, &QPushButton::clicked, this, [this]() {
        if (!current_image_.empty()) {
            auto dets = ctrl_.detect(current_image_);
            displayImageFullQuality(ctrl_.drawDetections(current_image_, dets), ui_->detectImageLabel, detected_pixmap_);
        } else {
            QMessageBox::warning(this, "警告", "请先打开或拍摄一张图像");
        }
    });

    // ---- 检测预览: 双击全屏 ----
    ui_->detectImageLabel->installEventFilter(this);

    // ---- AppController signals ----
    connect(&ctrl_, &AppController::statusMessage, this, [this](const QString& msg) {
        ui_->statusLabel->setText(msg);
    });
    connect(&ctrl_, &AppController::errorMessage, this, [this](const QString& msg) {
        ui_->statusLabel->setText("错误: " + msg);
        QMessageBox::critical(this, "错误", msg);
    });
    connect(&ctrl_, &AppController::stitchingFinished, this, [this](const cv::Mat& result) {
        stitched_result_ = result;
        displayImageFullQuality(result, ui_->stitchImageLabel, stitched_pixmap_);
    });
}

void MainWindow::initGridTables() {
    int gx = ConfigManager::instance().gridSizeX();
    int gy = ConfigManager::instance().gridSizeY();

    QStringList headers;
    for (int i = 1; i <= gx; ++i) headers << QString::number(i);

    ui_->topCellsTable->setRowCount(gy);
    ui_->topCellsTable->setColumnCount(gx);
    ui_->topCellsTable->setHorizontalHeaderLabels(headers);
    ui_->topCellsTable->setVerticalHeaderLabels(headers);
    ui_->topCellsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui_->topCellsTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int r = 0; r < gy; ++r) {
        for (int c = 0; c < gx; ++c) {
            auto* ti = new QTableWidgetItem("");
            ti->setTextAlignment(Qt::AlignCenter);
            ui_->topCellsTable->setItem(r, c, ti);
        }
    }
}

void MainWindow::appendLog(const QString& msg, const QString& level) {
    QString color;
    if (level == "ERROR") color = "#D9534F";     // 工业红
    else if (level == "WARN") color = "#F5A623";  // 安全黄
    else if (level == "DEBUG") color = "#5A5D63"; // 禁用灰
    else color = "#8B8E94";                       // 次要文字灰

    QString prefix;
    if (level == "ERROR") prefix = "[ERR] ";
    else if (level == "WARN") prefix = "[WRN] ";
    else if (level == "DEBUG") prefix = "[DBG] ";
    else prefix = "[INF] ";

    status_log_edit_->append(
        QString("<span style='color:%1;'>%2%3</span>").arg(color, prefix, msg.toHtmlEscaped()));
}

// ==================== Camera ====================

void MainWindow::on_connectCamera() {
    QString id = ui_->cameraCombo->currentData().toString();
    if (ctrl_.connectCamera(id.toStdString())) {
        ui_->cameraToggleButton->setText("断开");
        ui_->captureImageButton->setEnabled(true);
        // Clear placeholder text so preview area shows immediately
        ui_->cameraImageLabel->setText({});
        ctrl_.startCameraCapture();
        camera_update_timer_->start(33);
        // Grab first frame immediately to avoid initial blank delay
        updateCameraImage();
        ui_->statusLabel->setText("相机已连接");

        // Sync exposure UI state from camera
        bool auto_on = ctrl_.cameraHandler().getAutoExposure();
        ui_->autoExposureCheck->setChecked(auto_on);
        auto& slider = ui_->exposureSlider;
        auto& label = ui_->exposureValueLabel;

        // Configure slider range from camera's actual exposure range (SDK: Toupcam_get_ExpTimeRange)
        float expo_min_ms = 0.01f, expo_max_ms = 100.0f, expo_def_ms = 10.0f;
        ctrl_.cameraHandler().getExposureRange(expo_min_ms, expo_max_ms, expo_def_ms);
        slider->setMinimum(qMax(1, static_cast<int>(expo_min_ms * 100.0f)));
        slider->setMaximum(static_cast<int>(expo_max_ms * 100.0f));

        // Set current value from camera (use RealExpoTime for actual hardware value)
        float expo = auto_on ? ctrl_.cameraHandler().getRealExposure()
                             : ctrl_.cameraHandler().getExposure();
        int slider_val = qBound(slider->minimum(), static_cast<int>(expo * 100.0f), slider->maximum());
        slider->setValue(slider_val);
        label->setText(QString::number(expo, 'f', 2) + " ms");
        slider->setEnabled(!auto_on);
        label->setEnabled(!auto_on);
        ui_->autoExposureCheck->setEnabled(true);

        // Sync rotation state from camera
        int rot = ctrl_.cameraHandler().getRotation();
        ui_->rotationCombo->setCurrentIndex(rot / 90);
        ui_->rotationCombo->setEnabled(true);

        // Populate resolution combo from camera's supported resolutions
        auto* res_combo = ui_->resolutionCombo;
        res_combo->clear();
        res_combo->setEnabled(true);
        auto resolutions = ctrl_.cameraHandler().getSupportedResolutions();
        int cur_w = 0, cur_h = 0;
        ctrl_.cameraHandler().getResolution(cur_w, cur_h);
        int select_idx = 0;
        for (size_t i = 0; i < resolutions.size(); ++i) {
            auto [w, h] = resolutions[i];
            QString label = QString("%1x%2").arg(w).arg(h);
            res_combo->addItem(label, QVariant(QSize(w, h)));
            if (w == cur_w && h == cur_h) {
                select_idx = static_cast<int>(i);
            }
        }
        res_combo->setCurrentIndex(select_idx);
    }
}

void MainWindow::on_disconnectCamera() {
    camera_update_timer_->stop();
    ctrl_.stopCameraCapture();
    ctrl_.disconnectCamera();
    ui_->cameraToggleButton->setText("连接");
    ui_->captureImageButton->setEnabled(false);
    ui_->cameraImageLabel->setText("相机预览");
    ui_->cameraImageLabel->setPixmap({});
    ui_->statusLabel->setText("相机已断开");
    ui_->autoExposureCheck->setEnabled(false);
    ui_->exposureSlider->setEnabled(false);
    ui_->exposureValueLabel->setEnabled(false);
    ui_->rotationCombo->setEnabled(false);
    ui_->resolutionCombo->setEnabled(false);
}

void MainWindow::on_enumerateCameras() {
    ui_->cameraCombo->clear();
    for (const auto& cam : ctrl_.cameraHandler().enumerateCameras()) {
        ui_->cameraCombo->addItem(cam.display_name.c_str(), cam.id.c_str());
    }
}

void MainWindow::on_captureImage() {
    cv::Mat frame;
    if (ctrl_.captureImage(frame)) {
        current_image_ = frame;
        displayImage(frame, ui_->cameraImageLabel);
    }
}

// ==================== Arm ====================

void MainWindow::on_connectArm() {
    std::string ip = ui_->armIpEdit->text().toStdString();
    int port = ui_->armPortSpin->value();
    ui_->armToggleButton->setEnabled(false);
    ui_->armStatusLabel->setText("连接中...");
    ui_->armStatusLabel->setProperty("connStatus", "connecting");
    ui_->armStatusLabel->style()->unpolish(ui_->armStatusLabel);
    ui_->armStatusLabel->style()->polish(ui_->armStatusLabel);
    auto* ctrl = &ctrl_;
    arm_connect_future_ = QtConcurrent::run([ctrl, ip, port]() -> bool {
        return ctrl->connectArm(ip, port);
    });
    arm_connect_watcher_.setFuture(arm_connect_future_);
}

void MainWindow::onArmConnectFinished() {
    ui_->armToggleButton->setEnabled(true);
    if (arm_connect_future_.result()) {
        ui_->armToggleButton->setText("断开");
        ui_->armStatusLabel->setText("已连接");
        ui_->armStatusLabel->setProperty("connStatus", "connected");
    } else {
        ui_->armStatusLabel->setText("连接失败");
        ui_->armStatusLabel->setProperty("connStatus", "disconnected");
    }
    ui_->armStatusLabel->style()->unpolish(ui_->armStatusLabel);
    ui_->armStatusLabel->style()->polish(ui_->armStatusLabel);
}

void MainWindow::on_disconnectArm() {
    ctrl_.disconnectArm();
    ui_->armToggleButton->setText("连接");
    ui_->armStatusLabel->setText("未连接");
    ui_->armStatusLabel->setProperty("connStatus", "disconnected");
    ui_->armStatusLabel->style()->unpolish(ui_->armStatusLabel);
    ui_->armStatusLabel->style()->polish(ui_->armStatusLabel);
}

void MainWindow::on_moveToPosition() {
    ui_->moveToPosButton->setEnabled(false);
    auto* ctrl = &ctrl_;
    int x = static_cast<int>(ui_->xPosSpin->value());
    int y = static_cast<int>(ui_->yPosSpin->value());
    int z = static_cast<int>(ui_->zPosSpin->value());
    arm_move_future_ = QtConcurrent::run([ctrl, x, y, z]() {
        ctrl->armController().moveToPosition(0, x);
        ctrl->armController().moveToPosition(1, y);
        ctrl->armController().moveToPosition(2, z);
        ctrl->armController().moveToPosition(3, 0);
        ctrl->armController().moveToPosition(4, 0);
    });
    arm_move_watcher_.setFuture(arm_move_future_);
}

void MainWindow::onArmMoveFinished() {
    ui_->moveToPosButton->setEnabled(true);
    ui_->zeroButton->setEnabled(true);
}

void MainWindow::on_readPosition() {
    auto& arm = ctrl_.armController();
    ui_->xPosSpin->setValue(arm.readPosition(0));
    ui_->yPosSpin->setValue(arm.readPosition(1));
    ui_->zPosSpin->setValue(arm.readPosition(2));
}

void MainWindow::on_zeroArm() {
    if (!ctrl_.armController().isConnected()) {
        QMessageBox::warning(this, "警告", "机械臂未连接");
        return;
    }
    ui_->zeroButton->setEnabled(false);
    auto* ctrl = &ctrl_;
    arm_move_future_ = QtConcurrent::run([ctrl]() {
        for (int i = 0; i < 5; ++i) ctrl->armController().moveToPosition(i, 0);
    });
    arm_move_watcher_.setFuture(arm_move_future_);
    ui_->xPosSpin->setValue(0);
    ui_->yPosSpin->setValue(0);
    ui_->zPosSpin->setValue(0);
}

// ==================== S-Movement ====================

void MainWindow::on_toggleStartStopSMovement() {
    if (ctrl_.movementController().getStatus().running) {
        ctrl_.stopSMovement();
        ui_->quickScanBtn->setText("开始扫描");
        ui_->quickPauseBtn->setEnabled(false);
        ui_->quickPauseBtn->setText("暂停");
        return;
    }

    if (!ctrl_.armController().isConnected() || !ctrl_.cameraHandler().isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接机械臂和相机");
        return;
    }
    ctrl_.startCameraCapture();

    auto& cfg = ConfigManager::instance();
    cv::Size gs(cfg.gridSizeX(), cfg.gridSizeY());

    ctrl_.movementController().setImageSaveCallback([this](const cv::Mat& frame, const std::string& path, int row, int col) {
        std::string fn = path + "/" + std::to_string(row) + "_" + std::to_string(col) + ".jpg";
        bool ok = cv::imwrite(fn, frame);
        if (ok) {
            s_movement_images_[{row, col}] = fn;
            QMetaObject::invokeMethod(this, [this, frame, row, col]() {
                if (row < 10 && col < 10) {
                    auto* item = ui_->topCellsTable->item(row, col);
                    if (item) {
                        item->setBackground(QColor(144, 238, 144));
                        auto* lbl = new QLabel();
                        lbl->setPixmap(QPixmap::fromImage(cvMatToQImage(frame))
                            .scaled(50, 50, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                        lbl->setAlignment(Qt::AlignCenter);
                        ui_->topCellsTable->setCellWidget(row, col, lbl);
                    }
                }
            }, Qt::QueuedConnection);
        }
        return ok;
    });

    if (ctrl_.startSMovement(gs, cfg.stepSize(), cfg.zHeight())) {
        ui_->quickScanBtn->setText("停止扫描");
        ui_->quickPauseBtn->setEnabled(true);
        ui_->quickPauseBtn->setText("暂停");
        appendLog("S-movement started", "INFO");
    }
}

void MainWindow::on_togglePauseSMovement() {
    if (ctrl_.movementController().getStatus().paused) {
        ctrl_.resumeSMovement();
        ui_->quickPauseBtn->setText("暂停");
    } else {
        ctrl_.pauseSMovement();
        ui_->quickPauseBtn->setText("继续");
    }
}

// ==================== Detection ====================

void MainWindow::on_loadModel() {
    auto& cfg = ConfigManager::instance();
    DetectionSettingsDialog dlg(this);
    dlg.setParamPath(QString::fromStdString(cfg.modelParamPath()));
    dlg.setBinPath(QString::fromStdString(cfg.modelBinPath()));
    dlg.setConfidenceThreshold(0.3);
    dlg.setNmsThreshold(0.3);

    if (dlg.exec() == QDialog::Accepted) {
        model_loaded_ = ctrl_.loadDetectorModel(
            dlg.paramPath().toStdString(), dlg.binPath().toStdString());
        if (model_loaded_) {
            ui_->statusLabel->setText("模型已加载");
            appendLog("Detection model loaded", "INFO");
        }
    }
}

void MainWindow::on_detSettings() {
    DetectionSettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        ctrl_.detector().setConfidenceThreshold(dlg.confidenceThreshold());
        ctrl_.detector().setNmsThreshold(dlg.nmsThreshold());
    }
}

// ==================== Stitching ====================

void MainWindow::on_stitchRun() {
    auto& cfg = ConfigManager::instance();
    std::string dir = cfg.imageSaveBasePath();
    cv::Size gs(cfg.gridSizeX(), cfg.gridSizeY());

    auto images = ctrl_.loadImages(dir);
    if (images.empty()) {
        QMessageBox::warning(this, "警告", "没有图像可拼接，请先在设置中指定目录");
        return;
    }

    ui_->stitchRunButton->setEnabled(false);
    stitching_future_ = QtConcurrent::run([this, images, gs]() {
        auto sorted = ctrl_.stitcher().sortImagesInSCurveOrder(images, gs);
        return ctrl_.stitcher().stitchImages(sorted, gs);
    });
    stitching_watcher_.setFuture(stitching_future_);
}

void MainWindow::onStitchingFinished() {
    ui_->stitchRunButton->setEnabled(true);
    cv::Mat result = stitching_future_.result();
    if (!result.empty()) {
        stitched_result_ = result;
        displayImage(result, ui_->stitchImageLabel);
        ui_->statusLabel->setText("拼接完成");
    }
}

void MainWindow::on_stitchSettings() {
    auto& cfg = ConfigManager::instance();
    StitchingSettingsDialog dlg(this);
    dlg.setGridSizeX(cfg.gridSizeX());
    dlg.setGridSizeY(cfg.gridSizeY());
    dlg.setInputDir(QString::fromStdString(cfg.imageSaveBasePath()));
    if (dlg.exec() == QDialog::Accepted) {
        cfg.setGridSizeX(dlg.gridSizeX());
        cfg.setGridSizeY(dlg.gridSizeY());
        cfg.setImageSaveBasePath(dlg.inputDir().toStdString());
    }
}

void MainWindow::on_saveStitch() {
    if (stitched_result_.empty()) {
        QMessageBox::warning(this, "警告", "没有拼接结果");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "保存拼接结果", "./stitched.jpg",
        "JPEG (*.jpg);;PNG (*.png)");
    if (!path.isEmpty()) cv::imwrite(path.toStdString(), stitched_result_);
}

// ==================== Timer ====================

void MainWindow::updateCameraImage() {
    cv::Mat frame;
    if (ctrl_.captureImage(frame)) {
        current_image_ = frame;

        // Calculate FPS
        fps_frame_count_++;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - last_fps_timestamp_ >= 1000) {
            current_fps_ = fps_frame_count_ * 1000.0f / (now - last_fps_timestamp_);
            fps_frame_count_ = 0;
            last_fps_timestamp_ = now;
        }
        if (last_fps_timestamp_ == 0) {
            last_fps_timestamp_ = now;
        }

        // When auto exposure is on, sync slider and label with camera's actual hardware value
        if (ctrl_.cameraHandler().getAutoExposure()) {
            float expo = ctrl_.cameraHandler().getRealExposure();
            int slider_val = qBound(ui_->exposureSlider->minimum(),
                                    static_cast<int>(expo * 100.0f),
                                    ui_->exposureSlider->maximum());
            ui_->exposureSlider->setValue(slider_val);
            ui_->exposureValueLabel->setText(QString::number(expo, 'f', 2) + " ms");
        }

        if (image_detection_enabled_ && model_loaded_ && !detection_busy_) {
            detection_busy_ = true;
            cv::Mat frame_with_overlay = frame.clone();
            addCameraOverlay(frame_with_overlay);
            auto* ctrl = &ctrl_;
            detection_future_ = QtConcurrent::run([ctrl, frame]() -> DetectionResult {
                DetectionResult result;
                result.frame = frame;
                result.detections = ctrl->detect(frame);
                return result;
            });
            detection_watcher_.setFuture(detection_future_);
            displayImage(frame_with_overlay, ui_->cameraImageLabel);
        } else if (!image_detection_enabled_ || !model_loaded_) {
            addCameraOverlay(frame);
            displayImage(frame, ui_->cameraImageLabel);
        }
    }
}

void MainWindow::onDetectionFinished() {
    detection_busy_ = false;
    DetectionResult result = detection_future_.result();
    if (!result.frame.empty()) {
        displayImage(ctrl_.drawDetections(result.frame, result.detections), ui_->cameraImageLabel);
    }
}

// ==================== Callbacks ====================

void MainWindow::onArmStatusChanged(const arm::ArmStatus& status) {
    if (status.current_positions.size() >= 3) {
        ui_->xPosSpin->setValue(status.current_positions[0]);
        ui_->yPosSpin->setValue(status.current_positions[1]);
        ui_->zPosSpin->setValue(status.current_positions[2]);
    }
    ui_->armStatusLabel->setText(QString::fromStdString(status.status_message));
}

void MainWindow::onMovementStatus(const arm::SMovementStatus& status) {
    appendLog(QString::fromStdString(status.status_message), "INFO");
    if (status.total_points > 0) {
        appendLog(QString("采集进度: %1/%2").arg(status.current_point).arg(status.total_points), "INFO");
    }
}

// ==================== Grid ====================

void MainWindow::on_topCellsTable_cellClicked(int row, int column) {
    // Navigate arm to this cell position
    if (ctrl_.armController().isConnected()) {
        int step = ConfigManager::instance().stepSize();
        ctrl_.armController().moveToPosition(0, column * step);
        ctrl_.armController().moveToPosition(1, row * step);
        ctrl_.armController().moveToPosition(2, ConfigManager::instance().zHeight());
    }

    // Show image preview if available
    auto it = s_movement_images_.find({row, column});
    if (it != s_movement_images_.end()) {
        cv::Mat img = cv::imread(it->second);
        if (!img.empty()) {
            QDialog dlg(this);
            dlg.setWindowTitle(QString("预览 [%1,%2]").arg(row).arg(column));
            dlg.setMinimumSize(400, 300);
            dlg.resize(800, 600);
            auto* layout = new QVBoxLayout(&dlg);
            auto* scrollArea = new QScrollArea(&dlg);
            auto* label = new QLabel(&dlg);
            label->setPixmap(QPixmap::fromImage(cvMatToQImage(img)));
            label->setAlignment(Qt::AlignCenter);
            scrollArea->setWidget(label);
            scrollArea->setWidgetResizable(true);
            layout->addWidget(scrollArea);
            dlg.exec();
        }
    }
}

// ==================== Helpers ====================

void MainWindow::displayImage(const cv::Mat& image, QLabel* label) {
    if (image.empty()) return;
    QPixmap pix = QPixmap::fromImage(cvMatToQImage(image));
    label->setPixmap(pix.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QImage MainWindow::cvMatToQImage(const cv::Mat& mat) {
    if (mat.empty()) return {};
    if (mat.type() == CV_8UC3) {
        // Safe: mat outlives this QImage (caller owns the cv::Mat)
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888)
            .rgbSwapped();
    }
    if (mat.type() == CV_8UC1) {
        // Safe: mat outlives this QImage (caller owns the cv::Mat)
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
    }
    // Convert format: must deep-copy because the local cv::Mat is destroyed on return.
    // Without .copy(), rgbSwapped() shares the QImage's data buffer which points into
    // the local cv::Mat rgb — use-after-free when rgb goes out of scope.
    cv::Mat rgb;
    mat.convertTo(rgb, CV_8UC3);
    return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888)
        .rgbSwapped()
        .copy();
}

void MainWindow::addCameraOverlay(cv::Mat& image) {
    if (image.empty()) return;

    auto status = ctrl_.cameraHandler().getStatus();
    int rot = ctrl_.cameraHandler().getRotation();

    // Line 1: resolution | rotation
    std::string line1 = std::to_string(status.width) + "x" + std::to_string(status.height)
                     + " | " + std::to_string(rot) + "°";
    // Line 2: FPS
    std::string line2 = std::to_string(static_cast<int>(current_fps_)) + " FPS";

    int font = cv::FONT_HERSHEY_SIMPLEX;
    double scale = 0.7;          // much bigger than 0.45
    cv::Scalar color(0, 0, 0);       // black text
    cv::Scalar bg(255, 255, 255);    // white background
    int thickness = 2;
    int baseline = 0;

    cv::Size size1 = cv::getTextSize(line1, font, scale, thickness, &baseline);
    cv::Size size2 = cv::getTextSize(line2, font, scale, thickness, &baseline);

    int margin = 10;
    int line_gap = 6;
    int bg_pad = 4;

    // Background rectangle covering both lines
    int bg_w = std::max(size1.width, size2.width) + bg_pad * 2;
    int bg_h = size1.height + size2.height + line_gap + bg_pad * 2;
    cv::Point bg_tl(margin, image.rows - margin - bg_h);
    cv::Point bg_br(margin + bg_w, image.rows - margin);
    cv::rectangle(image, bg_tl, bg_br, bg, -1);

    // Line 1: resolution | rotation
    cv::Point origin1(margin + bg_pad, image.rows - margin - bg_h + size1.height + bg_pad);
    cv::putText(image, line1, origin1, font, scale, color, thickness);

    // Line 2: FPS
    cv::Point origin2(margin + bg_pad, origin1.y + size1.height + line_gap);
    cv::putText(image, line2, origin2, font, scale, color, thickness);
}

void MainWindow::displayImageFullQuality(const cv::Mat& image, QLabel* label, QPixmap& storage) {
    if (image.empty()) return;
    storage = QPixmap::fromImage(cvMatToQImage(image));
    label->setPixmap(storage.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    // Fullscreen dialog: close on ESC or click
    if (obj == fullscreen_dlg_) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                fullscreen_dlg_->close();
                fullscreen_dlg_ = nullptr;
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            fullscreen_dlg_->close();
            fullscreen_dlg_ = nullptr;
            return true;
        }
        return false;
    }

    // Preview labels: double-click to fullscreen
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (obj == ui_->stitchImageLabel && !stitched_pixmap_.isNull()) {
            showImageFullscreen(stitched_pixmap_);
            return true;
        }
        if (obj == ui_->detectImageLabel && !detected_pixmap_.isNull()) {
            showImageFullscreen(detected_pixmap_);
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::showImageFullscreen(const QPixmap& pixmap) {
    if (fullscreen_dlg_) {
        fullscreen_dlg_->close();
        fullscreen_dlg_ = nullptr;
    }

    auto* dlg = new QLabel(nullptr, Qt::Window | Qt::FramelessWindowHint);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setPixmap(pixmap.scaled(dlg->screen()->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    dlg->setAlignment(Qt::AlignCenter);
    dlg->setStyleSheet("background-color: black;");
    dlg->setCursor(Qt::CrossCursor);
    dlg->setFocusPolicy(Qt::StrongFocus);
    dlg->installEventFilter(this);
    dlg->showFullScreen();
    fullscreen_dlg_ = dlg;
}

#ifdef _WIN32
#include <windows.h>
extern int main(int argc, char* argv[]);
int WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
