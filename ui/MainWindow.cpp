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
#include "infra/report/EdgeCrop.h"

// Sub-controllers
#include "ui/controllers/CameraPaneController.h"
#include "ui/controllers/ArmPaneController.h"
#include "ui/controllers/ScanController.h"
#include "ui/controllers/StitchController.h"
#include "ui/controllers/DetectController.h"
#include "ui/FolderSelectDialog.h"

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

    // ── Create sub-controllers (before signal wiring) ──
    camera_ctrl_ = new CameraPaneController(ui_, ctrl_, this);
    arm_ctrl_    = new ArmPaneController(ui_, ctrl_, this);
    scan_ctrl_   = new ScanController(ui_, ctrl_, this);
    stitch_ctrl_ = new StitchController(ui_, ctrl_, this);
    detect_ctrl_ = new DetectController(ui_, ctrl_, this);

    // Wire controller log signals → MainWindow::appendLog
    connect(camera_ctrl_, &CameraPaneController::logMessage, this, &MainWindow::appendLog);
    connect(arm_ctrl_,    &ArmPaneController::logMessage,    this, &MainWindow::appendLog);
    connect(scan_ctrl_,   &ScanController::logMessage,       this, &MainWindow::appendLog);
    connect(stitch_ctrl_, &StitchController::logMessage,     this, &MainWindow::appendLog);
    connect(detect_ctrl_, &DetectController::logMessage,     this, &MainWindow::appendLog);

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
    ui_->gainSlider->setEnabled(false);
    ui_->gainValueLabel->setEnabled(false);
    ui_->sharpeningSlider->setEnabled(false);
    ui_->sharpeningValueLabel->setEnabled(false);
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
    ui_->fullscreenToggleBtn->setChecked(true);
    ui_->fullscreenToggleBtn->setText(QStringLiteral("退出全屏"));
    ui_->scanNegativeCheck->setEnabled(false);
    ui_->scanNegativeCheck->setChecked(true);
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

    // ── Watcher connections now live in controllers for: stitching, detection,
    //     negative_stitching, manual_detect, arm_connect, arm_move ──
    // Frame replace watcher stays in MainWindow (cross-controller)
    connect(&frame_replace_watcher_, &QFutureWatcher<std::pair<cv::Mat,bool>>::finished, this, [this]() {
        auto [frame, ok] = frame_replace_future_.result();
        int row = frame_replace_row_, col = frame_replace_col_;
        if (!ok || frame.empty()) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("相机采集帧失败，请检查相机连接"));
            return;
        }
        if (row < 0 || col < 0) return;
        // Thumbnail update — edge-aware crop
        auto& cfg0 = ConfigManager::instance();
        int cropSize = cfg0.centerCropSize();
        cv::Mat cropped;
        if (cropSize > 0 && cropSize < frame.cols && cropSize < frame.rows) {
            int gRows = cfg0.gridSizeY(), gCols = cfg0.gridSizeX();
            cv::Rect roi = report::computeEdgeAwareCropRoi(
                cv::Size(frame.cols, frame.rows), cropSize,
                row, gCols - 1 - col, gRows, gCols);
            cropped = frame(roi);
        } else {
            cropped = frame;
        }
        cv::Mat thumbOrig, thumbNeg;
        cv::resize(cropped, thumbOrig, cv::Size(50, 50), 0, 0, cv::INTER_AREA);
        cv::bitwise_not(thumbOrig, thumbNeg);
        QPixmap pixOrig = QPixmap::fromImage(cvMatToQImage(thumbOrig));
        QPixmap pixNeg = QPixmap::fromImage(cvMatToQImage(thumbNeg));
        {
            std::lock_guard<std::mutex> lock(scan_ctrl_->gridThumbnailsMutex());
            scan_ctrl_->gridThumbnails()[{row, col}] = {pixOrig, pixNeg};
        }
        QPixmap pix = scan_ctrl_->scanShowingNegative() ? pixNeg : pixOrig;
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
        appendLog(QStringLiteral("单元格 [行%1,列%2] 帧已替换").arg(row+1).arg(col+1), QStringLiteral("INFO"));
    });
    connect(&zstack_watcher_, &QFutureWatcher<ZStackResult>::finished, this, &MainWindow::onZStackFinished);

    // Reference log panel from .ui (placed in left sidebar's hardware section)
    status_log_edit_ = ui_->statusLogEdit;

    initGridTables();

    // Real-time detection only meaningful with a camera connected
    ui_->enableDetectionCheck->setChecked(false);
    ui_->enableDetectionCheck->setEnabled(false);

    // Rename camera-area button, hide redundant toolbar one
    ui_->captureImageButton->setText(QStringLiteral("捕获"));
    ui_->quickCaptureBtn->setVisible(false);

    // IP address: validate format without fixed-width mask for natural text flow
    ui_->armIpEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression("^(?:(?:25[0-5]|2[0-4]\\d|1\\d{2}|[1-9]?\\d)\\.){3}"
                           "(?:25[0-5]|2[0-4]\\d|1\\d{2}|[1-9]?\\d)$"),
        ui_->armIpEdit));

    // FPS label and camera preview are defined in .ui file
    fps_label_ = ui_->fpsLabel;
    camera_preview_check_ = ui_->cameraPreviewCheck;

    // Negative film display toggle (placed below stitch preview) — managed by StitchController
    {
        auto* negBtn = new QPushButton(QStringLiteral("显示负片结果"), this);
        negBtn->setCheckable(true);
        negBtn->setVisible(false);
        negBtn->setEnabled(false);
        stitch_ctrl_->setNegativeToggleBtn(negBtn);

        // Wrap stitchImageLabel + button in a container inside the splitter
        if (auto* splitter = qobject_cast<QSplitter*>(ui_->stitchImageLabel->parentWidget())) {
            auto* container = new QWidget(this);
            auto* vlay = new QVBoxLayout(container);
            vlay->setContentsMargins(0, 0, 0, 0);
            int idx = splitter->indexOf(ui_->stitchImageLabel);
            if (idx >= 0) {
                splitter->replaceWidget(idx, container);
                vlay->addWidget(ui_->stitchImageLabel);
                vlay->addWidget(negBtn);
            }
        }
        connect(negBtn, &QPushButton::toggled, this, [this](bool checked) {
            stitch_ctrl_->setStitchShowingNegative(checked);
            const cv::Mat& mat = (checked && !stitch_ctrl_->stitchedResultNegative().empty())
                ? stitch_ctrl_->stitchedResultNegative() : stitch_ctrl_->stitchedResult();
            if (!mat.empty()) {
                displayImageFullQuality(mat, ui_->stitchImageLabel, stitch_ctrl_->stitchedPixmap());
            }
            stitch_ctrl_->negativeToggleBtn()->setText(checked ? QStringLiteral("显示原始结果") : QStringLiteral("显示负片结果"));
        });
    }

    setupMenuNavigation();
    connectSignals();

    // Wire module callbacks
    ctrl_.cameraHandler().setStatusCallback([this](const camera::CameraStatus& s) {
        if (!s.status_message.empty()) {
            QString lvl = s.connected ? QStringLiteral("INFO") : QStringLiteral("ERROR");
            QMetaObject::invokeMethod(this, [this, msg = QString::fromStdString(s.status_message), lvl]() {
                appendLog(QStringLiteral("相机: ") + msg, lvl);
            }, Qt::QueuedConnection);
        }
    });
    ctrl_.armController().setStatusCallback([this](const arm::ArmStatus& s) {
        QMetaObject::invokeMethod(arm_ctrl_, "onArmStatusChanged", Q_ARG(const arm::ArmStatus&, s));
    });
    ctrl_.movementController().setStatusCallback([this](const arm::SMovementStatus& s) {
        QMetaObject::invokeMethod(scan_ctrl_, [this, s]() { scan_ctrl_->onMovementStatus(s); }, Qt::QueuedConnection);
    });
    ctrl_.stitcher().setProgressCallback([this](int cur, int total) {
        int pct = total > 0 ? cur * 100 / total : 0;
        QMetaObject::invokeMethod(this, [this, pct]() {
            if (auto* dlg = stitch_ctrl_->stitchProgressDlg())
                dlg->setValue(pct);
        }, Qt::QueuedConnection);
    });
    ctrl_.stitcher().setStatusCallback([this](const std::string& msg) {
        QString qmsg = QString::fromStdString(msg);
        QMetaObject::invokeMethod(this, [this, qmsg]() {
            appendLog(qmsg, QStringLiteral("INFO"));
        }, Qt::QueuedConnection);
    });

    // Auto-scan cameras on startup
    camera_ctrl_->onEnumerateCameras();

    // Workspace selection dialog (deferred to after main window is shown)
    QTimer::singleShot(0, this, [this]() {
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString defaultWs = docs + QStringLiteral("/ScannerData");
        QString path = showFolderSelectDialog(this,
            QStringLiteral("选择工作空间"),
            QStringLiteral("请选择工作空间路径："),
            defaultWs,
            QStringLiteral("选择工作空间目录"));
        if (!path.isEmpty()) {
            QDir().mkpath(path);
            ConfigManager::instance().setImageSaveBasePath(path.toStdString());
        }
    });

    // Check if model was already auto-loaded by AppController
    if (ctrl_.isModelLoaded()) {
        detect_ctrl_->setModelLoaded(true);
        appendLog(QStringLiteral("默认检测模型已加载"), QStringLiteral("INFO"));
    }

    // Rename: "开始检测" → "检测单帧" (vs real-time = continuous)
    ui_->quickDetectBtn->setText(QStringLiteral("检测单帧"));

    // Move real-time detection checkbox next to "检测单帧" button in toolbar
    {
        QWidget* toolbar = ui_->quickDetectBtn->parentWidget();
        if (toolbar && toolbar->layout()) {
            auto* tlay = qobject_cast<QBoxLayout*>(toolbar->layout());
            if (tlay) {
                int btnIdx = tlay->indexOf(ui_->quickDetectBtn);
                if (btnIdx >= 0) {
                    ui_->enableDetectionCheck->setText(QStringLiteral("实时检测"));
                    tlay->insertWidget(btnIdx + 1, ui_->enableDetectionCheck);
                    tlay->insertWidget(tlay->indexOf(ui_->enableDetectionCheck) + 1, ui_->scanNegativeCheck);
                }
                // Add detection settings button to toolbar
                auto* detSettingsBtn = new QPushButton(QStringLiteral("缺陷检测"), toolbar);
                detSettingsBtn->setObjectName(QStringLiteral("quickDetSettingsBtn"));
                connect(detSettingsBtn, &QPushButton::clicked, detect_ctrl_, &DetectController::onDetSettings);
                tlay->insertWidget(tlay->indexOf(ui_->scanNegativeCheck) + 1, detSettingsBtn);
            }
        }
    }

    appendLog("Application initialized", "INFO");
    SPDLOG_INFO("[MainWindow] MainWindow initialized");

    // ── 全局键盘快捷键 ──
    // Space: 紧急停止 (最高优先级，始终可用)
    sc_emergency_stop_ = new QShortcut(QKeySequence(Qt::Key_Space), this);
    sc_emergency_stop_->setContext(Qt::ApplicationShortcut);
    connect(sc_emergency_stop_, &QShortcut::activated, this, [this]() {
        if (ctrl_.movementController().getStatus().running) scan_ctrl_->onToggleScanStartStop();
        for (int i = 0; i < 5; ++i) ctrl_.armController().stopAllMovements(i);
        if (ui_->contMoveBtn->isChecked()) ui_->contMoveBtn->setChecked(false);
        appendLog("紧急停止：所有运动已停止 (快捷键 Space)", "WARN");
    });

    // F5: 开始/停止扫描
    sc_toggle_scan_ = new QShortcut(QKeySequence(Qt::Key_F5), this);
    sc_toggle_scan_->setContext(Qt::ApplicationShortcut);
    connect(sc_toggle_scan_, &QShortcut::activated,
            scan_ctrl_, &ScanController::onToggleScanStartStop);

    // F6: 开始拼接
    sc_start_stitch_ = new QShortcut(QKeySequence(Qt::Key_F6), this);
    sc_start_stitch_->setContext(Qt::ApplicationShortcut);
    connect(sc_start_stitch_, &QShortcut::activated,
            stitch_ctrl_, &StitchController::onStitchRun);

    // F7: 检测单帧
    sc_manual_detect_ = new QShortcut(QKeySequence(Qt::Key_F7), this);
    sc_manual_detect_->setContext(Qt::ApplicationShortcut);
    connect(sc_manual_detect_, &QShortcut::activated,
            detect_ctrl_, &DetectController::onManualDetect);

    // Ctrl+E: 实时检测开关
    sc_toggle_detection_ = new QShortcut(QKeySequence("Ctrl+E"), this);
    sc_toggle_detection_->setContext(Qt::ApplicationShortcut);
    connect(sc_toggle_detection_, &QShortcut::activated, this, [this]() {
        ui_->enableDetectionCheck->toggle();
    });

    // Ctrl+F: 全屏切换 (补充 F11)
    sc_toggle_fullscreen_ = new QShortcut(QKeySequence("Ctrl+F"), this);
    sc_toggle_fullscreen_->setContext(Qt::ApplicationShortcut);
    connect(sc_toggle_fullscreen_, &QShortcut::activated, this, [this]() {
        ui_->actionFullscreen->toggle();
    });
}

MainWindow::~MainWindow() {
    ctrl_.stopCameraCapture();
    // Wait for pending async operations before tearing down UI
    // (watchers are now in controllers; controllers are children of MainWindow,
    //  so they are destroyed automatically when MainWindow is destroyed)
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
        QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开图像"), wd, QStringLiteral("图像 (*.jpg *.png *.bmp)"));
        if (!path.isEmpty()) {
            current_image_ = cv::imread(path.toStdString());
            if (!current_image_.empty()) {
                current_image_source_ = path.toStdString();
                displayImage(current_image_, ui_->cameraImageLabel);
                ui_->quickDetectBtn->setEnabled(true);
            }
        }
    });
    connect(ui_->actionSaveStitch, &QAction::triggered, stitch_ctrl_, &StitchController::onSaveStitch);

    // 文件 → 打开扫描文件夹 (defined in .ui)
    connect(ui_->actionOpenScanDir, &QAction::triggered, this, [this]() {
        QString ws = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
        QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择扫描文件夹"), ws,
                                                        QFileDialog::ShowDirsOnly);
        if (dir.isEmpty()) return;

        // 扫描图像保存在 original/ 子目录，自动定位
        QString runDir = dir;  // 运行目录（供负片查找等使用）
        {
            QString origPath = dir + QStringLiteral("/original");
            if (QFileInfo::exists(origPath) && QFileInfo(origPath).isDir())
                dir = origPath;
        }

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
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("文件夹中没有 {row}_{col}.jpg 格式的图像。"));
            return;
        }

        // ── Prepare UI state ──
        stitch_ctrl_->setLastScanDir(runDir.toStdString());
        ctrl_.setScanDir(runDir.toStdString());
        auto& cfg = ConfigManager::instance();
        cv::Size gs(cfg.gridSizeX(), cfg.gridSizeY());
        {
            std::lock_guard<std::mutex> lock(scan_ctrl_->scanStateMutex());
            scan_ctrl_->scanBaseDir() = runDir.toStdString();
        }
        {
            std::lock_guard<std::mutex> lock(scan_ctrl_->movementImagesMutex());
            scan_ctrl_->movementImages().clear();
        }
        {
            std::lock_guard<std::mutex> lock(scan_ctrl_->gridThumbnailsMutex());
            scan_ctrl_->gridThumbnails().clear();
        }
        ui_->topCellsTable->clearContents();
        ui_->statusLabel->setText(QStringLiteral("正在加载图像..."));

        // ── Async: imread + thumbnail generation in worker thread ──
        if (folder_load_watcher_) {
            folder_load_watcher_->disconnect();
            folder_load_watcher_->deleteLater();
        }
        folder_load_watcher_ = new QFutureWatcher<FolderLoadResult>(this);
        connect(folder_load_watcher_, &QFutureWatcher<FolderLoadResult>::finished, this,
                [this, dir, gs]() {
            ui_->statusLabel->setText(QStringLiteral("就绪"));
            auto result = folder_load_watcher_->result();
            if (result.count == 0) {
                appendLog(QStringLiteral("加载失败: 没有可读取的图像"), QStringLiteral("WARN"));
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
                    std::lock_guard<std::mutex> lock(scan_ctrl_->movementImagesMutex());
                    scan_ctrl_->movementImages()[{row, col}] = fn;
                }
                {
                    std::lock_guard<std::mutex> lock(scan_ctrl_->gridThumbnailsMutex());
                    scan_ctrl_->gridThumbnails()[{row, col}] = {result.thumbnails[i], result.thumbnails[i]};
                }
                QPixmap pix = scan_ctrl_->scanShowingNegative() ? result.thumbnails[i] : result.thumbnails[i];
                auto* lbl = new QLabel();
                lbl->setPixmap(pix);
                lbl->setScaledContents(true);
                lbl->setContentsMargins(0, 0, 0, 0);
                lbl->setAlignment(Qt::AlignCenter);
                lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                ui_->topCellsTable->setCellWidget(row, col, lbl);
            }
            appendLog(QStringLiteral("已加载 %1 张图像: %2").arg(result.count).arg(dir), QStringLiteral("INFO"));
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
                // Thumbnail: edge-aware crop → resize → QPixmap
                cv::Mat cropped;
                if (cropSize > 0 && cropSize < img.cols && cropSize < img.rows) {
                    cv::Rect roi = report::computeEdgeAwareCropRoi(
                        cv::Size(img.cols, img.rows), cropSize,
                        row, gs.width - 1 - col, gs.height, gs.width);
                    cropped = img(roi);
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
        QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入配置"), QCoreApplication::applicationDirPath(), QStringLiteral("JSON (*.json)"));
        if (!path.isEmpty()) {
            ConfigManager::instance().loadFromFile(path.toStdString());
            ui_->statusLabel->setText(QStringLiteral("配置已导入: ") + path);
        }
    });
    connect(ui_->actionExportConfig, &QAction::triggered, this, [this]() {
        // Symmetric with 导入配置: let the user choose where to write, defaulting to
        // the app directory's config.json (the same file the app actually loads).
        QString defPath = QCoreApplication::applicationDirPath() + QStringLiteral("/config.json");
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出配置"), defPath, QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) return;
        if (ConfigManager::instance().saveToFile(path.toStdString())) {
            ui_->statusLabel->setText(QStringLiteral("配置已导出: ") + path);
            appendLog(QStringLiteral("配置已导出: ") + path, QStringLiteral("INFO"));
        } else {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("配置导出失败"));
        }
    });
    connect(ui_->actionExit, &QAction::triggered, this, &QWidget::close);

    // ---- 视图 ----
    connect(ui_->actionToggleSidebar, &QAction::toggled, this, [this](bool visible) { ui_->menuPanel->setVisible(visible); });
    connect(ui_->actionToggleLog, &QAction::toggled, this, [this](bool visible) { ui_->statusLogEdit->setVisible(visible); });
    connect(ui_->actionFullscreen, &QAction::toggled, this, [this](bool fs) {
        if (fs) showFullScreen(); else showNormal();
        ui_->fullscreenToggleBtn->setChecked(fs);
        ui_->fullscreenToggleBtn->setText(fs ? QStringLiteral("退出全屏") : QStringLiteral("进入全屏"));
    });

    // 工具栏全屏切换按钮
    connect(ui_->fullscreenToggleBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) showFullScreen(); else showNormal();
        ui_->actionFullscreen->setChecked(checked);
    });

    // ---- 设置 ----
    connect(ui_->actionDetSettings, &QAction::triggered, detect_ctrl_, &DetectController::onDetSettings);
    connect(ui_->actionStitchSettings, &QAction::triggered, stitch_ctrl_, &StitchController::onStitchSettings);
    connect(ui_->actionArmZero, &QAction::triggered, arm_ctrl_, &ArmPaneController::onZeroArm);
    // (actionLoadModel removed)
    // ---- 扫描参数设置 ----
    connect(ui_->actionScanParams, &QAction::triggered, this, [this]() {
        auto& cfg = ConfigManager::instance();
        ScanParametersDialog dlg(this);
        dlg.setStepSize(cfg.stepSize());
        dlg.setDwellTimeMs(cfg.dwellTimeMs());
        if (dlg.exec() == QDialog::Accepted) {
            cfg.setStepSize(dlg.stepSize());
            cfg.setDwellTimeMs(dlg.dwellTimeMs());
            cfg.saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
            appendLog(tr("扫描参数已更新: 步长=%1 脉冲, 停留时间=%2 ms")
                .arg(dlg.stepSize()).arg(dlg.dwellTimeMs()), QStringLiteral("INFO"));
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
        dlg.setZHeight(cfg.zHeight());
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
                cfg.setZHeight(dlg.zHeight());
                appendLog(tr("球冠参数已更新: R=%1, h=%2, dH=%3, zBase=%4")
                    .arg(dlg.sphereRadius()).arg(dlg.sphereCapHeight())
                    .arg(dlg.sphereHeightOffset()).arg(dlg.zBaseHeight()), QStringLiteral("INFO"));
            } else if (mode == 1) {
                // Manual per-position Z-map mode
                std::string mapPath = dlg.zMapFile();
                if (!mapPath.empty()) {
                    cfg.setZMapFile(mapPath);
                    appendLog(tr("Z-Map 文件已设置: %1").arg(QString::fromStdString(mapPath)), QStringLiteral("INFO"));
                }
            } else {
                // Radial Z-map mode (mode == 2)
                std::string radialPath = dlg.zRadialFile();
                if (!radialPath.empty()) {
                    cfg.setZRadialFile(radialPath);
                    appendLog(tr("径向 Z-Map 文件已设置: %1").arg(QString::fromStdString(radialPath)), QStringLiteral("INFO"));
                }
            }
            cfg.setZMode(mode);
            cfg.saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
            static const char* modeNames[] = {"球冠补偿", "手动 Z-Map", "径向 Z-Map"};
            appendLog(tr("Z 轴模式已切换为: %1").arg(modeNames[mode]), QStringLiteral("INFO"));
        }
    });

    // ---- 帮助 ----
    connect(ui_->actionUserGuide, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, QStringLiteral("使用说明"),
            QStringLiteral(
            "明场显微成像验证组件\n\n"
            "工作流程:\n"
            "1. 连接相机和机械臂 (左侧硬件连接区)\n"
            "2. 设置网格参数，点击「开始扫描」执行S型扫描采集\n"
            "3. 扫描完成后自动拼接，也可手动点击「开始拼接」\n"
            "4. 加载检测模型后，点击「开始检测」执行目标检测\n\n"
            "快捷键:\n"
            "Space    — 紧急停止\n"
            "F5       — 开始/停止扫描\n"
            "F6       — 开始拼接\n"
            "F7       — 检测单帧\n"
            "Ctrl+E   — 实时检测开关\n"
            "Ctrl+F   — 全屏切换\n"
            "Ctrl+O   — 打开图像\n"
            "Ctrl+S   — 保存结果\n"
            "F11      — 全屏切换\n"
            "F1       — 使用说明"));
    });

    // 添加快捷键参考到帮助菜单
    auto* actionShortcuts = new QAction(QStringLiteral("快捷键参考"), this);
    ui_->menuHelp->insertAction(ui_->actionUserGuide, actionShortcuts);
    connect(actionShortcuts, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, QStringLiteral("快捷键参考"),
            QStringLiteral(
            "Space    — 紧急停止（全局可用）\n"
            "F5       — 开始/停止扫描\n"
            "F6       — 开始拼接\n"
            "F7       — 检测单帧\n"
            "Ctrl+E   — 实时检测开关\n"
            "Ctrl+F   — 全屏切换\n"
            "Ctrl+O   — 打开图像\n"
            "Ctrl+S   — 保存拼接结果\n"
            "F11      — 全屏切换\n"
            "F1       — 使用说明\n"
            "Ctrl+Q   — 退出程序"));
    });

    // ---- 扫描页面: 相机 ----
    connect(ui_->scanCamerasButton, &QPushButton::clicked, camera_ctrl_, &CameraPaneController::onEnumerateCameras);
    connect(ui_->cameraToggleButton, &QPushButton::clicked, camera_ctrl_, [this]() {
        if (ctrl_.cameraHandler().isConnected()) {
            camera_ctrl_->onDisconnectCamera();
        } else {
            camera_ctrl_->onConnectCamera();
        }
    });
    connect(ui_->captureImageButton, &QPushButton::clicked, camera_ctrl_, &CameraPaneController::onCaptureImage);

    // ---- 相机曝光控制 ----
    connect(ui_->autoExposureCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s) {
        bool auto_on = (s == Qt::Checked);
        ctrl_.cameraHandler().setAutoExposure(auto_on);
        ui_->exposureSlider->setEnabled(!auto_on);
        ui_->exposureValueLabel->setEnabled(!auto_on);
        ui_->gainSlider->setEnabled(!auto_on);
        ui_->gainValueLabel->setEnabled(!auto_on);
        if (auto_on) {
            // When switching to auto, immediately sync real exposure value from camera hardware
            float expo = ctrl_.cameraHandler().getRealExposure();
            int slider_val = qBound(ui_->exposureSlider->minimum(),
                                    static_cast<int>(expo * 100.0f),
                                    ui_->exposureSlider->maximum());
            ui_->exposureSlider->setValue(slider_val);
            ui_->exposureValueLabel->setText(QString::number(expo, 'f', 2) + QStringLiteral(" ms"));
            // Sync gain (also controlled by AE hardware)
            float current_gain = ctrl_.cameraHandler().getGain();
            int gain_val = qBound(ui_->gainSlider->minimum(),
                                  static_cast<int>(current_gain * 100.0f),
                                  ui_->gainSlider->maximum());
            ui_->gainSlider->setValue(gain_val);
            ui_->gainValueLabel->setText(QString::number(current_gain, 'f', 2) + QStringLiteral("x"));
        }
    });
    connect(ui_->exposureSlider, &QSlider::valueChanged, this, [this](int val) {
        float exposure = static_cast<float>(val) / 100.0f;
        ui_->exposureValueLabel->setText(QString::number(exposure, 'f', 2) + QStringLiteral(" ms"));
        // Only apply to camera when not in auto-exposure mode, to avoid overriding auto exposure
        if (!ctrl_.cameraHandler().getAutoExposure()) {
            ctrl_.cameraHandler().setExposure(exposure);
            ConfigManager::instance().setCameraExposure(exposure);
            ConfigManager::instance().saveToFile(
                QCoreApplication::applicationDirPath().toStdString() + "/config.json");
        }
    });

    // ---- 相机增益控制 ----
    connect(ui_->gainSlider, &QSlider::valueChanged, this, [this](int val) {
        float gain = static_cast<float>(val) / 100.0f;
        ui_->gainValueLabel->setText(QString::number(gain, 'f', 2) + QStringLiteral("x"));
        if (!ctrl_.cameraHandler().getAutoExposure()) {
            ctrl_.cameraHandler().setGain(gain);
            ConfigManager::instance().setCameraGain(gain);
            ConfigManager::instance().saveToFile(
                QCoreApplication::applicationDirPath().toStdString() + "/config.json");
        }
    });

    // ---- 相机锐化控制 ----
    connect(ui_->sharpeningSlider, &QSlider::valueChanged, this, [this](int val) {
        if (val == 0)
            ui_->sharpeningValueLabel->setText(QStringLiteral("关"));
        else
            ui_->sharpeningValueLabel->setText(QString::number(val));
        ctrl_.cameraHandler().setSharpening(static_cast<unsigned short>(val));
        ConfigManager::instance().setCameraSharpening(val);
        ConfigManager::instance().saveToFile(
            QCoreApplication::applicationDirPath().toStdString() + "/config.json");
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
    connect(ui_->quickCaptureBtn, &QPushButton::clicked, camera_ctrl_, &CameraPaneController::onCaptureImage);
    connect(ui_->quickScanBtn, &QPushButton::clicked, scan_ctrl_, &ScanController::onToggleScanStartStop);
    connect(ui_->quickPauseBtn, &QPushButton::clicked, scan_ctrl_, &ScanController::onTogglePauseScan);
    connect(ui_->quickStitchBtn, &QPushButton::clicked, stitch_ctrl_, &StitchController::onStitchRun);
    connect(ui_->quickSaveBtn, &QPushButton::clicked, stitch_ctrl_, &StitchController::onSaveStitch);
    connect(ui_->quickDetectBtn, &QPushButton::clicked, detect_ctrl_, &DetectController::onManualDetect);
    connect(ui_->enableDetectionCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s) {
        image_detection_enabled_ = (s == Qt::Checked);
        if (!image_detection_enabled_) {
            ui_->detectImageLabel->setPixmap({});
            ui_->detectImageLabel->setText(QStringLiteral("等待检测..."));
            detect_ctrl_->detectedPixmap() = QPixmap();
        }
    });

    // ---- 扫描网格负片显示 ----
    connect(ui_->scanNegativeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        scan_ctrl_->scanShowingNegative() = checked;
        std::lock_guard<std::mutex> lock(scan_ctrl_->gridThumbnailsMutex());
        for (auto& [rc, pixmaps] : scan_ctrl_->gridThumbnails()) {
            auto* lbl = qobject_cast<QLabel*>(ui_->topCellsTable->cellWidget(rc.first, rc.second));
            if (lbl) lbl->setPixmap(checked ? pixmaps.second : pixmaps.first);
            applyCellBorder(rc.first, rc.second);
        }
    });

    // ---- 扫描页面: 机械臂 ----
    connect(ui_->armToggleButton, &QPushButton::clicked, arm_ctrl_, [this]() {
        if (ctrl_.armController().isConnected()) {
            arm_ctrl_->onDisconnectArm();
        } else {
            arm_ctrl_->onConnectArm();
        }
    });
    connect(ui_->moveToPosButton, &QPushButton::clicked, arm_ctrl_, &ArmPaneController::onMoveToPosition);
    connect(ui_->readPosButton, &QPushButton::clicked, arm_ctrl_, &ArmPaneController::onReadPosition);
    connect(ui_->zeroButton, &QPushButton::clicked, arm_ctrl_, &ArmPaneController::onZeroArm);

    // ---- 机械臂: 速度 / 连续运动 / 紧急停止 ----
    connect(ui_->setSpeedBtn, &QPushButton::clicked, this, [this]() {
        int spd = ui_->speedSpin->value();
        for (int i = 0; i < 5; ++i) ctrl_.armController().setSpeed(i, spd);
        ConfigManager::instance().setDefaultSpeed(spd);
        ConfigManager::instance().saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
        appendLog(QStringLiteral("速度已设置: %1").arg(spd), QStringLiteral("INFO"));
    });
    connect(ui_->contMoveBtn, &QPushButton::toggled, this, [this](bool checked) {
        int axis = ui_->contAxisCombo->currentIndex();
        if (checked) {
            bool dir = ui_->contFwdRadio->isChecked();
            ctrl_.armController().startContinuousMovement(axis, dir);
            ui_->contMoveBtn->setText(QStringLiteral("停止"));
            ui_->contAxisCombo->setEnabled(false);
            ui_->contFwdRadio->setEnabled(false);
            ui_->contRevRadio->setEnabled(false);
        } else {
            ctrl_.armController().stopContinuousMovement(axis);
            ui_->contMoveBtn->setText(QStringLiteral("连续移动"));
            ui_->contAxisCombo->setEnabled(true);
            ui_->contFwdRadio->setEnabled(true);
            ui_->contRevRadio->setEnabled(true);
        }
    });
    auto emergStop = [this]() {
        if (ctrl_.movementController().getStatus().running) ctrl_.stopSMovement();
        for (int i = 0; i < 5; ++i) ctrl_.armController().stopAllMovements(i);
        if (ui_->contMoveBtn->isChecked()) ui_->contMoveBtn->setChecked(false);
        appendLog(QStringLiteral("紧急停止：所有运动已停止"), QStringLiteral("WARN"));
    };
    connect(ui_->emergStopBtn, &QPushButton::clicked, this, emergStop);
    connect(ui_->toolbarEmergStopBtn, &QPushButton::clicked, this, emergStop);

    // ---- 扫描页面: 网格 ----
    connect(ui_->topCellsTable, &QTableWidget::cellClicked, this, &MainWindow::on_topCellsTable_cellClicked);
    connect(ui_->topCellsTable, &QTableWidget::currentCellChanged,
            this, [this](int curRow, int curCol, int prevRow, int prevCol) {
        if (prevRow >= 0 && prevCol >= 0) {
            selected_grid_row_ = -1;
            selected_grid_col_ = -1;
            applyCellBorder(prevRow, prevCol);
        }
        if (curRow >= 0 && curCol >= 0) {
            selected_grid_row_ = curRow;
            selected_grid_col_ = curCol;
            applyCellBorder(curRow, curCol);
        }
    });

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
        ui_->statusLabel->setText(QStringLiteral("错误: ") + msg);
        QMessageBox::critical(this, QStringLiteral("错误"), msg);
    });
    connect(&ctrl_, &AppController::modelLoaded, this, [this]() {
        detect_ctrl_->setModelLoaded(true);
        ui_->statusLabel->setText(QStringLiteral("默认模型已加载"));
        appendLog(QStringLiteral("自动加载默认检测模型成功"), QStringLiteral("INFO"));
    });
    // 依当前检测算法刷新就绪状态：灰尘算法无需模型即就绪，可直接检测
    detect_ctrl_->setModelLoaded(ctrl_.isModelLoaded());
    connect(&ctrl_, &AppController::stitchingFinished, stitch_ctrl_, &StitchController::onStitchingFinishedExternal);

    // Camera preview (event-driven, replaces QTimer polling)
    connect(&ctrl_, &AppController::cameraFrameReady, this, &MainWindow::onCameraFrameReady);

    connect(&ctrl_, &AppController::cameraExposureChanged, this, [this]() {
        if (!ctrl_.cameraHandler().getAutoExposure()) return;
        float expo = ctrl_.cameraHandler().getRealExposure();
        QSignalBlocker blocker(ui_->exposureSlider);
        int val = qBound(ui_->exposureSlider->minimum(), static_cast<int>(expo * 100.0f),
                         ui_->exposureSlider->maximum());
        ui_->exposureSlider->setValue(val);
        ui_->exposureValueLabel->setText(QString::number(expo, 'f', 2) + QStringLiteral(" ms"));
        // Sync gain — also controlled by AE hardware
        float current_gain = ctrl_.cameraHandler().getGain();
        {
            QSignalBlocker gain_blocker(ui_->gainSlider);
            int gval = qBound(ui_->gainSlider->minimum(),
                              static_cast<int>(current_gain * 100.0f),
                              ui_->gainSlider->maximum());
            ui_->gainSlider->setValue(gval);
        }
        ui_->gainValueLabel->setText(QString::number(current_gain, 'f', 2) + QStringLiteral("x"));
    });

    connect(&ctrl_, &AppController::cameraDisconnected, this, [this]() {
        appendLog(QStringLiteral("相机意外断开"), QStringLiteral("ERROR"));
        camera_ctrl_->onDisconnectCamera();
    });

    connect(&ctrl_, &AppController::cameraError, this, [this](const QString& msg) {
        appendLog(QStringLiteral("相机错误: ") + msg, QStringLiteral("ERROR"));
    });
}

void MainWindow::initGridTables() {
    int gx = ConfigManager::instance().gridSizeX();
    int gy = ConfigManager::instance().gridSizeY();

    // Clear old scan state (table rebuilt, old coordinates invalid)
    // scanned_cells_ and scan position are now in ScanController
    selected_grid_row_ = -1;
    selected_grid_col_ = -1;

    // Quadrant 3 coordinate system layout: row numbers on right, top-right is scan start (row=1, col=1)
    // RTL: column 0 -> right side, column gx-1 -> left side, vertical header naturally on right
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

void MainWindow::applyCellBorder(int row, int col) {
    bool isScanning = (row == scan_ctrl_->currentScanRow() && col == scan_ctrl_->currentScanCol());
    bool isSelected = (row == selected_grid_row_ && col == selected_grid_col_);
    bool isScanned = scan_ctrl_->scannedCells().count({row, col}) > 0;

    auto* lbl = qobject_cast<QLabel*>(ui_->topCellsTable->cellWidget(row, col));
    if (lbl) {
        QString style;
        if (isScanning)
            style = QStringLiteral("border: 2px solid #0078D7;");
        else if (isSelected)
            style = QStringLiteral("border: 2px solid #FF8C00;");
        else if (isScanned)
            style = QStringLiteral("border: 1px solid #28A745;");
        lbl->setStyleSheet(style);
        return;
    }
    auto* item = ui_->topCellsTable->item(row, col);
    if (item) {
        if (isScanning)
            item->setBackground(QColor(0, 120, 215, 40));
        else if (isSelected)
            item->setBackground(QColor(255, 140, 0, 40));
        else if (isScanned)
            item->setBackground(QColor(40, 167, 69, 25));
        else
            item->setBackground(QColor(0, 0, 0, 0));
    }
}

void MainWindow::appendLog(const QString& msg, const QString& level) {
    QString color;
    if (level == QStringLiteral("ERROR")) color = QStringLiteral("#D9534F");     // Industrial red
    else if (level == QStringLiteral("WARN")) color = QStringLiteral("#F5A623");  // Safety yellow
    else if (level == QStringLiteral("DEBUG")) color = QStringLiteral("#5A5D63"); // Disabled gray
    else color = QStringLiteral("#8B8E94");                       // Secondary text gray

    QString prefix;
    if (level == QStringLiteral("ERROR")) prefix = QStringLiteral("[ERR] ");
    else if (level == QStringLiteral("WARN")) prefix = QStringLiteral("[WRN] ");
    else if (level == QStringLiteral("DEBUG")) prefix = QStringLiteral("[DBG] ");
    else prefix = QStringLiteral("[INF] ");

    status_log_edit_->append(
        QStringLiteral("<span style='color:%1;'>%2%3</span>").arg(color, prefix, msg.toHtmlEscaped()));
}

// Thread-safe log append — can be called from any thread.
void MainWindow::appendLogSafe(const QString& msg, const QString& level) {
    QMetaObject::invokeMethod(this, [this, msg, level]() {
        appendLog(msg, level);
    }, Qt::QueuedConnection);
}

// ==================== Camera Preview (event-driven, via signal) ====================

void MainWindow::onCameraFrameReady(const cv::Mat& frame) {
    if (frame.empty()) return;

    // Preview checkbox controls whether to render — unchecked = skip all processing
    if (!camera_preview_check_ || !camera_preview_check_->isChecked()) return;

    current_image_ = frame;
    current_image_source_.clear();  // Live frame has no source file, detection JSON uses timestamp

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
        fps_label_->setText(QString::number(static_cast<int>(current_fps_)) + QStringLiteral(" FPS"));

    // Display (frame is pre-scaled RGB from CameraHandler)
    QImage img(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    ui_->cameraImageLabel->setPixmap(QPixmap::fromImage(img));

    // Real-time detection (if enabled)
    if (image_detection_enabled_) {
        detect_ctrl_->runRealTimeDetection(frame);
    }

    // Keep camera fullscreen live
    if (camera_fullscreen_active_ && fullscreen_dlg_) {
        QPixmap pix = QPixmap::fromImage(cvMatToQImage(current_image_));
        fullscreen_dlg_->setPixmap(pix.scaled(fullscreen_dlg_->screen()->size(),
                                              Qt::KeepAspectRatio, Qt::FastTransformation));
    }
}

// ==================== Grid ====================

void MainWindow::on_topCellsTable_cellClicked(int /*row*/, int /*column*/) {
    // Handled via eventFilter (left-click → fullscreen preview) to avoid
    // Qt double-emission of cellClicked for cell + cellWidget.
}

void MainWindow::onZStackFinished() {
    zstack_busy_ = false;
    auto res = zstack_future_.result();
    int row = res.row, col = res.col;
    if (!res.ok) {
        QMessageBox::warning(this, QStringLiteral("Z-Stack 对焦"),
            QStringLiteral("Z-Stack 对焦失败: [行%1,列%2]\n请检查日志").arg(row+1).arg(col+1));
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

            auto& zcfg = ConfigManager::instance();
            int cropSize = zcfg.centerCropSize();
            cv::Mat cropped;
            if (cropSize > 0 && cropSize < frame.cols && cropSize < frame.rows) {
                int gRows = zcfg.gridSizeY(), gCols = zcfg.gridSizeX();
                cv::Rect roi = report::computeEdgeAwareCropRoi(
                    cv::Size(frame.cols, frame.rows), cropSize,
                    row, gCols - 1 - col, gRows, gCols);
                cropped = frame(roi);
            } else {
                cropped = frame;
            }
            cv::Mat thumbOrig, thumbNeg;
            cv::resize(cropped, thumbOrig, cv::Size(50, 50), 0, 0, cv::INTER_AREA);
            cv::bitwise_not(thumbOrig, thumbNeg);
            QPixmap pixOrig = QPixmap::fromImage(cvMatToQImage(thumbOrig));
            QPixmap pixNeg = QPixmap::fromImage(cvMatToQImage(thumbNeg));
            {
                std::lock_guard<std::mutex> lock(scan_ctrl_->gridThumbnailsMutex());
                scan_ctrl_->gridThumbnails()[{row, col}] = {pixOrig, pixNeg};
            }
            QPixmap pix = scan_ctrl_->scanShowingNegative() ? pixNeg : pixOrig;
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
        detail = QStringLiteral("\n清晰度范围: %1 ~ %2\n采样层数: %3")
            .arg(minS, 0, 'f', 1).arg(maxS, 0, 'f', 1).arg(samples.size());
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(QStringLiteral("Z-Stack 对焦完成"));
    msgBox.setText(QStringLiteral("单元格 [行%1,列%2]\n\n"
                           "最佳 Z: %3 脉冲  (原: %4)\n"
                           "峰值清晰度: %5%6")
                       .arg(row+1).arg(col+1)
                       .arg(res.optimalZ).arg(res.prevZ)
                       .arg(res.peakScore, 0, 'f', 1)
                       .arg(detail));
    msgBox.setInformativeText(QStringLiteral("是否将此最佳 Z 值写入 Z-Map JSON？"));
    QPushButton* btnYes = msgBox.addButton(QStringLiteral("写入 Z-Map"), QMessageBox::AcceptRole);
    QPushButton* btnNo  = msgBox.addButton(QStringLiteral("仅更新图像"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(btnYes);
    msgBox.exec();

    if (msgBox.clickedButton() == btnYes) {
        // Write Z-Map JSON on UI thread
        QString zPath = QString::fromStdString(res.zMapPath);
        if (!QFileInfo::exists(zPath)) {
            QString tmpl = QCoreApplication::applicationDirPath()
                           + QStringLiteral("/z_map_template.json");
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
            appendLog(QStringLiteral("Z-Map 已更新: [行%1,列%2] Z=%3")
                .arg(row+1).arg(col+1).arg(res.optimalZ), QStringLiteral("INFO"));
        }
    } else {
        appendLog(QStringLiteral("Z-Stack 对焦完成 (未写入Z-Map): [行%1,列%2] 最佳Z=%3")
            .arg(row+1).arg(col+1).arg(res.optimalZ), QStringLiteral("INFO"));
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

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        bool fs = isFullScreen();
        ui_->actionFullscreen->setChecked(fs);
        ui_->fullscreenToggleBtn->setChecked(fs);
        ui_->fullscreenToggleBtn->setText(fs ? QStringLiteral("退出全屏") : QStringLiteral("进入全屏"));
    }
    QMainWindow::changeEvent(event);
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
                std::lock_guard<std::mutex> lock(scan_ctrl_->movementImagesMutex());
                auto it = scan_ctrl_->movementImages().find({row, col});
                if (it != scan_ctrl_->movementImages().end()) img_path = it->second;
            }
            if (!img_path.empty()) {
                std::string loadPath = img_path;
                if (scan_ctrl_->scanShowingNegative()) {
                    std::string scanDir;
                    {
                        std::lock_guard<std::mutex> lock(scan_ctrl_->scanStateMutex());
                        scanDir = scan_ctrl_->scanBaseDir();
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
                    if (img.empty() && scan_ctrl_->scanShowingNegative()) {
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
                QStringLiteral("设置 Z 值 [行%1,列%2]").arg(row+1).arg(col+1));
            QAction* setScale = menu.addAction(
                QStringLiteral("设置缩放比例 [行%1,列%2]").arg(row+1).arg(col+1));
            QAction* setCropOffset = menu.addAction(
                QStringLiteral("设置裁剪偏移 [行%1,列%2]").arg(row+1).arg(col+1));
            menu.addSeparator();
            QAction* replaceFrame = nullptr;
            if (ctrl_.cameraHandler().isConnected()) {
                replaceFrame = menu.addAction(
                    QStringLiteral("替换当前帧 [行%1,列%2]").arg(row+1).arg(col+1));
            }
            QAction* zstackAF = nullptr;
            if (ctrl_.cameraHandler().isConnected() && ctrl_.armController().isConnected()) {
                zstackAF = menu.addAction(
                    QStringLiteral("Z-Stack 自动对焦 [行%1,列%2]").arg(row+1).arg(col+1));
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
                // Write back — preserve existing keys for multi-key files
                QJsonObject root;
                if (QFileInfo::exists(filePath)) {
                    QFile rf(filePath);
                    if (rf.open(QIODevice::ReadOnly)) {
                        QJsonDocument d = QJsonDocument::fromJson(rf.readAll());
                        rf.close();
                        if (d.isObject()) root = d.object();
                    }
                }
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
                int newZ = QInputDialog::getInt(this, QStringLiteral("设置 Z 值"),
                    QStringLiteral("输入 [行%1,列%2] 的 Z 轴高度 (脉冲数):").arg(row+1).arg(col+1),
                    curZ, 0, 80000, 100, &ok);
                if (ok && updateMapCell(zPath, "z_values", newZ,
                        "Z-Map — 每个网格位置的 Z 轴高度（脉冲数）")) {
                    appendLog(QStringLiteral("Z-Map 已更新: [%1,%2] Z=%3")
                        .arg(row+1).arg(col+1).arg(newZ), QStringLiteral("INFO"));
                    QMessageBox::information(this, QStringLiteral("保存完成"),
                        QStringLiteral("已保存 Z 值: [行%1,列%2] Z=%3")
                            .arg(row+1).arg(col+1).arg(newZ));
                }
            } else if (chosen == setScale) {
                QString sPath = QString::fromStdString(cfg.scaleMapFile());
                bool ok = false;
                double curS = readCellValue(sPath, "scale_values", 1.0);
                double newS = QInputDialog::getDouble(this, QStringLiteral("设置缩放比例"),
                    QStringLiteral("输入 [行%1,列%2] 的缩放因子:").arg(row+1).arg(col+1),
                    curS, 0.5, 2.0, 3, &ok);
                if (ok && updateMapCell(sPath, "scale_values", newS,
                        "Scale-Map — 每个网格位置的缩放因子")) {
                    appendLog(QStringLiteral("Scale-Map 已更新: [%1,%2] scale=%3")
                        .arg(row+1).arg(col+1).arg(newS, 0, 'f', 3), QStringLiteral("INFO"));
                    QMessageBox::information(this, QStringLiteral("保存完成"),
                        QStringLiteral("已保存缩放: [行%1,列%2] scale=%3")
                            .arg(row+1).arg(col+1).arg(newS, 0, 'f', 3));
                }
            } else if (chosen == setCropOffset) {
                QString coPath = QString::fromStdString(cfg.cropOffsetFile());
                bool ok = false;
                int curOx = static_cast<int>(readCellValue(coPath, "ox_values", 0));
                int curOy = static_cast<int>(readCellValue(coPath, "oy_values", 0));

                QDialog dlg(this);
                dlg.setWindowTitle(QStringLiteral("设置裁剪偏移 [行%1,列%2]").arg(row+1).arg(col+1));
                auto* dlgLayout = new QFormLayout(&dlg);
                auto* oxSpin = new QSpinBox(&dlg);
                oxSpin->setRange(-500, 500);
                oxSpin->setValue(curOx);
                oxSpin->setSuffix(QStringLiteral(" px"));
                auto* oySpin = new QSpinBox(&dlg);
                oySpin->setRange(-500, 500);
                oySpin->setValue(curOy);
                oySpin->setSuffix(QStringLiteral(" px"));
                dlgLayout->addRow(QStringLiteral("水平偏移 (ox):"), oxSpin);
                dlgLayout->addRow(QStringLiteral("垂直偏移 (oy):"), oySpin);
                auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
                connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
                connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
                dlgLayout->addRow(btnBox);

                if (dlg.exec() == QDialog::Accepted) {
                    int newOx = oxSpin->value();
                    int newOy = oySpin->value();
                    bool oxOk = updateMapCell(coPath, "ox_values", newOx,
                        "Crop Offset Map — 每个网格位置的裁剪偏移量(像素)");
                    bool oyOk = updateMapCell(coPath, "oy_values", newOy,
                        "Crop Offset Map — 每个网格位置的裁剪偏移量(像素)");
                    if (oxOk && oyOk) {
                        appendLog(QStringLiteral("Crop Offset 已更新: [%1,%2] ox=%3 oy=%4")
                            .arg(row+1).arg(col+1).arg(newOx).arg(newOy), QStringLiteral("INFO"));
                        QMessageBox::information(this, QStringLiteral("保存完成"),
                            QStringLiteral("已保存裁剪偏移: [行%1,列%2] ox=%3 oy=%4")
                                .arg(row+1).arg(col+1).arg(newOx).arg(newOy));
                    }
                }
            } else if (replaceFrame && chosen == replaceFrame) {
                // Async: capture frame + overwrite files in worker thread
                std::string origPath;
                {
                    std::lock_guard<std::mutex> lock(scan_ctrl_->movementImagesMutex());
                    auto it = scan_ctrl_->movementImages().find({row, col});
                    if (it != scan_ctrl_->movementImages().end()) origPath = it->second;
                }
                if (origPath.empty()) {
                    QMessageBox::warning(this, QStringLiteral("警告"),
                        QStringLiteral("单元格 [行%1,列%2] 没有已扫描的图像").arg(row+1).arg(col+1));
                } else {
                    frame_replace_row_ = row;
                    frame_replace_col_ = col;
                    QString qOrig = QString::fromStdString(origPath);
                    QString qNeg = QString(qOrig).replace(QStringLiteral("/original/"), QStringLiteral("/negative/"));
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
                int zRange = QInputDialog::getInt(this, QStringLiteral("Z-Stack 自动对焦 — 扫描范围"),
                    QStringLiteral("Z 轴扫描范围 (脉冲数):\n"
                            "例如 ±2000 → 共扫描 4000 脉冲范围\n"
                            "[行%1,列%2]")
                        .arg(row+1).arg(col+1),
                    2000, 100, 20000, 100, &rangeOk);
                if (!rangeOk) return true;

                int zStep = QInputDialog::getInt(this, QStringLiteral("Z-Stack 自动对焦 — 步进"),
                    QStringLiteral("Z 轴步进 (脉冲数):\n"
                            "步进越小越精确，但扫描层数越多\n"
                            "[行%1,列%2] 范围 ±%3")
                        .arg(row+1).arg(col+1).arg(zRange),
                    100, 20, 1000, 10, &stepOk);
                if (!stepOk) return true;

                if (zstack_busy_.exchange(true)) {
                    QMessageBox::warning(this, QStringLiteral("Z-Stack 对焦"),
                        QStringLiteral("已有 Z-Stack 对焦正在进行中，请等待完成后再试。"));
                    return true;
                }

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

                appendLog(QStringLiteral("启动 Z-Stack 对焦: [行%1,列%2] Z=%3 范围[%4,%5] 步进%6")
                    .arg(row+1).arg(col+1).arg(cellZ).arg(zStart).arg(zEnd).arg(zStep), QStringLiteral("INFO"));

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
                    appendLogSafe(QStringLiteral("Z-Stack: 移动到起始位置..."), QStringLiteral("INFO"));
                    if (!arm.moveAxesConcurrent(cellX, cellY, zStart)) {
                        appendLogSafe(QStringLiteral("Z-Stack: 移动失败"), QStringLiteral("ERROR"));
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

                    int prevZ = zStart;

                    for (int z = lo; z <= hi; z += zStep) {
                        if (z != prevZ) {
                            arm.moveToPosition(2, z);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                        prevZ = z;
                        stepIdx++;

                        cv::Mat frame;
                        if (!cam.captureTriggerFrame(frame) || frame.empty()) {
                            continue;
                        }

                        double score = core::SharpnessEvaluator::laplacianVarianceBGR(frame);
                        res.focusData.samples.push_back({z, score});

                        if (score > bestScore) {
                            bestScore = score;
                            bestZ = z;
                        }

                        if (stepIdx % 10 == 0) {
                            appendLogSafe(QStringLiteral("Z-Stack: %1/%2  Z=%3  score=%4")
                                .arg(stepIdx).arg(totalSteps).arg(z)
                                .arg(score, 0, 'f', 1), QStringLiteral("INFO"));
                        }
                    }

                    res.optimalZ  = bestZ;
                    res.peakScore = bestScore;
                    res.focusData.optimalZ  = bestZ;
                    res.focusData.peakScore = bestScore;

                    if (res.focusData.samples.empty()) {
                        appendLogSafe(QStringLiteral("Z-Stack: 无有效采样，对焦失败"), QStringLiteral("ERROR"));
                        return res;
                    }

                    appendLogSafe(QStringLiteral("Z-Stack: 最佳 Z=%1 清晰度=%2 (共%3层)")
                        .arg(bestZ).arg(bestScore, 0, 'f', 1)
                        .arg(static_cast<int>(res.focusData.samples.size())), QStringLiteral("INFO"));

                    // Step 3: Move to optimal Z and capture final image
                    if (bestZ != prevZ) {
                        arm.moveToPosition(2, bestZ);
                        std::this_thread::sleep_for(std::chrono::milliseconds(150));
                    }

                    cv::Mat finalFrame;
                    if (!cam.captureTriggerFrame(finalFrame) || finalFrame.empty()) {
                        appendLogSafe(QStringLiteral("Z-Stack: 最终拍摄失败"), QStringLiteral("ERROR"));
                        return res;
                    }
                    res.ok = true;

                    // Step 4: Write final image to disk + update Z-Map JSON
                    {
                        std::lock_guard<std::mutex> lock(scan_ctrl_->movementImagesMutex());
                        auto it = scan_ctrl_->movementImages().find({zStackRow, zStackCol});
                        if (it != scan_ctrl_->movementImages().end()) {
                            res.origPath = it->second;
                            res.negPath  = QString::fromStdString(it->second)
                                            .replace(QStringLiteral("/original/"), QStringLiteral("/negative/")).toStdString();
                            cv::imwrite(res.origPath, finalFrame);
                            cv::Mat negFrame;
                            cv::bitwise_not(finalFrame, negFrame);
                            cv::imwrite(res.negPath, negFrame);
                        }
                    }

                    {
                        auto& cfg2 = ConfigManager::instance();
                        QString zPath = QString::fromStdString(cfg2.zMapFile());
                        if (zPath.isEmpty()) {
                            QString docs = QStandardPaths::writableLocation(
                                QStandardPaths::DocumentsLocation);
                            zPath = docs + QStringLiteral("/ScannerData/z_map.json");
                        }
                        res.zMapPath = zPath.toStdString();
                        res.prevZ = cellZ;
                    }

                    appendLogSafe(QStringLiteral("Z-Stack 完成: [行%1,列%2] 最佳Z=%3 峰值=%4")
                        .arg(zStackRow+1).arg(zStackCol+1).arg(bestZ)
                        .arg(bestScore, 0, 'f', 1), QStringLiteral("INFO"));
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

        // Move arm directly (middle-click uses computed grid position)
        ctrl_.armController().moveAxesConcurrent(x, y, z);
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
            QPixmap pix = !detect_ctrl_->detectedPixmap().isNull() ? detect_ctrl_->detectedPixmap()
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
