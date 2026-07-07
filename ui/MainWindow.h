#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QFuture>
#include <QTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QProgressDialog>
#include <QSpinBox>
#include <QTextEdit>
#include <QFutureWatcher>
#include <mutex>
#include <QtConcurrent/QtConcurrent>
#include <opencv2/opencv.hpp>

#include "core/arm/IArmController.h"
#include "core/camera/ICameraHandler.h"
#include "core/detector/IDetector.h"

class AppController;
namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(AppController& ctrl, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Camera
    void on_connectCamera();
    void on_disconnectCamera();
    void on_enumerateCameras();
    void on_captureImage();

    // Arm
    void on_connectArm();
    void on_disconnectArm();
    void on_moveToPosition();
    void on_readPosition();
    void on_zeroArm();

    // S-Movement
    void on_toggleStartStopSMovement();
    void on_togglePauseSMovement();

    // Detection
    void on_loadModel();
    void on_detSettings();
    void on_manualDetect();

    // Stitching
    void on_stitchRun();
    void on_stitchSettings();
    void on_saveStitch();

    // Timer / Callbacks
    void onCameraFrameReady(const cv::Mat& frame);
    void onStitchingFinished();
    void onDetectionFinished();
    void onArmConnectFinished();
    void onArmMoveFinished();
    void onZeroProgressFinished();
    bool isArmAtZero();
    void startScanSequence();
    void onArmStatusChanged(const arm::ArmStatus& status);
    void onMovementStatus(const arm::SMovementStatus& status);

    // Grid click
    void on_topCellsTable_cellClicked(int row, int column);

    // Menu navigation
    void onMenuButtonClicked(int id);

private:
    void setupMenuNavigation();
    void connectSignals();
    void initGridTables();
    void displayImage(const cv::Mat& image, QLabel* label);
    void displayImageFullQuality(const cv::Mat& image, QLabel* label, QPixmap& storage);
    QImage cvMatToQImage(const cv::Mat& mat);
    void addCameraOverlay(cv::Mat& image);
    void appendLog(const QString& msg, const QString& level = "INFO");
    void showImageFullscreen(const QPixmap& pixmap);

    bool eventFilter(QObject* obj, QEvent* event) override;

    AppController& ctrl_;
    Ui::MainWindow* ui_;

    // Log panel
    QTextEdit* status_log_edit_;

    // State
    std::map<std::pair<int, int>, std::string> s_movement_images_;
    mutable std::mutex s_movement_images_mutex_;
    cv::Mat current_image_, stitched_result_;
    QPixmap stitched_pixmap_, detected_pixmap_;
    QLabel* fullscreen_dlg_ = nullptr;
    bool camera_fullscreen_active_ = false;
    bool detect_fullscreen_active_ = false;
    bool model_loaded_ = false;
    bool image_detection_enabled_ = false;
    bool scanning_ = false;
    bool scan_was_running_ = false;
    bool scan_stopped_by_user_ = false;
    QWidget* preview_dlg_ = nullptr; // grid cell fullscreen preview
    QProgressDialog* stitch_progress_dlg_ = nullptr;

    QFuture<cv::Mat> stitching_future_;
    QFutureWatcher<cv::Mat> stitching_watcher_;

    struct DetectionResult {
        cv::Mat frame;
        std::vector<detector::Detection> detections;
    };
    QFuture<DetectionResult> detection_future_;
    QFutureWatcher<DetectionResult> detection_watcher_;
    std::atomic<bool> detection_busy_{false};

    // Manual (one-shot) detection
    QFuture<DetectionResult> manual_detect_future_;
    QFutureWatcher<DetectionResult> manual_detect_watcher_;

    // Stitching progress
    QProgressBar* stitch_progress_ = nullptr;

    // FPS label next to resolution combo
    QLabel* fps_label_ = nullptr;

    // Camera preview toggle in toolbar
    QCheckBox* camera_preview_check_ = nullptr;

    // FPS tracking for camera overlay
    qint64 last_fps_timestamp_ = 0;
    int fps_frame_count_ = 0;
    float current_fps_ = 0.0f;

    QFuture<bool> arm_connect_future_;
    QFutureWatcher<bool> arm_connect_watcher_;
    QFuture<void> arm_move_future_;
    QFutureWatcher<void> arm_move_watcher_;
    QFuture<bool> zero_progress_future_;
    QFutureWatcher<bool> zero_progress_watcher_;
};
