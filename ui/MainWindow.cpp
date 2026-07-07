#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/AppController.h"
#include "ui/DetectionSettingsDialog.h"
#include "ui/StitchingSettingsDialog.h"
#include "ui/SphereSettingsDialog.h"
#include "infra/config/ConfigManager.h"
#include "infra/config/PathUtils.h"

#include <spdlog/spdlog.h>
#include <QEvent>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QStandardPaths>
#include <QStatusBar>
#include <QHeaderView>
#include <QDateTime>
#include <QDir>
#include <QTimer>
#include <QDialog>
#include <QBoxLayout>
#include <QGridLayout>
#include <QProgressBar>
#include <QProgressDialog>
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

    // Minimum preview size — ensures valid scaling target before layout settles
    ui_->cameraImageLabel->setMinimumSize(320, 240);
    ui_->stitchImageLabel->setMinimumSize(320, 240);
    ui_->detectImageLabel->setMinimumSize(320, 240);

    // Context-sensitive initial button states — enabled only when prerequisites met
    // Camera-dependent
    ui_->quickDetectBtn->setEnabled(false);
    ui_->captureImageButton->setEnabled(false);
    ui_->enableDetectionCheck->setEnabled(false);
    ui_->autoExposureCheck->setEnabled(false);
    ui_->exposureSlider->setEnabled(false);
    ui_->exposureValueLabel->setEnabled(false);
    ui_->rotationCombo->setEnabled(false);
    ui_->hflipCheck->setEnabled(false);
    ui_->vflipCheck->setEnabled(false);
    ui_->resolutionCombo->setEnabled(false);
    // Arm-dependent
    ui_->quickScanBtn->setEnabled(false);
    ui_->quickSaveBtn->setEnabled(false);
    ui_->moveToPosButton->setEnabled(false);
    ui_->readPosButton->setEnabled(false);
    ui_->xPosSpin->setEnabled(false);
    ui_->yPosSpin->setEnabled(false);
    ui_->zPosSpin->setEnabled(false);
    ui_->zeroButton->setEnabled(false);
    ui_->cellMoveCheck->setEnabled(false);
    ui_->cellMoveCheck->setChecked(false);
    ui_->toolbarEmergStopBtn->setEnabled(false);
    ui_->speedSpin->setValue(ConfigManager::instance().defaultSpeed());

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

    connect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished, this, &MainWindow::onStitchingFinished);
    connect(&detection_watcher_, &QFutureWatcher<DetectionResult>::finished, this, &MainWindow::onDetectionFinished);
    connect(&manual_detect_watcher_, &QFutureWatcher<DetectionResult>::finished, this, [this]() {
        DetectionResult result = manual_detect_future_.result();
        if (!result.frame.empty()) {
            cv::Mat annotated = ctrl_.drawDetections(result.frame, result.detections);
            displayImageFullQuality(annotated, ui_->detectImageLabel, detected_pixmap_);
            appendLog(QString("手动检测完成: %1 个目标").arg(result.detections.size()), "INFO");
        }
    });
    connect(&arm_connect_watcher_, &QFutureWatcher<bool>::finished, this, &MainWindow::onArmConnectFinished);
    connect(&arm_move_watcher_, &QFutureWatcher<void>::finished, this, &MainWindow::onArmMoveFinished);
    connect(&zero_progress_watcher_, &QFutureWatcher<bool>::finished, this, &MainWindow::onZeroProgressFinished);

    // Reference log panel from .ui (placed in left sidebar's hardware section)
    status_log_edit_ = ui_->statusLogEdit;

    initGridTables();

    // Real-time detection only meaningful with a camera connected
    ui_->enableDetectionCheck->setChecked(false);
    ui_->enableDetectionCheck->setEnabled(false);

    // Rename camera-area button, hide redundant toolbar one
    ui_->captureImageButton->setText("捕获");
    ui_->quickCaptureBtn->setVisible(false);

    // IP address: validate format without fixed-width mask for natural text flow
    ui_->armIpEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression("^(?:(?:25[0-5]|2[0-4]\\d|1\\d{2}|[1-9]?\\d)\\.){3}"
                           "(?:25[0-5]|2[0-4]\\d|1\\d{2}|[1-9]?\\d)$"),
        ui_->armIpEdit));

    // FPS label and camera preview are defined in .ui file
    fps_label_ = ui_->fpsLabel;
    camera_preview_check_ = ui_->cameraPreviewCheck;

    // Stitch progress bar — placed below stitch preview
    stitch_progress_ = new QProgressBar(this);
    stitch_progress_->setRange(0, 100);
    stitch_progress_->setValue(0);
    stitch_progress_->setTextVisible(true);
    stitch_progress_->setFormat("拼接进度: %p%");
    stitch_progress_->setVisible(false);
    QWidget* stitch_parent = ui_->stitchImageLabel->parentWidget();
    if (stitch_parent) {
        // Create a container that holds both the label and the progress bar
        auto* container = new QWidget(this);
        auto* vlay = new QVBoxLayout(container);
        vlay->setContentsMargins(0, 0, 0, 0);

        // Handle different parent types — must capture index BEFORE
        // adding widgets to container (addWidget reparents, invalidating indexOf)
        if (auto* splitter = qobject_cast<QSplitter*>(stitch_parent)) {
            int idx = splitter->indexOf(ui_->stitchImageLabel);
            if (idx >= 0) {
                splitter->replaceWidget(idx, container);
                vlay->addWidget(ui_->stitchImageLabel);
                vlay->addWidget(stitch_progress_);
            }
        } else if (auto* gl = qobject_cast<QGridLayout*>(stitch_parent->layout())) {
            int idx = gl->indexOf(ui_->stitchImageLabel);
            if (idx >= 0) {
                int row, col, rs, cs;
                gl->getItemPosition(idx, &row, &col, &rs, &cs);
                gl->removeWidget(ui_->stitchImageLabel);
                gl->addWidget(container, row, col, rs, cs);
                vlay->addWidget(ui_->stitchImageLabel);
                vlay->addWidget(stitch_progress_);
            }
        } else if (auto* lay = qobject_cast<QBoxLayout*>(stitch_parent->layout())) {
            int idx = lay->indexOf(ui_->stitchImageLabel);
            if (idx >= 0) {
                lay->insertWidget(idx + 1, stitch_progress_);
            }
            // Container not needed for simple box layout case
            delete container;
            container = nullptr;
        }
    }

    setupMenuNavigation();
    connectSignals();

    // Wire module callbacks
    ctrl_.cameraHandler().setStatusCallback([this](const camera::CameraStatus& s) {
        if (!s.status_message.empty()) {
            QString lvl = s.connected ? "INFO" : "ERROR";
            QMetaObject::invokeMethod(this, [this, msg = QString::fromStdString(s.status_message), lvl]() {
                appendLog("相机: " + msg, lvl);
            }, Qt::QueuedConnection);
        }
    });
    ctrl_.armController().setStatusCallback([this](const arm::ArmStatus& s) {
        QMetaObject::invokeMethod(this, "onArmStatusChanged", Q_ARG(const arm::ArmStatus&, s));
    });
    ctrl_.movementController().setStatusCallback([this](const arm::SMovementStatus& s) {
        QMetaObject::invokeMethod(this, [this, s]() { onMovementStatus(s); }, Qt::QueuedConnection);
    });
    ctrl_.stitcher().setProgressCallback([this](int cur, int total) {
        int pct = total > 0 ? cur * 100 / total : 0;
        QMetaObject::invokeMethod(this, [this, pct]() {
            stitch_progress_->setValue(pct);
            if (stitch_progress_dlg_) stitch_progress_dlg_->setValue(pct);
        }, Qt::QueuedConnection);
    });
    ctrl_.stitcher().setStatusCallback([this](const std::string& msg) {
        QString qmsg = QString::fromStdString(msg);
        QMetaObject::invokeMethod(this, [this, qmsg]() {
            appendLog(qmsg, "INFO");
        }, Qt::QueuedConnection);
    });

    // Auto-scan cameras on startup
    on_enumerateCameras();

    // Workspace selection dialog (deferred to after main window is shown)
    QTimer::singleShot(0, this, [this]() {
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString defaultWs = docs + "/ScannerData";

        QDialog dlg(this);
        dlg.setWindowTitle("选择工作空间");
        dlg.setMinimumWidth(500);
        auto* vlay = new QVBoxLayout(&dlg);
        vlay->addWidget(new QLabel("请选择工作空间路径：", &dlg));

        auto* hlay = new QHBoxLayout();
        auto* pathEdit = new QLineEdit(defaultWs, &dlg);
        hlay->addWidget(pathEdit);
        auto* browseBtn = new QPushButton("浏览...", &dlg);
        hlay->addWidget(browseBtn);
        vlay->addLayout(hlay);

        auto* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        auto* okBtn = new QPushButton("确认", &dlg);
        auto* cancelBtn = new QPushButton("取消", &dlg);
        btnLayout->addWidget(okBtn);
        btnLayout->addWidget(cancelBtn);
        vlay->addLayout(btnLayout);

        connect(browseBtn, &QPushButton::clicked, [&]() {
            QString dir = QFileDialog::getExistingDirectory(&dlg, "选择目录", pathEdit->text(),
                                                            QFileDialog::ShowDirsOnly);
            if (!dir.isEmpty()) pathEdit->setText(dir);
        });
        connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

        if (dlg.exec() == QDialog::Accepted && !pathEdit->text().isEmpty()) {
            QDir().mkpath(pathEdit->text());
            ConfigManager::instance().setImageSaveBasePath(pathEdit->text().toStdString());
        }
    });

    // Check if model was already auto-loaded by AppController
    if (ctrl_.isModelLoaded()) {
        model_loaded_ = true;
        appendLog("默认检测模型已加载", "INFO");
    }

    // Rename: "开始检测" → "检测单帧" (vs real-time = continuous)
    ui_->quickDetectBtn->setText("检测单帧");

    // Move real-time detection checkbox next to "检测单帧" button in toolbar
    {
        QWidget* toolbar = ui_->quickDetectBtn->parentWidget();
        if (toolbar && toolbar->layout()) {
            auto* tlay = qobject_cast<QBoxLayout*>(toolbar->layout());
            if (tlay) {
                int btnIdx = tlay->indexOf(ui_->quickDetectBtn);
                if (btnIdx >= 0) {
                    ui_->enableDetectionCheck->setText("实时检测");
                    tlay->insertWidget(btnIdx + 1, ui_->enableDetectionCheck);
                }
            }
        }
    }

    appendLog("Application initialized", "INFO");
    SPDLOG_INFO("MainWindow initialized");
}

MainWindow::~MainWindow() {
    ctrl_.stopCameraCapture();
    // Wait for pending async operations before tearing down UI
    stitching_watcher_.waitForFinished();
    detection_watcher_.waitForFinished();
    manual_detect_watcher_.waitForFinished();
    arm_connect_watcher_.waitForFinished();
    arm_move_watcher_.waitForFinished();
    delete ui_;
}

void MainWindow::setupMenuNavigation() {
}

void MainWindow::onMenuButtonClicked(int) {
}

void MainWindow::connectSignals() {
    // ---- 文件 ----
    connect(ui_->actionOpenImage, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "打开图像", "", "图像 (*.jpg *.png *.bmp)");
        if (!path.isEmpty()) {
            current_image_ = cv::imread(path.toStdString());
            if (!current_image_.empty()) {
                displayImage(current_image_, ui_->cameraImageLabel);
                ui_->quickDetectBtn->setEnabled(true);
            }
        }
    });
    connect(ui_->actionSaveStitch, &QAction::triggered, this, &MainWindow::on_saveStitch);

    // 文件 → 打开扫描文件夹 (defined in .ui)
    connect(ui_->actionOpenScanDir, &QAction::triggered, this, [this]() {
        QString ws = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
        QString dir = QFileDialog::getExistingDirectory(this, "选择扫描文件夹", ws,
                                                        QFileDialog::ShowDirsOnly);
        if (dir.isEmpty()) return;

        auto& cfg = ConfigManager::instance();
        cv::Size gs(cfg.gridSizeX(), cfg.gridSizeY());
        auto images = ctrl_.loadImages(dir.toStdString());
        if (images.empty()) {
            QMessageBox::warning(this, "警告", "所选文件夹中没有图像文件。");
            return;
        }

        // Parse filename pattern {row}_{col}.jpg to build image map
        std::map<std::pair<int,int>, cv::Mat> cellImages;
        QDir qdir(dir);
        for (const auto& fi : qdir.entryInfoList({"*.jpg", "*.png", "*.bmp"}, QDir::Files)) {
            QString name = fi.baseName(); // e.g. "0_1"
            int us = name.indexOf('_');
            if (us < 0) continue;
            bool okR, okC;
            int r = name.left(us).toInt(&okR);
            int c = name.mid(us+1).toInt(&okC);
            if (!okR || !okC) continue;
            cv::Mat img = cv::imread(fi.absoluteFilePath().toStdString());
            if (!img.empty()) cellImages[{r, c}] = img;
        }
        if (cellImages.empty()) {
            QMessageBox::warning(this, "警告", "文件夹中没有 {row}_{col}.jpg 格式的图像。");
            return;
        }

        // Clear previous cell data
        {
            std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
            s_movement_images_.clear();
        }
        ui_->topCellsTable->clearContents();

        // Load images into cells
        for (auto& [rc, img] : cellImages) {
            int row = rc.first, col = rc.second;
            if (row >= gs.height || col >= gs.width) continue;

            std::string fn = dir.toStdString() + "/" + std::to_string(row) + "_" + std::to_string(col) + ".jpg";
            {
                std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
                s_movement_images_[{row, col}] = fn;
            }

            // Show thumbnail in cell (center-crop to match stitching before resize)
            int cropSize = ConfigManager::instance().centerCropSize();
            cv::Mat cropped;
            if (cropSize > 0 && cropSize < img.cols && cropSize < img.rows) {
                int left = (img.cols - cropSize) / 2;
                int top = (img.rows - cropSize) / 2;
                cropped = img(cv::Rect(left, top, cropSize, cropSize));
            } else {
                cropped = img;
            }
            cv::Mat thumb;
            cv::resize(cropped, thumb, cv::Size(50, 50), 0, 0, cv::INTER_AREA);
            QPixmap pix = QPixmap::fromImage(cvMatToQImage(thumb));
            auto* item = ui_->topCellsTable->item(row, col);
            if (item) {
                auto* lbl = new QLabel();
                lbl->setPixmap(pix);
                lbl->setScaledContents(true);
                lbl->setContentsMargins(0, 0, 0, 0);
                lbl->setAlignment(Qt::AlignCenter);
                lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                ui_->topCellsTable->setCellWidget(row, col, lbl);
            }
        }
        appendLog(QString("已加载 %1 张图像: %2").arg(cellImages.size()).arg(dir), "INFO");
    });

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

    // ---- 视图 ----
    connect(ui_->actionToggleSidebar, &QAction::toggled, this, [this](bool visible) { ui_->menuPanel->setVisible(visible); });
    connect(ui_->actionToggleLog, &QAction::toggled, this, [this](bool visible) { ui_->statusLogEdit->setVisible(visible); });

    // ---- 设置 ----
    connect(ui_->actionDetSettings, &QAction::triggered, this, &MainWindow::on_detSettings);
    connect(ui_->actionStitchSettings, &QAction::triggered, this, &MainWindow::on_stitchSettings);
    connect(ui_->actionArmZero, &QAction::triggered, this, &MainWindow::on_zeroArm);
    connect(ui_->actionLoadModel, &QAction::triggered, this, &MainWindow::on_loadModel);
    // ---- 球冠参数设置 ----
    connect(ui_->actionSphereSettings, &QAction::triggered, this, [this]() {
        auto& cfg = ConfigManager::instance();
        SphereSettingsDialog dlg(this);
        dlg.setSphereRadius(cfg.sphereRadius());
        dlg.setSphereCapHeight(cfg.sphereCapHeight());
        dlg.setSphereHeightOffset(cfg.sphereHeightOffset());
        dlg.setZBaseHeight(cfg.zBaseHeight());
        if (dlg.exec() == QDialog::Accepted) {
            int dH = dlg.sphereHeightOffset();
            int h  = dlg.sphereCapHeight();
            int zBase = dlg.zBaseHeight();
            // Constraint: dH <= zBase - h (so Z at cap center >= 0)
            if (dH > zBase - h) {
                QMessageBox::warning(this, tr("参数错误"),
                    tr("高度偏移 dH (%1) 不能大于 %2 (zBase - h = %3 - %4)")
                        .arg(dH).arg(zBase - h).arg(zBase).arg(h));
                return;
            }
            cfg.setSphereRadius(dlg.sphereRadius());
            cfg.setSphereCapHeight(dlg.sphereCapHeight());
            cfg.setSphereHeightOffset(dlg.sphereHeightOffset());
            cfg.setZBaseHeight(dlg.zBaseHeight());
            cfg.saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
            appendLog(tr("球冠参数已更新: R=%1, h=%2, dH=%3, zBase=%4")
                .arg(dlg.sphereRadius()).arg(dlg.sphereCapHeight())
                .arg(dlg.sphereHeightOffset()).arg(dlg.zBaseHeight()), "INFO");
        }
    });

    // ---- 帮助 ----
    connect(ui_->actionUserGuide, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "使用说明",
            "明场显微成像验证组件\n\n"
            "工作流程:\n"
            "1. 连接相机和机械臂 (左侧硬件连接区)\n"
            "2. 设置网格参数，点击「开始扫描」执行S型扫描采集\n"
            "3. 扫描完成后自动拼接，也可手动点击「开始拼接」\n"
            "4. 加载检测模型后，点击「开始检测」执行目标检测\n\n"
            "工具栏按钮:\n"
            "拍照 - 手动拍摄单帧图像\n"
            "开始扫描 / 暂停 - 控制S型扫描流程\n"
            "开始拼接 - 对已采集图像执行拼接\n"
            "保存结果 - 保存拼接或检测结果\n"
            "开始检测 / 检测单帧 - 执行目标检测\n\n"
            "快捷键:\n"
            "Ctrl+O - 打开图像  Ctrl+S - 保存结果\n"
            "F1 - 显示使用说明");
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
        bool was_capturing = ctrl_.cameraHandler().isCapturing();
        if (was_capturing) ctrl_.stopCameraCapture();
        ctrl_.cameraHandler().setRotation(degrees);
        if (was_capturing) ctrl_.startCameraCapture();
    });

    // ---- 相机翻转控制 ----
    connect(ui_->hflipCheck, &QCheckBox::toggled, this, [this](bool checked) {
        ctrl_.cameraHandler().setHFlip(checked);
    });
    connect(ui_->vflipCheck, &QCheckBox::toggled, this, [this](bool checked) {
        ctrl_.cameraHandler().setVFlip(checked);
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
    connect(ui_->quickDetectBtn, &QPushButton::clicked, this, &MainWindow::on_manualDetect);
    connect(ui_->enableDetectionCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s) {
        image_detection_enabled_ = (s == Qt::Checked);
        if (!image_detection_enabled_) {
            ui_->detectImageLabel->setPixmap({});
            ui_->detectImageLabel->setText("等待检测...");
            detected_pixmap_ = QPixmap();
        }
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

    // ---- 机械臂: 速度 / 连续运动 / 紧急停止 ----
    connect(ui_->setSpeedBtn, &QPushButton::clicked, this, [this]() {
        int spd = ui_->speedSpin->value();
        for (int i = 0; i < 5; ++i) ctrl_.armController().setSpeed(i, spd);
        ConfigManager::instance().setDefaultSpeed(spd);
        ConfigManager::instance().saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
        appendLog(QString("速度已设置: %1").arg(spd), "INFO");
    });
    connect(ui_->contMoveBtn, &QPushButton::toggled, this, [this](bool checked) {
        int axis = ui_->contAxisCombo->currentIndex();
        if (checked) {
            bool dir = ui_->contFwdRadio->isChecked();
            ctrl_.armController().startContinuousMovement(axis, dir);
            ui_->contMoveBtn->setText("停止");
            ui_->contAxisCombo->setEnabled(false);
            ui_->contFwdRadio->setEnabled(false);
            ui_->contRevRadio->setEnabled(false);
        } else {
            ctrl_.armController().stopContinuousMovement(axis);
            ui_->contMoveBtn->setText("连续移动");
            ui_->contAxisCombo->setEnabled(true);
            ui_->contFwdRadio->setEnabled(true);
            ui_->contRevRadio->setEnabled(true);
        }
    });
    auto emergStop = [this]() {
        if (ctrl_.movementController().getStatus().running) ctrl_.stopSMovement();
        for (int i = 0; i < 5; ++i) ctrl_.armController().stopAllMovements(i);
        if (ui_->contMoveBtn->isChecked()) ui_->contMoveBtn->setChecked(false);
        appendLog("紧急停止：所有运动已停止", "WARN");
    };
    connect(ui_->emergStopBtn, &QPushButton::clicked, this, emergStop);
    connect(ui_->toolbarEmergStopBtn, &QPushButton::clicked, this, emergStop);

    // ---- 扫描页面: 网格 ----
    connect(ui_->topCellsTable, &QTableWidget::cellClicked, this, &MainWindow::on_topCellsTable_cellClicked);

    // ---- 拼接页面 ----

    // ---- 网格表格: 左键预览 / 中键位移 ----
    ui_->topCellsTable->viewport()->installEventFilter(this);

    // ---- 拼接预览: 双击全屏 ----
    ui_->stitchImageLabel->installEventFilter(this);

    // ---- 相机预览: 双击全屏 ----
    ui_->cameraImageLabel->installEventFilter(this);

    // ---- 检测页面 ----

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
    connect(&ctrl_, &AppController::modelLoaded, this, [this]() {
        model_loaded_ = true;
        ui_->statusLabel->setText("默认模型已加载");
        appendLog("自动加载默认检测模型成功", "INFO");
    });
    connect(&ctrl_, &AppController::stitchingFinished, this, [this](const cv::Mat& result) {
        stitched_result_ = result;
        displayImageFullQuality(result, ui_->stitchImageLabel, stitched_pixmap_);
        ui_->quickSaveBtn->setEnabled(true);
    });

    // Camera preview (event-driven, replaces QTimer polling)
    connect(&ctrl_, &AppController::cameraFrameReady, this, &MainWindow::onCameraFrameReady);

    connect(&ctrl_, &AppController::cameraExposureChanged, this, [this]() {
        if (!ctrl_.cameraHandler().getAutoExposure()) return;
        float expo = ctrl_.cameraHandler().getRealExposure();
        QSignalBlocker blocker(ui_->exposureSlider);
        int val = qBound(ui_->exposureSlider->minimum(), static_cast<int>(expo * 100.0f),
                         ui_->exposureSlider->maximum());
        ui_->exposureSlider->setValue(val);
        ui_->exposureValueLabel->setText(QString::number(expo, 'f', 2) + " ms");
    });

    connect(&ctrl_, &AppController::cameraDisconnected, this, [this]() {
        appendLog("相机意外断开", "ERROR");
        on_disconnectCamera();
    });

    connect(&ctrl_, &AppController::cameraError, this, [this](const QString& msg) {
        appendLog("相机错误: " + msg, "ERROR");
    });
}

void MainWindow::initGridTables() {
    int gx = ConfigManager::instance().gridSizeX();
    int gy = ConfigManager::instance().gridSizeY();

    // Headers: columns right-to-left (col 1 = rightmost), rows top-to-bottom
    QStringList colHeaders, rowHeaders;
    for (int i = gx; i >= 1; --i) colHeaders << QString::number(i);
    for (int i = 1; i <= gy; ++i) rowHeaders << QString::number(i);

    ui_->topCellsTable->setRowCount(gy);
    ui_->topCellsTable->setColumnCount(gx);
    ui_->topCellsTable->setHorizontalHeaderLabels(colHeaders);
    ui_->topCellsTable->setVerticalHeaderLabels(rowHeaders);
    ui_->topCellsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui_->topCellsTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui_->topCellsTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    ui_->topCellsTable->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    ui_->topCellsTable->setStyleSheet(
        "#topCellsTable { gridline-color: #16191D; }"
        "QTableWidget::item { padding: 0px; border: none; }"
        "QTableWidget::item:selected { border: none; }");

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
        ui_->quickDetectBtn->setEnabled(true);
        ui_->enableDetectionCheck->setEnabled(true);
        // Clear placeholder text so preview area shows immediately
        ui_->cameraImageLabel->setText({});

        // ── Default rotation 270° BEFORE capture (ToupCam SDK requirement) ──
        ctrl_.cameraHandler().setRotation(270);
        {
            QSignalBlocker blocker(ui_->rotationCombo);
            ui_->rotationCombo->setCurrentIndex(3); // 270°
        }
        ui_->rotationCombo->setEnabled(true);

        // Sync flip states from camera hardware
        if (ui_->hflipCheck) ui_->hflipCheck->setChecked(ctrl_.cameraHandler().getHFlip());
        if (ui_->vflipCheck) ui_->vflipCheck->setChecked(ctrl_.cameraHandler().getVFlip());

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
            QString label = QString("%1x%2").arg(w).arg(h);
            res_combo->addItem(label, QVariant(QSize(w, h)));
            if (w == cur_w && h == cur_h) {
                select_idx = static_cast<int>(i);
            }
        }
        res_combo->setCurrentIndex(select_idx);

        ctrl_.startCameraCapture();
        // First frame arrives via signal on next event-loop iteration,
        // after Qt layouts settle — no manual defer needed
        ui_->statusLabel->setText("相机已连接");

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
        label->setText(QString::number(expo, 'f', 2) + " ms");
        slider->setEnabled(!auto_on);
        label->setEnabled(!auto_on);
        ui_->autoExposureCheck->setEnabled(true);
        if (ui_->hflipCheck) ui_->hflipCheck->setEnabled(true);
        if (ui_->vflipCheck) ui_->vflipCheck->setEnabled(true);
    }
}

void MainWindow::on_disconnectCamera() {
    ctrl_.stopCameraCapture();
    ctrl_.disconnectCamera();
    ui_->cameraToggleButton->setText("连接");
    ui_->captureImageButton->setEnabled(false);
    ui_->quickDetectBtn->setEnabled(false);
    ui_->enableDetectionCheck->setChecked(false);
    ui_->enableDetectionCheck->setEnabled(false);
    ui_->cameraImageLabel->setText("相机预览");
    ui_->cameraImageLabel->setPixmap({});
    ui_->statusLabel->setText("相机已断开");
    ui_->autoExposureCheck->setEnabled(false);
    ui_->exposureSlider->setEnabled(false);
    ui_->exposureValueLabel->setEnabled(false);
    ui_->rotationCombo->setEnabled(false);
    ui_->resolutionCombo->setEnabled(false);
    if (ui_->hflipCheck) ui_->hflipCheck->setEnabled(false);
    if (ui_->vflipCheck) ui_->vflipCheck->setEnabled(false);
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

        // Save capture to Documents with timestamp
        QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      + "/ScannerData/captures";
        QDir().mkpath(dir);
        auto now = QDateTime::currentDateTime();
        QString filename = dir + "/capture_" + now.toString("yyyyMMdd_HHmmss") + ".jpg";
        cv::imwrite(filename.toStdString(), frame);
        appendLog(QString("捕获已保存: %1").arg(filename), "INFO");
    }
}

// ==================== Arm ====================

void MainWindow::on_connectArm() {
    std::string ip = ui_->armIpEdit->text().toStdString();
    int port = ui_->armPortSpin->value();
    ui_->armToggleButton->setEnabled(false);
    ui_->armStatusLabel->setText(QString::fromUtf8("●"));
    ui_->armStatusLabel->setProperty("connStatus", "connecting");
    ui_->armStatusLabel->setToolTip("连接中...");
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
        ui_->armStatusLabel->setText(QString::fromUtf8("●"));
        ui_->armStatusLabel->setProperty("connStatus", "connected");
        ui_->armStatusLabel->setToolTip("已连接");
        ui_->moveToPosButton->setEnabled(true);
        ui_->readPosButton->setEnabled(true);
        ui_->xPosSpin->setEnabled(true);
        ui_->yPosSpin->setEnabled(true);
        ui_->zPosSpin->setEnabled(true);
        ui_->cellMoveCheck->setEnabled(true);
        ui_->setSpeedBtn->setEnabled(true);
        ui_->speedSpin->setValue(ConfigManager::instance().defaultSpeed());
        ui_->speedSpin->setEnabled(true);
        ui_->aPosSpin->setEnabled(true);
        ui_->bPosSpin->setEnabled(true);
        ui_->contAxisCombo->setEnabled(true);
        ui_->contFwdRadio->setEnabled(true);
        ui_->contRevRadio->setEnabled(true);
        ui_->contMoveBtn->setEnabled(true);
        ui_->emergStopBtn->setEnabled(true);
        ui_->toolbarEmergStopBtn->setEnabled(true);
        ui_->quickScanBtn->setEnabled(true);
        ui_->zeroButton->setEnabled(true);
    } else {
        ui_->armStatusLabel->setText(QString::fromUtf8("●"));
        ui_->armStatusLabel->setProperty("connStatus", "disconnected");
        ui_->armStatusLabel->setToolTip("连接失败");
    }
    ui_->armStatusLabel->style()->unpolish(ui_->armStatusLabel);
    ui_->armStatusLabel->style()->polish(ui_->armStatusLabel);
}

void MainWindow::on_disconnectArm() {
    ctrl_.disconnectArm();
    ui_->armToggleButton->setText("连接");
    ui_->armStatusLabel->setText(QString::fromUtf8("●"));
    ui_->armStatusLabel->setProperty("connStatus", "disconnected");
    ui_->armStatusLabel->setToolTip("未连接");
    ui_->armStatusLabel->style()->unpolish(ui_->armStatusLabel);
    ui_->armStatusLabel->style()->polish(ui_->armStatusLabel);
    ui_->moveToPosButton->setEnabled(false);
    ui_->readPosButton->setEnabled(false);
    ui_->xPosSpin->setEnabled(false);
    ui_->yPosSpin->setEnabled(false);
    ui_->zPosSpin->setEnabled(false);
    ui_->cellMoveCheck->setEnabled(false);
    ui_->cellMoveCheck->setChecked(false);
    ui_->setSpeedBtn->setEnabled(false);
    ui_->speedSpin->setEnabled(false);
    ui_->aPosSpin->setEnabled(false);
    ui_->bPosSpin->setEnabled(false);
    ui_->contAxisCombo->setEnabled(false);
    ui_->contFwdRadio->setEnabled(false);
    ui_->contRevRadio->setEnabled(false);
    ui_->contMoveBtn->setEnabled(false);
    ui_->emergStopBtn->setEnabled(false);
    ui_->toolbarEmergStopBtn->setEnabled(false);
    ui_->quickScanBtn->setEnabled(false);
    ui_->zeroButton->setEnabled(false);
}

void MainWindow::on_moveToPosition() {
    ui_->moveToPosButton->setEnabled(false);
    auto* ctrl = &ctrl_;
    int x = static_cast<int>(ui_->xPosSpin->value());
    int y = static_cast<int>(ui_->yPosSpin->value());
    int z = static_cast<int>(ui_->zPosSpin->value());
    arm_move_future_ = QtConcurrent::run([ctrl, x, y, z]() {
        ctrl->armController().moveAxesConcurrent(x, y, z);
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
        ctrl->armController().moveAxesConcurrent(0, 0, 0);
    });
    arm_move_watcher_.setFuture(arm_move_future_);
    ui_->xPosSpin->setValue(0);
    ui_->yPosSpin->setValue(0);
    ui_->zPosSpin->setValue(0);
}

// ==================== S-Movement ====================

bool MainWindow::isArmAtZero() {
    static constexpr double kZeroTolerance = 200.0;
    auto& armCtrl = ctrl_.armController();
    for (int i = 0; i < 5; ++i) {
        double pos = armCtrl.readPosition(i);
        if (std::abs(pos) > kZeroTolerance) {
            return false;
        }
    }
    return true;
}

void MainWindow::startScanSequence() {
    auto& cfg = ConfigManager::instance();
    cv::Size gs(cfg.gridSizeX(), cfg.gridSizeY());

    ctrl_.movementController().setImageSaveCallback([this](const cv::Mat& frame, const std::string& path, int row, int col) {
        // Display numbering: 1-indexed, columns right-to-left
        int gx = ConfigManager::instance().gridSizeX();
        int dispRow = row + 1;
        int dispCol = gx - col;  // physical col 0 → display col gx, physical col (gx-1) → display col 1
        std::string fn = path + "/" + std::to_string(dispRow) + "_" + std::to_string(dispCol) + ".jpg";
        bool ok = cv::imwrite(fn, frame);
        if (ok) {
            // Store with display coordinates (reverse column for table)
            int tableCol = gx - 1 - col; // table column index (col 0 = leftmost)
            {
                std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
                s_movement_images_[{row, tableCol}] = fn;
            }
            // Thumbnail generation in scan thread: avoid passing full-res frame to UI
            if (row < 10 && tableCol < 10) {
                // Center-crop to match stitching behavior, then resize to thumbnail
                int cropSize = ConfigManager::instance().centerCropSize();
                cv::Mat cropped;
                if (cropSize > 0 && cropSize < frame.cols && cropSize < frame.rows) {
                    int left = (frame.cols - cropSize) / 2;
                    int top = (frame.rows - cropSize) / 2;
                    cropped = frame(cv::Rect(left, top, cropSize, cropSize));
                } else {
                    cropped = frame;
                }
                cv::Mat thumb;
                cv::resize(cropped, thumb, cv::Size(50, 50), 0, 0, cv::INTER_AREA);
                QPixmap pix = QPixmap::fromImage(cvMatToQImage(thumb));
                QMetaObject::invokeMethod(this, [this, row, tableCol, pix]() {
                    auto* item = ui_->topCellsTable->item(row, tableCol);
                    if (item) {
                        auto* lbl = new QLabel();
                        lbl->setPixmap(pix);
                        lbl->setScaledContents(true);
                        lbl->setContentsMargins(0, 0, 0, 0);
                        lbl->setAlignment(Qt::AlignCenter);
                        lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                        ui_->topCellsTable->setCellWidget(row, tableCol, lbl);
                    }
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
        ui_->cameraImageLabel->setText("扫描中...");
        ui_->quickScanBtn->setText("停止扫描");
        ui_->quickPauseBtn->setEnabled(true);
        ui_->quickPauseBtn->setText("暂停扫描");
        appendLog("扫描已启动", "INFO");
    }
}

void MainWindow::on_toggleStartStopSMovement() {
    if (ctrl_.movementController().getStatus().running) {
        scan_stopped_by_user_ = true;
        ctrl_.stopSMovement();
        ui_->quickScanBtn->setText("开始扫描");
        ui_->quickPauseBtn->setEnabled(false);
        ui_->quickPauseBtn->setText("暂停扫描");
        return;
    }

    if (!ctrl_.cameraHandler().isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接相机");
        return;
    }
    if (!ctrl_.armController().isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接机械臂");
        return;
    }

    // Check if arm is already at zero
    if (isArmAtZero()) {
        // Already zeroed — start scanning immediately
        ctrl_.startCameraCapture();
        startScanSequence();
        return;
    }

    // Not at zero — run zero sequence, then stop (user clicks scan again)
    ctrl_.startCameraCapture();

    ui_->quickScanBtn->setEnabled(false);
    ui_->quickScanBtn->setText("正在归零...");

    auto* progress = new QProgressDialog("机械臂未归零，正在归零中...\n归零完成后请再次点击扫描", QString(), 0, 5, this);
    progress->setWindowTitle("归零中");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    progress->setCancelButton(nullptr);
    progress->show();

    auto* ctrl = &ctrl_;
    zero_progress_future_ = QtConcurrent::run([ctrl, progress]() {
        auto& armCtrl = ctrl->armController();
        QMetaObject::invokeMethod(progress, "setLabelText", Qt::QueuedConnection,
            Q_ARG(QString, QString("正在归零所有轴...")));
        QMetaObject::invokeMethod(progress, "setValue", Qt::QueuedConnection,
            Q_ARG(int, 1));
        armCtrl.moveAxesConcurrent(0, 0, 0);
        QMetaObject::invokeMethod(progress, "setValue", Qt::QueuedConnection,
            Q_ARG(int, 5));
        return true;
    });
    zero_progress_watcher_.setFuture(zero_progress_future_);
}

void MainWindow::onZeroProgressFinished() {
    auto* progress = findChild<QProgressDialog*>();
    if (progress) {
        progress->close();
        progress->deleteLater();
    }

    ui_->quickScanBtn->setEnabled(true);
    ui_->quickScanBtn->setText("开始扫描");
    appendLog("归零完成，请再次点击扫描", "INFO");
    QMessageBox::information(this, "归零完成", "归零完成，请重新点击开始扫描");
}

void MainWindow::on_togglePauseSMovement() {
    if (ctrl_.movementController().getStatus().paused) {
        ctrl_.resumeSMovement();
        ui_->quickPauseBtn->setText("暂停扫描");
    } else {
        ctrl_.pauseSMovement();
        ui_->quickPauseBtn->setText("继续扫描");
    }
}

// ==================== Detection ====================

void MainWindow::on_loadModel() {
    auto& cfg = ConfigManager::instance();
    DetectionSettingsDialog dlg(&ctrl_.detector(), this);
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
    DetectionSettingsDialog dlg(&ctrl_.detector(), this);
    if (dlg.exec() == QDialog::Accepted) {
        ctrl_.detector().setConfidenceThreshold(dlg.confidenceThreshold());
        ctrl_.detector().setNmsThreshold(dlg.nmsThreshold());
    }
}

void MainWindow::on_manualDetect() {
    if (!model_loaded_) {
        QMessageBox::warning(this, "警告", "请先加载检测模型");
        return;
    }
    if (current_image_.empty()) {
        QMessageBox::warning(this, "警告", "请先打开或拍摄一张图像");
        return;
    }

    // Single-frame detection: clone current frame, run async, show result once
    cv::Mat img = current_image_.clone();
    auto* ctrl = &ctrl_;
    manual_detect_future_ = QtConcurrent::run([ctrl, img]() -> DetectionResult {
        DetectionResult result;
        result.frame = img;
        result.detections = ctrl->detect(img);
        return result;
    });
    manual_detect_watcher_.setFuture(manual_detect_future_);
}

// ==================== Stitching ====================

void MainWindow::on_stitchRun() {
    auto& cfg = ConfigManager::instance();
    cv::Size gs(cfg.gridSizeX(), cfg.gridSizeY());

    // Folder selection dialog (same style as workspace selection)
    QString defaultPath = QString::fromStdString(cfg.imageSaveBasePath());
    QDialog dlg(this);
    dlg.setWindowTitle("选择拼接图像文件夹");
    dlg.setMinimumWidth(500);
    auto* vlay = new QVBoxLayout(&dlg);
    vlay->addWidget(new QLabel("请选择包含扫描图像的文件夹：", &dlg));

    auto* hlay = new QHBoxLayout();
    auto* pathEdit = new QLineEdit(defaultPath, &dlg);
    hlay->addWidget(pathEdit);
    auto* browseBtn = new QPushButton("浏览...", &dlg);
    hlay->addWidget(browseBtn);
    vlay->addLayout(hlay);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* okBtn = new QPushButton("确认", &dlg);
    auto* cancelBtn = new QPushButton("取消", &dlg);
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    vlay->addLayout(btnLayout);

    connect(browseBtn, &QPushButton::clicked, [&]() {
        QString dir = QFileDialog::getExistingDirectory(&dlg, "选择目录", pathEdit->text(),
                                                        QFileDialog::ShowDirsOnly);
        if (!dir.isEmpty()) pathEdit->setText(dir);
    });
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted || pathEdit->text().isEmpty()) return;

    std::string dirPath = pathEdit->text().toStdString();

    // Use position-based loading: parse row_col from filenames (like docs/stitch.py)
    cv::Size detectedGrid = gs;
    auto positioned = ctrl_.loadImagesWithPositions(dirPath, detectedGrid);

    if (positioned.empty()) {
        // Fallback: legacy sequential loading
        auto images = ctrl_.loadImages(dirPath);
        if (images.empty()) {
            QMessageBox::warning(this, "警告",
                QString("目录 %1 中没有图像。\n请先执行扫描采集图像").arg(pathEdit->text()));
            return;
        }
        // Confirm before stitching
        auto btn = QMessageBox::question(this, "确认拼接",
            QString("已选择 %1 张图像，是否开始拼接？").arg(images.size()),
            QMessageBox::Yes | QMessageBox::No);
        if (btn != QMessageBox::Yes) return;

        stitch_progress_dlg_ = new QProgressDialog("正在拼接图像...", QString(), 0, 100, this);
        stitch_progress_dlg_->setWindowModality(Qt::WindowModal);
        stitch_progress_dlg_->setAutoClose(false);
        stitch_progress_dlg_->show();

        stitch_progress_->setValue(0);
        stitch_progress_->setVisible(true);
        ui_->statusLabel->setText("正在拼接...");
        stitching_future_ = QtConcurrent::run([this, images, gs]() {
            auto sorted = ctrl_.stitcher().sortImagesInSCurveOrder(images, gs);
            return ctrl_.stitcher().stitchImages(sorted, gs);
        });
        stitching_watcher_.setFuture(stitching_future_);
        return;
    }

    // Confirm before stitching
    auto btn = QMessageBox::question(this, "确认拼接",
        QString("已选择 %1 张图像 (网格 %2x%3)，是否开始拼接？")
            .arg(positioned.size())
            .arg(detectedGrid.width)
            .arg(detectedGrid.height),
        QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    stitch_progress_dlg_ = new QProgressDialog("正在拼接图像...", QString(), 0, 100, this);
    stitch_progress_dlg_->setWindowModality(Qt::WindowModal);
    stitch_progress_dlg_->setAutoClose(false);
    stitch_progress_dlg_->show();

    stitch_progress_->setValue(0);
    stitch_progress_->setVisible(true);
    ui_->statusLabel->setText("正在拼接...");

    // Apply center crop setting from config
    ctrl_.stitcher().setCenterCropSize(ConfigManager::instance().centerCropSize());

    stitching_future_ = QtConcurrent::run([this, positioned, detectedGrid]() {
        return ctrl_.stitcher().stitchImagesWithPositions(positioned, detectedGrid);
    });
    stitching_watcher_.setFuture(stitching_future_);
}

void MainWindow::onStitchingFinished() {
    // Close progress dialog after showing 100%
    if (stitch_progress_dlg_) {
        stitch_progress_dlg_->setValue(100);
        stitch_progress_->setValue(100);
        stitch_progress_dlg_->close();
        stitch_progress_dlg_->deleteLater();
        stitch_progress_dlg_ = nullptr;
    }

    stitch_progress_->setVisible(false);
    cv::Mat result = stitching_future_.result();
    if (!result.empty()) {
        stitched_result_ = result;
        displayImageFullQuality(result, ui_->stitchImageLabel, stitched_pixmap_);
        // Auto-save to workspace
        QString ws = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
        QDir().mkpath(ws);
        QString savePath = ws + "/stitched_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
        std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 0};
        cv::imwrite(savePath.toStdString(), result, params);
        ui_->quickSaveBtn->setEnabled(true);
        ui_->statusLabel->setText("拼接完成");
        QMessageBox::information(this, "拼接完成",
            QString("图像拼接已完成！\n已保存至：%1").arg(savePath));
    } else {
        ui_->statusLabel->setText("拼接失败");
        QMessageBox::warning(this, "拼接失败", "图像拼接失败，请检查图像文件。");
    }
}

void MainWindow::on_stitchSettings() {
    auto& cfg = ConfigManager::instance();
    StitchingSettingsDialog dlg(this);
    dlg.setGridSizeX(cfg.gridSizeX());
    dlg.setGridSizeY(cfg.gridSizeY());
    dlg.setCenterCropSize(cfg.centerCropSize());
    dlg.setInputDir(QString::fromStdString(cfg.imageSaveBasePath()));
    if (dlg.exec() == QDialog::Accepted) {
        cfg.setGridSizeX(dlg.gridSizeX());
        cfg.setGridSizeY(dlg.gridSizeY());
        cfg.setCenterCropSize(dlg.centerCropSize());
        cfg.setImageSaveBasePath(dlg.inputDir().toStdString());
    }
}

void MainWindow::on_saveStitch() {
    if (stitched_result_.empty()) {
        QMessageBox::warning(this, "警告", "没有拼接结果");
        return;
    }
    QString ws = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
    QString defPath = ws + "/stitched.png";
    QString path = QFileDialog::getSaveFileName(this, "保存拼接结果", defPath,
        "PNG (*.png);;BMP (*.bmp);;TIFF (*.tiff)");
    if (!path.isEmpty()) cv::imwrite(path.toStdString(), stitched_result_);
}

// ==================== Camera Preview (event-driven, via signal) ====================

void MainWindow::onCameraFrameReady(const cv::Mat& frame) {
    if (frame.empty()) return;

    // Preview checkbox controls whether to render — unchecked = skip all processing
    if (!camera_preview_check_ || !camera_preview_check_->isChecked()) return;

    current_image_ = frame;

    // FPS counting (per-second window)
    fps_frame_count_++;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - last_fps_timestamp_ >= 1000) {
        current_fps_ = fps_frame_count_ * 1000.0f / (now - last_fps_timestamp_);
        fps_frame_count_ = 0;
        last_fps_timestamp_ = now;
    }
    if (last_fps_timestamp_ == 0) last_fps_timestamp_ = now;
    if (fps_label_)
        fps_label_->setText(QString::number(static_cast<int>(current_fps_)) + " FPS");

    // Display (frame is pre-scaled RGB from CameraHandler)
    QImage img(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    ui_->cameraImageLabel->setPixmap(QPixmap::fromImage(img));

    // Real-time detection (if enabled)
    if (image_detection_enabled_ && model_loaded_ && !detection_busy_) {
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

    // Keep camera fullscreen live
    if (camera_fullscreen_active_ && fullscreen_dlg_) {
        QPixmap pix = QPixmap::fromImage(cvMatToQImage(current_image_));
        fullscreen_dlg_->setPixmap(pix.scaled(fullscreen_dlg_->screen()->size(),
                                              Qt::KeepAspectRatio, Qt::FastTransformation));
    }
}

void MainWindow::onDetectionFinished() {
    detection_busy_ = false;
    DetectionResult result = detection_future_.result();
    if (!result.frame.empty()) {
        cv::Mat annotated = ctrl_.drawDetections(result.frame, result.detections);
        // Detection overlay ONLY on the detection preview panel (bottom-right),
        // NOT on the camera preview (left) — camera stays clean
        displayImageFullQuality(annotated, ui_->detectImageLabel, detected_pixmap_);

        // Keep detection fullscreen live — continues updating in real-time
        if (detect_fullscreen_active_ && fullscreen_dlg_) {
            QPixmap pix = QPixmap::fromImage(cvMatToQImage(annotated));
            fullscreen_dlg_->setPixmap(pix.scaled(fullscreen_dlg_->screen()->size(),
                                                  Qt::KeepAspectRatio, Qt::FastTransformation));
        }
    }
}

// ==================== Callbacks ====================

void MainWindow::onArmStatusChanged(const arm::ArmStatus& status) {
    if (status.current_positions.size() >= 5) {
        ui_->xPosSpin->setValue(status.current_positions[0]);
        ui_->yPosSpin->setValue(status.current_positions[1]);
        ui_->zPosSpin->setValue(status.current_positions[2]);
        // Update realtime position label
        ui_->posRealtimeLabel->setText(
            QString("当前位置: X=%1  Y=%2  Z=%3  A=%4  B=%5")
                .arg(status.current_positions[0])
                .arg(status.current_positions[1])
                .arg(status.current_positions[2])
                .arg(status.current_positions[3])
                .arg(status.current_positions[4]));
    }
    ui_->armStatusLabel->setToolTip(QString::fromStdString(status.status_message));
}

void MainWindow::onMovementStatus(const arm::SMovementStatus& status) {
    // Throttle per-point updates: only log meaningful status changes
    if (!status.status_message.empty()
        && status.status_message.find("Moving") == std::string::npos
        && status.status_message.find("Arrived") == std::string::npos) {
        appendLog(QString::fromStdString(status.status_message), "INFO");
    }
    // Throttle progress: update every 5 points or at completion
    if (status.total_points > 0
        && (status.current_point % 5 == 0 || status.current_point >= status.total_points)) {
        appendLog(QString("采集进度: %1/%2").arg(status.current_point).arg(status.total_points), "INFO");
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
        ui_->quickScanBtn->setText("开始扫描");
        ui_->quickPauseBtn->setEnabled(false);
        ui_->quickPauseBtn->setText("暂停扫描");

        if (scan_stopped_by_user_) {
            scan_stopped_by_user_ = false;
            appendLog("S型扫描已停止", "WARN");
            ui_->statusLabel->setText("扫描已停止");
        } else {
            appendLog("S型扫描完成", "INFO");
            ui_->statusLabel->setText("扫描完成");
            QMessageBox::information(this, "扫描完成", "扫描已完成！");
        }
    }
}

// ==================== Grid ====================

void MainWindow::on_topCellsTable_cellClicked(int /*row*/, int /*column*/) {
    // Handled via eventFilter (left-click → fullscreen preview) to avoid
    // Qt double-emission of cellClicked for cell + cellWidget.
}

// ==================== Helpers ====================

void MainWindow::displayImage(const cv::Mat& image, QLabel* label) {
    if (image.empty()) return;
    QPixmap pix = QPixmap::fromImage(cvMatToQImage(image));
    label->setPixmap(pix.scaled(label->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
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

    // Red text: resolution + FPS
    std::string text = std::to_string(status.width) + "x" + std::to_string(status.height)
                     + "  " + std::to_string(static_cast<int>(current_fps_)) + " FPS";

    int font = cv::FONT_HERSHEY_SIMPLEX;
    double scale = 1.3;
    cv::Scalar color(0, 0, 255);        // red text (BGR)
    int thickness = 3;

    int margin = 10;
    cv::Point origin(margin, image.rows - margin);
    cv::putText(image, text, origin, font, scale, color, thickness);
}

void MainWindow::displayImageFullQuality(const cv::Mat& image, QLabel* label, QPixmap& storage) {
    if (image.empty()) return;
    storage = QPixmap::fromImage(cvMatToQImage(image));
    label->setPixmap(storage.scaled(label->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    // Grid preview: close on ESC or click
    if (obj == preview_dlg_) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress) {
            preview_dlg_->close();
            preview_dlg_ = nullptr;
            return true;
        }
        return false;
    }

    // Fullscreen dialog: close on ESC or click
    if (obj == fullscreen_dlg_) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                fullscreen_dlg_->close();
                fullscreen_dlg_ = nullptr;
                camera_fullscreen_active_ = false;
                detect_fullscreen_active_ = false;
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            fullscreen_dlg_->close();
            fullscreen_dlg_ = nullptr;
            camera_fullscreen_active_ = false;
            detect_fullscreen_active_ = false;
            return true;
        }
        return false;
    }

    // Grid table: left-click → fullscreen preview, middle-click → arm movement
    if (obj == ui_->topCellsTable->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        auto* item = ui_->topCellsTable->itemAt(me->pos());
        if (!item) return false;

        if (me->button() == Qt::LeftButton) {
            // Fullscreen image preview
            if (preview_dlg_) return true; // guard double-popup
            int row = item->row(), col = item->column();
            std::string img_path;
            {
                std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
                auto it = s_movement_images_.find({row, col});
                if (it != s_movement_images_.end()) img_path = it->second;
            }
            if (!img_path.empty()) {
                cv::Mat img = cv::imread(img_path);
                if (!img.empty()) {
                    auto* dlg = new QLabel(nullptr, Qt::Window | Qt::FramelessWindowHint);
                    preview_dlg_ = dlg;
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->setPixmap(QPixmap::fromImage(cvMatToQImage(img))
                                   .scaled(dlg->screen()->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    dlg->setAlignment(Qt::AlignCenter);
                    dlg->setStyleSheet("background-color: black;");
                    dlg->setCursor(Qt::CrossCursor);
                    dlg->installEventFilter(this);
                    dlg->showFullScreen();
                    connect(dlg, &QObject::destroyed, this, [this]() { preview_dlg_ = nullptr; });
                }
            }
            return true;
        }

        if (me->button() == Qt::MiddleButton && ui_->cellMoveCheck->isChecked()
            && ctrl_.armController().isConnected()) {
            int row = item->row(), col = item->column();
            int step = ConfigManager::instance().stepSize();
            int gx = ConfigManager::instance().gridSizeX();
            int actualCol = gx - 1 - col;
            int x = actualCol * step, y = row * step, z = ConfigManager::instance().zHeight();
            auto* ctrl = &ctrl_;
            QtConcurrent::run([ctrl, x, y, z]() {
                ctrl->armController().moveAxesConcurrent(x, y, z);
            });
            return true;
        }
    }

    // Stitch / Detect preview: single-click to fullscreen
    if (event->type() == QEvent::MouseButtonPress) {
        if (obj == ui_->stitchImageLabel) {
            QPixmap pix = ui_->stitchImageLabel->pixmap();
            if (!pix.isNull()) {
                showImageFullscreen(pix);
                return true;
            }
        }
        if (obj == ui_->detectImageLabel) {
            QPixmap pix = !detected_pixmap_.isNull() ? detected_pixmap_
                : !current_image_.empty() ? QPixmap::fromImage(cvMatToQImage(current_image_))
                : QPixmap();
            if (!pix.isNull()) {
                showImageFullscreen(pix);
                detect_fullscreen_active_ = true;
                return true;
            }
        }
    }

    // Camera preview: double-click to fullscreen
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (obj == ui_->cameraImageLabel && !current_image_.empty()) {
            showImageFullscreen(QPixmap::fromImage(cvMatToQImage(current_image_)));
            camera_fullscreen_active_ = true;
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
