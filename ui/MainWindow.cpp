// MSVC: windows.h must be first to prevent WDK/OpenCV header conflicts
#ifdef _MSC_VER
#define NOMINMAX
#include <windows.h>
#endif

#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/AppController.h"
#include "ui/DetectionSettingsDialog.h"
#include "ui/StitchingSettingsDialog.h"
#include "ui/SphereSettingsDialog.h"
#include "ui/ScanParametersDialog.h"
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
#include <QMenu>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <cmath>
#include <filesystem>
#include <regex>

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
    ui_->negativeCheck->setEnabled(false);
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
    ui_->scanNegativeCheck->setEnabled(false);
    ui_->scanNegativeCheck->setChecked(false);
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
    connect(&negative_stitching_watcher_, &QFutureWatcher<cv::Mat>::finished, this, [this]() {
        onNegativeStitchingFinished();
    });
    connect(&manual_detect_watcher_, &QFutureWatcher<DetectionResult>::finished, this, [this]() {
        DetectionResult result = manual_detect_future_.result();
        if (!result.frame.empty()) {
            cv::Mat annotated = ctrl_.drawDetections(result.frame, result.detections);
            displayImageFullQuality(annotated, ui_->detectImageLabel, detected_pixmap_);
            appendLog(QString("手动检测完成: %1 个目标").arg(result.detections.size()), "INFO");
            // 导出检测结果（类型/尺寸/位置）到扫描目录下的 JSON
            std::string jsonPath = ctrl_.saveDetectionsJson(
                result.detections, result.frame, current_image_source_);
            if (!jsonPath.empty())
                appendLog(QString("检测结果已导出: %1").arg(QString::fromStdString(jsonPath)), "INFO");
        }
    });
    connect(&arm_connect_watcher_, &QFutureWatcher<bool>::finished, this, &MainWindow::onArmConnectFinished);
    connect(&arm_move_watcher_, &QFutureWatcher<void>::finished, this, &MainWindow::onArmMoveFinished);
    connect(&frame_replace_watcher_, &QFutureWatcher<std::pair<cv::Mat,bool>>::finished, this, [this]() {
        auto [frame, ok] = frame_replace_future_.result();
        int row = frame_replace_row_, col = frame_replace_col_;
        if (!ok || frame.empty()) {
            QMessageBox::warning(this, "错误", "相机采集帧失败，请检查相机连接");
            return;
        }
        if (row < 0 || col < 0) return;
        // Thumbnail update
        int cropSize = ConfigManager::instance().centerCropSize();
        cv::Mat cropped;
        if (cropSize > 0 && cropSize < frame.cols && cropSize < frame.rows) {
            int left = (frame.cols - cropSize) / 2;
            int top  = (frame.rows - cropSize) / 2;
            cropped = frame(cv::Rect(left, top, cropSize, cropSize));
        } else {
            cropped = frame;
        }
        cv::Mat thumbOrig, thumbNeg;
        cv::resize(cropped, thumbOrig, cv::Size(50, 50), 0, 0, cv::INTER_AREA);
        cv::bitwise_not(thumbOrig, thumbNeg);
        QPixmap pixOrig = QPixmap::fromImage(cvMatToQImage(thumbOrig));
        QPixmap pixNeg = QPixmap::fromImage(cvMatToQImage(thumbNeg));
        {
            std::lock_guard<std::mutex> lock(grid_thumbnails_mutex_);
            grid_thumbnails_[{row, col}] = {pixOrig, pixNeg};
        }
        QPixmap pix = scan_showing_negative_ ? pixNeg : pixOrig;
        auto* cellItem = ui_->topCellsTable->item(row, col);
        if (cellItem) {
            auto* lbl = qobject_cast<QLabel*>(ui_->topCellsTable->cellWidget(row, col));
            if (lbl) {
                lbl->setPixmap(pix);
            } else {
                auto* newLbl = new QLabel();
                newLbl->setPixmap(pix);
                newLbl->setScaledContents(true);
                newLbl->setContentsMargins(0, 0, 0, 0);
                newLbl->setAlignment(Qt::AlignCenter);
                newLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                ui_->topCellsTable->setCellWidget(row, col, newLbl);
            }
        }
        appendLog(QString("单元格 [行%1,列%2] 帧已替换").arg(row+1).arg(col+1), "INFO");
    });
    connect(&zstack_watcher_, &QFutureWatcher<ZStackResult>::finished, this, &MainWindow::onZStackFinished);
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

    // Negative film display toggle (in stitch area, after progress bar)
    negative_toggle_btn_ = new QPushButton("显示负片结果", this);
    negative_toggle_btn_->setCheckable(true);
    negative_toggle_btn_->setVisible(false);
    negative_toggle_btn_->setEnabled(false);
    if (stitch_progress_->parentWidget()) {
        auto* parentLayout = stitch_progress_->parentWidget()->layout();
        if (parentLayout) {
            parentLayout->addWidget(negative_toggle_btn_);
        }
    }
    connect(negative_toggle_btn_, &QPushButton::toggled, this, [this](bool checked) {
        stitch_showing_negative_ = checked;
        const cv::Mat& mat = (checked && !stitched_result_negative_.empty())
            ? stitched_result_negative_ : stitched_result_;
        if (!mat.empty()) {
            displayImageFullQuality(mat, ui_->stitchImageLabel, stitched_pixmap_);
        }
        negative_toggle_btn_->setText(checked ? "显示原始结果" : "显示负片结果");
    });

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
                    tlay->insertWidget(tlay->indexOf(ui_->enableDetectionCheck) + 1, ui_->scanNegativeCheck);
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
        QString wd = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
        if (wd.isEmpty() || !QFileInfo::exists(wd)) wd = QCoreApplication::applicationDirPath();
        QString path = QFileDialog::getOpenFileName(this, "打开图像", wd, "图像 (*.jpg *.png *.bmp)");
        if (!path.isEmpty()) {
            current_image_ = cv::imread(path.toStdString());
            if (!current_image_.empty()) {
                current_image_source_ = path.toStdString();
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

        // ── Collect file list only (fast, no imread) ──
        std::regex namePattern(R"(^(\d+)_(\d+)(?:_(\d+))?$)");
        QDir qdir(dir);
        std::vector<std::pair<std::pair<int,int>, QString>> fileList;
        for (const auto& fi : qdir.entryInfoList({"*.jpg", "*.png", "*.bmp"}, QDir::Files)) {
            std::string name = fi.baseName().toStdString();
            std::smatch m;
            if (!std::regex_match(name, m, namePattern)) continue;
            int r = std::stoi(m[1].str());
            int c = std::stoi(m[2].str());
            fileList.push_back({{r, c}, fi.absoluteFilePath()});
        }
        if (fileList.empty()) {
            QMessageBox::warning(this, "警告", "文件夹中没有 {row}_{col}.jpg 格式的图像。");
            return;
        }

        // ── Prepare UI state ──
        last_scan_dir_ = dir.toStdString();
        ctrl_.setScanDir(dir.toStdString());
        auto& cfg = ConfigManager::instance();
        cv::Size gs(cfg.gridSizeX(), cfg.gridSizeY());
        {
            std::lock_guard<std::mutex> lock(scan_state_mutex_);
            scan_base_dir_ = dir.toStdString();
        }
        {
            std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
            s_movement_images_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(grid_thumbnails_mutex_);
            grid_thumbnails_.clear();
        }
        ui_->topCellsTable->clearContents();
        ui_->statusLabel->setText("正在加载图像...");

        // ── Async: imread + thumbnail generation in worker thread ──
        if (folder_load_watcher_) {
            folder_load_watcher_->disconnect();
            folder_load_watcher_->deleteLater();
        }
        folder_load_watcher_ = new QFutureWatcher<FolderLoadResult>(this);
        connect(folder_load_watcher_, &QFutureWatcher<FolderLoadResult>::finished, this,
                [this, dir, gs]() {
            ui_->statusLabel->setText("就绪");
            auto result = folder_load_watcher_->result();
            if (result.count == 0) {
                appendLog("加载失败: 没有可读取的图像", "WARN");
                return;
            }
            // Populate table from precomputed data
            for (int i = 0; i < (int)result.thumbnails.size(); ++i) {
                int row = i / gs.width;
                int col = i % gs.width;
                if (row >= gs.height) break;
                const std::string& fn = result.cellFiles[i];
                if (fn.empty()) continue;
                {
                    std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
                    s_movement_images_[{row, col}] = fn;
                }
                {
                    std::lock_guard<std::mutex> lock(grid_thumbnails_mutex_);
                    grid_thumbnails_[{row, col}] = {result.thumbnails[i], result.thumbnails[i]};
                }
                QPixmap pix = scan_showing_negative_ ? result.thumbnails[i] : result.thumbnails[i];
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
            appendLog(QString("已加载 %1 张图像: %2").arg(result.count).arg(dir), "INFO");
            ui_->scanNegativeCheck->setEnabled(true);
        });

        int cropSize = ConfigManager::instance().centerCropSize();
        folder_load_watcher_->setFuture(QtConcurrent::run([fileList, gs, cropSize]() -> FolderLoadResult {
            FolderLoadResult r;
            r.gridSize = gs;
            r.cellFiles.resize(gs.width * gs.height);
            r.thumbnails.resize(gs.width * gs.height);
            for (auto& [rc, path] : fileList) {
                int row = rc.first - 1, col = rc.second - 1;  // 1-indexed → 0-indexed
                if (row < 0 || row >= gs.height || col < 0 || col >= gs.width) continue;
                cv::Mat img = cv::imread(path.toStdString());
                if (img.empty()) continue;
                int idx = row * gs.width + col;
                r.cellFiles[idx] = path.toStdString();
                // Thumbnail: center-crop → resize → QPixmap
                cv::Mat cropped;
                if (cropSize > 0 && cropSize < img.cols && cropSize < img.rows) {
                    int left = (img.cols - cropSize) / 2;
                    int top  = (img.rows - cropSize) / 2;
                    cropped = img(cv::Rect(left, top, cropSize, cropSize));
                } else {
                    cropped = img;
                }
                cv::Mat thumb;
                cv::resize(cropped, thumb, cv::Size(50, 50), 0, 0, cv::INTER_AREA);
                r.thumbnails[idx] = QPixmap::fromImage(
                    QImage(thumb.data, thumb.cols, thumb.rows, thumb.step, QImage::Format_BGR888).copy());
                r.count++;
            }
            return r;
        }));
    });

    connect(ui_->actionImportConfig, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "导入配置", QCoreApplication::applicationDirPath(), "JSON (*.json)");
        if (!path.isEmpty()) {
            ConfigManager::instance().loadFromFile(path.toStdString());
            ui_->statusLabel->setText("配置已导入: " + path);
        }
    });
    connect(ui_->actionExportConfig, &QAction::triggered, this, [this]() {
        // Symmetric with 导入配置: let the user choose where to write, defaulting to
        // the app directory's config.json (the same file the app actually loads).
        QString defPath = QCoreApplication::applicationDirPath() + "/config.json";
        QString path = QFileDialog::getSaveFileName(this, "导出配置", defPath, "JSON (*.json)");
        if (path.isEmpty()) return;
        if (ConfigManager::instance().saveToFile(path.toStdString())) {
            ui_->statusLabel->setText("配置已导出: " + path);
            appendLog("配置已导出: " + path, "INFO");
        } else {
            QMessageBox::warning(this, "警告", "配置导出失败");
        }
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
    // ---- 扫描参数设置 ----
    connect(ui_->actionScanParams, &QAction::triggered, this, [this]() {
        auto& cfg = ConfigManager::instance();
        ScanParametersDialog dlg(this);
        dlg.setDwellTimeMs(cfg.dwellTimeMs());
        if (dlg.exec() == QDialog::Accepted) {
            cfg.setDwellTimeMs(dlg.dwellTimeMs());
            cfg.saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
            appendLog(tr("扫描参数已更新: 停留时间=%1 ms").arg(dlg.dwellTimeMs()), "INFO");
        }
    });

    // ---- Z 轴参数设置 ----
    connect(ui_->actionSphereSettings, &QAction::triggered, this, [this]() {
        auto& cfg = ConfigManager::instance();
        SphereSettingsDialog dlg(this);
        dlg.setZMode(cfg.zMode());
        dlg.setSphereRadius(cfg.sphereRadius());
        dlg.setSphereCapHeight(cfg.sphereCapHeight());
        dlg.setSphereHeightOffset(cfg.sphereHeightOffset());
        dlg.setZBaseHeight(cfg.zBaseHeight());
        dlg.setZMapFile(cfg.zMapFile());
        dlg.setZRadialFile(cfg.zRadialFile());
        if (dlg.exec() == QDialog::Accepted) {
            int mode = dlg.zMode();
            if (mode == 0) {
                // Spherical cap mode — validate constraints
                int dH = dlg.sphereHeightOffset();
                int h  = dlg.sphereCapHeight();
                int zBase = dlg.zBaseHeight();
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
                appendLog(tr("球冠参数已更新: R=%1, h=%2, dH=%3, zBase=%4")
                    .arg(dlg.sphereRadius()).arg(dlg.sphereCapHeight())
                    .arg(dlg.sphereHeightOffset()).arg(dlg.zBaseHeight()), "INFO");
            } else if (mode == 1) {
                // Manual per-position Z-map mode
                std::string mapPath = dlg.zMapFile();
                if (!mapPath.empty()) {
                    cfg.setZMapFile(mapPath);
                    appendLog(tr("Z-Map 文件已设置: %1").arg(QString::fromStdString(mapPath)), "INFO");
                }
            } else {
                // Radial Z-map mode (mode == 2)
                std::string radialPath = dlg.zRadialFile();
                if (!radialPath.empty()) {
                    cfg.setZRadialFile(radialPath);
                    appendLog(tr("径向 Z-Map 文件已设置: %1").arg(QString::fromStdString(radialPath)), "INFO");
                }
            }
            cfg.setZMode(mode);
            cfg.saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
            static const char* modeNames[] = {"球冠补偿", "手动 Z-Map", "径向 Z-Map"};
            appendLog(tr("Z 轴模式已切换为: %1").arg(modeNames[mode]), "INFO");
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
    connect(ui_->negativeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        ctrl_.cameraHandler().setNegative(checked);
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

    // ---- 扫描网格负片显示 ----
    connect(ui_->scanNegativeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        scan_showing_negative_ = checked;
        std::lock_guard<std::mutex> lock(grid_thumbnails_mutex_);
        for (auto& [rc, pixmaps] : grid_thumbnails_) {
            auto* lbl = qobject_cast<QLabel*>(ui_->topCellsTable->cellWidget(rc.first, rc.second));
            if (lbl) lbl->setPixmap(checked ? pixmaps.second : pixmaps.first);
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
    // 依当前检测算法刷新就绪状态：灰尘算法无需模型即就绪，可直接检测
    model_loaded_ = ctrl_.isModelLoaded();
    connect(&ctrl_, &AppController::stitchingFinished, this, [this](const cv::Mat& result) {
        stitched_result_ = result;
        stitched_result_negative_ = cv::Mat();  // 清除旧负片结果
        displayImageFullQuality(result, ui_->stitchImageLabel, stitched_pixmap_);
        ui_->quickSaveBtn->setEnabled(true);
        if (negative_toggle_btn_) {
            negative_toggle_btn_->setVisible(false);
            negative_toggle_btn_->setEnabled(false);
            negative_toggle_btn_->setChecked(false);
        }
        stitch_showing_negative_ = false;

        // 如果扫描目录中存在负片图像，异步拼接
        std::string scanDirCopy;
        {
            std::lock_guard<std::mutex> lock(scan_state_mutex_);
            scanDirCopy = scan_base_dir_;
        }
        if (!scanDirCopy.empty() && hasNegativeImages(scanDirCopy)) {
            int gx = ConfigManager::instance().gridSizeX();
            int gy = ConfigManager::instance().gridSizeY();
            appendLog("检测到负片图像，开始负片拼接...", "INFO");
            startNegativeStitching(scanDirCopy, cv::Size(gx, gy));
        }
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

    // 第3象限坐标系布局：行号在右侧，右上角为扫描起点 (row=1, col=1)
    // RTL: column 0 → 右侧，column gx-1 → 左侧，vertical header 自然出现在右侧
    QStringList colHeaders, rowHeaders;
    for (int i = 1; i <= gx; ++i) colHeaders << QString::number(i);
    for (int i = 1; i <= gy; ++i) rowHeaders << QString::number(i);

    ui_->topCellsTable->setLayoutDirection(Qt::RightToLeft);
    ui_->topCellsTable->setRowCount(gy);
    ui_->topCellsTable->setColumnCount(gx);
    ui_->topCellsTable->setHorizontalHeaderLabels(colHeaders);
    ui_->topCellsTable->setVerticalHeaderLabels(rowHeaders);
    ui_->topCellsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui_->topCellsTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui_->topCellsTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    ui_->topCellsTable->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    ui_->topCellsTable->setStyleSheet(
        "#topCellsTable { gridline-color: #16191D; border: none; }"
        "QHeaderView::section { border: none; }"
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

// Thread-safe log append — can be called from any thread.
void MainWindow::appendLogSafe(const QString& msg, const QString& level) {
    QMetaObject::invokeMethod(this, [this, msg, level]() {
        appendLog(msg, level);
    }, Qt::QueuedConnection);
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
        if (ui_->negativeCheck) ui_->negativeCheck->setEnabled(true);
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
    if (ui_->negativeCheck) ui_->negativeCheck->setEnabled(false);
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

        // Save capture to Documents with timestamp (imwrite async)
        QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      + "/ScannerData/captures";
        QDir().mkpath(dir);
        auto now = QDateTime::currentDateTime();
        QString ts = now.toString("yyyyMMdd_HHmmss");
        QString filename = dir + "/capture_" + ts + ".jpg";
        QString negDir = dir + "/negative";
        QDir().mkpath(negDir);
        QString negFn = negDir + "/capture_" + ts + ".jpg";
        current_image_source_ = filename.toStdString();
        appendLog(QString("捕获已保存: %1").arg(filename), "INFO");

        // Async imwrite (fire-and-forget)
        QtConcurrent::run([frame, filename, negFn]() {
            cv::imwrite(filename.toStdString(), frame);
            cv::Mat neg;
            cv::bitwise_not(frame, neg);
            cv::imwrite(negFn.toStdString(), neg);
        });
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
        ui_->scanNegativeCheck->setEnabled(true);
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
    {
        std::lock_guard<std::mutex> lock(scan_state_mutex_);
        scan_base_dir_.clear();  // 新扫描开始，清除旧目录
    }
    {
        std::lock_guard<std::mutex> lock(grid_thumbnails_mutex_);
        grid_thumbnails_.clear();
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
        static bool dirs_created = false;
        if (!dirs_created) {
            std::filesystem::create_directories(origDir);
            std::filesystem::create_directories(negDir);
            dirs_created = false; // reset for next call (static but scoped per lambda)
            // Actually, create_directories is idempotent — just call every time
        }
        std::filesystem::create_directories(origDir);
        std::filesystem::create_directories(negDir);

        // 文件名编号 1-indexed；RTL 表格布局下 col=0 在右侧（扫描起点）
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
            // tableCol == physical col；RTL 下 col=0 出现在右侧（扫描起点）
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
                    int left = (frame.cols - cropSize) / 2;
                    int top = (frame.rows - cropSize) / 2;
                    cropped = frame(cv::Rect(left, top, cropSize, cropSize));
                } else {
                    cropped = frame;
                }
                // Original thumbnail
                cv::Mat thumbOrig;
                cv::resize(cropped, thumbOrig, cv::Size(50, 50), 0, 0, cv::INTER_AREA);
                QPixmap pixOrig = QPixmap::fromImage(cvMatToQImage(thumbOrig));
                // Negative thumbnail
                cv::Mat thumbNeg;
                cv::bitwise_not(thumbOrig, thumbNeg);
                QPixmap pixNeg = QPixmap::fromImage(cvMatToQImage(thumbNeg));
                // Store both
                {
                    std::lock_guard<std::mutex> lock(grid_thumbnails_mutex_);
                    grid_thumbnails_[{row, tableCol}] = {pixOrig, pixNeg};
                }
                QPixmap pix = scan_showing_negative_ ? pixNeg : pixOrig;
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
    zero_progress_dlg_ = progress;
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
    if (zero_progress_dlg_) {
        zero_progress_dlg_->close();
        zero_progress_dlg_->deleteLater();
        zero_progress_dlg_ = nullptr;
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
    DetectionSettingsDialog dlg(&ctrl_, this);
    dlg.setDetectionAlgorithm(cfg.detectionAlgorithm());
    dlg.setParamPath(QString::fromStdString(cfg.modelParamPath()));
    dlg.setBinPath(QString::fromStdString(cfg.modelBinPath()));
    dlg.setConfidenceThreshold(0.3);
    dlg.setNmsThreshold(0.3);
    dlg.setDustParams(ctrl_.dustParams());

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
    auto& cfg = ConfigManager::instance();

    // 快照当前状态，取消时回退实时测试对检测器的改动
    int prevAlgo = ctrl_.detectorAlgorithm();
    detector::DustDetectionParams prevDust = ctrl_.dustParams();
    float prevConf = ctrl_.detector().getConfidenceThreshold();
    float prevNms  = ctrl_.detector().getNmsThreshold();

    DetectionSettingsDialog dlg(&ctrl_, this);
    dlg.setDetectionAlgorithm(cfg.detectionAlgorithm());
    dlg.setParamPath(QString::fromStdString(cfg.modelParamPath()));
    dlg.setBinPath(QString::fromStdString(cfg.modelBinPath()));
    dlg.setConfidenceThreshold(ctrl_.detector().getConfidenceThreshold());
    dlg.setNmsThreshold(ctrl_.detector().getNmsThreshold());
    dlg.setDustParams(prevDust);

    if (dlg.exec() == QDialog::Accepted) {
        int algo = dlg.detectionAlgorithm();
        detector::DustDetectionParams dp = dlg.dustParams();

        // 持久化到 ConfigManager
        cfg.setDetectionAlgorithm(algo);
        cfg.setDustClaheClip(dp.claheClip);
        cfg.setDustBgBlur(dp.bgBlurSize);
        cfg.setDustMinArea(dp.minArea);
        cfg.setDustMaxArea(dp.maxArea);
        cfg.setDustDilateIter(dp.dilateIter);
        cfg.setDustMaxIter(dp.maxIter);
        cfg.setDustNmsIou(dp.nmsIou);

        // 应用到运行时检测器
        ctrl_.setDustParams(dp);
        ctrl_.setDetectorAlgorithm(algo);
        ctrl_.detector().setConfidenceThreshold(dlg.confidenceThreshold());
        ctrl_.detector().setNmsThreshold(dlg.nmsThreshold());
        model_loaded_ = ctrl_.isModelLoaded();
    } else {
        // 回退实时测试可能造成的算法/参数改动
        ctrl_.setDetectorAlgorithm(prevAlgo);
        ctrl_.setDustParams(prevDust);
        ctrl_.detector().setConfidenceThreshold(prevConf);
        ctrl_.detector().setNmsThreshold(prevNms);
        model_loaded_ = ctrl_.isModelLoaded();
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
    last_scan_dir_ = dirPath;  // 记录目录用于负片拼接
    ctrl_.setScanDir(dirPath);  // 同步给 AppController 供检测 JSON 导出定位

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
            ctrl_.stitcher().setCenterCropSize(ConfigManager::instance().centerCropSize());
            ctrl_.stitcher().setAlgorithm(ConfigManager::instance().stitchAlgorithm());
            ctrl_.stitcher().setFeatherWidth(ConfigManager::instance().featherWidth());
            ctrl_.stitcher().setScaleMode(ConfigManager::instance().scaleMode());
            ctrl_.stitcher().setScaleMapFile(ConfigManager::instance().scaleMapFile());
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
    ctrl_.stitcher().setAlgorithm(ConfigManager::instance().stitchAlgorithm());
    ctrl_.stitcher().setFeatherWidth(ConfigManager::instance().featherWidth());
    ctrl_.stitcher().setScaleMode(ConfigManager::instance().scaleMode());
    ctrl_.stitcher().setScaleMapFile(ConfigManager::instance().scaleMapFile());

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
        stitched_result_negative_ = cv::Mat();  // 清除旧负片结果
        displayImageFullQuality(result, ui_->stitchImageLabel, stitched_pixmap_);
        if (negative_toggle_btn_) {
            negative_toggle_btn_->setVisible(false);
            negative_toggle_btn_->setEnabled(false);
            negative_toggle_btn_->setChecked(false);
        }
        stitch_showing_negative_ = false;
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

        // 触发负片拼接
        if (!last_scan_dir_.empty() && hasNegativeImages(last_scan_dir_)) {
            int gx = ConfigManager::instance().gridSizeX();
            int gy = ConfigManager::instance().gridSizeY();
            startNegativeStitching(last_scan_dir_, cv::Size(gx, gy));
        }
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
    dlg.setStitchAlgorithm(cfg.stitchAlgorithm());
    dlg.setFeatherWidth(cfg.featherWidth());
    dlg.setScaleMode(cfg.scaleMode());
    dlg.setScaleMapFile(QString::fromStdString(cfg.scaleMapFile()));
    dlg.setInputDir(QString::fromStdString(cfg.imageSaveBasePath()));
    if (dlg.exec() == QDialog::Accepted) {
        cfg.setGridSizeX(dlg.gridSizeX());
        cfg.setGridSizeY(dlg.gridSizeY());
        cfg.setCenterCropSize(dlg.centerCropSize());
        cfg.setStitchAlgorithm(dlg.stitchAlgorithm());
        cfg.setFeatherWidth(dlg.featherWidth());
        cfg.setScaleMode(dlg.scaleMode());
        cfg.setScaleMapFile(dlg.scaleMapFile().toStdString());
        cfg.setImageSaveBasePath(dlg.inputDir().toStdString());
        cfg.saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
    }
}

void MainWindow::on_saveStitch() {
    const cv::Mat& saveMat = (stitch_showing_negative_ && !stitched_result_negative_.empty())
        ? stitched_result_negative_ : stitched_result_;
    if (saveMat.empty()) {
        QMessageBox::warning(this, "警告", "没有拼接结果");
        return;
    }
    QString ws = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
    QString defPath = ws + "/stitched.png";
    QString path = QFileDialog::getSaveFileName(this, "保存拼接结果", defPath,
        "PNG (*.png);;BMP (*.bmp);;TIFF (*.tiff)");
    if (!path.isEmpty()) cv::imwrite(path.toStdString(), saveMat);
}

// ==================== Camera Preview (event-driven, via signal) ====================

void MainWindow::onCameraFrameReady(const cv::Mat& frame) {
    if (frame.empty()) return;

    // Preview checkbox controls whether to render — unchecked = skip all processing
    if (!camera_preview_check_ || !camera_preview_check_->isChecked()) return;

    current_image_ = frame;
    current_image_source_.clear();  // 实时帧无源文件，检测 JSON 用时间戳命名

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
        // Only auto-update when user is NOT editing (avoids overwriting manual input)
        if (!ui_->xPosSpin->hasFocus()) ui_->xPosSpin->setValue(status.current_positions[0]);
        if (!ui_->yPosSpin->hasFocus()) ui_->yPosSpin->setValue(status.current_positions[1]);
        if (!ui_->zPosSpin->hasFocus()) ui_->zPosSpin->setValue(status.current_positions[2]);
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
            ui_->scanNegativeCheck->setEnabled(true);
        } else {
            appendLog("S型扫描完成", "INFO");
            ui_->statusLabel->setText("扫描完成");
            QMessageBox::information(this, "扫描完成", "扫描已完成！");
            ui_->scanNegativeCheck->setEnabled(true);
        }
    }
}

// ==================== Grid ====================

void MainWindow::on_topCellsTable_cellClicked(int /*row*/, int /*column*/) {
    // Handled via eventFilter (left-click → fullscreen preview) to avoid
    // Qt double-emission of cellClicked for cell + cellWidget.
}

void MainWindow::onZStackFinished() {
    auto res = zstack_future_.result();
    int row = res.row, col = res.col;
    if (!res.ok) {
        QMessageBox::warning(this, "Z-Stack 对焦",
            QString("Z-Stack 对焦失败: [行%1,列%2]\n请检查日志").arg(row+1).arg(col+1));
        return;
    }
    if (row < 0 || col < 0) return;

    // Load the final image and update grid thumbnail
    QString origQPath = QString::fromStdString(res.origPath);
    if (!origQPath.isEmpty() && QFileInfo::exists(origQPath)) {
        auto* watcher = new QFutureWatcher<cv::Mat>(this);
        connect(watcher, &QFutureWatcher<cv::Mat>::finished, this, [this, watcher, row, col]() {
            cv::Mat frame = watcher->result();
            if (frame.empty()) return;

            int cropSize = ConfigManager::instance().centerCropSize();
            cv::Mat cropped;
            if (cropSize > 0 && cropSize < frame.cols && cropSize < frame.rows) {
                int left = (frame.cols - cropSize) / 2;
                int top  = (frame.rows - cropSize) / 2;
                cropped = frame(cv::Rect(left, top, cropSize, cropSize));
            } else {
                cropped = frame;
            }
            cv::Mat thumbOrig, thumbNeg;
            cv::resize(cropped, thumbOrig, cv::Size(50, 50), 0, 0, cv::INTER_AREA);
            cv::bitwise_not(thumbOrig, thumbNeg);
            QPixmap pixOrig = QPixmap::fromImage(cvMatToQImage(thumbOrig));
            QPixmap pixNeg = QPixmap::fromImage(cvMatToQImage(thumbNeg));
            {
                std::lock_guard<std::mutex> lock(grid_thumbnails_mutex_);
                grid_thumbnails_[{row, col}] = {pixOrig, pixNeg};
            }
            QPixmap pix = scan_showing_negative_ ? pixNeg : pixOrig;
            auto* lbl = qobject_cast<QLabel*>(ui_->topCellsTable->cellWidget(row, col));
            if (lbl) {
                lbl->setPixmap(pix);
            } else {
                auto* newLbl = new QLabel();
                newLbl->setPixmap(pix);
                newLbl->setScaledContents(true);
                newLbl->setContentsMargins(0, 0, 0, 0);
                newLbl->setAlignment(Qt::AlignCenter);
                newLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                ui_->topCellsTable->setCellWidget(row, col, newLbl);
            }
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([origQPath]() {
            return cv::imread(origQPath.toStdString());
        }));
    }

    // Show result and ask for Z-Map confirmation
    auto& samples = res.focusData.samples;
    QString detail;
    if (!samples.empty()) {
        double minS = samples[0].score, maxS = samples[0].score;
        for (auto& s : samples) { minS = std::min(minS, s.score); maxS = std::max(maxS, s.score); }
        detail = QString("\n清晰度范围: %1 ~ %2\n采样层数: %3")
            .arg(minS, 0, 'f', 1).arg(maxS, 0, 'f', 1).arg(samples.size());
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Z-Stack 对焦完成");
    msgBox.setText(QString("单元格 [行%1,列%2]\n\n"
                           "最佳 Z: %3 脉冲  (原: %4)\n"
                           "峰值清晰度: %5%6")
                       .arg(row+1).arg(col+1)
                       .arg(res.optimalZ).arg(res.prevZ)
                       .arg(res.peakScore, 0, 'f', 1)
                       .arg(detail));
    msgBox.setInformativeText("是否将此最佳 Z 值写入 Z-Map JSON？");
    QPushButton* btnYes = msgBox.addButton("写入 Z-Map", QMessageBox::AcceptRole);
    QPushButton* btnNo  = msgBox.addButton("仅更新图像", QMessageBox::RejectRole);
    msgBox.setDefaultButton(btnYes);
    msgBox.exec();

    if (msgBox.clickedButton() == btnYes) {
        // Write Z-Map JSON on UI thread
        QString zPath = QString::fromStdString(res.zMapPath);
        if (!QFileInfo::exists(zPath)) {
            QString tmpl = QCoreApplication::applicationDirPath()
                           + "/z_map_template.json";
            if (QFileInfo::exists(tmpl))
                QFile::copy(tmpl, zPath);
        }
        QJsonArray rows;
        if (QFileInfo::exists(zPath)) {
            QFile f(zPath);
            if (f.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                f.close();
                if (doc.isObject() && doc["z_values"].isArray())
                    rows = doc["z_values"].toArray();
            }
        }
        int gx = ConfigManager::instance().gridSizeX();
        int zBase = ConfigManager::instance().zBaseHeight();
        while (rows.size() <= row) {
            QJsonArray r;
            for (int c = 0; c < gx; ++c) r.append((int)zBase);
            rows.append(r);
        }
        QJsonArray targetRow = rows[row].toArray();
        while (targetRow.size() <= col) targetRow.append((int)zBase);
        targetRow[col] = res.optimalZ;
        rows[row] = targetRow;

        QDir().mkpath(QFileInfo(zPath).absolutePath());
        QFile f(zPath);
        if (f.open(QIODevice::WriteOnly)) {
            QJsonObject root;
            root["description"] = "Z-Map — Z-Stack 自动对焦标定";
            root["z_values"] = rows;
            f.write(QJsonDocument(root).toJson());
            f.close();
            appendLog(QString("Z-Map 已更新: [行%1,列%2] Z=%3")
                .arg(row+1).arg(col+1).arg(res.optimalZ), "INFO");
        }
    } else {
        appendLog(QString("Z-Stack 对焦完成 (未写入Z-Map): [行%1,列%2] 最佳Z=%3")
            .arg(row+1).arg(col+1).arg(res.optimalZ), "INFO");
    }
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
            // Fullscreen image preview — async imread to avoid UI block
            if (preview_dlg_) return true;
            int row = item->row(), col = item->column();
            std::string img_path;
            {
                std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
                auto it = s_movement_images_.find({row, col});
                if (it != s_movement_images_.end()) img_path = it->second;
            }
            if (!img_path.empty()) {
                std::string loadPath = img_path;
                if (scan_showing_negative_) {
                    std::string scanDir;
                    {
                        std::lock_guard<std::mutex> lock(scan_state_mutex_);
                        scanDir = scan_base_dir_;
                    }
                    if (!scanDir.empty()) {
                        std::filesystem::path origPath(img_path);
                        loadPath = scanDir + "/negative/" + origPath.filename().string();
                    }
                }
                // Launch async imread; use a local watcher so multiple clicks don't conflict
                cell_preview_load_path_ = loadPath;
                auto* watcher = new QFutureWatcher<cv::Mat>(this);
                connect(watcher, &QFutureWatcher<cv::Mat>::finished, this, [this, watcher]() {
                    cv::Mat img = watcher->result();
                    // Fallback to original if negative file doesn't exist
                    if (img.empty() && scan_showing_negative_) {
                        img = cv::imread(cell_preview_load_path_);
                    }
                    if (!img.empty() && !preview_dlg_) {
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
                    watcher->deleteLater();
                });
                watcher->setFuture(QtConcurrent::run([loadPath]() {
                    return cv::imread(loadPath);
                }));
            }
            return true;
        }

        if (me->button() == Qt::RightButton) {
            int row = item->row(), col = item->column();
            auto& cfg = ConfigManager::instance();

            QMenu menu;
            QAction* setZ     = menu.addAction(
                QString("设置 Z 值 [行%1,列%2]").arg(row+1).arg(col+1));
            QAction* setScale = menu.addAction(
                QString("设置缩放比例 [行%1,列%2]").arg(row+1).arg(col+1));
            menu.addSeparator();
            QAction* replaceFrame = nullptr;
            if (ctrl_.cameraHandler().isConnected()) {
                replaceFrame = menu.addAction(
                    QString("替换当前帧 [行%1,列%2]").arg(row+1).arg(col+1));
            }
            QAction* zstackAF = nullptr;
            if (ctrl_.cameraHandler().isConnected() && ctrl_.armController().isConnected()) {
                zstackAF = menu.addAction(
                    QString("Z-Stack 自动对焦 [行%1,列%2]").arg(row+1).arg(col+1));
            }

            QAction* chosen = menu.exec(me->globalPos());
            int gx = cfg.gridSizeX(), gy = cfg.gridSizeY();

            // ── Helper: update a single cell in a JSON 2D array file ──
            auto updateMapCell = [&](const QString& filePath,
                                      const QString& key,
                                      double defaultValue,
                                      const QString& desc) -> bool {
                // Read existing file if any
                QJsonArray rows;
                if (QFileInfo::exists(filePath)) {
                    QFile f(filePath);
                    if (f.open(QIODevice::ReadOnly)) {
                        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                        f.close();
                        if (doc.isObject() && doc[key].isArray())
                            rows = doc[key].toArray();
                    }
                }
                // Pad to grid size
                while (rows.size() < gy) {
                    QJsonArray r;
                    for (int c = 0; c < gx; ++c) r.append(defaultValue);
                    rows.append(r);
                }
                QJsonArray targetRow = rows[row].toArray();
                while (targetRow.size() < gx) targetRow.append(defaultValue);
                targetRow[col] = defaultValue;
                rows[row] = targetRow;
                // Write back
                QJsonObject root;
                root["description"] = desc;
                root[key] = rows;
                QDir().mkpath(QFileInfo(filePath).absolutePath());
                QFile f(filePath);
                if (!f.open(QIODevice::WriteOnly)) return false;
                f.write(QJsonDocument(root).toJson());
                f.close();
                return true;
            };

            // ── Helper: read current cell value ──
            auto readCellValue = [&](const QString& filePath, const QString& key,
                                      double defaultVal) -> double {
                if (!QFileInfo::exists(filePath)) return defaultVal;
                QFile f(filePath);
                if (!f.open(QIODevice::ReadOnly)) return defaultVal;
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                f.close();
                if (!doc.isObject() || !doc[key].isArray()) return defaultVal;
                QJsonArray rows = doc[key].toArray();
                if (row >= rows.size() || !rows[row].isArray()) return defaultVal;
                QJsonArray cols = rows[row].toArray();
                if (col >= cols.size()) return defaultVal;
                return cols[col].toDouble(defaultVal);
            };

            if (chosen == setZ) {
                QString zPath = QString::fromStdString(cfg.zMapFile());
                bool ok = false;
                int curZ = static_cast<int>(readCellValue(zPath, "z_values", cfg.zHeight()));
                int newZ = QInputDialog::getInt(this, "设置 Z 值",
                    QString("输入 [行%1,列%2] 的 Z 轴高度 (脉冲数):").arg(row+1).arg(col+1),
                    curZ, 0, 80000, 100, &ok);
                if (ok && updateMapCell(zPath, "z_values", newZ,
                        "Z-Map — 每个网格位置的 Z 轴高度（脉冲数）")) {
                    appendLog(QString("Z-Map 已更新: [%1,%2] Z=%3")
                        .arg(row+1).arg(col+1).arg(newZ), "INFO");
                    QMessageBox::information(this, "完成",
                        QString("已保存 Z 值: [行%1,列%2] Z=%3")
                            .arg(row+1).arg(col+1).arg(newZ));
                }
            } else if (chosen == setScale) {
                QString sPath = QString::fromStdString(cfg.scaleMapFile());
                bool ok = false;
                double curS = readCellValue(sPath, "scale_values", 1.0);
                double newS = QInputDialog::getDouble(this, "设置缩放比例",
                    QString("输入 [行%1,列%2] 的缩放因子:").arg(row+1).arg(col+1),
                    curS, 0.5, 2.0, 3, &ok);
                if (ok && updateMapCell(sPath, "scale_values", newS,
                        "Scale-Map — 每个网格位置的缩放因子")) {
                    appendLog(QString("Scale-Map 已更新: [%1,%2] scale=%3")
                        .arg(row+1).arg(col+1).arg(newS, 0, 'f', 3), "INFO");
                    QMessageBox::information(this, "完成",
                        QString("已保存缩放: [行%1,列%2] scale=%3")
                            .arg(row+1).arg(col+1).arg(newS, 0, 'f', 3));
                }
            } else if (replaceFrame && chosen == replaceFrame) {
                // Async: capture frame + overwrite files in worker thread
                std::string origPath;
                {
                    std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
                    auto it = s_movement_images_.find({row, col});
                    if (it != s_movement_images_.end()) origPath = it->second;
                }
                if (origPath.empty()) {
                    QMessageBox::warning(this, "提示",
                        QString("单元格 [行%1,列%2] 没有已扫描的图像").arg(row+1).arg(col+1));
                } else {
                    frame_replace_row_ = row;
                    frame_replace_col_ = col;
                    QString qOrig = QString::fromStdString(origPath);
                    QString qNeg = QString(qOrig).replace("/original/", "/negative/");
                    auto& cam = ctrl_.cameraHandler();
                    frame_replace_future_ = QtConcurrent::run([&cam, origPath, qNeg]() -> std::pair<cv::Mat,bool> {
                        cv::Mat frame;
                        if (!cam.captureTriggerFrame(frame) || frame.empty())
                            return {cv::Mat(), false};
                        cv::imwrite(origPath, frame);
                        cv::Mat negFrame;
                        cv::bitwise_not(frame, negFrame);
                        cv::imwrite(qNeg.toStdString(), negFrame);
                        return {frame, true};
                    });
                    frame_replace_watcher_.setFuture(frame_replace_future_);
                }
            } else if (zstackAF && chosen == zstackAF) {
                // ── Z-Stack autofocus dialog ──
                bool rangeOk = false, stepOk = false;
                int zRange = QInputDialog::getInt(this, "Z-Stack 自动对焦 — 扫描范围",
                    QString("Z 轴扫描范围 (脉冲数):\n"
                            "例如 ±2000 → 共扫描 4000 脉冲范围\n"
                            "[行%1,列%2]")
                        .arg(row+1).arg(col+1),
                    2000, 100, 20000, 100, &rangeOk);
                if (!rangeOk) return true;

                int zStep = QInputDialog::getInt(this, "Z-Stack 自动对焦 — 步进",
                    QString("Z 轴步进 (脉冲数):\n"
                            "步进越小越精确，但扫描层数越多\n"
                            "[行%1,列%2] 范围 ±%3")
                        .arg(row+1).arg(col+1).arg(zRange),
                    100, 20, 1000, 10, &stepOk);
                if (!stepOk) return true;

                int gx = cfg.gridSizeX(), gy = cfg.gridSizeY();
                int stepSize = cfg.stepSize();
                int zBase = cfg.zBaseHeight();

                // Compute cell XY + current Z
                int cellX = col * stepSize;
                int cellY = row * stepSize;
                int cellZ = cfg.zHeight(); // fallback
                {
                    // Determine Z for this cell using current Z-mode
                    int zMode = cfg.zMode();
                    if (zMode == 2) {
                        // Radial Z-map — compute r from grid center
                        double cx = static_cast<double>((gx - 1) * stepSize) / 2.0;
                        double cy = static_cast<double>((gy - 1) * stepSize) / 2.0;
                        double dx = static_cast<double>(cellX) - cx;
                        double dy = static_cast<double>(cellY) - cy;
                        double r_raw = std::sqrt(dx * dx + dy * dy);
                        double halfDiag = std::sqrt(cx * cx + cy * cy);
                        double r_norm = (halfDiag > 0.0) ? (r_raw / halfDiag) : 0.0;
                        // Note: interpolateRadialZ lives in SMovementController;
                        // for simplicity use raw formula here
                        std::string rPath = cfg.zRadialFile();
                        if (!rPath.empty()) {
                            QFile rf(QString::fromStdString(rPath));
                            if (rf.open(QIODevice::ReadOnly)) {
                                QJsonDocument rd = QJsonDocument::fromJson(rf.readAll());
                                rf.close();
                                if (rd.isObject() && rd["z_radial"].isArray()) {
                                    QJsonArray entries = rd["z_radial"].toArray();
                                    std::vector<std::pair<double,int>> pts;
                                    for (int i = 0; i < entries.size(); ++i) {
                                        if (!entries[i].isObject()) continue;
                                        QJsonObject o = entries[i].toObject();
                                        pts.emplace_back(o["r"].toDouble(), o["z"].toInt());
                                    }
                                    if (!pts.empty()) {
                                        std::sort(pts.begin(), pts.end());
                                        if (r_norm <= pts.front().first) cellZ = pts.front().second;
                                        else if (r_norm >= pts.back().first) cellZ = pts.back().second;
                                        else {
                                            for (size_t i = 0; i < pts.size()-1; ++i) {
                                                if (r_norm >= pts[i].first && r_norm <= pts[i+1].first) {
                                                    double range = pts[i+1].first - pts[i].first;
                                                    double t = (range > 0) ? (r_norm - pts[i].first)/range : 0;
                                                    cellZ = static_cast<int>(std::round(pts[i].second + t*(pts[i+1].second - pts[i].second)));
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else if (zMode == 1) {
                        // Manual Z-map — read from JSON
                        QString zPath = QString::fromStdString(cfg.zMapFile());
                        if (QFileInfo::exists(zPath)) {
                            QFile zf(zPath);
                            if (zf.open(QIODevice::ReadOnly)) {
                                QJsonDocument zd = QJsonDocument::fromJson(zf.readAll());
                                zf.close();
                                if (zd.isObject() && zd["z_values"].isArray()) {
                                    QJsonArray rows2 = zd["z_values"].toArray();
                                    if (row < rows2.size() && rows2[row].isArray()) {
                                        QJsonArray cols2 = rows2[row].toArray();
                                        if (col < cols2.size())
                                            cellZ = cols2[col].toInt(zBase);
                                    }
                                }
                            }
                        }
                    }
                }

                int zStart = std::max(0, cellZ - zRange);
                int zEnd   = std::min(zBase, cellZ + zRange);

                appendLog(QString("启动 Z-Stack 对焦: [行%1,列%2] Z=%3 范围[%4,%5] 步进%6")
                    .arg(row+1).arg(col+1).arg(cellZ).arg(zStart).arg(zEnd).arg(zStep), "INFO");

                // ── Async Z-Stack execution ──
                auto& arm = ctrl_.armController();
                auto& cam = ctrl_.cameraHandler();
                int zStackRow = row, zStackCol = col;

                zstack_future_ = QtConcurrent::run([&arm, &cam, zStackRow, zStackCol,
                    cellX, cellY, cellZ, zStart, zEnd, zStep, zBase, gx, this]() -> ZStackResult
                {
                    ZStackResult res;
                    res.row = zStackRow;
                    res.col = zStackCol;

                    // Step 1: Move arm to cell XY at start Z
                    appendLogSafe("Z-Stack: 移动到起始位置...", "INFO");
                    if (!arm.moveAxesConcurrent(cellX, cellY, zStart)) {
                        appendLogSafe("Z-Stack: 移动失败", "ERROR");
                        return res;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));

                    // Step 2: Z sweep — capture at each Z and evaluate sharpness
                    int lo = std::min(zStart, zEnd);
                    int hi = std::max(zStart, zEnd);
                    int totalSteps = (hi - lo) / zStep + 1;
                    int stepIdx = 0;
                    double bestScore = 0.0;
                    int    bestZ = zStart;

                    // Use single-axis Z move for sweep (faster than 3-axis)
                    // First positioning already done above with 3-axis move
                    int prevZ = zStart;

                    for (int z = lo; z <= hi; z += zStep) {
                        // Move only Z axis
                        if (z != prevZ) {
                            arm.moveToPosition(2, z);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                        prevZ = z;
                        stepIdx++;

                        // Capture frame
                        cv::Mat frame;
                        if (!cam.captureTriggerFrame(frame) || frame.empty()) {
                            continue;
                        }

                        // Evaluate sharpness
                        double score = core::SharpnessEvaluator::laplacianVarianceBGR(frame);
                        res.focusData.samples.push_back({z, score});

                        if (score > bestScore) {
                            bestScore = score;
                            bestZ = z;
                        }

                        // Log progress every 10 steps
                        if (stepIdx % 10 == 0) {
                            appendLogSafe(QString("Z-Stack: %1/%2  Z=%3  score=%4")
                                .arg(stepIdx).arg(totalSteps).arg(z)
                                .arg(score, 0, 'f', 1), "INFO");
                        }
                    }

                    res.optimalZ  = bestZ;
                    res.peakScore = bestScore;
                    res.focusData.optimalZ  = bestZ;
                    res.focusData.peakScore = bestScore;

                    if (res.focusData.samples.empty()) {
                        appendLogSafe("Z-Stack: 无有效采样，对焦失败", "ERROR");
                        return res;
                    }

                    appendLogSafe(QString("Z-Stack: 最佳 Z=%1 清晰度=%2 (共%3层)")
                        .arg(bestZ).arg(bestScore, 0, 'f', 1)
                        .arg(static_cast<int>(res.focusData.samples.size())), "INFO");

                    // Step 3: Move to optimal Z and capture final image
                    if (bestZ != prevZ) {
                        arm.moveToPosition(2, bestZ);
                        std::this_thread::sleep_for(std::chrono::milliseconds(150));
                    }

                    cv::Mat finalFrame;
                    if (!cam.captureTriggerFrame(finalFrame) || finalFrame.empty()) {
                        appendLogSafe("Z-Stack: 最终拍摄失败", "ERROR");
                        return res;
                    }
                    res.ok = true;

                    // Step 4: Write final image to disk + update Z-Map JSON
                    {
                        std::lock_guard<std::mutex> lock(s_movement_images_mutex_);
                        auto it = s_movement_images_.find({zStackRow, zStackCol});
                        if (it != s_movement_images_.end()) {
                            res.origPath = it->second;
                            res.negPath  = QString::fromStdString(it->second)
                                            .replace("/original/", "/negative/").toStdString();
                            cv::imwrite(res.origPath, finalFrame);
                            cv::Mat negFrame;
                            cv::bitwise_not(finalFrame, negFrame);
                            cv::imwrite(res.negPath, negFrame);
                        }
                    }

                    // Store Z-Map path for confirmation write on UI thread
                    {
                        auto& cfg2 = ConfigManager::instance();
                        QString zPath = QString::fromStdString(cfg2.zMapFile());
                        if (zPath.isEmpty()) {
                            QString docs = QStandardPaths::writableLocation(
                                QStandardPaths::DocumentsLocation);
                            zPath = docs + "/ScannerData/z_map.json";
                        }
                        res.zMapPath = zPath.toStdString();
                        res.prevZ = cellZ;
                    }

                    appendLogSafe(QString("Z-Stack 完成: [行%1,列%2] 最佳Z=%3 峰值=%4")
                        .arg(zStackRow+1).arg(zStackCol+1).arg(bestZ)
                        .arg(bestScore, 0, 'f', 1), "INFO");
                    return res;
                });
                zstack_watcher_.setFuture(zstack_future_);
            }
            return true;
        }

        if (me->button() == Qt::MiddleButton && ui_->cellMoveCheck->isChecked()
            && ctrl_.armController().isConnected()) {
            int row = item->row(), col = item->column();
            auto& cfg = ConfigManager::instance();
            int step = cfg.stepSize();
            int gx = cfg.gridSizeX(), gy = cfg.gridSizeY();
            int x = col * step, y = row * step;

            // Compute Z based on current Z mode (same logic as SMovementController)
            int z = cfg.zHeight(); // fallback
            int zMode = cfg.zMode();
            int zBase = cfg.zBaseHeight();

            if (zMode == 1) {
                // Manual per-position Z-map: look up from JSON file
                QString zPath = QString::fromStdString(cfg.zMapFile());
                if (QFileInfo::exists(zPath)) {
                    QFile f(zPath);
                    if (f.open(QIODevice::ReadOnly)) {
                        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                        f.close();
                        if (doc.isObject()) {
                            QJsonArray rows = doc["z_values"].toArray();
                            if (row < rows.size()) {
                                QJsonArray cols = rows[row].toArray();
                                if (col < cols.size()) {
                                    int mapZ = cols[col].toInt(cfg.zHeight());
                                    z = std::max(0, std::min(mapZ, zBase));
                                }
                            }
                        }
                    }
                }
            } else if (zMode == 2) {
                // Radial Z-map: distance from center → linear interpolation
                double gcx = static_cast<double>(step * (gx - 1)) / 2.0;
                double gcy = static_cast<double>(step * (gy - 1)) / 2.0;
                double dx = static_cast<double>(x) - gcx;
                double dy = static_cast<double>(y) - gcy;
                double r_raw = std::sqrt(dx * dx + dy * dy);
                double halfDiag = std::sqrt(
                    (gcx * gcx) + (gcy * gcy));
                double r_norm = (halfDiag > 0.0) ? (r_raw / halfDiag) : 0.0;

                QString zrPath = QString::fromStdString(cfg.zRadialFile());
                if (QFileInfo::exists(zrPath)) {
                    QFile f(zrPath);
                    if (f.open(QIODevice::ReadOnly)) {
                        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                        f.close();
                        if (doc.isObject()) {
                            QJsonObject rootObj = doc.object();
                            QJsonArray entries = rootObj[QStringLiteral("z_radial")].toArray();
                            if (!entries.empty()) {
                                // Linear interpolation
                                QJsonObject e0 = entries[0].toObject();
                                double prev_r = e0[QStringLiteral("r")].toDouble();
                                int prev_z = e0[QStringLiteral("z")].toInt();
                                if (r_norm <= prev_r) {
                                    z = std::max(0, std::min(prev_z, zBase));
                                } else {
                                    for (int i = 1; i < entries.size(); ++i) {
                                        QJsonObject ei = entries[i].toObject();
                                        double cur_r = ei[QStringLiteral("r")].toDouble();
                                        int cur_z = ei[QStringLiteral("z")].toInt();
                                        if (r_norm <= cur_r) {
                                            double range = cur_r - prev_r;
                                            double t = (range > 0.0) ? (r_norm - prev_r) / range : 0.0;
                                            z = std::max(0, std::min(
                                                static_cast<int>(std::round(prev_z + t * (cur_z - prev_z))), zBase));
                                            break;
                                        }
                                        prev_r = cur_r;
                                        prev_z = cur_z;
                                    }
                                    // r_norm > last entry → use last
                                    QJsonObject eLast = entries.last().toObject();
                                    if (r_norm > eLast[QStringLiteral("r")].toDouble()) {
                                        z = std::max(0, std::min(eLast[QStringLiteral("z")].toInt(), zBase));
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
            // Spherical cap Z compensation (default, mode 0)
            // Grid center = cap center
            double cx = static_cast<double>(step * (gx - 1)) / 2.0;
            double cy = static_cast<double>(step * (gy - 1)) / 2.0;
            double R = static_cast<double>(cfg.sphereRadius());
            double h = static_cast<double>(cfg.sphereCapHeight());
            int dH = cfg.sphereHeightOffset();

            if (h > 0.0) {
                double Rs = (R * R + h * h) / (2.0 * h);
                double dx2 = static_cast<double>(x) - cx;
                double dy2 = static_cast<double>(y) - cy;
                double r = std::sqrt(dx2 * dx2 + dy2 * dy2);

                double cap = 0.0;
                if (r <= R) {
                    double sq = Rs * Rs - r * r;
                    cap = std::sqrt(std::max(0.0, sq)) - (Rs - h);
                }
                int zVal = static_cast<int>(std::round(
                    static_cast<double>(zBase) - cap - static_cast<double>(dH)));
                z = std::max(0, std::min(zVal, zBase));
            } else {
                z = zBase;
            }
        }

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

// ── Negative Film Helpers ─────────────────────────────────────────────

bool MainWindow::hasNegativeImages(const std::string& directory) const {
    if (directory.empty()) return false;
    try {
        std::string negDir = directory + "/negative";
        if (!std::filesystem::exists(negDir)) return false;
        for (const auto& entry : std::filesystem::directory_iterator(negDir)) {
            if (entry.is_regular_file()) return true;
        }
    } catch (...) {}
    return false;
}

void MainWindow::startNegativeStitching(const std::string& directory, const cv::Size& grid_size) {
    std::string negDir = directory + "/negative";
    negative_stitching_future_ = QtConcurrent::run([this, negDir, grid_size]() -> cv::Mat {
        std::regex pattern(R"(^(\d+)_(\d+)(?:_(\d+))?)");
        std::vector<stitch::PositionedImage> positioned;
        int maxRow = 0, maxCol = 0;

        try {
            // Pass 1: find max row/col
            for (const auto& entry : std::filesystem::directory_iterator(negDir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".bmp") continue;

                std::string stem = entry.path().stem().string();
                stem.erase(std::remove(stem.begin(), stem.end(), ' '), stem.end());

                std::smatch match;
                if (std::regex_search(stem, match, pattern)) {
                    maxRow = std::max(maxRow, std::stoi(match[1].str()));
                    maxCol = std::max(maxCol, std::stoi(match[2].str()));
                }
            }

            if (maxRow == 0 || maxCol == 0) return cv::Mat();

            // Pass 2: load images with RTL column mapping
            for (const auto& entry : std::filesystem::directory_iterator(negDir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".bmp") continue;

                std::string stem = entry.path().stem().string();
                stem.erase(std::remove(stem.begin(), stem.end(), ' '), stem.end());

                std::smatch match;
                if (std::regex_search(stem, match, pattern)) {
                    int row = std::stoi(match[1].str());
                    int col = std::stoi(match[2].str());
                    int z = match[3].matched ? std::stoi(match[3].str()) : 0;
                    cv::Mat img = cv::imread(entry.path().string());
                    if (!img.empty()) {
                        stitch::PositionedImage pi;
                        pi.image = img;
                        pi.row = row - 1;             // 1-indexed → 0-indexed
                        pi.col = maxCol - col;        // RTL: disp col 1 → canvas rightmost
                        pi.z = z;
                        positioned.push_back(pi);
                    }
                }
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading negative images: {}", e.what());
            return cv::Mat();
        }

        if (positioned.empty()) return cv::Mat();
        cv::Size detectedGrid(maxCol, maxRow);
        auto& cfg = ConfigManager::instance();
        ctrl_.stitcher().setCenterCropSize(cfg.centerCropSize());
        ctrl_.stitcher().setAlgorithm(cfg.stitchAlgorithm());
        ctrl_.stitcher().setFeatherWidth(cfg.featherWidth());
        ctrl_.stitcher().setScaleMode(cfg.scaleMode());
        ctrl_.stitcher().setScaleMapFile(cfg.scaleMapFile());
        return ctrl_.stitcher().stitchImagesWithPositions(positioned, detectedGrid);
    });
    negative_stitching_watcher_.setFuture(negative_stitching_future_);
}

void MainWindow::onNegativeStitchingFinished() {
    cv::Mat negResult = negative_stitching_future_.result();
    if (!negResult.empty()) {
        stitched_result_negative_ = negResult;
        if (negative_toggle_btn_) {
            negative_toggle_btn_->setVisible(true);
            negative_toggle_btn_->setEnabled(true);
            negative_toggle_btn_->setChecked(false);
            negative_toggle_btn_->setText("显示负片结果");
        }
        appendLog("负片拼接完成", "INFO");
    } else {
        appendLog("负片拼接失败或无负片图像", "WARN");
    }
}

