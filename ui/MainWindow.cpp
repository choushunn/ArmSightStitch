#include "MainWindow.h"
#include "ui/AppController.h"
#include "infra/config/ConfigManager.h"

#include <spdlog/spdlog.h>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStatusBar>
#include <QtConcurrent/QtConcurrent>
#include <cmath>
#include <filesystem>

MainWindow::MainWindow(AppController& ctrl, QWidget *parent)
    : QMainWindow(parent)
    , ctrl_(ctrl)
{
    SPDLOG_INFO("MainWindow initializing...");
    setupUI();

    camera_update_timer_ = new QTimer(this);
    connect(camera_update_timer_, &QTimer::timeout, this, &MainWindow::updateCameraImage);

    ctrl_.cameraHandler().setImageCallback([this](const cv::Mat& frame) {
        QMetaObject::invokeMethod(this, [this, frame]() {
            current_image_ = frame;
        }, Qt::QueuedConnection);
    });

    ctrl_.cameraHandler().setStatusCallback([this](const camera::CameraStatus& status) {
        QMetaObject::invokeMethod(this, "onCameraStatusChanged",
            Q_ARG(const camera::CameraStatus&, status));
    });

    ctrl_.armController().setStatusCallback([this](const arm::ArmStatus& status) {
        QMetaObject::invokeMethod(this, "onArmStatusChanged",
            Q_ARG(const arm::ArmStatus&, status));
    });

    ctrl_.movementController().setStatusCallback([this](const arm::SMovementStatus& status) {
        QMetaObject::invokeMethod(this, "onMovementStatus",
            Q_ARG(const arm::SMovementStatus&, status));
    });

    ctrl_.stitcher().setProgressCallback([this](int current, int total) {
        QMetaObject::invokeMethod(this, "onStitchingProgress",
            Q_ARG(int, current), Q_ARG(int, total));
    });

    ctrl_.stitcher().setStatusCallback([this](const std::string& status) {
        QMetaObject::invokeMethod(this, "onStitchingStatus",
            Q_ARG(const std::string&, status));
    });

    connect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished,
            this, &MainWindow::onStitchingFinished);

    connect(&ctrl_, &AppController::statusMessage, this, [this](const QString& msg) {
        status_bar_label_->setText(msg);
    });
    connect(&ctrl_, &AppController::errorMessage, this, [this](const QString& msg) {
        status_bar_label_->setText("Error: " + msg);
        QMessageBox::critical(this, "Error", msg);
    });
    connect(&ctrl_, &AppController::stitchingFinished, this, [this](const cv::Mat& result) {
        stitched_result_ = result;
        displayImage(result, stitched_image_label_);
    });

    SPDLOG_INFO("MainWindow: Enumerating cameras...");
    on_enumerateCamerasButton_clicked();
    SPDLOG_INFO("MainWindow initialized");
}

MainWindow::~MainWindow() {
    SPDLOG_INFO("MainWindow shutting down...");
    camera_update_timer_->stop();
    SPDLOG_INFO("MainWindow shutdown complete");
}

// ==================== UI Setup ====================

void MainWindow::setupUI() {
    setWindowTitle("ArmSightStitch");
    resize(1500, 900);

    tab_widget_ = new QTabWidget(this);
    setCentralWidget(tab_widget_);

    setupArmCameraTab();
    setupDetectionTab();
    setupStitchingTab();

    status_bar_label_ = new QLabel("Ready");
    statusBar()->addWidget(status_bar_label_);
}

void MainWindow::setupDetectionTab() {
    QWidget *detection_tab = new QWidget(this);
    QVBoxLayout *main_layout = new QVBoxLayout(detection_tab);

    model_group_ = new QGroupBox("模型加载");
    QGridLayout *model_layout = new QGridLayout(model_group_);
    auto& cfg = ConfigManager::instance();
    param_path_edit_ = new QLineEdit(QString::fromStdString(cfg.modelParamPath()));
    bin_path_edit_ = new QLineEdit(QString::fromStdString(cfg.modelBinPath()));
    load_model_button_ = new QPushButton("加载模型");
    model_layout->addWidget(new QLabel("Param:"), 0, 0);
    model_layout->addWidget(param_path_edit_, 0, 1);
    model_layout->addWidget(new QLabel("Bin:"), 1, 0);
    model_layout->addWidget(bin_path_edit_, 1, 1);
    model_layout->addWidget(load_model_button_, 2, 0, 1, 2);

    detection_settings_group_ = new QGroupBox("检测设置");
    QGridLayout *settings_layout = new QGridLayout(detection_settings_group_);
    confidence_threshold_spin_ = new QDoubleSpinBox();
    confidence_threshold_spin_->setRange(0, 1);
    confidence_threshold_spin_->setValue(0.3);
    confidence_threshold_spin_->setSingleStep(0.05);
    set_confidence_threshold_button_ = new QPushButton("设置置信度");
    nms_threshold_spin_ = new QDoubleSpinBox();
    nms_threshold_spin_->setRange(0, 1);
    nms_threshold_spin_->setValue(0.3);
    nms_threshold_spin_->setSingleStep(0.05);
    set_nms_threshold_button_ = new QPushButton("设置NMS");
    settings_layout->addWidget(new QLabel("置信度:"), 0, 0);
    settings_layout->addWidget(confidence_threshold_spin_, 0, 1);
    settings_layout->addWidget(set_confidence_threshold_button_, 0, 2);
    settings_layout->addWidget(new QLabel("NMS:"), 1, 0);
    settings_layout->addWidget(nms_threshold_spin_, 1, 1);
    settings_layout->addWidget(set_nms_threshold_button_, 1, 2);

    detection_group_ = new QGroupBox("检测");
    QHBoxLayout *detection_layout = new QHBoxLayout(detection_group_);
    load_image_button_ = new QPushButton("加载图像");
    detect_button_ = new QPushButton("检测");
    detection_layout->addWidget(load_image_button_);
    detection_layout->addWidget(detect_button_);

    detection_image_label_ = new QLabel("检测结果");
    detection_image_label_->setAlignment(Qt::AlignCenter);
    detection_image_label_->setStyleSheet("background: #f0f0f0; border: 1px solid #ccc;");
    detection_image_label_->setMinimumHeight(400);
    detection_status_text_ = new QTextEdit();
    detection_status_text_->setReadOnly(true);
    detection_status_text_->setMaximumHeight(100);

    main_layout->addWidget(model_group_);
    main_layout->addWidget(detection_settings_group_);
    main_layout->addWidget(detection_group_);
    main_layout->addWidget(new QLabel("结果:"));
    main_layout->addWidget(detection_image_label_);
    main_layout->addWidget(new QLabel("状态:"));
    main_layout->addWidget(detection_status_text_);
    tab_widget_->addTab(detection_tab, "目标检测");

    connect(load_model_button_, &QPushButton::clicked, this, &MainWindow::on_loadModelButton_clicked);
    connect(load_image_button_, &QPushButton::clicked, this, &MainWindow::on_loadImageButton_clicked);
    connect(detect_button_, &QPushButton::clicked, this, &MainWindow::on_detectButton_clicked);
    connect(set_confidence_threshold_button_, &QPushButton::clicked, this, &MainWindow::on_setConfidenceThresholdButton_clicked);
    connect(set_nms_threshold_button_, &QPushButton::clicked, this, &MainWindow::on_setNmsThresholdButton_clicked);
}

void MainWindow::setupArmCameraTab() {
    arm_camera_tab_ = new QWidget(this);
    arm_camera_main_layout_ = new QHBoxLayout(arm_camera_tab_);

    // Left: 10x10 cells
    left_widget_ = new QWidget();
    left_layout_ = new QVBoxLayout(left_widget_);
    top_cells_label_ = new QLabel("S型运动图像");
    top_cells_label_->setAlignment(Qt::AlignCenter);
    top_cells_label_->setStyleSheet("font-weight: bold;");
    left_layout_->addWidget(top_cells_label_);

    top_cells_table_ = new QTableWidget(10, 10);
    top_cells_table_->setHorizontalHeaderLabels({"1","2","3","4","5","6","7","8","9","10"});
    top_cells_table_->setVerticalHeaderLabels({"1","2","3","4","5","6","7","8","9","10"});
    top_cells_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    top_cells_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    top_cells_table_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    top_cells_table_->setMinimumSize(300, 300);
    left_layout_->addWidget(top_cells_table_, 1);

    bottom_cells_label_ = new QLabel("点击移动");
    bottom_cells_label_->setAlignment(Qt::AlignCenter);
    bottom_cells_label_->setStyleSheet("font-weight: bold;");
    left_layout_->addWidget(bottom_cells_label_);

    bottom_cells_table_ = new QTableWidget(10, 10);
    bottom_cells_table_->setHorizontalHeaderLabels({"1","2","3","4","5","6","7","8","9","10"});
    bottom_cells_table_->setVerticalHeaderLabels({"1","2","3","4","5","6","7","8","9","10"});
    bottom_cells_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bottom_cells_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    bottom_cells_table_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    bottom_cells_table_->setMinimumSize(300, 300);
    left_layout_->addWidget(bottom_cells_table_, 1);

    for (int r = 0; r < 10; ++r) {
        for (int c = 0; c < 10; ++c) {
            QTableWidgetItem *ti = new QTableWidgetItem("");
            ti->setTextAlignment(Qt::AlignCenter);
            top_cells_table_->setItem(r, c, ti);
            QTableWidgetItem *bi = new QTableWidgetItem(QString("%1,%2").arg(r).arg(c));
            bi->setTextAlignment(Qt::AlignCenter);
            bottom_cells_table_->setItem(r, c, bi);
        }
    }

    // Center: Camera
    center_widget_ = new QWidget();
    center_layout_ = new QVBoxLayout(center_widget_);

    camera_connection_group_ = new QGroupBox("相机连接");
    QGridLayout *conn_layout = new QGridLayout(camera_connection_group_);
    camera_combo_ = new QComboBox();
    connect_camera_button_ = new QPushButton("连接");
    disconnect_camera_button_ = new QPushButton("断开");
    disconnect_camera_button_->setEnabled(false);
    enumerate_cameras_button_ = new QPushButton("枚举");
    conn_layout->addWidget(new QLabel("相机:"), 0, 0);
    conn_layout->addWidget(camera_combo_, 0, 1);
    conn_layout->addWidget(enumerate_cameras_button_, 0, 2);
    conn_layout->addWidget(connect_camera_button_, 1, 0);
    conn_layout->addWidget(disconnect_camera_button_, 1, 1);

    camera_control_group_ = new QGroupBox("控制");
    QHBoxLayout *ctrl_layout = new QHBoxLayout(camera_control_group_);
    start_capture_button_ = new QPushButton("开始采集");
    stop_capture_button_ = new QPushButton("停止采集");
    stop_capture_button_->setEnabled(false);
    capture_image_button_ = new QPushButton("拍照");
    capture_image_button_->setEnabled(false);
    QCheckBox *enable_detection = new QCheckBox("实时检测");
    enable_detection->setChecked(true);
    connect(enable_detection, &QCheckBox::stateChanged, this, &MainWindow::on_enableDetectionCheckbox_stateChanged);
    ctrl_layout->addWidget(start_capture_button_);
    ctrl_layout->addWidget(stop_capture_button_);
    ctrl_layout->addWidget(capture_image_button_);
    ctrl_layout->addWidget(enable_detection);

    camera_settings_group_ = new QGroupBox("设置");
    QGridLayout *set_layout = new QGridLayout(camera_settings_group_);
    exposure_spin_ = new QDoubleSpinBox();
    exposure_spin_->setRange(0, 1000);
    exposure_spin_->setValue(100);
    set_exposure_button_ = new QPushButton("曝光");
    gain_spin_ = new QDoubleSpinBox();
    gain_spin_->setRange(0, 5);
    gain_spin_->setValue(1);
    set_gain_button_ = new QPushButton("增益");
    width_spin_ = new QSpinBox();
    width_spin_->setRange(1, 4096);
    width_spin_->setValue(640);
    height_spin_ = new QSpinBox();
    height_spin_->setRange(1, 4096);
    height_spin_->setValue(480);
    set_resolution_button_ = new QPushButton("分辨率");
    save_directory_edit_ = new QLineEdit(QDir::currentPath());
    set_save_directory_button_ = new QPushButton("选择路径");
    set_layout->addWidget(new QLabel("曝光:"), 0, 0);
    set_layout->addWidget(exposure_spin_, 0, 1);
    set_layout->addWidget(set_exposure_button_, 0, 2);
    set_layout->addWidget(new QLabel("增益:"), 1, 0);
    set_layout->addWidget(gain_spin_, 1, 1);
    set_layout->addWidget(set_gain_button_, 1, 2);
    set_layout->addWidget(new QLabel("分辨率:"), 2, 0);
    set_layout->addWidget(width_spin_, 2, 1);
    set_layout->addWidget(height_spin_, 2, 2);
    set_layout->addWidget(set_resolution_button_, 2, 3);
    set_layout->addWidget(new QLabel("保存:"), 3, 0);
    set_layout->addWidget(save_directory_edit_, 3, 1, 1, 2);
    set_layout->addWidget(set_save_directory_button_, 3, 3);

    camera_image_label_ = new QLabel("相机图像");
    camera_image_label_->setAlignment(Qt::AlignCenter);
    camera_image_label_->setStyleSheet("background: #f0f0f0; border: 1px solid #ccc;");
    camera_image_label_->setMinimumHeight(400);
    camera_status_text_ = new QTextEdit();
    camera_status_text_->setReadOnly(true);
    camera_status_text_->setMaximumHeight(100);

    center_layout_->addWidget(camera_connection_group_);
    center_layout_->addWidget(camera_control_group_);
    center_layout_->addWidget(camera_settings_group_);
    center_layout_->addWidget(camera_image_label_);
    center_layout_->addWidget(camera_status_text_);

    // Right: Arm
    right_widget_ = new QWidget();
    right_layout_ = new QVBoxLayout(right_widget_);

    auto& cfg = ConfigManager::instance();

    arm_connection_group_ = new QGroupBox("机械臂连接");
    QGridLayout *arm_conn = new QGridLayout(arm_connection_group_);
    arm_ip_edit_ = new QLineEdit(QString::fromStdString(cfg.armIp()));
    arm_port_spin_ = new QSpinBox();
    arm_port_spin_->setRange(1, 65535);
    arm_port_spin_->setValue(cfg.armPort());
    connect_arm_button_ = new QPushButton("连接");
    disconnect_arm_button_ = new QPushButton("断开");
    disconnect_arm_button_->setEnabled(false);
    arm_conn->addWidget(new QLabel("IP:"), 0, 0);
    arm_conn->addWidget(arm_ip_edit_, 0, 1);
    arm_conn->addWidget(new QLabel("端口:"), 0, 2);
    arm_conn->addWidget(arm_port_spin_, 0, 3);
    arm_conn->addWidget(connect_arm_button_, 1, 0, 1, 2);
    arm_conn->addWidget(disconnect_arm_button_, 1, 2, 1, 2);

    arm_position_group_ = new QGroupBox("位置控制");
    QGridLayout *pos = new QGridLayout(arm_position_group_);
    x_pos_spin_ = new QDoubleSpinBox(); x_pos_spin_->setRange(0, 999999);
    y_pos_spin_ = new QDoubleSpinBox(); y_pos_spin_->setRange(0, 999999);
    z_pos_spin_ = new QDoubleSpinBox(); z_pos_spin_->setRange(0, 999999);
    a_pos_spin_ = new QDoubleSpinBox(); a_pos_spin_->setRange(0, 999999);
    b_pos_spin_ = new QDoubleSpinBox(); b_pos_spin_->setRange(0, 999999);
    move_to_position_button_ = new QPushButton("移动");
    read_position_button_ = new QPushButton("读取");
    zero_button_ = new QPushButton("归零");
    pos->addWidget(new QLabel("X:"), 0, 0); pos->addWidget(x_pos_spin_, 0, 1);
    pos->addWidget(new QLabel("Y:"), 0, 2); pos->addWidget(y_pos_spin_, 0, 3);
    pos->addWidget(new QLabel("Z:"), 1, 0); pos->addWidget(z_pos_spin_, 1, 1);
    pos->addWidget(new QLabel("A:"), 1, 2); pos->addWidget(a_pos_spin_, 1, 3);
    pos->addWidget(new QLabel("B:"), 2, 0); pos->addWidget(b_pos_spin_, 2, 1);
    pos->addWidget(move_to_position_button_, 2, 2);
    pos->addWidget(read_position_button_, 2, 3);
    pos->addWidget(zero_button_, 3, 0, 1, 4);

    arm_continuous_group_ = new QGroupBox("连续运动");
    QGridLayout *cont = new QGridLayout(arm_continuous_group_);
    continuous_axis_combo_ = new QComboBox();
    continuous_axis_combo_->addItems({"X","Y","Z","A","B"});
    direction_combo_ = new QComboBox();
    direction_combo_->addItems({"正向","反向"});
    start_continuous_button_ = new QPushButton("开始");
    stop_continuous_button_ = new QPushButton("停止");
    stop_all_button_ = new QPushButton("停止所有");
    cont->addWidget(new QLabel("轴:"), 0, 0);
    cont->addWidget(continuous_axis_combo_, 0, 1);
    cont->addWidget(new QLabel("方向:"), 0, 2);
    cont->addWidget(direction_combo_, 0, 3);
    cont->addWidget(start_continuous_button_, 1, 0, 1, 2);
    cont->addWidget(stop_continuous_button_, 1, 2);
    cont->addWidget(stop_all_button_, 1, 3);

    arm_speed_group_ = new QGroupBox("速度");
    QGridLayout *spd = new QGridLayout(arm_speed_group_);
    speed_spin_ = new QDoubleSpinBox();
    speed_spin_->setRange(0, 100000);
    speed_spin_->setValue(cfg.defaultSpeed());
    set_speed_button_ = new QPushButton("设置");
    read_speed_button_ = new QPushButton("读取");
    current_speed_label_ = new QLabel("当前: --");
    real_time_position_checkbox_ = new QCheckBox("实时位置");
    spd->addWidget(new QLabel("速度:"), 0, 0);
    spd->addWidget(speed_spin_, 0, 1);
    spd->addWidget(set_speed_button_, 0, 2);
    spd->addWidget(read_speed_button_, 0, 3);
    spd->addWidget(current_speed_label_, 1, 0, 1, 4);
    spd->addWidget(real_time_position_checkbox_, 2, 0, 1, 4);

    s_movement_group_ = new QGroupBox("S型运动");
    QGridLayout *sm = new QGridLayout(s_movement_group_);
    start_s_movement_button_ = new QPushButton("开始");
    stop_s_movement_button_ = new QPushButton("停止");
    pause_s_movement_button_ = new QPushButton("暂停");
    resume_s_movement_button_ = new QPushButton("恢复");
    sm->addWidget(start_s_movement_button_, 0, 0);
    sm->addWidget(stop_s_movement_button_, 0, 1);
    sm->addWidget(pause_s_movement_button_, 0, 2);
    sm->addWidget(resume_s_movement_button_, 0, 3);
    movement_progress_bar_ = new QProgressBar();
    movement_status_text_ = new QTextEdit();
    movement_status_text_->setReadOnly(true);
    movement_status_text_->setMaximumHeight(100);

    right_layout_->addWidget(arm_connection_group_);
    right_layout_->addWidget(arm_position_group_);
    right_layout_->addWidget(arm_continuous_group_);
    right_layout_->addWidget(arm_speed_group_);
    right_layout_->addWidget(s_movement_group_);
    right_layout_->addWidget(movement_progress_bar_);
    right_layout_->addWidget(movement_status_text_);

    arm_camera_main_layout_->addWidget(left_widget_, 1);
    arm_camera_main_layout_->addWidget(center_widget_, 2);
    arm_camera_main_layout_->addWidget(right_widget_, 1);
    tab_widget_->addTab(arm_camera_tab_, "机械臂与相机");

    // Camera signals
    connect(connect_camera_button_, &QPushButton::clicked, this, &MainWindow::on_connectCameraButton_clicked);
    connect(disconnect_camera_button_, &QPushButton::clicked, this, &MainWindow::on_disconnectCameraButton_clicked);
    connect(start_capture_button_, &QPushButton::clicked, this, &MainWindow::on_startCaptureButton_clicked);
    connect(stop_capture_button_, &QPushButton::clicked, this, &MainWindow::on_stopCaptureButton_clicked);
    connect(capture_image_button_, &QPushButton::clicked, this, &MainWindow::on_captureImageButton_clicked);
    connect(set_save_directory_button_, &QPushButton::clicked, this, &MainWindow::on_setSaveDirectoryButton_clicked);
    connect(enumerate_cameras_button_, &QPushButton::clicked, this, &MainWindow::on_enumerateCamerasButton_clicked);
    connect(set_exposure_button_, &QPushButton::clicked, this, &MainWindow::on_setExposureButton_clicked);
    connect(set_gain_button_, &QPushButton::clicked, this, &MainWindow::on_setGainButton_clicked);
    connect(set_resolution_button_, &QPushButton::clicked, this, &MainWindow::on_setResolutionButton_clicked);

    // Arm signals
    connect(connect_arm_button_, &QPushButton::clicked, this, &MainWindow::on_connectArmButton_clicked);
    connect(disconnect_arm_button_, &QPushButton::clicked, this, &MainWindow::on_disconnectArmButton_clicked);
    connect(move_to_position_button_, &QPushButton::clicked, this, &MainWindow::on_moveToPositionButton_clicked);
    connect(start_continuous_button_, &QPushButton::clicked, this, &MainWindow::on_startContinuousButton_clicked);
    connect(stop_continuous_button_, &QPushButton::clicked, this, &MainWindow::on_stopContinuousButton_clicked);
    connect(stop_all_button_, &QPushButton::clicked, this, &MainWindow::on_stopAllButton_clicked);
    connect(read_position_button_, &QPushButton::clicked, this, &MainWindow::on_readPositionButton_clicked);
    connect(set_speed_button_, &QPushButton::clicked, this, &MainWindow::on_setSpeedButton_clicked);
    connect(read_speed_button_, &QPushButton::clicked, this, &MainWindow::on_readSpeedButton_clicked);
    connect(zero_button_, &QPushButton::clicked, this, &MainWindow::on_zeroButton_clicked);
    connect(real_time_position_checkbox_, &QCheckBox::stateChanged, this, &MainWindow::on_realTimePositionCheckbox_stateChanged);
    connect(start_s_movement_button_, &QPushButton::clicked, this, &MainWindow::on_startSMovementButton_clicked);
    connect(stop_s_movement_button_, &QPushButton::clicked, this, &MainWindow::on_stopSMovementButton_clicked);
    connect(pause_s_movement_button_, &QPushButton::clicked, this, &MainWindow::on_pauseSMovementButton_clicked);
    connect(resume_s_movement_button_, &QPushButton::clicked, this, &MainWindow::on_resumeSMovementButton_clicked);

    connect(top_cells_table_, &QTableWidget::cellClicked, this, &MainWindow::on_topCellsTable_cellClicked);
    connect(bottom_cells_table_, &QTableWidget::cellClicked, this, &MainWindow::on_bottomCellsTable_cellClicked);
}

void MainWindow::setupStitchingTab() {
    QWidget *stitching_tab = new QWidget(this);
    QVBoxLayout *main_layout = new QVBoxLayout(stitching_tab);

    stitching_input_group_ = new QGroupBox("拼接输入");
    QGridLayout *input_layout = new QGridLayout(stitching_input_group_);
    input_dir_edit_ = new QLineEdit("./images");
    load_images_button_ = new QPushButton("加载图像");
    stitch_grid_size_x_spin_ = new QSpinBox();
    stitch_grid_size_x_spin_->setRange(1, 50);
    stitch_grid_size_x_spin_->setValue(ConfigManager::instance().gridSizeX());
    stitch_grid_size_y_spin_ = new QSpinBox();
    stitch_grid_size_y_spin_->setRange(1, 50);
    stitch_grid_size_y_spin_->setValue(ConfigManager::instance().gridSizeY());
    input_layout->addWidget(new QLabel("目录:"), 0, 0);
    input_layout->addWidget(input_dir_edit_, 0, 1);
    input_layout->addWidget(load_images_button_, 0, 2);
    input_layout->addWidget(new QLabel("网格X:"), 1, 0);
    input_layout->addWidget(stitch_grid_size_x_spin_, 1, 1);
    input_layout->addWidget(new QLabel("网格Y:"), 1, 2);
    input_layout->addWidget(stitch_grid_size_y_spin_, 1, 3);

    stitching_control_group_ = new QGroupBox("拼接控制");
    QHBoxLayout *control_layout = new QHBoxLayout(stitching_control_group_);
    stitch_images_button_ = new QPushButton("开始拼接");
    save_stitched_image_button_ = new QPushButton("保存结果");
    control_layout->addWidget(stitch_images_button_);
    control_layout->addWidget(save_stitched_image_button_);

    stitching_progress_bar_ = new QProgressBar();
    stitching_status_text_ = new QTextEdit();
    stitching_status_text_->setReadOnly(true);
    stitching_status_text_->setMaximumHeight(100);
    stitched_image_label_ = new QLabel("拼接结果");
    stitched_image_label_->setAlignment(Qt::AlignCenter);
    stitched_image_label_->setStyleSheet("background: #f0f0f0; border: 1px solid #ccc;");
    stitched_image_label_->setMinimumHeight(400);

    main_layout->addWidget(stitching_input_group_);
    main_layout->addWidget(stitching_control_group_);
    main_layout->addWidget(stitching_progress_bar_);
    main_layout->addWidget(stitching_status_text_);
    main_layout->addWidget(stitched_image_label_);
    tab_widget_->addTab(stitching_tab, "图像拼接");

    connect(stitch_images_button_, &QPushButton::clicked, this, &MainWindow::on_stitchImagesButton_clicked);
    connect(load_images_button_, &QPushButton::clicked, this, &MainWindow::on_loadImagesButton_clicked);
    connect(save_stitched_image_button_, &QPushButton::clicked, this, &MainWindow::on_saveStitchedImageButton_clicked);
}

// ==================== Helpers ====================

void MainWindow::displayImage(const cv::Mat& image, QLabel* label) {
    if (image.empty()) return;
    QImage qimage = cvMatToQImage(image);
    label->setPixmap(QPixmap::fromImage(qimage).scaled(
        label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QImage MainWindow::cvMatToQImage(const cv::Mat& mat) {
    if (mat.empty()) return QImage();
    if (mat.type() == CV_8UC3) {
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888).rgbSwapped();
    }
    if (mat.type() == CV_8UC1) {
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
    }
    cv::Mat rgb;
    mat.convertTo(rgb, CV_8UC3);
    return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).rgbSwapped();
}

// ==================== Arm Slots ====================

void MainWindow::on_connectArmButton_clicked() {
    std::string ip = arm_ip_edit_->text().toStdString();
    int port = arm_port_spin_->value();
    if (ctrl_.connectArm(ip, port)) {
        connect_arm_button_->setEnabled(false);
        disconnect_arm_button_->setEnabled(true);
    }
}

void MainWindow::on_disconnectArmButton_clicked() {
    ctrl_.disconnectArm();
    connect_arm_button_->setEnabled(true);
    disconnect_arm_button_->setEnabled(false);
}

void MainWindow::on_moveToPositionButton_clicked() {
    auto& arm = ctrl_.armController();
    arm.moveToPosition(0, static_cast<int>(x_pos_spin_->value()));
    arm.moveToPosition(1, static_cast<int>(y_pos_spin_->value()));
    arm.moveToPosition(2, static_cast<int>(z_pos_spin_->value()));
    arm.moveToPosition(3, static_cast<int>(a_pos_spin_->value()));
    arm.moveToPosition(4, static_cast<int>(b_pos_spin_->value()));
    status_bar_label_->setText("Moving to position");
}

void MainWindow::on_startContinuousButton_clicked() {
    int axis = continuous_axis_combo_->currentIndex();
    bool dir = direction_combo_->currentIndex() == 0;
    ctrl_.armController().startContinuousMovement(axis, dir);
}

void MainWindow::on_stopContinuousButton_clicked() {
    ctrl_.armController().stopContinuousMovement(continuous_axis_combo_->currentIndex());
}

void MainWindow::on_stopAllButton_clicked() {
    for (int i = 0; i < 5; ++i) ctrl_.armController().stopAllMovements(i);
}

void MainWindow::on_readPositionButton_clicked() {
    auto& arm = ctrl_.armController();
    x_pos_spin_->setValue(arm.readPosition(0));
    y_pos_spin_->setValue(arm.readPosition(1));
    z_pos_spin_->setValue(arm.readPosition(2));
    a_pos_spin_->setValue(arm.readPosition(3));
    b_pos_spin_->setValue(arm.readPosition(4));
}

void MainWindow::on_setSpeedButton_clicked() {
    int spd = static_cast<int>(speed_spin_->value());
    for (int i = 0; i < 5; ++i) ctrl_.armController().setSpeed(i, spd);
}

void MainWindow::on_readSpeedButton_clicked() {
    if (!ctrl_.armController().isConnected()) return;
    int spd = ctrl_.armController().readSpeed(0);
    current_speed_label_->setText(QString("当前: %1").arg(spd));
}

void MainWindow::on_zeroButton_clicked() {
    if (!ctrl_.armController().isConnected()) {
        QMessageBox::warning(this, "警告", "机械臂未连接");
        return;
    }
    auto& arm = ctrl_.armController();
    for (int i = 0; i < 5; ++i) arm.moveToPosition(i, 0);
    x_pos_spin_->setValue(0); y_pos_spin_->setValue(0);
    z_pos_spin_->setValue(0); a_pos_spin_->setValue(0);
    b_pos_spin_->setValue(0);
}

void MainWindow::on_realTimePositionCheckbox_stateChanged(int state) {
    if (state == Qt::Checked) {
        ctrl_.armController().startAutoRead(0.3f);
    } else {
        ctrl_.armController().stopAutoRead();
    }
}

void MainWindow::on_startSMovementButton_clicked() {
    if (!ctrl_.armController().isConnected()) {
        QMessageBox::warning(this, "警告", "机械臂未连接");
        return;
    }
    if (!ctrl_.cameraHandler().isConnected()) {
        QMessageBox::warning(this, "警告", "相机未连接");
        return;
    }
    if (!ctrl_.startCameraCapture()) {
        QMessageBox::warning(this, "警告", "相机启动失败");
        return;
    }

    auto& cfg = ConfigManager::instance();
    cv::Size gridSize(cfg.gridSizeX(), cfg.gridSizeY());

    // Setup image save callback to update UI
    std::string savePath = cfg.imageSaveBasePath() + "/" +
        std::to_string(QDir(cfg.imageSaveBasePath().c_str()).entryList(
            QDir::Dirs | QDir::NoDotAndDotDot).size() + 1);

    ctrl_.movementController().setImageSaveCallback(
        [this, savePath](const cv::Mat& frame, const std::string& path, int row, int col) {
            std::string filename = path + "/" + std::to_string(row) + "_" + std::to_string(col) + ".jpg";
            bool saved = cv::imwrite(filename, frame);
            if (saved) {
                s_movement_images_[std::make_pair(row, col)] = filename;
                QMetaObject::invokeMethod(this, [this, frame, row, col]() {
                    if (row < 10 && col < 10) {
                        QTableWidgetItem *item = top_cells_table_->item(row, col);
                        if (item) {
                            item->setText(QString("%1,%2").arg(row).arg(col));
                            item->setBackground(QColor(144, 238, 144));
                            QLabel *imgLabel = new QLabel();
                            imgLabel->setPixmap(QPixmap::fromImage(
                                cvMatToQImage(frame)).scaled(50, 50, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                            imgLabel->setAlignment(Qt::AlignCenter);
                            top_cells_table_->setCellWidget(row, col, imgLabel);
                        }
                    }
                }, Qt::QueuedConnection);
            }
            return saved;
        });

    if (ctrl_.startSMovement(gridSize, cfg.stepSize(), cfg.zHeight())) {
        start_s_movement_button_->setEnabled(false);
        stop_s_movement_button_->setEnabled(true);
        pause_s_movement_button_->setEnabled(true);
        resume_s_movement_button_->setEnabled(false);
    } else {
        QMessageBox::critical(this, "错误", "S型运动启动失败");
    }
}

void MainWindow::on_stopSMovementButton_clicked() {
    ctrl_.stopSMovement();
    start_s_movement_button_->setEnabled(true);
    stop_s_movement_button_->setEnabled(false);
    pause_s_movement_button_->setEnabled(false);
    resume_s_movement_button_->setEnabled(false);
}

void MainWindow::on_pauseSMovementButton_clicked() {
    ctrl_.pauseSMovement();
    pause_s_movement_button_->setEnabled(false);
    resume_s_movement_button_->setEnabled(true);
}

void MainWindow::on_resumeSMovementButton_clicked() {
    ctrl_.resumeSMovement();
    pause_s_movement_button_->setEnabled(true);
    resume_s_movement_button_->setEnabled(false);
}

// ==================== Camera Slots ====================

void MainWindow::on_connectCameraButton_clicked() {
    std::string id = camera_combo_->currentData().toString().toStdString();
    if (ctrl_.connectCamera(id)) {
        connect_camera_button_->setEnabled(false);
        disconnect_camera_button_->setEnabled(true);
        start_capture_button_->setEnabled(true);
        capture_image_button_->setEnabled(true);
    }
}

void MainWindow::on_disconnectCameraButton_clicked() {
    ctrl_.disconnectCamera();
    connect_camera_button_->setEnabled(true);
    disconnect_camera_button_->setEnabled(false);
    start_capture_button_->setEnabled(true);
    stop_capture_button_->setEnabled(false);
    capture_image_button_->setEnabled(false);
}

void MainWindow::on_startCaptureButton_clicked() {
    if (ctrl_.startCameraCapture()) {
        start_capture_button_->setEnabled(false);
        stop_capture_button_->setEnabled(true);
        camera_update_timer_->start(33);
    } else {
        QMessageBox::critical(this, "错误", "采集启动失败");
    }
}

void MainWindow::on_stopCaptureButton_clicked() {
    ctrl_.stopCameraCapture();
    start_capture_button_->setEnabled(true);
    stop_capture_button_->setEnabled(false);
    camera_update_timer_->stop();
}

void MainWindow::on_captureImageButton_clicked() {
    if (!ctrl_.cameraHandler().isConnected()) {
        QMessageBox::warning(this, "警告", "相机未连接");
        return;
    }
    if (!ctrl_.startCameraCapture()) {
        QMessageBox::warning(this, "警告", "采集启动失败");
        return;
    }

    static int counter = 0;
    counter++;
    QString saveDir = save_directory_edit_->text();
    QDir().mkpath(saveDir);

    cv::Mat frame;
    bool captured = false;
    for (int i = 0; i < 10 && !captured; ++i) {
        if (ctrl_.captureImage(frame)) {
            captured = true;
            current_image_ = frame;
            displayImage(frame, camera_image_label_);
            QString path = saveDir + "/image_" + QString("%1").arg(counter, 4, 10, QChar('0')) + ".jpg";
            cv::imwrite(path.toStdString(), frame);
            status_bar_label_->setText(QString("Saved: %1").arg(path));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    ctrl_.stopCameraCapture();
    if (!captured) QMessageBox::critical(this, "错误", "采集图像失败");
}

void MainWindow::on_enumerateCamerasButton_clicked() {
    camera_combo_->clear();
    for (const auto& cam : ctrl_.cameraHandler().enumerateCameras()) {
        camera_combo_->addItem(cam.display_name.c_str(), cam.id.c_str());
    }
}

void MainWindow::on_setExposureButton_clicked() {
    ctrl_.cameraHandler().setExposure(exposure_spin_->value());
}

void MainWindow::on_setGainButton_clicked() {
    ctrl_.cameraHandler().setGain(gain_spin_->value());
}

void MainWindow::on_setResolutionButton_clicked() {
    ctrl_.cameraHandler().setResolution(width_spin_->value(), height_spin_->value());
}

void MainWindow::on_setSaveDirectoryButton_clicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择目录", save_directory_edit_->text());
    if (!dir.isEmpty()) save_directory_edit_->setText(dir);
}

// ==================== Detection Slots ====================

void MainWindow::on_loadModelButton_clicked() {
    model_loaded_ = ctrl_.loadDetectorModel(
        param_path_edit_->text().toStdString(),
        bin_path_edit_->text().toStdString());
}

void MainWindow::on_loadImageButton_clicked() {
    QString path = QFileDialog::getOpenFileName(this, "选择图像", "./",
        "图像 (*.jpg *.jpeg *.png *.bmp)");
    if (path.isEmpty()) return;
    current_image_ = cv::imread(path.toStdString());
    if (current_image_.empty()) {
        QMessageBox::warning(this, "警告", "图像加载失败");
        return;
    }
    displayImage(current_image_, detection_image_label_);
    detection_result_ = cv::Mat();
}

void MainWindow::on_detectButton_clicked() {
    if (!model_loaded_ || current_image_.empty()) return;
    auto detections = ctrl_.detect(current_image_);
    detection_result_ = ctrl_.drawDetections(current_image_, detections);
    displayImage(detection_result_, detection_image_label_);
    detection_status_text_->append(QString("检测到 %1 个目标").arg(detections.size()));
}

void MainWindow::on_setConfidenceThresholdButton_clicked() {
    ctrl_.detector().setConfidenceThreshold(confidence_threshold_spin_->value());
}

void MainWindow::on_setNmsThresholdButton_clicked() {
    ctrl_.detector().setNmsThreshold(nms_threshold_spin_->value());
}

// ==================== Stitching Slots ====================

void MainWindow::on_stitchImagesButton_clicked() {
    std::string inputDir = input_dir_edit_->text().toStdString();
    int gx = stitch_grid_size_x_spin_->value();
    int gy = stitch_grid_size_y_spin_->value();
    cv::Size gridSize(gx, gy);

    if (!std::filesystem::exists(inputDir)) {
        QString dir = QFileDialog::getExistingDirectory(this, "选择图像目录");
        if (dir.isEmpty()) return;
        inputDir = dir.toStdString();
        input_dir_edit_->setText(dir);
    }

    std::vector<cv::Mat> images = ctrl_.loadImages(inputDir);
    if (images.empty()) {
        QMessageBox::warning(this, "警告", "没有图像");
        return;
    }

    stitching_ = true;
    stitch_images_button_->setEnabled(false);

    stitching_future_ = QtConcurrent::run([this, images, gridSize]() {
        std::vector<cv::Mat> sorted = ctrl_.stitcher().sortImagesInSCurveOrder(images, gridSize);
        return ctrl_.stitcher().stitchImages(sorted, gridSize);
    });
    stitching_watcher_.setFuture(stitching_future_);
}

void MainWindow::onStitchingFinished() {
    cv::Mat result = stitching_future_.result();
    stitching_ = false;
    stitch_images_button_->setEnabled(true);
    if (!result.empty()) {
        stitched_result_ = result;
        displayImage(result, stitched_image_label_);
        status_bar_label_->setText("拼接完成");
    } else {
        QMessageBox::critical(this, "错误", "拼接失败");
    }
}

void MainWindow::on_loadImagesButton_clicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择图像目录", input_dir_edit_->text());
    if (!dir.isEmpty()) {
        input_dir_edit_->setText(dir);
    }
}

void MainWindow::on_saveStitchedImageButton_clicked() {
    if (stitched_result_.empty()) {
        QMessageBox::warning(this, "警告", "没有拼接结果");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "保存拼接结果", "./stitched.jpg",
        "JPEG (*.jpg);;PNG (*.png)");
    if (!path.isEmpty()) {
        cv::imwrite(path.toStdString(), stitched_result_);
    }
}

// ==================== Timer ====================

void MainWindow::updateCameraImage() {
    if (!ctrl_.cameraHandler().isConnected() || !ctrl_.cameraHandler().isCapturing()) return;
    cv::Mat frame;
    if (ctrl_.captureImage(frame)) {
        current_image_ = frame;
        if (image_detection_enabled_ && model_loaded_) {
            auto detections = ctrl_.detect(frame);
            displayImage(ctrl_.drawDetections(frame, detections), camera_image_label_);
        } else {
            displayImage(frame, camera_image_label_);
        }
    }
}

// ==================== Callbacks ====================

void MainWindow::onArmStatusChanged(const arm::ArmStatus& status) {
    x_pos_spin_->setValue(status.current_positions[0]);
    y_pos_spin_->setValue(status.current_positions[1]);
    z_pos_spin_->setValue(status.current_positions[2]);
    a_pos_spin_->setValue(status.current_positions[3]);
    b_pos_spin_->setValue(status.current_positions[4]);
}

void MainWindow::onCameraStatusChanged(const camera::CameraStatus& status) {
    camera_status_text_->append(QString::fromStdString(status.status_message));
}

void MainWindow::onMovementStatus(const arm::SMovementStatus& status) {
    movement_status_text_->append(QString::fromStdString(status.status_message));
    if (status.total_points > 0) {
        movement_progress_bar_->setValue((status.current_point * 100) / status.total_points);
    }
}

void MainWindow::onStitchingProgress(int current, int total) {
    stitching_progress_bar_->setValue((current * 100) / total);
}

void MainWindow::onStitchingStatus(const std::string& status) {
    stitching_status_text_->append(QString::fromStdString(status));
}

void MainWindow::onDetectionResult(const std::vector<detector::Detection>&, const cv::Mat&) {}

void MainWindow::on_enableDetectionCheckbox_stateChanged(int state) {
    image_detection_enabled_ = (state == Qt::Checked);
}

void MainWindow::on_topCellsTable_cellClicked(int row, int column) {
    auto it = s_movement_images_.find(std::make_pair(row, column));
    if (it != s_movement_images_.end()) {
        cv::Mat img = cv::imread(it->second);
        if (!img.empty()) {
            cv::namedWindow("Preview", cv::WINDOW_NORMAL);
            cv::imshow("Preview", img);
        }
    }
}

void MainWindow::on_bottomCellsTable_cellClicked(int row, int column) {
    if (!ctrl_.armController().isConnected()) return;
    int step = ConfigManager::instance().stepSize();
    ctrl_.armController().moveToPosition(0, column * step);
    ctrl_.armController().moveToPosition(1, row * step);
    ctrl_.armController().moveToPosition(2, ConfigManager::instance().zHeight());
    status_bar_label_->setText(QString("Move to (%1, %2)").arg(column * step).arg(row * step));
}
