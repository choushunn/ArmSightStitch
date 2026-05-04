#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QButtonGroup>
#include <QFuture>
#include <QTimer>
#include <QFutureWatcher>
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

    // Stitching
    void on_stitchRun();
    void on_stitchSettings();
    void on_saveStitch();

    // Timer / Callbacks
    void updateCameraImage();
    void onStitchingFinished();
    void onArmStatusChanged(const arm::ArmStatus& status);
    void onMovementStatus(const arm::SMovementStatus& status);

    // Grid click
    void on_topCellsTable_cellClicked(int row, int column);
    void on_bottomCellClicked(int id);

private:
    void setupUI();
    void connectSignals();
    void initGridTables();
    void displayImage(const cv::Mat& image, QLabel* label);
    QImage cvMatToQImage(const cv::Mat& mat);

    AppController& ctrl_;
    Ui::MainWindow* ui_;
    QButtonGroup* bottom_cells_group_;

    // State
    std::map<std::pair<int, int>, std::string> s_movement_images_;
    cv::Mat current_image_, stitched_result_;
    bool model_loaded_ = false;
    bool image_detection_enabled_ = true;

    QTimer* camera_update_timer_;
    QFuture<cv::Mat> stitching_future_;
    QFutureWatcher<cv::Mat> stitching_watcher_;
};
