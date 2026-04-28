#pragma once

#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QTabWidget>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTimer>
#include <opencv2/opencv.hpp>
#include <QImage>
#include <QPixmap>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QTableWidget>
#include <QScrollArea>
#include <QHeaderView>
#include <QAbstractItemView>

#include "detector/YoloDetector.h"
#include "arm/ModbusArmController.h"
#include "arm/SMovementController.h"
#include "camera/CameraHandler.h"
#include "stitch/ImageStitcher.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Arm Control Slots
    void on_connectArmButton_clicked();
    void on_disconnectArmButton_clicked();
    void on_moveToPositionButton_clicked();
    void on_startContinuousButton_clicked();
    void on_stopContinuousButton_clicked();
    void on_stopAllButton_clicked();
    void on_readPositionButton_clicked();
    void on_setSpeedButton_clicked();
    void on_readSpeedButton_clicked();
    void on_zeroButton_clicked();
    void on_realTimePositionCheckbox_stateChanged(int state);
    void on_startSMovementButton_clicked();
    void on_stopSMovementButton_clicked();
    void on_pauseSMovementButton_clicked();
    void on_resumeSMovementButton_clicked();
    
    // Camera Slots
    void on_connectCameraButton_clicked();
    void on_disconnectCameraButton_clicked();
    void on_startCaptureButton_clicked();
    void on_stopCaptureButton_clicked();
    void on_captureImageButton_clicked();
    void on_setSaveDirectoryButton_clicked();
    void on_enumerateCamerasButton_clicked();
    void on_setExposureButton_clicked();
    void on_setGainButton_clicked();
    void on_setResolutionButton_clicked();
    
    // Detection Slots
    void on_loadModelButton_clicked();
    void on_loadImageButton_clicked();
    void on_detectButton_clicked();
    void on_setConfidenceThresholdButton_clicked();
    void on_setNmsThresholdButton_clicked();
    
    // Stitching Slots
    void on_stitchImagesButton_clicked();
    void on_loadImagesButton_clicked();
    void on_saveStitchedImageButton_clicked();
    
    // Timer Slots
    void updateArmStatus();
    void updateCameraStatus();
    void updateCameraImage();
    
    // Image Callback Slot
    void onImageCaptured(const cv::Mat& frame);
    
    // Arm Status Callback Slot
    void onArmStatusChanged(const arm::ArmStatus& status);
    
    // Camera Status Callback Slot
    void onCameraStatusChanged(const camera::CameraStatus& status);
    
    // Movement Progress Callback Slot
    void onMovementProgress(int current, int total);
    
    // Movement Status Callback Slot
    void onMovementStatus(const arm::SMovementStatus& status);
    
    // Stitching Progress Callback Slot
    void onStitchingProgress(int current, int total);
    
    // Stitching Status Callback Slot
    void onStitchingStatus(const std::string& status);
    
    // Stitching Finished Slot (for concurrent stitching)
    void onStitchingFinished();
    
    // Detection Result Callback Slot
    void onDetectionResult(const std::vector<detector::Detection>& detections, const cv::Mat& image);
    
    // Detection Toggle Slot
    void on_enableDetectionCheckbox_stateChanged(int state);
    
    // Cell Click Slots
    void on_topCellsTable_cellClicked(int row, int column);
    void on_bottomCellsTable_cellClicked(int row, int column);

private:
    // UI Setup
    void setupUI();
    void setupArmCameraTab();
    void setupDetectionTab();
    void setupStitchingTab();
    
    // Helper Functions
    void displayImage(const cv::Mat& image, QLabel* label);
    QImage cvMatToQImage(const cv::Mat& mat);
    
    // Member Variables
    
    // Modules
    detector::YoloDetector yolo_detector_;
    arm::ModbusArmController arm_controller_;
    arm::SMovementController s_movement_controller_;
    camera::CameraHandler camera_handler_;
    stitch::ImageStitcher image_stitcher_;
    
    // UI Components
    QTabWidget *tab_widget_;
    
    // Main Layout Widgets for Arm-Camera Combined Tab
    QWidget *arm_camera_tab_;
    QHBoxLayout *arm_camera_main_layout_;
    QWidget *left_widget_;
    QWidget *center_widget_;
    QWidget *right_widget_;
    QVBoxLayout *left_layout_;
    QVBoxLayout *center_layout_;
    QVBoxLayout *right_layout_;
    
    // 10x10 Cells
    QTableWidget *top_cells_table_;    // 上方单元格：显示S型运动时的图片
    QTableWidget *bottom_cells_table_; // 下方单元格：点击移动功能
    QLabel *top_cells_label_;
    QLabel *bottom_cells_label_;
    
    // Arm Control Components (moved from tab to right widget)
    QGroupBox *arm_connection_group_;
    QLineEdit *arm_ip_edit_;
    QSpinBox *arm_port_spin_;
    QPushButton *connect_arm_button_;
    QPushButton *disconnect_arm_button_;
    
    QGroupBox *arm_position_group_;
    QDoubleSpinBox *x_pos_spin_;
    QDoubleSpinBox *y_pos_spin_;
    QDoubleSpinBox *z_pos_spin_;
    QDoubleSpinBox *a_pos_spin_;
    QDoubleSpinBox *b_pos_spin_;
    QPushButton *move_to_position_button_;
    QPushButton *read_position_button_;
    QPushButton *zero_button_;
    
    QGroupBox *arm_continuous_group_;
    QComboBox *continuous_axis_combo_;
    QComboBox *direction_combo_;
    QPushButton *start_continuous_button_;
    QPushButton *stop_continuous_button_;
    QPushButton *stop_all_button_;
    
    QGroupBox *arm_speed_group_;
    QDoubleSpinBox *speed_spin_;
    QPushButton *set_speed_button_;
    QLabel *current_speed_label_;
    QPushButton *read_speed_button_;
    
    // Real-time position reading
    QCheckBox *real_time_position_checkbox_;
    
    QGroupBox *s_movement_group_;
    QPushButton *start_s_movement_button_;
    QPushButton *stop_s_movement_button_;
    QPushButton *pause_s_movement_button_;
    QPushButton *resume_s_movement_button_;
    
    QProgressBar *movement_progress_bar_;
    QTextEdit *movement_status_text_;
    
    // Camera Components (moved from tab to center widget)
    QGroupBox *camera_connection_group_;
    QComboBox *camera_combo_;
    QPushButton *connect_camera_button_;
    QPushButton *disconnect_camera_button_;
    QPushButton *enumerate_cameras_button_;
    
    QGroupBox *camera_control_group_;
    QPushButton *start_capture_button_;
    QPushButton *stop_capture_button_;
    QPushButton *capture_image_button_;
    
    QGroupBox *camera_settings_group_;
    QDoubleSpinBox *exposure_spin_;
    QPushButton *set_exposure_button_;
    QDoubleSpinBox *gain_spin_;
    QPushButton *set_gain_button_;
    QSpinBox *width_spin_;
    QSpinBox *height_spin_;
    QPushButton *set_resolution_button_;
    QLineEdit *save_directory_edit_;
    QPushButton *set_save_directory_button_;
    
    QLabel *camera_image_label_;
    QTextEdit *camera_status_text_;
    
    // Detection Tab
    QGroupBox *model_group_;
    QLineEdit *param_path_edit_;
    QLineEdit *bin_path_edit_;
    QPushButton *load_model_button_;
    
    QGroupBox *detection_settings_group_;
    QDoubleSpinBox *confidence_threshold_spin_;
    QDoubleSpinBox *nms_threshold_spin_;
    QPushButton *set_confidence_threshold_button_;
    QPushButton *set_nms_threshold_button_;
    
    QGroupBox *detection_group_;
    QPushButton *load_image_button_;
    QPushButton *detect_button_;
    
    QLabel *detection_image_label_;
    QTextEdit *detection_status_text_;
    
    // Stitching Tab
    QGroupBox *stitching_input_group_;
    QLineEdit *input_dir_edit_;
    QPushButton *load_images_button_;
    QSpinBox *stitch_grid_size_x_spin_;
    QSpinBox *stitch_grid_size_y_spin_;
    
    QGroupBox *stitching_control_group_;
    QPushButton *stitch_images_button_;
    QPushButton *save_stitched_image_button_;
    
    QProgressBar *stitching_progress_bar_;
    QTextEdit *stitching_status_text_;
    QLabel *stitched_image_label_;
    
    // Status Bar
    QLabel *status_bar_label_;
    
    // Timers
    QTimer *arm_status_timer_;
    QTimer *camera_status_timer_;
    QTimer *camera_update_timer_;
    
    // Images
    cv::Mat current_image_;
    cv::Mat detection_result_;
    cv::Mat stitched_result_;
    std::map<std::pair<int, int>, std::string> s_movement_images_; // 存储S型运动时的图片路径
    
    // Flags
    bool model_loaded_ = false;
    bool camera_connected_ = false;
    bool arm_connected_ = false;
    bool capturing_ = false;
    bool detecting_ = false;
    bool stitching_ = false;
    bool s_movement_running_ = false;
    bool image_detection_enabled_ = true; // 默认开启图像检测
    
    // UI Components for Detection Toggle
    QCheckBox *enable_detection_checkbox_;
    
    // Concurrent stitching
    QFuture<cv::Mat> stitching_future_;
    QFutureWatcher<cv::Mat> stitching_watcher_;
    
    // Paths
    std::string model_param_path_;
    std::string model_bin_path_;
    std::string input_dir_path_;
    std::string save_path_;
};
