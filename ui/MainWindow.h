#pragma once

// MSVC: Qt headers pull in windows.h which conflicts with winsock2.h needed by
// ModbusArmController. WIN32_LEAN_AND_MEAN + NOMINMAX must be defined before
// any Windows header is included.
#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

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
#include <QShortcut>
#include <mutex>
#include <QtConcurrent/QtConcurrent>
#include <opencv2/opencv.hpp>

#include "core/arm/ModbusArmController.h"
#include "core/arm/SMovementController.h"
#include "core/camera/CameraHandler.h"
#include "core/detector/YoloDetector.h"
#include "core/SharpnessEvaluator.h"
#include "core/stitch/ImageStitcher.h"

// Forward-declare controllers
class CameraPaneController;
class ArmPaneController;
class ScanController;
class StitchController;
class DetectController;

class AppController;
namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow {
    Q_OBJECT

    // Controllers access MainWindow shared state via parent() cast
    friend class CameraPaneController;
    friend class ArmPaneController;
    friend class ScanController;
    friend class StitchController;
    friend class DetectController;

public:
    explicit MainWindow(AppController& ctrl, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Camera frame
    void onCameraFrameReady(const cv::Mat& frame);

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

    bool eventFilter(QObject* obj, QEvent* event) override;
    void changeEvent(QEvent* event) override;

    // Shared state accessors for controllers
    const cv::Mat& currentImage() const { return current_image_; }
    void setCurrentImage(const cv::Mat& img) { current_image_ = img; }
    const std::string& currentImageSource() const { return current_image_source_; }
    void setCurrentImageSource(const std::string& src) { current_image_source_ = src; }
    void clearCurrentImageSource() { current_image_source_.clear(); }
    bool isDetectFullscreenActive() const { return detect_fullscreen_active_; }
    void setDetectFullscreenActive(bool v) { detect_fullscreen_active_ = v; }
    QLabel* fullscreenDialog() const { return fullscreen_dlg_; }

    AppController& ctrl_;
    Ui::MainWindow* ui_;

    // Sub-controllers (owned, initialized after setupUi)
    CameraPaneController* camera_ctrl_ = nullptr;
    ArmPaneController* arm_ctrl_ = nullptr;
    ScanController* scan_ctrl_ = nullptr;
    StitchController* stitch_ctrl_ = nullptr;
    DetectController* detect_ctrl_ = nullptr;

    // Log panel
    QTextEdit* status_log_edit_;

    // Shared state (accessed by multiple controllers + MainWindow)
    cv::Mat current_image_;
    std::string current_image_source_;           // current image source file

    // Grid selection state (UI only)
    int selected_grid_row_ = -1;
    int selected_grid_col_ = -1;
    void applyCellBorder(int row, int col);

    QPixmap stitched_pixmap_, detected_pixmap_;
    QLabel* fullscreen_dlg_ = nullptr;
    bool camera_fullscreen_active_ = false;
    bool detect_fullscreen_active_ = false;
    bool image_detection_enabled_ = false;
    QWidget* preview_dlg_ = nullptr; // grid cell fullscreen preview

    // Keyboard shortcuts
    QShortcut* sc_emergency_stop_ = nullptr;
    QShortcut* sc_toggle_scan_ = nullptr;
    QShortcut* sc_start_stitch_ = nullptr;
    QShortcut* sc_manual_detect_ = nullptr;
    QShortcut* sc_toggle_detection_ = nullptr;
    QShortcut* sc_toggle_fullscreen_ = nullptr;

    // FPS tracking
    QLabel* fps_label_ = nullptr;
    QCheckBox* camera_preview_check_ = nullptr;
    qint64 last_fps_timestamp_ = 0;
    int fps_frame_count_ = 0;
    float current_fps_ = 0.0f;

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
