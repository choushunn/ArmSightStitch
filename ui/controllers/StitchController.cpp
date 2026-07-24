#ifdef _MSC_VER
#define NOMINMAX
#include <windows.h>
#endif

#include "StitchController.h"
#include "ui_MainWindow.h"
#include "ui/AppController.h"
#include "ui/StitchingSettingsDialog.h"
#include "infra/config/ConfigManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include "ui/FolderSelectDialog.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <regex>
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

StitchController::StitchController(Ui::MainWindow* ui, AppController& ctrl,
                                   QObject* parent)
    : QObject(parent)
    , ui_(ui)
    , ctrl_(ctrl)
{
    connect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished,
            this, &StitchController::onStitchingFinished);
    connect(&negative_stitching_watcher_, &QFutureWatcher<cv::Mat>::finished,
            this, [this]() { onNegativeStitchingFinished(); });
}

StitchController::~StitchController() {
    stitching_watcher_.waitForFinished();
    negative_stitching_watcher_.waitForFinished();
}

void StitchController::onStitchRun() {
    auto& cfg = ConfigManager::instance();
    cv::Size gs(cfg.gridSizeX(), cfg.gridSizeY());

    QString defaultPath = QString::fromStdString(cfg.imageSaveBasePath());
    QString path = showFolderSelectDialog(qobject_cast<QWidget*>(parent()),
        QStringLiteral("选择拼接图像文件夹"),
        QStringLiteral("请选择包含扫描图像的文件夹："),
        defaultPath,
        QStringLiteral("选择图像文件夹"));
    if (path.isEmpty()) return;

    std::string dirPath = path.toStdString();
    std::string runDir = dirPath;  // Run directory (for negative stitch lookup etc.)
    // Scan images are saved in original/ subdirectory, auto-locate
    {
        std::string origPath = dirPath + "/original";
        if (std::filesystem::exists(origPath) && std::filesystem::is_directory(origPath))
            dirPath = origPath;
    }
    last_scan_dir_ = runDir;
    ctrl_.setScanDir(runDir);

    if (stitch_progress_dlg_) return;

    // Phase 1: background image loading
    stitch_progress_dlg_ = new QProgressDialog(QStringLiteral("正在加载图像..."), QString(), 0, 0,
                                               qobject_cast<QWidget*>(parent()));
    stitch_progress_dlg_->setWindowTitle(QStringLiteral("拼接"));
    stitch_progress_dlg_->setWindowModality(Qt::WindowModal);
    stitch_progress_dlg_->setAutoClose(false);
    stitch_progress_dlg_->show();
    ui_->statusLabel->setText(QStringLiteral("正在加载图像..."));

    stitching_future_ = QtConcurrent::run([this, dirPath, gs]() -> cv::Mat {
        cv::Size grid = gs;
        stitch_positioned_ = ctrl_.loadImagesWithPositions(dirPath, grid);
        if (!stitch_positioned_.empty()) {
            stitch_positioned_.swap(stitch_positioned_);
            stitch_raw_images_.clear();
            stitch_load_grid_ = grid;
            stitch_load_count_ = static_cast<int>(stitch_positioned_.size());
        } else {
            stitch_raw_images_ = ctrl_.loadImages(dirPath);
            stitch_positioned_.clear();
            stitch_load_grid_ = gs;
            stitch_load_count_ = static_cast<int>(stitch_raw_images_.size());
        }
        return stitch_load_count_ > 0 ? cv::Mat(cv::Size(1, 1), CV_8UC1) : cv::Mat();
    });

    // Phase 1 complete -> confirmation dialog -> Phase 2
    disconnect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished,
               this, &StitchController::onStitchingFinished);
    connect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished, this, [this]() {
        disconnect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished, this, nullptr);

        if (stitch_progress_dlg_) {
            stitch_progress_dlg_->close();
            stitch_progress_dlg_->deleteLater();
            stitch_progress_dlg_ = nullptr;
        }

        if (stitch_load_count_ == 0) {
            ui_->statusLabel->setText(QStringLiteral("拼接失败"));
            QMessageBox::warning(qobject_cast<QWidget*>(parent()), QStringLiteral("拼接失败"),
                                 QStringLiteral("目录中没有图像。"));
            return;
        }

        // Confirmation dialog
        QString confirmMsg;
        if (!stitch_positioned_.empty()) {
            confirmMsg = QStringLiteral("已加载 %1 张图像 (网格 %2 x %3)，是否开始拼接？")
                .arg(stitch_load_count_)
                .arg(stitch_load_grid_.width)
                .arg(stitch_load_grid_.height);
        } else {
            confirmMsg = QStringLiteral("已加载 %1 张图像，是否开始拼接？")
                .arg(stitch_load_count_);
        }
        auto btn = QMessageBox::question(qobject_cast<QWidget*>(parent()), QStringLiteral("确认拼接"), confirmMsg,
            QMessageBox::Yes | QMessageBox::No);
        if (btn != QMessageBox::Yes) return;

        // Phase 2: background stitching
        stitch_progress_dlg_ = new QProgressDialog(QStringLiteral("正在拼接图像..."), QString(), 0, 100,
                                                   qobject_cast<QWidget*>(parent()));
        stitch_progress_dlg_->setWindowTitle(QStringLiteral("拼接"));
        stitch_progress_dlg_->setWindowModality(Qt::WindowModal);
        stitch_progress_dlg_->setAutoClose(false);
        stitch_progress_dlg_->show();
        ui_->statusLabel->setText(QStringLiteral("正在拼接..."));

        auto& cfg2 = ConfigManager::instance();
        ctrl_.stitcher().setCenterCropSize(cfg2.centerCropSize());
        ctrl_.stitcher().setAlgorithm(cfg2.stitchAlgorithm());
        ctrl_.stitcher().setFeatherWidth(cfg2.featherWidth());
        ctrl_.stitcher().setScaleMode(cfg2.scaleMode());
        ctrl_.stitcher().setScaleMapFile(cfg2.scaleMapFile());
        ctrl_.stitcher().setZCorrectionCoef(cfg2.zCorrectionCoef());
        ctrl_.stitcher().setCropOffsetFile(cfg2.cropOffsetFile());

        if (!stitch_positioned_.empty()) {
            auto positioned = std::move(stitch_positioned_);
            cv::Size grid = stitch_load_grid_;
            stitching_future_ = QtConcurrent::run([this, positioned, grid]() {
                return ctrl_.stitcher().stitchImagesWithPositions(positioned, grid);
            });
        } else {
            auto images = std::move(stitch_raw_images_);
            cv::Size grid = stitch_load_grid_;
            stitching_future_ = QtConcurrent::run([this, images, grid]() {
                auto sorted = ctrl_.stitcher().sortImagesInSCurveOrder(images, grid);
                return ctrl_.stitcher().stitchImages(sorted, grid);
            });
        }

        connect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished,
                this, &StitchController::onStitchingFinished);
        stitching_watcher_.setFuture(stitching_future_);
    });
    stitching_watcher_.setFuture(stitching_future_);
}

void StitchController::onStitchingFinished() {
    // Close progress dialog after showing 100%
    if (stitch_progress_dlg_) {
        stitch_progress_dlg_->setValue(100);
        stitch_progress_dlg_->close();
        stitch_progress_dlg_->deleteLater();
        stitch_progress_dlg_ = nullptr;
    }
    cv::Mat result = stitching_future_.result();
    if (result.empty()) {
        ui_->statusLabel->setText(QStringLiteral("拼接失败"));
        QMessageBox::warning(qobject_cast<QWidget*>(parent()), QStringLiteral("拼接失败"),
                             QStringLiteral("目录中没有图像，或拼接处理失败。"));
    } else {
        stitched_result_ = result;
        stitched_result_negative_ = cv::Mat();  // Clear old negative result
        displayImageFullQualityHelper(result, ui_->stitchImageLabel, stitched_pixmap_);
        if (negative_toggle_btn_) {
            negative_toggle_btn_->setVisible(false);
            negative_toggle_btn_->setEnabled(false);
            negative_toggle_btn_->setChecked(false);
        }
        stitch_showing_negative_ = false;
        // Auto-save to workspace
        QString ws = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
        QDir().mkpath(ws);
        QString savePath = ws + QStringLiteral("/stitched_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")) + QStringLiteral(".png");
        std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 0};
        cv::imwrite(savePath.toStdString(), result, params);
        ui_->quickSaveBtn->setEnabled(true);
        ui_->statusLabel->setText(QStringLiteral("拼接完成"));
        QMessageBox::information(qobject_cast<QWidget*>(parent()), QStringLiteral("拼接完成"),
            QStringLiteral("图像拼接已完成！\n已保存至：%1").arg(savePath));

        emit stitchingComplete(result);

        // Trigger negative stitching
        if (!last_scan_dir_.empty() && hasNegativeImages(last_scan_dir_)) {
            int gx = ConfigManager::instance().gridSizeX();
            int gy = ConfigManager::instance().gridSizeY();
            emit logMessage(QStringLiteral("检测到负片图像，开始负片拼接..."), QStringLiteral("INFO"));
            startNegativeStitching(last_scan_dir_, cv::Size(gx, gy));
        }
    }
}

void StitchController::onStitchingFinishedExternal(const cv::Mat& result) {
    if (result.empty()) {
        ui_->statusLabel->setText(QStringLiteral("拼接失败"));
        QMessageBox::warning(qobject_cast<QWidget*>(parent()), QStringLiteral("拼接失败"),
                             QStringLiteral("目录中没有图像，或拼接处理失败。"));
    } else {
        stitched_result_ = result;
        stitched_result_negative_ = cv::Mat();  // Clear old negative result
        displayImageFullQualityHelper(result, ui_->stitchImageLabel, stitched_pixmap_);
        if (negative_toggle_btn_) {
            negative_toggle_btn_->setVisible(false);
            negative_toggle_btn_->setEnabled(false);
            negative_toggle_btn_->setChecked(false);
        }
        stitch_showing_negative_ = false;
        // Auto-save to workspace
        QString ws = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
        QDir().mkpath(ws);
        QString savePath = ws + QStringLiteral("/stitched_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")) + QStringLiteral(".png");
        std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 0};
        cv::imwrite(savePath.toStdString(), result, params);
        ui_->quickSaveBtn->setEnabled(true);
        ui_->statusLabel->setText(QStringLiteral("拼接完成"));
        QMessageBox::information(qobject_cast<QWidget*>(parent()), QStringLiteral("拼接完成"),
            QStringLiteral("图像拼接已完成！\n已保存至：%1").arg(savePath));

        emit stitchingComplete(result);

        // Trigger negative stitching
        if (!last_scan_dir_.empty() && hasNegativeImages(last_scan_dir_)) {
            int gx = ConfigManager::instance().gridSizeX();
            int gy = ConfigManager::instance().gridSizeY();
            emit logMessage(QStringLiteral("检测到负片图像，开始负片拼接..."), QStringLiteral("INFO"));
            startNegativeStitching(last_scan_dir_, cv::Size(gx, gy));
        }
    }
}

void StitchController::onStitchSettings() {
    auto& cfg = ConfigManager::instance();
    StitchingSettingsDialog dlg(qobject_cast<QWidget*>(parent()));
    dlg.setGridSizeX(cfg.gridSizeX());
    dlg.setGridSizeY(cfg.gridSizeY());
    dlg.setCenterCropSize(cfg.centerCropSize());
    dlg.setStitchAlgorithm(cfg.stitchAlgorithm());
    dlg.setFeatherWidth(cfg.featherWidth());
    dlg.setScaleMode(cfg.scaleMode());
    dlg.setScaleMapFile(QString::fromStdString(cfg.scaleMapFile()));
    dlg.setZCorrectionCoef(cfg.zCorrectionCoef());
    dlg.setCropOffsetFile(QString::fromStdString(cfg.cropOffsetFile()));
    dlg.setInputDir(QString::fromStdString(cfg.imageSaveBasePath()));
    if (dlg.exec() == QDialog::Accepted) {
        cfg.setGridSizeX(dlg.gridSizeX());
        cfg.setGridSizeY(dlg.gridSizeY());
        cfg.setCenterCropSize(dlg.centerCropSize());
        cfg.setStitchAlgorithm(dlg.stitchAlgorithm());
        cfg.setFeatherWidth(dlg.featherWidth());
        cfg.setScaleMode(dlg.scaleMode());
        cfg.setScaleMapFile(dlg.scaleMapFile().toStdString());
        cfg.setZCorrectionCoef(dlg.zCorrectionCoef());
        cfg.setCropOffsetFile(dlg.cropOffsetFile().toStdString());
        cfg.setImageSaveBasePath(dlg.inputDir().toStdString());
        cfg.saveToFile(QCoreApplication::applicationDirPath().toStdString() + "/config.json");
        // Grid table will be refreshed by MainWindow after receiving the signal
    }
}

void StitchController::onSaveStitch() {
    const cv::Mat& saveMat = (stitch_showing_negative_ && !stitched_result_negative_.empty())
        ? stitched_result_negative_ : stitched_result_;
    if (saveMat.empty()) {
        QMessageBox::warning(qobject_cast<QWidget*>(parent()), QStringLiteral("警告"),
                             QStringLiteral("没有拼接结果"));
        return;
    }
    QString ws = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
    QString defPath = ws + QStringLiteral("/stitched.png");
    QString path = QFileDialog::getSaveFileName(qobject_cast<QWidget*>(parent()), QStringLiteral("保存拼接结果"), defPath,
        QStringLiteral("PNG (*.png);;BMP (*.bmp);;TIFF (*.tiff)"));
    if (!path.isEmpty()) cv::imwrite(path.toStdString(), saveMat);
}

bool StitchController::hasNegativeImages(const std::string& directory) const {
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

void StitchController::startNegativeStitching(const std::string& directory, const cv::Size& grid_size) {
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
                        pi.row = row - 1;             // 1-indexed -> 0-indexed
                        pi.col = maxCol - col;        // RTL: disp col 1 -> canvas rightmost
                        pi.z = z;
                        positioned.push_back(pi);
                    }
                }
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[StitchController] Error loading negative images: {}", e.what());
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
        ctrl_.stitcher().setZCorrectionCoef(cfg.zCorrectionCoef());
        ctrl_.stitcher().setCropOffsetFile(cfg.cropOffsetFile());
        return ctrl_.stitcher().stitchImagesWithPositions(positioned, detectedGrid);
    });
    negative_stitching_watcher_.setFuture(negative_stitching_future_);
}

void StitchController::onNegativeStitchingFinished() {
    cv::Mat negResult = negative_stitching_future_.result();
    if (!negResult.empty()) {
        stitched_result_negative_ = negResult;
        if (negative_toggle_btn_) {
            negative_toggle_btn_->setVisible(true);
            negative_toggle_btn_->setEnabled(true);
            negative_toggle_btn_->setChecked(false);
            negative_toggle_btn_->setText(QStringLiteral("显示负片结果"));
        }
        emit logMessage(QStringLiteral("负片拼接完成"), QStringLiteral("INFO"));
        emit negativeStitchingComplete(negResult);
    } else {
        emit logMessage(QStringLiteral("负片拼接失败或无负片图像"), QStringLiteral("WARN"));
    }
}
