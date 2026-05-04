#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/AppController.h"
#include "ui/DetectionSettingsDialog.h"
#include "ui/StitchingSettingsDialog.h"
#include "infra/config/ConfigManager.h"

#include <spdlog/spdlog.h>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QHeaderView>
#include <QTimer>
#include <QDir>
#include <filesystem>

MainWindow::MainWindow(AppController& ctrl, QWidget *parent)
    : QMainWindow(parent)
    , ctrl_(ctrl)
    , ui_(new Ui::MainWindow)
{
    ui_->setupUi(this);

    camera_update_timer_ = new QTimer(this);
    connect(camera_update_timer_, &QTimer::timeout, this, &MainWindow::updateCameraImage);
    connect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished, this, &MainWindow::onStitchingFinished);

    initGridTables();
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
        ui_->unifiedProgressBar->setValue(total > 0 ? (cur * 100) / total : 0);
    });
    ctrl_.stitcher().setStatusCallback([this](const std::string& msg) {
        ui_->unifiedStatusLog->appendPlainText(QString::fromStdString(msg));
    });

    SPDLOG_INFO("MainWindow initialized");
}

MainWindow::~MainWindow() {
    camera_update_timer_->stop();
    ctrl_.stopCameraCapture();
    delete ui_;
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
    connect(ui_->actionExportConfig, &QAction::triggered, this, [this]() {
        ConfigManager::instance().saveToFile("config_export.json");
        ui_->statusLabel->setText("配置已导出");
    });
    connect(ui_->actionExit, &QAction::triggered, this, &QWidget::close);

    // ---- 操作 ----
    connect(ui_->actionStartScan, &QAction::triggered, this, &MainWindow::on_toggleStartStopSMovement);
    connect(ui_->actionStopScan, &QAction::triggered, this, &MainWindow::on_toggleStartStopSMovement);
    connect(ui_->actionCaptureOnce, &QAction::triggered, this, &MainWindow::on_captureImage);
    connect(ui_->actionRunStitch, &QAction::triggered, this, &MainWindow::on_stitchRun);
    connect(ui_->actionRunDetect, &QAction::triggered, this, [this]() {
        if (!current_image_.empty()) {
            auto dets = ctrl_.detect(current_image_);
            displayImage(ctrl_.drawDetections(current_image_, dets), ui_->cameraImageLabel);
        }
    });

    // ---- 设置 ----
    connect(ui_->actionDetSettings, &QAction::triggered, this, &MainWindow::on_detSettings);
    connect(ui_->actionStitchSettings, &QAction::triggered, this, &MainWindow::on_stitchSettings);

    // ---- 工具 ----
    connect(ui_->actionArmZero, &QAction::triggered, this, &MainWindow::on_zeroArm);
    connect(ui_->actionLoadModel, &QAction::triggered, this, &MainWindow::on_loadModel);

    // ---- 视图 ----
    connect(ui_->actionViewArmPanel, &QAction::toggled, this, [this](bool visible) {
        ui_->scrollArea->setVisible(visible);
    });

    // ---- 帮助 ----
    connect(ui_->actionAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "关于 ArmSightStitch",
            "ArmSightStitch v2.0\n\n"
            "显微视觉扫描拼接系统\n"
            "基于机械臂Modbus TCP控制\n"
            "支持YOLO目标检测与图像拼接");
    });

    // ---- 面板控件 ----
    connect(ui_->scanCamerasButton, &QPushButton::clicked, this, &MainWindow::on_enumerateCameras);
    connect(ui_->cameraToggleButton, &QPushButton::clicked, this, [this]() {
        if (ctrl_.cameraHandler().isConnected()) {
            on_disconnectCamera();
        } else {
            on_connectCamera();
        }
    });
    connect(ui_->captureImageButton, &QPushButton::clicked, this, &MainWindow::on_captureImage);
    connect(ui_->enableDetectionCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s) {
        image_detection_enabled_ = (s == Qt::Checked);
    });

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

    connect(ui_->startStopToggleButton, &QPushButton::clicked, this, &MainWindow::on_toggleStartStopSMovement);
    connect(ui_->pauseToggleButton, &QPushButton::clicked, this, &MainWindow::on_togglePauseSMovement);

    // Grid clicks
    connect(ui_->topCellsTable, &QTableWidget::cellClicked, this, &MainWindow::on_topCellsTable_cellClicked);

    // AppController signals
    connect(&ctrl_, &AppController::statusMessage, this, [this](const QString& msg) {
        ui_->statusLabel->setText(msg);
    });
    connect(&ctrl_, &AppController::errorMessage, this, [this](const QString& msg) {
        ui_->statusLabel->setText("错误: " + msg);
        QMessageBox::critical(this, "错误", msg);
    });
    connect(&ctrl_, &AppController::stitchingFinished, this, [this](const cv::Mat& result) {
        stitched_result_ = result;
        displayImage(result, ui_->stitchImageLabel);
    });
}

void MainWindow::initGridTables() {
    QStringList headers;
    for (int i = 1; i <= 10; ++i) headers << QString::number(i);

    ui_->topCellsTable->setRowCount(10);
    ui_->topCellsTable->setColumnCount(10);
    ui_->topCellsTable->setHorizontalHeaderLabels(headers);
    ui_->topCellsTable->setVerticalHeaderLabels(headers);
    ui_->topCellsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui_->topCellsTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int r = 0; r < 10; ++r) {
        for (int c = 0; c < 10; ++c) {
            auto* ti = new QTableWidgetItem("");
            ti->setTextAlignment(Qt::AlignCenter);
            ui_->topCellsTable->setItem(r, c, ti);
        }
    }

    auto* grid = ui_->bottomCellsGrid;
    auto* gl = qobject_cast<QGridLayout*>(grid->layout());
    bottom_cells_group_ = new QButtonGroup(this);
    bottom_cells_group_->setExclusive(true);

    for (int r = 0; r < 10; ++r) {
        for (int c = 0; c < 10; ++c) {
            auto* btn = new QPushButton(QString("%1,%2").arg(r).arg(c));
            btn->setCheckable(true);
            btn->setFixedSize(50, 30);
            btn->setFont(QFont("", 9));
            gl->addWidget(btn, r, c);
            bottom_cells_group_->addButton(btn, r * 10 + c);
        }
    }

    connect(bottom_cells_group_, &QButtonGroup::idClicked, this, &MainWindow::on_bottomCellClicked);
    bottom_cells_group_->button(0)->setChecked(true);

    gl->setRowStretch(10, 1);
    gl->setColumnStretch(10, 1);
}

// ==================== Camera ====================

void MainWindow::on_connectCamera() {
    QString id = ui_->cameraCombo->currentData().toString();
    if (ctrl_.connectCamera(id.toStdString())) {
        ui_->cameraToggleButton->setText("断开");
        ui_->captureImageButton->setEnabled(true);
        ctrl_.startCameraCapture();
        camera_update_timer_->start(33);
        ui_->statusLabel->setText("相机已连接");
    }
}

void MainWindow::on_disconnectCamera() {
    camera_update_timer_->stop();
    ctrl_.stopCameraCapture();
    ctrl_.disconnectCamera();
    ui_->cameraToggleButton->setText("连接");
    ui_->captureImageButton->setEnabled(false);
    ui_->statusLabel->setText("相机已断开");
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
    if (ctrl_.connectArm(ip, port)) {
        ui_->armToggleButton->setText("断开");
        ui_->armStatusLabel->setText("已连接");
    }
}

void MainWindow::on_disconnectArm() {
    ctrl_.disconnectArm();
    ui_->armToggleButton->setText("连接");
    ui_->armStatusLabel->setText("未连接");
}

void MainWindow::on_moveToPosition() {
    auto& arm = ctrl_.armController();
    arm.moveToPosition(0, static_cast<int>(ui_->xPosSpin->value()));
    arm.moveToPosition(1, static_cast<int>(ui_->yPosSpin->value()));
    arm.moveToPosition(2, static_cast<int>(ui_->zPosSpin->value()));
    arm.moveToPosition(3, 0);
    arm.moveToPosition(4, 0);
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
    for (int i = 0; i < 5; ++i) ctrl_.armController().moveToPosition(i, 0);
    ui_->xPosSpin->setValue(0);
    ui_->yPosSpin->setValue(0);
    ui_->zPosSpin->setValue(0);
}

// ==================== S-Movement ====================

void MainWindow::on_toggleStartStopSMovement() {
    if (ctrl_.movementController().getStatus().running) {
        ctrl_.stopSMovement();
        ui_->startStopToggleButton->setText("开始");
        ui_->pauseToggleButton->setEnabled(false);
        ui_->pauseToggleButton->setText("暂停");
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
        ui_->startStopToggleButton->setText("停止");
        ui_->pauseToggleButton->setEnabled(true);
        ui_->pauseToggleButton->setText("暂停");
    }
}

void MainWindow::on_togglePauseSMovement() {
    if (ctrl_.movementController().getStatus().paused) {
        ctrl_.resumeSMovement();
        ui_->pauseToggleButton->setText("暂停");
    } else {
        ctrl_.pauseSMovement();
        ui_->pauseToggleButton->setText("恢复");
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

    ui_->actionRunStitch->setEnabled(false);
    stitching_future_ = QtConcurrent::run([this, images, gs]() {
        auto sorted = ctrl_.stitcher().sortImagesInSCurveOrder(images, gs);
        return ctrl_.stitcher().stitchImages(sorted, gs);
    });
    stitching_watcher_.setFuture(stitching_future_);
}

void MainWindow::onStitchingFinished() {
    ui_->actionRunStitch->setEnabled(true);
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
        if (image_detection_enabled_ && model_loaded_) {
            auto dets = ctrl_.detect(frame);
            displayImage(ctrl_.drawDetections(frame, dets), ui_->cameraImageLabel);
        } else {
            displayImage(frame, ui_->cameraImageLabel);
        }
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
    ui_->unifiedStatusLog->appendPlainText(QString::fromStdString(status.status_message));
    if (status.total_points > 0) {
        ui_->unifiedProgressBar->setValue((status.current_point * 100) / status.total_points);
    }
}

// ==================== Grid ====================

void MainWindow::on_topCellsTable_cellClicked(int row, int column) {
    auto it = s_movement_images_.find({row, column});
    if (it != s_movement_images_.end()) {
        cv::Mat img = cv::imread(it->second);
        if (!img.empty()) {
            cv::namedWindow("预览", cv::WINDOW_NORMAL);
            cv::imshow("预览", img);
        }
    }
}

void MainWindow::on_bottomCellClicked(int id) {
    if (!ctrl_.armController().isConnected()) return;
    int row = id / 10;
    int col = id % 10;
    int step = ConfigManager::instance().stepSize();
    ctrl_.armController().moveToPosition(0, col * step);
    ctrl_.armController().moveToPosition(1, row * step);
    ctrl_.armController().moveToPosition(2, ConfigManager::instance().zHeight());
}

// ==================== Helpers ====================

void MainWindow::displayImage(const cv::Mat& image, QLabel* label) {
    if (image.empty()) return;
    QPixmap pix = QPixmap::fromImage(cvMatToQImage(image));
    label->setPixmap(pix.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QImage MainWindow::cvMatToQImage(const cv::Mat& mat) {
    if (mat.empty()) return {};
    if (mat.type() == CV_8UC3)
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888).rgbSwapped();
    if (mat.type() == CV_8UC1)
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
    cv::Mat rgb;
    mat.convertTo(rgb, CV_8UC3);
    return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).rgbSwapped();
}
