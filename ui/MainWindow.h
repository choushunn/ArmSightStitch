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
#include <QImage>
#include <QPixmap>
#include <QMessageBox>
#include <QTableWidget>
#include <QScrollArea>
#include <QHeaderView>
#include <QAbstractItemView>
#include <opencv2/opencv.hpp>

class AppController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(AppController& ctrl, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Arm Control
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

    // Camera
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

    // Detection
    void on_loadModelButton_clicked();
    void on_loadImageButton_clicked();
    void on_detectButton_clicked();
    void on_setConfidenceThresholdButton_clicked();
    void on_setNmsThresholdButton_clicked();

    // Stitching
    void on_stitchImagesButton_clicked();
    void on_loadImagesButton_clicked();
    void on_saveStitchedImageButton_clicked();

    // Timer
    void updateCameraImage();

    // Callbacks
    void onArmStatusChanged(const arm::ArmStatus& status);
    void onCameraStatusChanged(const camera::CameraStatus& status);
    void onMovementStatus(const arm::SMovementStatus& status);
    void onStitchingProgress(int current, int total);
    void onStitchingStatus(const std::string& status);
    void onStitchingFinished();
    void onDetectionResult(const std::vector<detector::Detection>& detections, const cv::Mat& image);
    void on_enableDetectionCheckbox_stateChanged(int state);
    void on_topCellsTable_cellClicked(int row, int column);
    void on_bottomCellsTable_cellClicked(int row, int column);

private:
    void setupUI();
    void setupArmCameraTab();
    void setupDetectionTab();
    void setupStitchingTab();
    void displayImage(const cv::Mat& image, QLabel* label);
    QImage cvMatToQImage(const cv::Mat& mat);

    AppController& ctrl_;

    // UI Components
    QTabWidget *tab_widget_;

    // Arm-Camera Tab
    QWidget *arm_camera_tab_;
    QHBoxLayout *arm_camera_main_layout_;
    QWidget *left_widget_, *center_widget_, *right_widget_;
    QVBoxLayout *left_layout_, *center_layout_, *right_layout_;

    QTableWidget *top_cells_table_, *bottom_cells_table_;
    QLabel *top_cells_label_, *bottom_cells_label_;

    // Arm widgets
    QGroupBox *arm_connection_group_;
    QLineEdit *arm_ip_edit_;
    QSpinBox *arm_port_spin_;
    QPushButton *connect_arm_button_, *disconnect_arm_button_;

    QGroupBox *arm_position_group_;
    QDoubleSpinBox *x_pos_spin_, *y_pos_spin_, *z_pos_spin_, *a_pos_spin_, *b_pos_spin_;
    QPushButton *move_to_position_button_, *read_position_button_, *zero_button_;

    QGroupBox *arm_continuous_group_;
    QComboBox *continuous_axis_combo_, *direction_combo_;
    QPushButton *start_continuous_button_, *stop_continuous_button_, *stop_all_button_;

    QGroupBox *arm_speed_group_;
    QDoubleSpinBox *speed_spin_;
    QPushButton *set_speed_button_, *read_speed_button_;
    QLabel *current_speed_label_;

    QCheckBox *real_time_position_checkbox_;

    QGroupBox *s_movement_group_;
    QPushButton *start_s_movement_button_, *stop_s_movement_button_;
    QPushButton *pause_s_movement_button_, *resume_s_movement_button_;
    QProgressBar *movement_progress_bar_;
    QTextEdit *movement_status_text_;

    // Camera widgets
    QGroupBox *camera_connection_group_;
    QComboBox *camera_combo_;
    QPushButton *connect_camera_button_, *disconnect_camera_button_;
    QPushButton *enumerate_cameras_button_;

    QGroupBox *camera_control_group_;
    QPushButton *start_capture_button_, *stop_capture_button_, *capture_image_button_;

    QGroupBox *camera_settings_group_;
    QDoubleSpinBox *exposure_spin_;
    QPushButton *set_exposure_button_;
    QDoubleSpinBox *gain_spin_;
    QPushButton *set_gain_button_;
    QSpinBox *width_spin_, *height_spin_;
    QPushButton *set_resolution_button_;
    QLineEdit *save_directory_edit_;
    QPushButton *set_save_directory_button_;

    QLabel *camera_image_label_;
    QTextEdit *camera_status_text_;

    // Detection Tab
    QGroupBox *model_group_;
    QLineEdit *param_path_edit_, *bin_path_edit_;
    QPushButton *load_model_button_;

    QGroupBox *detection_settings_group_;
    QDoubleSpinBox *confidence_threshold_spin_, *nms_threshold_spin_;
    QPushButton *set_confidence_threshold_button_, *set_nms_threshold_button_;

    QGroupBox *detection_group_;
    QPushButton *load_image_button_, *detect_button_;
    QLabel *detection_image_label_;
    QTextEdit *detection_status_text_;

    // Stitching Tab
    QGroupBox *stitching_input_group_;
    QLineEdit *input_dir_edit_;
    QPushButton *load_images_button_;
    QSpinBox *stitch_grid_size_x_spin_, *stitch_grid_size_y_spin_;

    QGroupBox *stitching_control_group_;
    QPushButton *stitch_images_button_, *save_stitched_image_button_;

    QProgressBar *stitching_progress_bar_;
    QTextEdit *stitching_status_text_;
    QLabel *stitched_image_label_;
    QLabel *status_bar_label_;

    // Timers
    QTimer *camera_update_timer_;

    // State
    std::map<std::pair<int, int>, std::string> s_movement_images_;
    cv::Mat current_image_, detection_result_, stitched_result_;
    bool model_loaded_ = false;
    bool image_detection_enabled_ = true;
    bool stitching_ = false;

    // Concurrent stitching
    QFutureWatcher<cv::Mat> stitching_watcher_;
};
