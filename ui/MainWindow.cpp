#include "MainWindow.h"
#include "infra/config/ConfigManager.h"
#include "infra/workflow/WorkflowManager.h"

#include <cmath>
#include <QFileDialog>
#include <QFileInfo>
#include <QProgressDialog>
#include <QStatusBar>
#include <QHeaderView>
#include <QDir>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      s_movement_controller_(arm_controller_)
{
    std::cout << "MainWindow: Initializing main window..." << std::endl;

    setupUI();

    // Initialize camera update timer (for pulling images at ~30fps)
    camera_update_timer_ = new QTimer(this);
    connect(camera_update_timer_, &QTimer::timeout, this, &MainWindow::updateCameraImage);

    // Set up callbacks from service modules
    camera_handler_.setImageCallback([this](const cv::Mat& frame) {
        QMetaObject::invokeMethod(this, "onImageCaptured", Q_ARG(const cv::Mat&, frame));
    });

    camera_handler_.setStatusCallback([this](const camera::CameraStatus& status) {
        QMetaObject::invokeMethod(this, "onCameraStatusChanged", Q_ARG(const camera::CameraStatus&, status));
    });

    arm_controller_.setStatusCallback([this](const arm::ArmStatus& status) {
        QMetaObject::invokeMethod(this, "onArmStatusChanged", Q_ARG(const arm::ArmStatus&, status));
    });

    s_movement_controller_.setStatusCallback([this](const arm::SMovementStatus& status) {
        QMetaObject::invokeMethod(this, "onMovementStatus", Q_ARG(const arm::SMovementStatus&, status));
    });

    image_stitcher_.setProgressCallback([this](int current, int total) {
        QMetaObject::invokeMethod(this, "onStitchingProgress", Q_ARG(int, current), Q_ARG(int, total));
    });

    image_stitcher_.setStatusCallback([this](const std::string& status) {
        QMetaObject::invokeMethod(this, "onStitchingStatus", Q_ARG(const std::string&, status));
    });

    // Setup concurrent stitching watcher
    connect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished, this, &MainWindow::onStitchingFinished);

    // Initialize workflow manager (async, non-blocking)
    workflow_mgr_ = new WorkflowManager(
        arm_controller_, s_movement_controller_,
        camera_handler_, yolo_detector_, image_stitcher_, this);

    connect(workflow_mgr_, &WorkflowManager::statusMessage, this, [this](const QString& msg) {
        status_bar_label_->setText(msg);
    });
    connect(workflow_mgr_, &WorkflowManager::workflowError, this, [this](const QString& err) {
        status_bar_label_->setText("Error: " + err);
        QMessageBox::critical(this, "Workflow Error", err);
    });
    connect(workflow_mgr_, &WorkflowManager::stitchingFinished, this, [this](const cv::Mat& result) {
        stitched_result_ = result;
        displayImage(result, stitched_image_label_);
    });

    // Enumerate cameras on startup (non-blocking)
    std::cout << "MainWindow: Enumerating cameras..." << std::endl;
    on_enumerateCamerasButton_clicked();

    std::cout << "MainWindow: Initialization complete!" << std::endl;
}

MainWindow::~MainWindow() {
    std::cout << "MainWindow: Shutting down..." << std::endl;

    // Stop workflow and timers first
    if (workflow_mgr_) {
        workflow_mgr_->stopAutoWorkflow();
    }

    camera_update_timer_->stop();

    // Stop S-movement
    s_movement_controller_.stop();

    // Stop camera
    camera_handler_.stopCapture();
    camera_handler_.disconnect();

    // Stop Modbus auto-read and disconnect
    arm_controller_.stopAutoRead();
    arm_controller_.disconnect();

    std::cout << "MainWindow: Shutdown complete." << std::endl;
}

void MainWindow::setupUI() {
    setWindowTitle("ArmLite C++ Control System");
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

    model_layout->addWidget(new QLabel("Param文件:"), 0, 0);
    model_layout->addWidget(param_path_edit_, 0, 1);
    model_layout->addWidget(new QLabel("Bin文件:"), 1, 0);
    model_layout->addWidget(bin_path_edit_, 1, 1);
    model_layout->addWidget(load_model_button_, 2, 0, 1, 2);

    detection_settings_group_ = new QGroupBox("检测设置");
    QGridLayout *settings_layout = new QGridLayout(detection_settings_group_);

    confidence_threshold_spin_ = new QDoubleSpinBox();
    confidence_threshold_spin_->setRange(0, 1);
    confidence_threshold_spin_->setValue(0.3);
    confidence_threshold_spin_->setSingleStep(0.05);
    set_confidence_threshold_button_ = new QPushButton("设置置信度阈值");

    nms_threshold_spin_ = new QDoubleSpinBox();
    nms_threshold_spin_->setRange(0, 1);
    nms_threshold_spin_->setValue(0.3);
    nms_threshold_spin_->setSingleStep(0.05);
    set_nms_threshold_button_ = new QPushButton("设置NMS阈值");

    settings_layout->addWidget(new QLabel("置信度阈值:"), 0, 0);
    settings_layout->addWidget(confidence_threshold_spin_, 0, 1);
    settings_layout->addWidget(set_confidence_threshold_button_, 0, 2);
    settings_layout->addWidget(new QLabel("NMS阈值:"), 1, 0);
    settings_layout->addWidget(nms_threshold_spin_, 1, 1);
    settings_layout->addWidget(set_nms_threshold_button_, 1, 2);

    detection_group_ = new QGroupBox("检测控制");
    QHBoxLayout *detection_layout = new QHBoxLayout(detection_group_);

    load_image_button_ = new QPushButton("加载图像");
    detect_button_ = new QPushButton("开始检测");

    detection_layout->addWidget(load_image_button_);
    detection_layout->addWidget(detect_button_);

    detection_image_label_ = new QLabel("检测结果");
    detection_image_label_->setAlignment(Qt::AlignCenter);
    detection_image_label_->setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc;");
    detection_image_label_->setMinimumHeight(400);

    detection_status_text_ = new QTextEdit();
    detection_status_text_->setReadOnly(true);
    detection_status_text_->setMaximumHeight(100);

    main_layout->addWidget(model_group_);
    main_layout->addWidget(detection_settings_group_);
    main_layout->addWidget(detection_group_);
    main_layout->addWidget(new QLabel("检测结果:"));
    main_layout->addWidget(detection_image_label_);
    main_layout->addWidget(new QLabel("检测状态:"));
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

    top_cells_label_ = new QLabel("上方单元格: S型运动图像");
    top_cells_label_->setAlignment(Qt::AlignCenter);
    top_cells_label_->setStyleSheet("font-weight: bold;");
    left_layout_->addWidget(top_cells_label_);

    top_cells_table_ = new QTableWidget(10, 10);
    top_cells_table_->setHorizontalHeaderLabels({"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
    top_cells_table_->setVerticalHeaderLabels({"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
    top_cells_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    top_cells_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    top_cells_table_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    top_cells_table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    top_cells_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    top_cells_table_->setMinimumSize(300, 300);
    left_layout_->addWidget(top_cells_table_, 1);

    bottom_cells_label_ = new QLabel("下方单元格: 点击移动");
    bottom_cells_label_->setAlignment(Qt::AlignCenter);
    bottom_cells_label_->setStyleSheet("font-weight: bold;");
    left_layout_->addWidget(bottom_cells_label_);

    bottom_cells_table_ = new QTableWidget(10, 10);
    bottom_cells_table_->setHorizontalHeaderLabels({"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
    bottom_cells_table_->setVerticalHeaderLabels({"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
    bottom_cells_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bottom_cells_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    bottom_cells_table_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    bottom_cells_table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bottom_cells_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bottom_cells_table_->setMinimumSize(300, 300);
    left_layout_->addWidget(bottom_cells_table_, 1);

    for (int row = 0; row < 10; ++row) {
        for (int col = 0; col < 10; ++col) {
            QTableWidgetItem *top_item = new QTableWidgetItem("");
            top_item->setTextAlignment(Qt::AlignCenter);
            top_cells_table_->setItem(row, col, top_item);

            QTableWidgetItem *bottom_item = new QTableWidgetItem(QString("%1,%2").arg(row).arg(col));
            bottom_item->setTextAlignment(Qt::AlignCenter);
            bottom_cells_table_->setItem(row, col, bottom_item);
        }
    }

    // Center: Camera
    center_widget_ = new QWidget();
    center_layout_ = new QVBoxLayout(center_widget_);

    camera_connection_group_ = new QGroupBox("相机连接");
    QGridLayout *conn_layout = new QGridLayout(camera_connection_group_);

    camera_combo_ = new QComboBox();
    connect_camera_button_ = new QPushButton("连接相机");
    disconnect_camera_button_ = new QPushButton("断开相机");
    disconnect_camera_button_->setEnabled(false);
    enumerate_cameras_button_ = new QPushButton("枚举相机");

    conn_layout->addWidget(new QLabel("相机:"), 0, 0);
    conn_layout->addWidget(camera_combo_, 0, 1);
    conn_layout->addWidget(enumerate_cameras_button_, 0, 2);
    conn_layout->addWidget(connect_camera_button_, 1, 0, 1, 2);
    conn_layout->addWidget(disconnect_camera_button_, 1, 2);

    camera_control_group_ = new QGroupBox("相机控制");
    QHBoxLayout *control_layout = new QHBoxLayout(camera_control_group_);

    start_capture_button_ = new QPushButton("开始采集");
    stop_capture_button_ = new QPushButton("停止采集");
    stop_capture_button_->setEnabled(false);
    capture_image_button_ = new QPushButton("采集图像");
    capture_image_button_->setEnabled(false);
    enable_detection_checkbox_ = new QCheckBox("开启图像检测");
    enable_detection_checkbox_->setChecked(true);

    control_layout->addWidget(start_capture_button_);
    control_layout->addWidget(stop_capture_button_);
    control_layout->addWidget(capture_image_button_);
    control_layout->addWidget(enable_detection_checkbox_);

    camera_settings_group_ = new QGroupBox("相机设置");
    QGridLayout *settings_layout = new QGridLayout(camera_settings_group_);

    exposure_spin_ = new QDoubleSpinBox();
    exposure_spin_->setRange(0, 1000);
    exposure_spin_->setValue(100);
    set_exposure_button_ = new QPushButton("设置曝光");

    gain_spin_ = new QDoubleSpinBox();
    gain_spin_->setRange(0, 5);
    gain_spin_->setValue(1);
    set_gain_button_ = new QPushButton("设置增益");

    width_spin_ = new QSpinBox();
    width_spin_->setRange(1, 4096);
    width_spin_->setValue(640);
    height_spin_ = new QSpinBox();
    height_spin_->setRange(1, 4096);
    height_spin_->setValue(480);
    set_resolution_button_ = new QPushButton("设置分辨率");

    settings_layout->addWidget(new QLabel("曝光(ms):"), 0, 0);
    settings_layout->addWidget(exposure_spin_, 0, 1);
    settings_layout->addWidget(set_exposure_button_, 0, 2);
    settings_layout->addWidget(new QLabel("增益:"), 1, 0);
    settings_layout->addWidget(gain_spin_, 1, 1);
    settings_layout->addWidget(set_gain_button_, 1, 2);
    settings_layout->addWidget(new QLabel("宽度:"), 2, 0);
    settings_layout->addWidget(width_spin_, 2, 1);
    settings_layout->addWidget(new QLabel("高度:"), 2, 2);
    settings_layout->addWidget(height_spin_, 2, 3);

    save_directory_edit_ = new QLineEdit(QDir::currentPath());
    set_save_directory_button_ = new QPushButton("选择路径");
    settings_layout->addWidget(new QLabel("保存路径:"), 3, 0);
    settings_layout->addWidget(save_directory_edit_, 3, 1, 1, 2);
    settings_layout->addWidget(set_save_directory_button_, 3, 3);

    settings_layout->addWidget(set_resolution_button_, 4, 0, 1, 4);

    camera_image_label_ = new QLabel("相机图像");
    camera_image_label_->setAlignment(Qt::AlignCenter);
    camera_image_label_->setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc;");
    camera_image_label_->setMinimumHeight(400);

    camera_status_text_ = new QTextEdit();
    camera_status_text_->setReadOnly(true);
    camera_status_text_->setMaximumHeight(100);

    center_layout_->addWidget(camera_connection_group_);
    center_layout_->addWidget(camera_control_group_);
    center_layout_->addWidget(camera_settings_group_);
    center_layout_->addWidget(new QLabel("相机图像:"));
    center_layout_->addWidget(camera_image_label_);
    center_layout_->addWidget(new QLabel("相机状态:"));
    center_layout_->addWidget(camera_status_text_);

    // Right: Arm control
    right_widget_ = new QWidget();
    right_layout_ = new QVBoxLayout(right_widget_);

    arm_connection_group_ = new QGroupBox("机械臂连接");
    QGridLayout *arm_conn_layout = new QGridLayout(arm_connection_group_);

    auto& cfg = ConfigManager::instance();
    arm_ip_edit_ = new QLineEdit(QString::fromStdString(cfg.armIp()));
    arm_port_spin_ = new QSpinBox();
    arm_port_spin_->setRange(1, 65535);
    arm_port_spin_->setValue(cfg.armPort());
    connect_arm_button_ = new QPushButton("连接");
    disconnect_arm_button_ = new QPushButton("断开连接");
    disconnect_arm_button_->setEnabled(false);

    arm_conn_layout->addWidget(new QLabel("IP地址:"), 0, 0);
    arm_conn_layout->addWidget(arm_ip_edit_, 0, 1);
    arm_conn_layout->addWidget(new QLabel("端口:"), 0, 2);
    arm_conn_layout->addWidget(arm_port_spin_, 0, 3);
    arm_conn_layout->addWidget(connect_arm_button_, 1, 0, 1, 2);
    arm_conn_layout->addWidget(disconnect_arm_button_, 1, 2, 1, 2);

    arm_position_group_ = new QGroupBox("位置控制");
    QGridLayout *pos_layout = new QGridLayout(arm_position_group_);

    x_pos_spin_ = new QDoubleSpinBox();
    x_pos_spin_->setRange(0, 999999);
    x_pos_spin_->setDecimals(0);
    x_pos_spin_->setValue(0);
    y_pos_spin_ = new QDoubleSpinBox();
    y_pos_spin_->setRange(0, 999999);
    y_pos_spin_->setDecimals(0);
    y_pos_spin_->setValue(0);
    z_pos_spin_ = new QDoubleSpinBox();
    z_pos_spin_->setRange(0, 999999);
    z_pos_spin_->setDecimals(0);
    z_pos_spin_->setValue(0);
    a_pos_spin_ = new QDoubleSpinBox();
    a_pos_spin_->setRange(0, 999999);
    a_pos_spin_->setDecimals(0);
    a_pos_spin_->setValue(0);
    b_pos_spin_ = new QDoubleSpinBox();
    b_pos_spin_->setRange(0, 999999);
    b_pos_spin_->setDecimals(0);
    b_pos_spin_->setValue(0);
    move_to_position_button_ = new QPushButton("移动到位置");
    read_position_button_ = new QPushButton("读取位置");
    zero_button_ = new QPushButton("一键归零");

    pos_layout->addWidget(new QLabel("X轴:"), 0, 0);
    pos_layout->addWidget(x_pos_spin_, 0, 1);
    pos_layout->addWidget(new QLabel("Y轴:"), 0, 2);
    pos_layout->addWidget(y_pos_spin_, 0, 3);
    pos_layout->addWidget(new QLabel("Z轴:"), 1, 0);
    pos_layout->addWidget(z_pos_spin_, 1, 1);
    pos_layout->addWidget(new QLabel("A轴:"), 1, 2);
    pos_layout->addWidget(a_pos_spin_, 1, 3);
    pos_layout->addWidget(new QLabel("B轴:"), 2, 0);
    pos_layout->addWidget(b_pos_spin_, 2, 1);
    pos_layout->addWidget(move_to_position_button_, 2, 2);
    pos_layout->addWidget(read_position_button_, 2, 3);
    pos_layout->addWidget(zero_button_, 3, 0, 1, 4);

    arm_continuous_group_ = new QGroupBox("连续运动");
    QGridLayout *cont_layout = new QGridLayout(arm_continuous_group_);

    continuous_axis_combo_ = new QComboBox();
    continuous_axis_combo_->addItems({"X轴", "Y轴", "Z轴", "A轴", "B轴"});
    direction_combo_ = new QComboBox();
    direction_combo_->addItems({"正向", "反向"});
    start_continuous_button_ = new QPushButton("开始连续运动");
    stop_continuous_button_ = new QPushButton("停止连续运动");
    stop_all_button_ = new QPushButton("停止所有运动");

    cont_layout->addWidget(new QLabel("轴:"), 0, 0);
    cont_layout->addWidget(continuous_axis_combo_, 0, 1);
    cont_layout->addWidget(new QLabel("方向:"), 0, 2);
    cont_layout->addWidget(direction_combo_, 0, 3);
    cont_layout->addWidget(start_continuous_button_, 1, 0, 1, 2);
    cont_layout->addWidget(stop_continuous_button_, 1, 2);
    cont_layout->addWidget(stop_all_button_, 1, 3);

    arm_speed_group_ = new QGroupBox("速度设置");
    QGridLayout *speed_layout = new QGridLayout(arm_speed_group_);

    speed_spin_ = new QDoubleSpinBox();
    speed_spin_->setRange(0, 100000);
    speed_spin_->setValue(cfg.defaultSpeed());
    set_speed_button_ = new QPushButton("设置速度");
    read_speed_button_ = new QPushButton("读取速度");
    current_speed_label_ = new QLabel("当前速度: --");

    speed_layout->addWidget(new QLabel("速度:"), 0, 0);
    speed_layout->addWidget(speed_spin_, 0, 1);
    speed_layout->addWidget(set_speed_button_, 0, 2);
    speed_layout->addWidget(read_speed_button_, 0, 3);
    speed_layout->addWidget(current_speed_label_, 1, 0, 1, 4);

    real_time_position_checkbox_ = new QCheckBox("实时读取位置");
    speed_layout->addWidget(real_time_position_checkbox_, 2, 0, 1, 4);

    s_movement_group_ = new QGroupBox("S型运动");
    QGridLayout *s_move_layout = new QGridLayout(s_movement_group_);

    start_s_movement_button_ = new QPushButton("开始S型运动");
    stop_s_movement_button_ = new QPushButton("停止S型运动");
    pause_s_movement_button_ = new QPushButton("暂停S型运动");
    resume_s_movement_button_ = new QPushButton("恢复S型运动");

    s_move_layout->addWidget(start_s_movement_button_, 0, 0);
    s_move_layout->addWidget(stop_s_movement_button_, 0, 1);
    s_move_layout->addWidget(pause_s_movement_button_, 0, 2);
    s_move_layout->addWidget(resume_s_movement_button_, 0, 3);

    movement_progress_bar_ = new QProgressBar();
    movement_status_text_ = new QTextEdit();
    movement_status_text_->setReadOnly(true);
    movement_status_text_->setMaximumHeight(100);

    right_layout_->addWidget(arm_connection_group_);
    right_layout_->addWidget(arm_position_group_);
    right_layout_->addWidget(arm_continuous_group_);
    right_layout_->addWidget(arm_speed_group_);
    right_layout_->addWidget(s_movement_group_);
    right_layout_->addWidget(new QLabel("运动进度:"));
    right_layout_->addWidget(movement_progress_bar_);
    right_layout_->addWidget(new QLabel("运动状态:"));
    right_layout_->addWidget(movement_status_text_);

    arm_camera_main_layout_->addWidget(left_widget_, 1);
    arm_camera_main_layout_->addWidget(center_widget_, 2);
    arm_camera_main_layout_->addWidget(right_widget_, 1);

    tab_widget_->addTab(arm_camera_tab_, "机械臂与相机控制");

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
    connect(enable_detection_checkbox_, &QCheckBox::stateChanged, this, &MainWindow::on_enableDetectionCheckbox_stateChanged);

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

    // Cell click signals
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

    input_layout->addWidget(new QLabel("图像目录:"), 0, 0);
    input_layout->addWidget(input_dir_edit_, 0, 1);
    input_layout->addWidget(load_images_button_, 0, 2);
    input_layout->addWidget(new QLabel("网格X:"), 1, 0);
    input_layout->addWidget(stitch_grid_size_x_spin_, 1, 1);
    input_layout->addWidget(new QLabel("网格Y:"), 1, 2);
    input_layout->addWidget(stitch_grid_size_y_spin_, 1, 3);

    stitching_control_group_ = new QGroupBox("拼接控制");
    QHBoxLayout *control_layout = new QHBoxLayout(stitching_control_group_);

    stitch_images_button_ = new QPushButton("开始拼接");
    save_stitched_image_button_ = new QPushButton("保存拼接结果");

    control_layout->addWidget(stitch_images_button_);
    control_layout->addWidget(save_stitched_image_button_);

    stitching_progress_bar_ = new QProgressBar();
    stitching_status_text_ = new QTextEdit();
    stitching_status_text_->setReadOnly(true);
    stitching_status_text_->setMaximumHeight(100);

    stitched_image_label_ = new QLabel("拼接结果");
    stitched_image_label_->setAlignment(Qt::AlignCenter);
    stitched_image_label_->setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc;");
    stitched_image_label_->setMinimumHeight(400);

    main_layout->addWidget(stitching_input_group_);
    main_layout->addWidget(stitching_control_group_);
    main_layout->addWidget(new QLabel("拼接进度:"));
    main_layout->addWidget(stitching_progress_bar_);
    main_layout->addWidget(new QLabel("拼接状态:"));
    main_layout->addWidget(stitching_status_text_);
    main_layout->addWidget(new QLabel("拼接结果:"));
    main_layout->addWidget(stitched_image_label_);

    tab_widget_->addTab(stitching_tab, "图像拼接");

    connect(stitch_images_button_, &QPushButton::clicked, this, &MainWindow::on_stitchImagesButton_clicked);
    connect(load_images_button_, &QPushButton::clicked, this, &MainWindow::on_loadImagesButton_clicked);
    connect(save_stitched_image_button_, &QPushButton::clicked, this, &MainWindow::on_saveStitchedImageButton_clicked);
}

// Helper Functions
void MainWindow::displayImage(const cv::Mat& image, QLabel* label) {
    if (image.empty()) return;

    QImage qimage = cvMatToQImage(image);
    QPixmap pixmap = QPixmap::fromImage(qimage);
    label->setPixmap(pixmap.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QImage MainWindow::cvMatToQImage(const cv::Mat& mat) {
    if (mat.empty()) return QImage();

    if (mat.type() == CV_8UC3) {
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888).rgbSwapped();
    } else if (mat.type() == CV_8UC1) {
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
    } else {
        cv::Mat rgb;
        mat.convertTo(rgb, CV_8UC3);
        return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).rgbSwapped();
    }
}

// ==================== Arm Control Slots ====================

void MainWindow::on_connectArmButton_clicked() {
    std::string ip = arm_ip_edit_->text().toStdString();
    int port = arm_port_spin_->value();

    if (arm_controller_.connect(ip, port)) {
        connect_arm_button_->setEnabled(false);
        disconnect_arm_button_->setEnabled(true);
        status_bar_label_->setText("机械臂连接成功");
        arm_connected_ = true;

        int default_speed = ConfigManager::instance().defaultSpeed();
        for (int i = 0; i < 5; ++i) {
            arm_controller_.setSpeed(i, default_speed);
        }
    } else {
        status_bar_label_->setText("机械臂连接失败");
        QMessageBox::critical(this, "错误", "机械臂连接失败");
        arm_connected_ = false;
    }
}

void MainWindow::on_disconnectArmButton_clicked() {
    arm_controller_.disconnect();
    connect_arm_button_->setEnabled(true);
    disconnect_arm_button_->setEnabled(false);
    status_bar_label_->setText("机械臂已断开");
    arm_connected_ = false;
}

void MainWindow::on_moveToPositionButton_clicked() {
    arm_controller_.moveToPosition(0, static_cast<int>(x_pos_spin_->value()));
    arm_controller_.moveToPosition(1, static_cast<int>(y_pos_spin_->value()));
    arm_controller_.moveToPosition(2, static_cast<int>(z_pos_spin_->value()));
    arm_controller_.moveToPosition(3, static_cast<int>(a_pos_spin_->value()));
    arm_controller_.moveToPosition(4, static_cast<int>(b_pos_spin_->value()));

    status_bar_label_->setText("开始移动到指定位置");
}

void MainWindow::on_startContinuousButton_clicked() {
    int axis_id = continuous_axis_combo_->currentIndex();
    bool direction = (direction_combo_->currentIndex() == 0);
    arm_controller_.startContinuousMovement(axis_id, direction);
    status_bar_label_->setText("开始连续运动");
}

void MainWindow::on_stopContinuousButton_clicked() {
    int axis_id = continuous_axis_combo_->currentIndex();
    arm_controller_.stopContinuousMovement(axis_id);
    status_bar_label_->setText("停止连续运动");
}

void MainWindow::on_stopAllButton_clicked() {
    for (int i = 0; i < 5; ++i) {
        arm_controller_.stopAllMovements(i);
    }
    status_bar_label_->setText("停止所有运动");
}

void MainWindow::on_readPositionButton_clicked() {
    for (int i = 0; i < 5; ++i) {
        double position = arm_controller_.readPosition(i);
        switch (i) {
            case 0: x_pos_spin_->setValue(position); break;
            case 1: y_pos_spin_->setValue(position); break;
            case 2: z_pos_spin_->setValue(position); break;
            case 3: a_pos_spin_->setValue(position); break;
            case 4: b_pos_spin_->setValue(position); break;
        }
    }
    status_bar_label_->setText("位置读取完成");
}

void MainWindow::on_setSpeedButton_clicked() {
    int speed = static_cast<int>(speed_spin_->value());
    for (int i = 0; i < 5; ++i) {
        arm_controller_.setSpeed(i, speed);
    }
    status_bar_label_->setText("速度设置完成");
}

void MainWindow::on_readSpeedButton_clicked() {
    if (!arm_controller_.isConnected()) {
        QMessageBox::warning(this, "警告", "机械臂未连接");
        return;
    }
    int speed = arm_controller_.readSpeed(0);
    current_speed_label_->setText(QString("当前速度: %1").arg(speed));
    status_bar_label_->setText("速度读取完成");
}

void MainWindow::on_zeroButton_clicked() {
    if (!arm_controller_.isConnected()) {
        QMessageBox::warning(this, "警告", "机械臂未连接");
        return;
    }

    arm_controller_.moveToPosition(0, 0);
    arm_controller_.moveToPosition(1, 0);
    arm_controller_.moveToPosition(2, 0);
    arm_controller_.moveToPosition(3, 0);
    arm_controller_.moveToPosition(4, 0);

    x_pos_spin_->setValue(0);
    y_pos_spin_->setValue(0);
    z_pos_spin_->setValue(0);
    a_pos_spin_->setValue(0);
    b_pos_spin_->setValue(0);

    status_bar_label_->setText("所有轴已归零");
}

void MainWindow::on_realTimePositionCheckbox_stateChanged(int state) {
    if (state == Qt::Checked) {
        arm_controller_.startAutoRead(0.3f);
        status_bar_label_->setText("已开启实时位置读取");
    } else {
        arm_controller_.stopAutoRead();
        status_bar_label_->setText("已关闭实时位置读取");
    }
}

void MainWindow::on_startSMovementButton_clicked() {
    if (!arm_controller_.isConnected()) {
        QMessageBox::warning(this, "警告", "机械臂未连接");
        return;
    }

    if (!camera_handler_.isConnected()) {
        QMessageBox::warning(this, "警告", "相机未连接");
        return;
    }

    if (!camera_handler_.startCapture()) {
        QMessageBox::warning(this, "警告", "相机采集启动失败！");
        return;
    }

    auto& cfg = ConfigManager::instance();
    int grid_x = cfg.gridSizeX();
    int grid_y = cfg.gridSizeY();
    int step = cfg.stepSize();
    int z = cfg.zHeight();
    int end_x = step * (grid_x - 1);
    int end_y = step * (grid_y - 1);

    std::string base_path = cfg.imageSaveBasePath();
    int run_number = 0;
    QDir base_dir(QString::fromStdString(base_path));
    if (base_dir.exists()) {
        QStringList dirs = base_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& dir : dirs) {
            bool ok;
            int num = dir.toInt(&ok);
            if (ok && num > run_number) run_number = num;
        }
    }
    run_number++;
    std::string save_path = base_path + "/" + std::to_string(run_number);

    QDir dir(QString::fromStdString(save_path));
    if (!dir.exists() && !dir.mkpath(QString::fromStdString(save_path))) {
        QMessageBox::warning(this, "警告", "创建保存目录失败！");
        return;
    }

    s_movement_controller_.setSaveDirectory(save_path);
    s_movement_controller_.setRunNumber(run_number);
    s_movement_controller_.setMovementSpeed(cfg.defaultSpeed());

    arm::SMovementPoint start_pos = {0, 0, z, 0, 0, 0, 0};
    arm::SMovementPoint end_pos = {end_x, end_y, z, 0, 0, grid_y - 1, grid_x - 1};

    if (!s_movement_controller_.initialize(cv::Size(grid_x, grid_y), start_pos, end_pos)) {
        QMessageBox::critical(this, "错误", "S型运动初始化失败");
        return;
    }

    // Set image capture callback
    s_movement_controller_.setImageCaptureCallback([this](cv::Mat& frame) {
        return camera_handler_.captureSingleFrame(frame);
    });

    // Set image save callback
    s_movement_controller_.setImageSaveCallback([this, save_path](const cv::Mat& frame, const std::string& path, int row, int col) {
        std::string filename = path + "/" + std::to_string(row) + "_" + std::to_string(col) + ".jpg";

        bool saved = cv::imwrite(filename, frame);
        if (!saved) return false;

        QFile file(QString::fromStdString(filename));
        if (!file.exists()) return false;

        s_movement_images_[std::make_pair(row, col)] = filename;

        QMetaObject::invokeMethod(this, [this, frame, row, col]() {
            if (row < 10 && col < 10) {
                QTableWidgetItem *item = top_cells_table_->item(row, col);
                if (item) {
                    item->setText(QString("%1,%2").arg(row).arg(col));
                    item->setBackground(QColor(144, 238, 144));

                    QLabel *imageLabel = new QLabel();
                    QImage qimage = cvMatToQImage(frame);
                    QPixmap pixmap = QPixmap::fromImage(qimage);
                    imageLabel->setPixmap(pixmap.scaled(QSize(50, 50), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    imageLabel->setAlignment(Qt::AlignCenter);
                    top_cells_table_->setCellWidget(row, col, imageLabel);
                }
            }
        }, Qt::QueuedConnection);

        return true;
    });

    if (s_movement_controller_.start()) {
        start_s_movement_button_->setEnabled(false);
        stop_s_movement_button_->setEnabled(true);
        pause_s_movement_button_->setEnabled(true);
        resume_s_movement_button_->setEnabled(false);
        status_bar_label_->setText("S型运动开始");
    } else {
        QMessageBox::critical(this, "错误", "S型运动启动失败");
    }
}

void MainWindow::on_stopSMovementButton_clicked() {
    s_movement_controller_.stop();
    start_s_movement_button_->setEnabled(true);
    stop_s_movement_button_->setEnabled(false);
    pause_s_movement_button_->setEnabled(false);
    resume_s_movement_button_->setEnabled(false);
    status_bar_label_->setText("S型运动已停止");
}

void MainWindow::on_pauseSMovementButton_clicked() {
    s_movement_controller_.pause();
    pause_s_movement_button_->setEnabled(false);
    resume_s_movement_button_->setEnabled(true);
    status_bar_label_->setText("S型运动已暂停");
}

void MainWindow::on_resumeSMovementButton_clicked() {
    s_movement_controller_.resume();
    pause_s_movement_button_->setEnabled(true);
    resume_s_movement_button_->setEnabled(false);
    status_bar_label_->setText("S型运动已恢复");
}

// ==================== Camera Slots ====================

void MainWindow::on_connectCameraButton_clicked() {
    std::string device_id = camera_combo_->currentData().toString().toStdString();

    if (camera_handler_.connect(device_id)) {
        connect_camera_button_->setEnabled(false);
        disconnect_camera_button_->setEnabled(true);
        start_capture_button_->setEnabled(true);
        stop_capture_button_->setEnabled(false);
        capture_image_button_->setEnabled(true);
        status_bar_label_->setText("相机连接成功");
    } else {
        status_bar_label_->setText("相机连接失败");
        QMessageBox::critical(this, "错误", "相机连接失败");
    }
}

void MainWindow::on_disconnectCameraButton_clicked() {
    camera_handler_.disconnect();
    connect_camera_button_->setEnabled(true);
    disconnect_camera_button_->setEnabled(false);
    start_capture_button_->setEnabled(true);
    stop_capture_button_->setEnabled(false);
    capture_image_button_->setEnabled(false);
    status_bar_label_->setText("相机已断开");
}

void MainWindow::on_startCaptureButton_clicked() {
    if (camera_handler_.startCapture()) {
        start_capture_button_->setEnabled(false);
        stop_capture_button_->setEnabled(true);
        camera_update_timer_->start(33);
        status_bar_label_->setText("相机开始采集");
    } else {
        status_bar_label_->setText("相机采集启动失败");
        QMessageBox::critical(this, "错误", "相机采集启动失败");
    }
}

void MainWindow::on_stopCaptureButton_clicked() {
    camera_handler_.stopCapture();
    start_capture_button_->setEnabled(true);
    stop_capture_button_->setEnabled(false);
    camera_update_timer_->stop();
    status_bar_label_->setText("相机采集已停止");
}

void MainWindow::on_captureImageButton_clicked() {
    if (!camera_handler_.isConnected()) {
        QMessageBox::warning(this, "警告", "相机未连接！");
        return;
    }

    if (!camera_handler_.startCapture()) {
        QMessageBox::warning(this, "警告", "相机采集启动失败！");
        return;
    }

    static int imageCounter = 0;
    imageCounter++;
    QString filename = QString("image_%1.jpg").arg(imageCounter, 4, 10, QChar('0'));
    QString saveDirectory = save_directory_edit_->text();
    QString filepath = saveDirectory + "/" + filename;

    QDir dir(saveDirectory);
    if (!dir.exists() && !dir.mkpath(saveDirectory)) {
        QMessageBox::warning(this, "警告", "创建保存目录失败！");
        camera_handler_.stopCapture();
        return;
    }

    cv::Mat frame;
    bool captured = false;
    int retry_count = 0;
    const int max_retries = 10;

    while (retry_count < max_retries && !captured) {
        if (camera_handler_.captureSingleFrame(frame)) {
            captured = true;
            current_image_ = frame;
            displayImage(frame, camera_image_label_);

            if (cv::imwrite(filepath.toStdString(), frame)) {
                status_bar_label_->setText(QString("图像已保存: %1").arg(filepath));
            } else {
                QMessageBox::warning(this, "警告", "保存图像失败！");
            }
        } else {
            retry_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    camera_handler_.stopCapture();

    if (!captured) {
        status_bar_label_->setText("采集图像失败");
        QMessageBox::critical(this, "错误", "采集图像失败");
    }
}

void MainWindow::on_enumerateCamerasButton_clicked() {
    camera_combo_->clear();

    std::vector<camera::CameraDevice> cameras = camera_handler_.enumerateCameras();

    for (const auto& camera : cameras) {
        camera_combo_->addItem(camera.display_name.c_str(), camera.id.c_str());
    }

    status_bar_label_->setText("相机枚举完成");
}

void MainWindow::on_setExposureButton_clicked() {
    float exposure = exposure_spin_->value();
    if (camera_handler_.setExposure(exposure)) {
        status_bar_label_->setText("曝光设置完成");
    } else {
        status_bar_label_->setText("曝光设置失败");
        QMessageBox::critical(this, "错误", "曝光设置失败");
    }
}

void MainWindow::on_setGainButton_clicked() {
    float gain = gain_spin_->value();
    if (camera_handler_.setGain(gain)) {
        status_bar_label_->setText("增益设置完成");
    } else {
        status_bar_label_->setText("增益设置失败");
        QMessageBox::critical(this, "错误", "增益设置失败");
    }
}

void MainWindow::on_setResolutionButton_clicked() {
    int width = width_spin_->value();
    int height = height_spin_->value();
    if (camera_handler_.setResolution(width, height)) {
        status_bar_label_->setText("分辨率设置完成");
    } else {
        status_bar_label_->setText("分辨率设置失败");
        QMessageBox::critical(this, "错误", "分辨率设置失败");
    }
}

void MainWindow::on_setSaveDirectoryButton_clicked() {
    QString directory = QFileDialog::getExistingDirectory(
        this, "选择保存目录", save_directory_edit_->text());
    if (!directory.isEmpty()) {
        save_directory_edit_->setText(directory);
        status_bar_label_->setText(QString("保存目录: %1").arg(directory));
    }
}

// ==================== Detection Slots ====================

void MainWindow::on_loadModelButton_clicked() {
    std::string param_path = param_path_edit_->text().toStdString();
    std::string bin_path = bin_path_edit_->text().toStdString();

    if (yolo_detector_.loadModel(param_path, bin_path)) {
        model_loaded_ = true;
        status_bar_label_->setText("模型加载成功");
    } else {
        model_loaded_ = false;
        status_bar_label_->setText("模型加载失败");
        QMessageBox::critical(this, "错误", "模型加载失败");
    }
}

void MainWindow::on_loadImageButton_clicked() {
    QString file_path = QFileDialog::getOpenFileName(this, "选择图像文件", "./",
                                                    "图像文件 (*.jpg *.jpeg *.png *.bmp);;所有文件 (*.*)");
    if (file_path.isEmpty()) return;

    cv::Mat image = cv::imread(file_path.toStdString());
    if (image.empty()) {
        QMessageBox::warning(this, "警告", "图像加载失败");
        return;
    }

    current_image_ = image;
    displayImage(image, detection_image_label_);
    detection_result_ = cv::Mat();
    status_bar_label_->setText("图像加载成功");

    detection_status_text_->append("图像加载成功: " + file_path);
    detection_status_text_->append("图像大小: " + QString::number(image.cols) + "x" + QString::number(image.rows));
}

void MainWindow::on_detectButton_clicked() {
    if (!model_loaded_) {
        QMessageBox::warning(this, "警告", "模型未加载");
        return;
    }

    if (current_image_.empty()) {
        QMessageBox::warning(this, "警告", "没有图像可以检测");
        return;
    }

    std::vector<detector::Detection> detections = yolo_detector_.detect(current_image_);
    cv::Mat result = yolo_detector_.drawDetections(current_image_, detections);

    detection_result_ = result;
    displayImage(result, detection_image_label_);

    status_bar_label_->setText("检测完成，共检测到 " + QString::number(detections.size()) + " 个目标");

    detection_status_text_->append("检测完成，共检测到 " + QString::number(detections.size()) + " 个目标");
    for (const auto& detection : detections) {
        detection_status_text_->append(QString::fromStdString(detection.class_name) + "，置信度: " +
                                      QString::number(detection.confidence, 'f', 2) +
                                      "，位置: (" + QString::number(detection.bounding_box.x) + ", " +
                                      QString::number(detection.bounding_box.y) + ")，大小: " +
                                      QString::number(detection.bounding_box.width) + "x" +
                                      QString::number(detection.bounding_box.height));
    }
}

void MainWindow::on_setConfidenceThresholdButton_clicked() {
    yolo_detector_.setConfidenceThreshold(confidence_threshold_spin_->value());
    status_bar_label_->setText("置信度阈值设置完成");
}

void MainWindow::on_setNmsThresholdButton_clicked() {
    yolo_detector_.setNmsThreshold(nms_threshold_spin_->value());
    status_bar_label_->setText("NMS阈值设置完成");
}

// ==================== Stitching Slots ====================

void MainWindow::on_stitchImagesButton_clicked() {
    std::string input_dir = input_dir_edit_->text().toStdString();
    int grid_size_x = stitch_grid_size_x_spin_->value();
    int grid_size_y = stitch_grid_size_y_spin_->value();
    cv::Size grid_size(grid_size_x, grid_size_y);

    std::vector<cv::Mat> images;
    bool directory_exists = std::filesystem::exists(input_dir);

    if (!directory_exists) {
        QString dir_path = QFileDialog::getExistingDirectory(this, "选择图像目录", "./",
                                                             QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (dir_path.isEmpty()) {
            status_bar_label_->setText("未选择图像目录");
            return;
        }
        input_dir = dir_path.toStdString();
        input_dir_edit_->setText(dir_path);
        images = image_stitcher_.loadImagesFromDirectory(input_dir);
    } else {
        images = image_stitcher_.loadImagesFromDirectory(input_dir);
    }

    if (images.empty()) {
        QMessageBox::warning(this, "警告", "没有图像可以拼接");
        return;
    }

    // Sort once in S-curve order, then pass to stitchImages (which now uses simple row-major placement)
    // This fixes the double-sort bug
    std::vector<cv::Mat> sorted_images = image_stitcher_.sortImagesInSCurveOrder(images, grid_size);

    stitching_ = true;
    status_bar_label_->setText("图像拼接中...");
    stitch_images_button_->setEnabled(false);

    stitching_future_ = QtConcurrent::run([this, sorted_images, grid_size]() {
        return image_stitcher_.stitchImages(sorted_images, grid_size);
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
        status_bar_label_->setText("图像拼接完成");
    } else {
        status_bar_label_->setText("图像拼接失败");
        QMessageBox::critical(this, "错误", "图像拼接失败");
    }
}

void MainWindow::on_loadImagesButton_clicked() {
    status_bar_label_->setText("图像加载功能待实现");
}

void MainWindow::on_saveStitchedImageButton_clicked() {
    if (stitched_result_.empty()) {
        QMessageBox::warning(this, "警告", "没有拼接结果可以保存");
        return;
    }

    QString filename = QFileDialog::getSaveFileName(this, "保存拼接结果", "./stitched_result.jpg",
                                                     "JPEG Files (*.jpg);;PNG Files (*.png)");
    if (!filename.isEmpty()) {
        cv::imwrite(filename.toStdString(), stitched_result_);
        status_bar_label_->setText("拼接结果保存成功");
    }
}

// ==================== Timer Slots ====================

void MainWindow::updateArmStatus() {
}

void MainWindow::updateCameraStatus() {
}

void MainWindow::updateCameraImage() {
    if (!camera_handler_.isConnected() || !camera_handler_.isCapturing()) {
        return;
    }

    try {
        cv::Mat frame;
        if (camera_handler_.captureSingleFrame(frame)) {
            current_image_ = frame;

            if (image_detection_enabled_ && model_loaded_) {
                std::vector<detector::Detection> detections = yolo_detector_.detect(frame);
                cv::Mat result = yolo_detector_.drawDetections(frame, detections);
                displayImage(result, camera_image_label_);
            } else {
                displayImage(frame, camera_image_label_);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error updating camera image: " << e.what() << std::endl;
    }
}

// ==================== Callback Slots ====================

void MainWindow::onImageCaptured(const cv::Mat& frame) {
}

void MainWindow::onArmStatusChanged(const arm::ArmStatus& status) {
    std::string status_msg = "机械臂状态: " + status.status_message;
    status_bar_label_->setText(QString::fromStdString(status_msg));

    x_pos_spin_->setValue(status.current_positions[0]);
    y_pos_spin_->setValue(status.current_positions[1]);
    z_pos_spin_->setValue(status.current_positions[2]);
    a_pos_spin_->setValue(status.current_positions[3]);
    b_pos_spin_->setValue(status.current_positions[4]);
}

void MainWindow::onCameraStatusChanged(const camera::CameraStatus& status) {
    std::string status_msg = "相机状态: " + status.status_message;
    status_bar_label_->setText(QString::fromStdString(status_msg));

    camera_status_text_->append(QString::fromStdString(status_msg));
    camera_status_text_->append("分辨率: " + QString::number(status.width) + "x" + QString::number(status.height));
    camera_status_text_->append("帧率: " + QString::number(status.fps, 'f', 2) + " FPS");
}

void MainWindow::onMovementProgress(int current, int total) {
    movement_progress_bar_->setValue((current * 100) / total);
}

void MainWindow::onMovementStatus(const arm::SMovementStatus& status) {
    movement_status_text_->append(QString::fromStdString(status.status_message));

    if (status.total_points > 0) {
        int progress = (status.current_point * 100) / status.total_points;
        movement_progress_bar_->setValue(progress);
    }
}

void MainWindow::onStitchingProgress(int current, int total) {
    stitching_progress_bar_->setValue((current * 100) / total);
}

void MainWindow::onStitchingStatus(const std::string& status) {
    stitching_status_text_->append(QString::fromStdString(status));
}

void MainWindow::onDetectionResult(const std::vector<detector::Detection>& detections, const cv::Mat& image) {
}

void MainWindow::on_enableDetectionCheckbox_stateChanged(int state) {
    image_detection_enabled_ = (state == Qt::Checked);

    if (image_detection_enabled_) {
        status_bar_label_->setText("图像检测已开启");
    } else {
        status_bar_label_->setText("图像检测已关闭");
    }
}

void MainWindow::on_topCellsTable_cellClicked(int row, int column) {
    auto key = std::make_pair(row, column);
    auto it = s_movement_images_.find(key);

    if (it != s_movement_images_.end()) {
        std::string image_path = it->second;

        cv::Mat image = cv::imread(image_path);
        if (!image.empty()) {
            cv::namedWindow("Image Preview", cv::WINDOW_NORMAL);
            cv::imshow("Image Preview", image);
            cv::waitKey(0);
            cv::destroyWindow("Image Preview");
        } else {
            status_bar_label_->setText("无法加载图片");
        }
    } else {
        status_bar_label_->setText("该位置没有图片");
    }
}

void MainWindow::on_bottomCellsTable_cellClicked(int row, int column) {
    if (!arm_connected_) {
        status_bar_label_->setText("机械臂未连接");
        return;
    }

    double target_x = column * ConfigManager::instance().stepSize();
    double target_y = row * ConfigManager::instance().stepSize();
    double target_z = ConfigManager::instance().zHeight();

    arm_controller_.moveToPosition(0, static_cast<int>(target_x));
    arm_controller_.moveToPosition(1, static_cast<int>(target_y));
    arm_controller_.moveToPosition(2, static_cast<int>(target_z));

    status_bar_label_->setText(QString("移动到位置: (%1, %2, %3)").arg(target_x).arg(target_y).arg(target_z));
}
