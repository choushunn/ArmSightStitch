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
#include "core/SharpnessEvaluator.h"
#include "core/stitch/IStitcher.h"

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
    // (on_loadModel removed)
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

    // Z-Stack autofocus
    void onZStackFinished();

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
    void appendLogSafe(const QString& msg, const QString& level = "INFO");  // thread-safe
    void showImageFullscreen(const QPixmap& pixmap);
    void startNegativeStitching(const std::string& directory, const cv::Size& grid_size);
    void onNegativeStitchingFinished();
    bool hasNegativeImages(const std::string& directory) const;

    bool eventFilter(QObject* obj, QEvent* event) override;

    AppController& ctrl_;
    Ui::MainWindow* ui_;

    // Log panel
    QTextEdit* status_log_edit_;

    // State
    std::map<std::pair<int, int>, std::string> s_movement_images_;
    mutable std::mutex s_movement_images_mutex_;
    cv::Mat current_image_, stitched_result_;
    cv::Mat stitched_result_negative_;           // 负片拼接结果
    std::string current_image_source_;           // 当前图像来源文件（用于检测 JSON 命名，实时帧为空）
    std::string last_scan_dir_;                   // 最近扫描目录（用于负片拼接查找）
    std::string scan_base_dir_;                   // 扫描目录（用于子文件夹路径构造），受 scan_state_mutex_ 保护
    mutable std::mutex scan_state_mutex_;
    std::map<std::pair<int,int>, std::pair<QPixmap, QPixmap>> grid_thumbnails_;  // {orig, neg}，受 grid_thumbnails_mutex_ 保护
    mutable std::mutex grid_thumbnails_mutex_;

    // 扫描进度栅格样式
    std::set<std::pair<int, int>> scanned_cells_;
    int current_scan_row_ = -1;
    int current_scan_col_ = -1;
    int selected_grid_row_ = -1;
    int selected_grid_col_ = -1;
    void applyCellBorder(int row, int col);

    QPixmap stitched_pixmap_, detected_pixmap_;
    QLabel* fullscreen_dlg_ = nullptr;
    QPushButton* negative_toggle_btn_ = nullptr;  // 拼接结果显示切换按钮
    bool stitch_showing_negative_ = false;         // 当前是否显示负片拼接结果
    std::atomic<bool> scan_showing_negative_{false};  // 扫描网格负片显示，跨线程访问
    bool camera_fullscreen_active_ = false;
    bool detect_fullscreen_active_ = false;
    bool model_loaded_ = false;
    bool image_detection_enabled_ = false;
    bool scanning_ = false;
    bool scan_was_running_ = false;
    bool scan_stopped_by_user_ = false;
    QWidget* preview_dlg_ = nullptr; // grid cell fullscreen preview
    QProgressDialog* stitch_progress_dlg_ = nullptr;
    QProgressDialog* zero_progress_dlg_ = nullptr;
    // 两阶段拼接 — 阶段间暂存加载结果
    std::vector<stitch::PositionedImage> stitch_positioned_;
    std::vector<cv::Mat> stitch_raw_images_;
    cv::Size stitch_load_grid_{0, 0};
    int stitch_load_count_ = 0;  // 归零进度弹窗，避免 findChild 误匹配

    QFuture<cv::Mat> stitching_future_;
    QFutureWatcher<cv::Mat> stitching_watcher_;
    QFuture<cv::Mat> negative_stitching_future_;
    QFutureWatcher<cv::Mat> negative_stitching_watcher_;

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

    // Async folder loading — result from worker thread
    struct FolderLoadResult {
        std::vector<std::string> cellFiles;   // indexed flat: [row*gs.width + col]
        std::vector<QPixmap> thumbnails;      // indexed flat, same scheme
        cv::Size gridSize;
        int count = 0;
    };
    QFutureWatcher<FolderLoadResult>* folder_load_watcher_ = nullptr;

    // Async cell fullscreen preview
    std::string cell_preview_load_path_;

    // Async frame replace (right-click)
    QFuture<std::pair<cv::Mat, bool>> frame_replace_future_;
    QFutureWatcher<std::pair<cv::Mat, bool>> frame_replace_watcher_;
    int frame_replace_row_ = -1, frame_replace_col_ = -1;

    // Async Z-Stack autofocus (right-click)
    struct ZStackResult {
        bool    ok = false;
        int     row = -1, col = -1;
        int     optimalZ = 0;
        int     prevZ = 0;          // Z before scan
        double  peakScore = 0.0;
        std::string origPath;       // original image file path
        std::string negPath;        // negative image file path
        std::string zMapPath;       // Z-Map JSON file path (for confirmation write)
        core::ZFocusResult focusData;
    };
    QFuture<ZStackResult> zstack_future_;
    QFutureWatcher<ZStackResult> zstack_watcher_;
    std::atomic<bool> zstack_busy_{false};
};
