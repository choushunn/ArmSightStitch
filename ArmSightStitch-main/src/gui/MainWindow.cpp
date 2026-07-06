#include "MainWindow.h"

#include <cmath>
#include <QFileDialog>
#include <QFileInfo>
#include <QProgressDialog>
#include <QThread>
#include <QStatusBar>
#include <QHeaderView>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      s_movement_controller_(arm_controller_)
{
    // Setup logging
    std::cout << "MainWindow: Initializing main window..." << std::endl;
    
    // Setup UI
    std::cout << "MainWindow: Setting up UI..." << std::endl;
    setupUI();
    
    // Initialize timers
    std::cout << "MainWindow: Initializing timers..." << std::endl;
    arm_status_timer_ = new QTimer(this);
    connect(arm_status_timer_, &QTimer::timeout, this, &MainWindow::updateArmStatus);
    arm_status_timer_->start(500); // Update every 500ms
    
    camera_status_timer_ = new QTimer(this);
    connect(camera_status_timer_, &QTimer::timeout, this, &MainWindow::updateCameraStatus);
    camera_status_timer_->start(500); // Update every 500ms
    
    // Initialize camera update timer (for pulling images)
    camera_update_timer_ = new QTimer(this);
    connect(camera_update_timer_, &QTimer::timeout, this, &MainWindow::updateCameraImage);
    
    // Set up callbacks
    std::cout << "MainWindow: Setting up callbacks..." << std::endl;
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
    
    // Setup concurrent stitching
    connect(&stitching_watcher_, &QFutureWatcher<cv::Mat>::finished, this, &MainWindow::onStitchingFinished);
    
    // Enumerate cameras on startup
    std::cout << "MainWindow: Enumerating cameras..." << std::endl;
    on_enumerateCamerasButton_clicked();
    
    // 自动连接相机
    std::cout << "MainWindow: Auto-connecting camera..." << std::endl;
    if (camera_combo_->count() > 0) {
        camera_combo_->setCurrentIndex(0);
        on_connectCameraButton_clicked();
        
        // 自动启动相机捕获
        std::cout << "MainWindow: Auto-starting camera capture..." << std::endl;
        on_startCaptureButton_clicked();
    }
    
    // 自动连接机械臂
    std::cout << "MainWindow: Auto-connecting arm..." << std::endl;
    on_connectArmButton_clicked();
    
    // 默认勾选实时坐标
    std::cout << "MainWindow: Auto-enabling real-time position reading..." << std::endl;
    real_time_position_checkbox_->setChecked(true);
    on_realTimePositionCheckbox_stateChanged(Qt::Checked);
    
    std::cout << "MainWindow: Waiting for camera and arm to be ready..." << std::endl;
    QThread::sleep(2);
    
    // 自动归零和自动S型运动已禁用，用户可手动操作
    // std::cout << "MainWindow: Auto-zeroing all axes..." << std::endl;
    // on_zeroButton_clicked();
    // std::cout << "MainWindow: Waiting for zeroing to complete..." << std::endl;
    // QThread::sleep(3);
    // std::cout << "MainWindow: Auto-starting S-movement..." << std::endl;
    // on_startSMovementButton_clicked();
    
    std::cout << "MainWindow: Initialization complete!" << std::endl;
}

MainWindow::~MainWindow() {
    // Clean up resources
}

void MainWindow::setupUI() {
    // Set window properties
    setWindowTitle("ArmLite C++ Control System");
    resize(1500, 900);
    
    // Create tab widget
    tab_widget_ = new QTabWidget(this);
    setCentralWidget(tab_widget_);
    
    // Create Arm-Camera Combined Tab
    setupArmCameraTab();
    
    // Setup other tabs
    setupDetectionTab();
    setupStitchingTab();
    
    // Create status bar
    status_bar_label_ = new QLabel("Ready");
    statusBar()->addWidget(status_bar_label_);
}



void MainWindow::setupDetectionTab() {
    QWidget *detection_tab = new QWidget(this);
    QVBoxLayout *main_layout = new QVBoxLayout(detection_tab);
    
    // Model Group
    model_group_ = new QGroupBox("模型加载");
    QGridLayout *model_layout = new QGridLayout(model_group_);
    
    param_path_edit_ = new QLineEdit("c:/Users/SP/Desktop/ArmSightStitch-main/models/best-sim-opt.ncnn.param");
    bin_path_edit_ = new QLineEdit("c:/Users/SP/Desktop/ArmSightStitch-main/models/best-sim-opt.ncnn.bin");
    load_model_button_ = new QPushButton("加载模型");
    
    model_layout->addWidget(new QLabel("Param文件:"), 0, 0);
    model_layout->addWidget(param_path_edit_, 0, 1);
    model_layout->addWidget(new QLabel("Bin文件:"), 1, 0);
    model_layout->addWidget(bin_path_edit_, 1, 1);
    model_layout->addWidget(load_model_button_, 2, 0, 1, 2);
    
    // Detection Settings Group
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
    
    // Detection Group
    detection_group_ = new QGroupBox("检测控制");
    QHBoxLayout *detection_layout = new QHBoxLayout(detection_group_);
    
    load_image_button_ = new QPushButton("加载图像");
    detect_button_ = new QPushButton("开始检测");
    
    detection_layout->addWidget(load_image_button_);
    detection_layout->addWidget(detect_button_);
    
    // Image Display
    detection_image_label_ = new QLabel("检测结果");
    detection_image_label_->setAlignment(Qt::AlignCenter);
    detection_image_label_->setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc;");
    detection_image_label_->setMinimumHeight(400);
    
    // Status Text
    detection_status_text_ = new QTextEdit();
    detection_status_text_->setReadOnly(true);
    detection_status_text_->setMaximumHeight(100);
    
    // Add all groups to main layout
    main_layout->addWidget(model_group_);
    main_layout->addWidget(detection_settings_group_);
    main_layout->addWidget(detection_group_);
    main_layout->addWidget(new QLabel("检测结果:"));
    main_layout->addWidget(detection_image_label_);
    main_layout->addWidget(new QLabel("检测状态:"));
    main_layout->addWidget(detection_status_text_);
    
    // Add tab to tab widget
    tab_widget_->addTab(detection_tab, "目标检测");
    
    // Connect signals and slots
    connect(load_model_button_, &QPushButton::clicked, this, &MainWindow::on_loadModelButton_clicked);
    connect(load_image_button_, &QPushButton::clicked, this, &MainWindow::on_loadImageButton_clicked);
    connect(detect_button_, &QPushButton::clicked, this, &MainWindow::on_detectButton_clicked);
    connect(set_confidence_threshold_button_, &QPushButton::clicked, this, &MainWindow::on_setConfidenceThresholdButton_clicked);
    connect(set_nms_threshold_button_, &QPushButton::clicked, this, &MainWindow::on_setNmsThresholdButton_clicked);
}

void MainWindow::setupArmCameraTab() {
    // Create main tab widget
    arm_camera_tab_ = new QWidget(this);
    arm_camera_main_layout_ = new QHBoxLayout(arm_camera_tab_);
    
    // Create left widget for 10x10 cells
    left_widget_ = new QWidget();
    left_layout_ = new QVBoxLayout(left_widget_);
    
    // Top cells: display images during S-movement
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
    
    // Bottom cells: click-to-move functionality
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
    
    // Initialize cells
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
    
    // Create center widget for camera control
    center_widget_ = new QWidget();
    center_layout_ = new QVBoxLayout(center_widget_);
    
    // Camera Connection Group
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
    
    // Camera Control Group
    camera_control_group_ = new QGroupBox("相机控制");
    QHBoxLayout *control_layout = new QHBoxLayout(camera_control_group_);
    
    start_capture_button_ = new QPushButton("开始采集");
    stop_capture_button_ = new QPushButton("停止采集");
    stop_capture_button_->setEnabled(false);
    capture_image_button_ = new QPushButton("采集图像");
    capture_image_button_->setEnabled(false);
    enable_detection_checkbox_ = new QCheckBox("开启图像检测");
    enable_detection_checkbox_->setChecked(true); // 默认开启
    
    control_layout->addWidget(start_capture_button_);
    control_layout->addWidget(stop_capture_button_);
    control_layout->addWidget(capture_image_button_);
    control_layout->addWidget(enable_detection_checkbox_);
    
    // Camera Settings Group
    camera_settings_group_ = new QGroupBox("相机设置");
    QGridLayout *settings_layout = new QGridLayout(camera_settings_group_);
    
    auto_exposure_checkbox_ = new QCheckBox("自动曝光");
    auto_exposure_checkbox_->setChecked(false);
    
    exposure_spin_ = new QDoubleSpinBox();
    exposure_spin_->setRange(0, 1000);
    exposure_spin_->setValue(100);
    exposure_spin_->setEnabled(true); // 自动曝光关闭时启用
    set_exposure_button_ = new QPushButton("设置曝光");
    set_exposure_button_->setEnabled(true); // 自动曝光关闭时启用
    
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
    
    settings_layout->addWidget(auto_exposure_checkbox_, 0, 0, 1, 2);
    settings_layout->addWidget(new QLabel("曝光(ms):"), 1, 0);
    settings_layout->addWidget(exposure_spin_, 1, 1);
    settings_layout->addWidget(set_exposure_button_, 1, 2);
    settings_layout->addWidget(new QLabel("增益:"), 2, 0);
    settings_layout->addWidget(gain_spin_, 2, 1);
    settings_layout->addWidget(set_gain_button_, 2, 2);
    settings_layout->addWidget(new QLabel("宽度:"), 3, 0);
    settings_layout->addWidget(width_spin_, 3, 1);
    settings_layout->addWidget(new QLabel("高度:"), 3, 2);
    settings_layout->addWidget(height_spin_, 3, 3);
    
    save_directory_edit_ = new QLineEdit("C:/Users/SP/Desktop/ArmSightStitch-main/image");
    set_save_directory_button_ = new QPushButton("选择路径");
    settings_layout->addWidget(new QLabel("保存路径:"), 4, 0);
    settings_layout->addWidget(save_directory_edit_, 4, 1, 1, 2);
    settings_layout->addWidget(set_save_directory_button_, 4, 3);
    
    settings_layout->addWidget(set_resolution_button_, 5, 0, 1, 4);
    
    // Image Rotation Group
    image_rotation_group_ = new QGroupBox("图像旋转");
    QHBoxLayout *rotation_layout = new QHBoxLayout(image_rotation_group_);
    
    rotation_combo_ = new QComboBox();
    rotation_combo_->addItems({"0°", "90° 顺时针", "180°", "90° 逆时针"});
    rotation_combo_->setCurrentIndex(3); // 默认选择"90° 逆时针"
    
    rotation_layout->addWidget(new QLabel("旋转角度:"));
    rotation_layout->addWidget(rotation_combo_);
    
    // Crop Region Group
    crop_region_group_ = new QGroupBox("图像截取");
    QGridLayout *crop_layout = new QGridLayout(crop_region_group_);
    
    crop_width_spin_ = new QSpinBox();
    crop_width_spin_->setRange(0, 4096);
    crop_width_spin_->setValue(0); // 默认0表示使用原始宽度
    
    crop_height_spin_ = new QSpinBox();
    crop_height_spin_->setRange(0, 4096);
    crop_height_spin_->setValue(0); // 默认0表示使用原始高度
    
    set_crop_button_ = new QPushButton("设置截取");
    
    crop_layout->addWidget(new QLabel("X(宽度):"), 0, 0);
    crop_layout->addWidget(crop_width_spin_, 0, 1);
    crop_layout->addWidget(new QLabel("Y(高度):"), 0, 2);
    crop_layout->addWidget(crop_height_spin_, 0, 3);
    crop_layout->addWidget(set_crop_button_, 0, 4);
    crop_layout->addWidget(new QLabel("（0=原始像素，中心对齐）"), 1, 0, 1, 5);
    
    // Camera Image Display
    camera_image_label_ = new QLabel("相机图像");
    camera_image_label_->setAlignment(Qt::AlignCenter);
    camera_image_label_->setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc;");
    camera_image_label_->setMinimumHeight(400);
    
    // Camera Status
    camera_status_text_ = new QTextEdit();
    camera_status_text_->setReadOnly(true);
    camera_status_text_->setMaximumHeight(100);
    
    // Add camera components to center layout
    center_layout_->addWidget(camera_connection_group_);
    center_layout_->addWidget(camera_control_group_);
    center_layout_->addWidget(camera_settings_group_);
    center_layout_->addWidget(image_rotation_group_);
    center_layout_->addWidget(crop_region_group_);
    center_layout_->addWidget(new QLabel("相机图像:"));
    center_layout_->addWidget(camera_image_label_);
    center_layout_->addWidget(new QLabel("相机状态:"));
    center_layout_->addWidget(camera_status_text_);
    
    // Create right widget for arm control
    right_widget_ = new QWidget();
    right_layout_ = new QVBoxLayout(right_widget_);
    
    // Arm Connection Group
    arm_connection_group_ = new QGroupBox("机械臂连接");
    QGridLayout *arm_conn_layout = new QGridLayout(arm_connection_group_);
    
    arm_ip_edit_ = new QLineEdit("192.168.0.1"); // 默认IP修改为192.168.0.1
    arm_port_spin_ = new QSpinBox();
    arm_port_spin_->setRange(1, 65535);
    arm_port_spin_->setValue(502); // 默认端口修改为502
    connect_arm_button_ = new QPushButton("连接");
    disconnect_arm_button_ = new QPushButton("断开连接");
    disconnect_arm_button_->setEnabled(false);
    
    arm_conn_layout->addWidget(new QLabel("IP地址:"), 0, 0);
    arm_conn_layout->addWidget(arm_ip_edit_, 0, 1);
    arm_conn_layout->addWidget(new QLabel("端口:"), 0, 2);
    arm_conn_layout->addWidget(arm_port_spin_, 0, 3);
    arm_conn_layout->addWidget(connect_arm_button_, 1, 0, 1, 2);
    arm_conn_layout->addWidget(disconnect_arm_button_, 1, 2, 1, 2);
    
    // Arm Position Group
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
    
    // Arm Continuous Movement Group
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
    
    // Arm Speed Group
    arm_speed_group_ = new QGroupBox("速度设置");
    QGridLayout *speed_layout = new QGridLayout(arm_speed_group_);
    
    speed_spin_ = new QDoubleSpinBox();
    speed_spin_->setRange(0, 100000); // 修改为0-100000
    speed_spin_->setValue(70000); // 默认速度修改为70000
    set_speed_button_ = new QPushButton("设置速度");
    read_speed_button_ = new QPushButton("读取速度");
    current_speed_label_ = new QLabel("当前速度: --");
    
    speed_layout->addWidget(new QLabel("速度:"), 0, 0);
    speed_layout->addWidget(speed_spin_, 0, 1);
    speed_layout->addWidget(set_speed_button_, 0, 2);
    speed_layout->addWidget(read_speed_button_, 0, 3);
    speed_layout->addWidget(current_speed_label_, 1, 0, 1, 4);
    
    // Real-time position reading
    real_time_position_checkbox_ = new QCheckBox("实时读取位置");
    speed_layout->addWidget(real_time_position_checkbox_, 2, 0, 1, 4);
    
    // S-curve Movement Group
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
    
    // Spherical Cap Parameters Group
    spherical_cap_group_ = new QGroupBox("球冠参数");
    QGridLayout *cap_layout = new QGridLayout(spherical_cap_group_);
    
    cap_radius_spin_ = new QSpinBox();
    cap_radius_spin_->setRange(0, 1000000);
    cap_radius_spin_->setValue(230000);
    
    cap_height_spin_ = new QSpinBox();
    cap_height_spin_->setRange(0, 100000);
    cap_height_spin_->setValue(50000);
    
    delta_h_spin_ = new QSpinBox();
    delta_h_spin_->setRange(0, 80000);
    delta_h_spin_->setValue(0);
    
    cap_layout->addWidget(new QLabel("底面半径R(微米):"), 0, 0);
    cap_layout->addWidget(cap_radius_spin_, 0, 1);
    cap_layout->addWidget(new QLabel("高度h(微米):"), 1, 0);
    cap_layout->addWidget(cap_height_spin_, 1, 1);
    cap_layout->addWidget(new QLabel("高度差dH(微米):"), 2, 0);
    cap_layout->addWidget(delta_h_spin_, 2, 1);
    cap_layout->addWidget(new QLabel("(dH最大为 80000 - h)"), 3, 0, 1, 2);
    
    // Movement Progress and Status
    movement_progress_bar_ = new QProgressBar();
    movement_status_text_ = new QTextEdit();
    movement_status_text_->setReadOnly(true);
    movement_status_text_->setMaximumHeight(100);
    
    // Add arm components to right layout
    right_layout_->addWidget(arm_connection_group_);
    right_layout_->addWidget(arm_position_group_);
    right_layout_->addWidget(arm_continuous_group_);
    right_layout_->addWidget(arm_speed_group_);
    right_layout_->addWidget(s_movement_group_);
    right_layout_->addWidget(spherical_cap_group_);
    right_layout_->addWidget(new QLabel("运动进度:"));
    right_layout_->addWidget(movement_progress_bar_);
    right_layout_->addWidget(new QLabel("运动状态:"));
    right_layout_->addWidget(movement_status_text_);
    
    // Add all widgets to main layout
    arm_camera_main_layout_->addWidget(left_widget_, 1);
    arm_camera_main_layout_->addWidget(center_widget_, 2);
    arm_camera_main_layout_->addWidget(right_widget_, 1);
    
    // Add tab to tab widget
    tab_widget_->addTab(arm_camera_tab_, "机械臂与相机控制");
    
    // Connect signals and slots for camera
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
    connect(auto_exposure_checkbox_, &QCheckBox::stateChanged, this, &MainWindow::on_autoExposureCheckbox_stateChanged);
    connect(rotation_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::on_rotationCombo_currentIndexChanged);
    connect(set_crop_button_, &QPushButton::clicked, this, &MainWindow::on_setCropButton_clicked);
    
    // Connect signals and slots for arm
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
    
    // Connect cell click signals
    connect(top_cells_table_, &QTableWidget::cellClicked, this, &MainWindow::on_topCellsTable_cellClicked);
    connect(bottom_cells_table_, &QTableWidget::cellClicked, this, &MainWindow::on_bottomCellsTable_cellClicked);
}

void MainWindow::setupStitchingTab() {
    QWidget *stitching_tab = new QWidget(this);
    QVBoxLayout *main_layout = new QVBoxLayout(stitching_tab);
    
    // Input Group
    stitching_input_group_ = new QGroupBox("拼接输入");
    QGridLayout *input_layout = new QGridLayout(stitching_input_group_);
    
    input_dir_edit_ = new QLineEdit("./images");
    load_images_button_ = new QPushButton("加载图像");
    stitch_grid_size_x_spin_ = new QSpinBox();
    stitch_grid_size_x_spin_->setRange(1, 50);
    stitch_grid_size_x_spin_->setValue(10);
    stitch_grid_size_y_spin_ = new QSpinBox();
    stitch_grid_size_y_spin_->setRange(1, 50);
    stitch_grid_size_y_spin_->setValue(10);
    
    input_layout->addWidget(new QLabel("图像目录:"), 0, 0);
    input_layout->addWidget(input_dir_edit_, 0, 1);
    input_layout->addWidget(load_images_button_, 0, 2);
    input_layout->addWidget(new QLabel("网格X:"), 1, 0);
    input_layout->addWidget(stitch_grid_size_x_spin_, 1, 1);
    input_layout->addWidget(new QLabel("网格Y:"), 1, 2);
    input_layout->addWidget(stitch_grid_size_y_spin_, 1, 3);
    
    // Control Group
    stitching_control_group_ = new QGroupBox("拼接控制");
    QHBoxLayout *control_layout = new QHBoxLayout(stitching_control_group_);
    
    stitch_images_button_ = new QPushButton("开始拼接");
    save_stitched_image_button_ = new QPushButton("保存拼接结果");
    
    control_layout->addWidget(stitch_images_button_);
    control_layout->addWidget(save_stitched_image_button_);
    
    // Progress and Status
    stitching_progress_bar_ = new QProgressBar();
    stitching_status_text_ = new QTextEdit();
    stitching_status_text_->setReadOnly(true);
    stitching_status_text_->setMaximumHeight(100);
    
    // Result Image
    stitched_image_label_ = new QLabel("拼接结果");
    stitched_image_label_->setAlignment(Qt::AlignCenter);
    stitched_image_label_->setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc;");
    stitched_image_label_->setMinimumHeight(400);
    
    // Add all groups to main layout
    main_layout->addWidget(stitching_input_group_);
    main_layout->addWidget(stitching_control_group_);
    main_layout->addWidget(new QLabel("拼接进度:"));
    main_layout->addWidget(stitching_progress_bar_);
    main_layout->addWidget(new QLabel("拼接状态:"));
    main_layout->addWidget(stitching_status_text_);
    main_layout->addWidget(new QLabel("拼接结果:"));
    main_layout->addWidget(stitched_image_label_);
    
    // Add tab to tab widget
    tab_widget_->addTab(stitching_tab, "图像拼接");
    
    // Connect signals and slots
    connect(stitch_images_button_, &QPushButton::clicked, this, &MainWindow::on_stitchImagesButton_clicked);
    connect(load_images_button_, &QPushButton::clicked, this, &MainWindow::on_loadImagesButton_clicked);
    connect(save_stitched_image_button_, &QPushButton::clicked, this, &MainWindow::on_saveStitchedImageButton_clicked);
}

// Helper Functions
void MainWindow::displayImage(const cv::Mat& image, QLabel* label) {
    if (image.empty() || !label) {
        return;
    }
    
    try {
        QImage qimage = cvMatToQImage(image);
        if (!qimage.isNull()) {
            QPixmap pixmap = QPixmap::fromImage(qimage);
            label->setPixmap(pixmap.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    } catch (const std::exception& e) {
        std::cerr << "displayImage error: " << e.what() << std::endl;
    }
}

QImage MainWindow::cvMatToQImage(const cv::Mat& mat) {
    if (mat.empty()) {
        return QImage();
    }
    
    try {
        if (mat.type() == CV_8UC3) {
            cv::Mat rgb;
            cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
            QImage result(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
            return result.copy();
        } else if (mat.type() == CV_8UC1) {
            QImage result(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
            return result.copy();
        } else {
            cv::Mat rgb;
            mat.convertTo(rgb, CV_8UC3);
            cv::cvtColor(rgb, rgb, cv::COLOR_BGR2RGB);
            QImage result(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
            return result.copy();
        }
    } catch (const std::exception& e) {
        std::cerr << "cvMatToQImage error: " << e.what() << std::endl;
        return QImage();
    }
}

// Arm Control Slots
void MainWindow::on_connectArmButton_clicked() {
    std::string ip = arm_ip_edit_->text().toStdString();
    int port = arm_port_spin_->value();
    
    if (arm_controller_.connect(ip, port)) {
        connect_arm_button_->setEnabled(false);
        disconnect_arm_button_->setEnabled(true);
        status_bar_label_->setText("机械臂连接成功");
        arm_connected_ = true;
        
        // 自动设置速度为默认值70000
        int default_speed = 70000;
        for (int i = 0; i < 5; ++i) {
            arm_controller_.setSpeed(i, default_speed);
        }
        std::cout << "MainWindow: Set default speed to " << default_speed << std::endl;
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
    double x = x_pos_spin_->value();
    double y = y_pos_spin_->value();
    double z = z_pos_spin_->value();
    double a = a_pos_spin_->value();
    double b = b_pos_spin_->value();
    
    arm_controller_.moveToPosition(0, x);
    arm_controller_.moveToPosition(1, y);
    arm_controller_.moveToPosition(2, z);
    arm_controller_.moveToPosition(3, a);
    arm_controller_.moveToPosition(4, b);
    
    status_bar_label_->setText("开始移动到指定位置");
}

void MainWindow::on_startContinuousButton_clicked() {
    int axis_id = continuous_axis_combo_->currentIndex();
    bool direction = (direction_combo_->currentIndex() == 0); // 0: forward, 1: backward
    
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
        // No scaling, use raw position directly
        switch (i) {
            case 0: x_pos_spin_->setValue(position); break;
            case 1: y_pos_spin_->setValue(position); break;
            case 2: z_pos_spin_->setValue(position); break;
            case 3: a_pos_spin_->setValue(position); break;
            case 4: b_pos_spin_->setValue(position); break;
        }
        qDebug() << "MainWindow: Axis" << i << "position:" << position;
    }
    status_bar_label_->setText("位置读取完成");
}

void MainWindow::on_setSpeedButton_clicked() {
    int speed = speed_spin_->value();
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
    
    int speed = arm_controller_.readSpeed(0); // 读取X轴速度作为参考
    current_speed_label_->setText(QString("当前速度: %1").arg(speed));
    status_bar_label_->setText("速度读取完成");
}

void MainWindow::on_zeroButton_clicked() {
    if (!arm_controller_.isConnected()) {
        QMessageBox::warning(this, "警告", "机械臂未连接");
        return;
    }
    
    // 使每个轴直接回到0位置
    std::cout << "MainWindow: Zeroing all axes..." << std::endl;
    
    // 并行发送所有轴的归零命令
    arm_controller_.moveToPosition(0, 0); // X轴归零
    arm_controller_.moveToPosition(1, 0); // Y轴归零
    arm_controller_.moveToPosition(2, 0); // Z轴归零
    arm_controller_.moveToPosition(3, 0); // A轴归零
    arm_controller_.moveToPosition(4, 0); // B轴归零
    
    // 更新UI显示
    x_pos_spin_->setValue(0);
    y_pos_spin_->setValue(0);
    z_pos_spin_->setValue(0);
    a_pos_spin_->setValue(0);
    b_pos_spin_->setValue(0);
    
    status_bar_label_->setText("所有轴已归零");
    std::cout << "MainWindow: All axes zeroed" << std::endl;
}

void MainWindow::on_realTimePositionCheckbox_stateChanged(int state) {
    if (state == Qt::Checked) {
        // 启动实时读取
        arm_controller_.startAutoRead(0.3f); // 300ms更新一次
        status_bar_label_->setText("已开启实时位置读取");
    } else {
        // 停止实时读取
        arm_controller_.stopAutoRead();
        status_bar_label_->setText("已关闭实时位置读取");
    }
}

void MainWindow::on_startSMovementButton_clicked() {
    // 检查机械臂和相机连接状态
    std::cout << "MainWindow: Checking arm and camera connections..." << std::endl;
    if (!arm_controller_.isConnected()) {
        std::cout << "MainWindow: Arm is not connected" << std::endl;
        QMessageBox::warning(this, "警告", "机械臂未连接");
        return;
    }
    
    if (!camera_handler_.isConnected()) {
        std::cout << "MainWindow: Camera is not connected" << std::endl;
        QMessageBox::warning(this, "警告", "相机未连接");
        return;
    }
    
    std::cout << "MainWindow: Arm and camera are connected" << std::endl;
    
    // 启动相机采集
    std::cout << "MainWindow: Starting camera capture..." << std::endl;
    if (!camera_handler_.startCapture()) {
        std::cout << "MainWindow: Failed to start camera capture" << std::endl;
        QMessageBox::warning(this, "警告", "相机采集启动失败！");
        return;
    }
    std::cout << "MainWindow: Camera capture started successfully" << std::endl;
    
    // 使用默认参数
    double x_start = 0;
    double x_end = 387000; // 9*43000
    double y_start = 0;
    double y_end = 387000; // 9*43000
    int grid_size_x = 10;
    int grid_size_y = 10;
    double z_height = 80000;
    
    // Get spherical cap parameters
    int cap_radius = cap_radius_spin_->value();
    int cap_height = cap_height_spin_->value();
    int delta_h = delta_h_spin_->value();
    
    // Validate delta_h
    int max_delta_h = 80000 - cap_height;
    if (delta_h > max_delta_h) {
        delta_h = max_delta_h;
        delta_h_spin_->setValue(delta_h);
        QMessageBox::warning(this, "警告", QString("dH超过最大值，已自动调整为 %1").arg(max_delta_h));
    }
    
    std::cout << "MainWindow: S-movement parameters - Start: (" << x_start << ", " << y_start << "), End: (" << x_end << ", " << y_end << "), Grid: " << grid_size_x << "x" << grid_size_y << ", Z: " << z_height << std::endl;
    std::cout << "MainWindow: Spherical cap parameters - R=" << cap_radius << ", h=" << cap_height << ", dH=" << delta_h << std::endl;
    
    // 使用用户设置的保存路径
    std::string base_path = save_directory_edit_->text().toStdString();
    if (base_path.empty()) {
        base_path = "c:/Users/SP/Desktop/ArmSightStitch-main/image";
    }
    std::cout << "MainWindow: Using save path: " << base_path << std::endl;
    
    // Find the next run number by checking existing directories
    int run_number = 0;
    QDir base_dir(QString::fromStdString(base_path));
    if (base_dir.exists()) {
        QStringList dirs = base_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& dir : dirs) {
            bool ok;
            int num = dir.toInt(&ok);
            if (ok && num > run_number) {
                run_number = num;
            }
        }
    }
    run_number++; // Increment to get the next run number
    
    std::string save_path = base_path + "/" + std::to_string(run_number);
    
    std::cout << "MainWindow: Creating save directory: " << save_path << std::endl;
    
    // Create directory if it doesn't exist
    QDir dir(QString::fromStdString(save_path));
    if (!dir.exists()) {
        std::cout << "MainWindow: Directory doesn't exist, creating..." << std::endl;
        if (dir.mkpath(QString::fromStdString(save_path))) {
            std::cout << "MainWindow: Created save directory: " << save_path << std::endl;
        } else {
            std::cout << "MainWindow: Failed to create save directory" << std::endl;
            QMessageBox::warning(this, "警告", "创建保存目录失败！");
            return;
        }
    } else {
        std::cout << "MainWindow: Directory already exists: " << save_path << std::endl;
    }
    
    // Set save directory
    s_movement_controller_.setSaveDirectory(save_path);
    s_movement_controller_.setRunNumber(run_number);
    s_movement_controller_.setSphericalCapParameters(cap_radius, cap_height, delta_h);
    std::cout << "MainWindow: Set save directory: " << save_path << ", Run number: " << run_number << std::endl;
    
    // Create start and end points
    arm::SMovementPoint start_pos;
    start_pos.x = static_cast<int32_t>(x_start);
    start_pos.y = static_cast<int32_t>(y_start);
    start_pos.z = static_cast<int32_t>(z_height);
    start_pos.a = 0;
    start_pos.b = 0;
    start_pos.row = 0;
    start_pos.col = 0;
    
    arm::SMovementPoint end_pos;
    end_pos.x = static_cast<int32_t>(x_end);
    end_pos.y = static_cast<int32_t>(y_end);
    end_pos.z = static_cast<int32_t>(z_height);
    end_pos.a = 0;
    end_pos.b = 0;
    end_pos.row = grid_size_y - 1;
    end_pos.col = grid_size_x - 1;
    
    std::cout << "MainWindow: Start position: (" << start_pos.x << ", " << start_pos.y << ", " << start_pos.z << ")" << std::endl;
    std::cout << "MainWindow: End position: (" << end_pos.x << ", " << end_pos.y << ", " << end_pos.z << ")" << std::endl;
    
    // Initialize S-movement controller with boundary info for spherical cap calculation
    std::cout << "MainWindow: Initializing S-movement controller..." << std::endl;
    if (s_movement_controller_.initialize(cv::Size(grid_size_x, grid_size_y), start_pos, end_pos,
                                          x_start, x_end, y_start, y_end)) {
        std::cout << "MainWindow: S-movement controller initialized successfully" << std::endl;
        
        // Set image capture callback
        s_movement_controller_.setImageCaptureCallback([this](cv::Mat& frame) {
            std::cout << "MainWindow: Capturing image..." << std::endl;
            bool captured = camera_handler_.captureSingleFrame(frame);
            std::cout << "MainWindow: Image capture " << (captured ? "successful" : "failed") << std::endl;
            return captured;
        });
        
        // Set image save callback
        s_movement_controller_.setImageSaveCallback([this](const cv::Mat& frame, const std::string& path, int row, int col) {
            try {
                std::string filename = path + "/" + std::to_string(row) + "_" + std::to_string(col) + ".jpg";
                std::cout << "MainWindow: Saving image to: " << filename << std::endl;
                
                QDir dir(QString::fromStdString(path));
                if (!dir.exists()) {
                    std::cout << "MainWindow: Creating directory: " << path << std::endl;
                    if (!dir.mkpath(QString::fromStdString(path))) {
                        std::cout << "MainWindow: Failed to create directory" << std::endl;
                        return false;
                    }
                }
                
                std::cout << "MainWindow: Attempting to save image" << std::endl;
                bool saved = cv::imwrite(filename, frame);
                
                if (!saved) {
                    std::cout << "MainWindow: Failed to save image" << std::endl;
                    return false;
                }
                
                QFile file(QString::fromStdString(filename));
                if (file.exists()) {
                    std::cout << "MainWindow: Image saved and verified successfully" << std::endl;
                    s_movement_images_[std::make_pair(row, col)] = filename;
                    
                    QMetaObject::invokeMethod(this, [this, filename, row, col]() {
                        try {
                            if (row < 10 && col < 10) {
                                QTableWidgetItem *item = top_cells_table_->item(row, col);
                                if (!item) {
                                    item = new QTableWidgetItem();
                                    top_cells_table_->setItem(row, col, item);
                                }
                                item->setText(QString("%1,%2").arg(row).arg(col));
                                item->setBackground(QColor(144, 238, 144));
                                
                                cv::Mat frame_copy = cv::imread(filename);
                                if (!frame_copy.empty()) {
                                    QImage qimage = cvMatToQImage(frame_copy);
                                    if (!qimage.isNull()) {
                                        QPixmap pixmap = QPixmap::fromImage(qimage);
                                        QLabel *imageLabel = new QLabel();
                                        imageLabel->setPixmap(pixmap.scaled(QSize(50, 50), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                                        imageLabel->setAlignment(Qt::AlignCenter);
                                        top_cells_table_->setCellWidget(row, col, imageLabel);
                                    }
                                }
                                std::cout << "MainWindow: Updated cell (" << row << ", " << col << ") with image" << std::endl;
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "MainWindow: Exception in UI update: " << e.what() << std::endl;
                        }
                    }, Qt::QueuedConnection);
                    return true;
                } else {
                    std::cout << "MainWindow: Image save reported success but file does not exist" << std::endl;
                    return false;
                }
            } catch (const std::exception& e) {
                std::cerr << "MainWindow: Exception in image save callback: " << e.what() << std::endl;
                return false;
            }
        });
        
        // Start S-movement
        std::cout << "MainWindow: Starting S-movement..." << std::endl;
        if (s_movement_controller_.start()) {
            std::cout << "MainWindow: S-movement started successfully" << std::endl;
            start_s_movement_button_->setEnabled(false);
            stop_s_movement_button_->setEnabled(true);
            pause_s_movement_button_->setEnabled(true);
            resume_s_movement_button_->setEnabled(false);
            status_bar_label_->setText("S型运动开始");
        } else {
            std::cout << "MainWindow: Failed to start S-movement" << std::endl;
            QMessageBox::critical(this, "错误", "S型运动启动失败");
        }
    } else {
        std::cout << "MainWindow: Failed to initialize S-movement controller" << std::endl;
        QMessageBox::critical(this, "错误", "S型运动初始化失败");
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

// Camera Slots
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
    std::cout << "MainWindow: on_startCaptureButton_clicked called" << std::endl;
    if (camera_handler_.startCapture()) {
        std::cout << "MainWindow: Camera capture started successfully" << std::endl;
        start_capture_button_->setEnabled(false);
        stop_capture_button_->setEnabled(true);
        // Start camera update timer (30fps)
        camera_update_timer_->start(33);
        status_bar_label_->setText("相机开始采集");
    } else {
        std::cout << "MainWindow: Camera capture failed to start" << std::endl;
        status_bar_label_->setText("相机采集启动失败");
        QMessageBox::critical(this, "错误", "相机采集启动失败");
    }
}

void MainWindow::on_stopCaptureButton_clicked() {
    std::cout << "MainWindow: on_stopCaptureButton_clicked called" << std::endl;
    camera_handler_.stopCapture();
    start_capture_button_->setEnabled(true);
    stop_capture_button_->setEnabled(false);
    // Stop camera update timer
    camera_update_timer_->stop();
    status_bar_label_->setText("相机采集已停止");
    std::cout << "MainWindow: Camera capture stopped" << std::endl;
}

void MainWindow::on_captureImageButton_clicked() {
    if (!camera_handler_.isConnected()) {
        QMessageBox::warning(this, "警告", "相机未连接！");
        return;
    }

    // 启动相机采集
    std::cout << "MainWindow: Starting camera capture..." << std::endl;
    if (!camera_handler_.startCapture()) {
        std::cout << "MainWindow: Failed to start camera capture" << std::endl;
        QMessageBox::warning(this, "警告", "相机采集启动失败！");
        return;
    }
    std::cout << "MainWindow: Camera capture started successfully" << std::endl;

    // 生成文件名
    static int imageCounter = 0;
    imageCounter++;
    QString filename = QString("image_%1.jpg").arg(imageCounter, 4, 10, QChar('0'));
    QString saveDirectory = save_directory_edit_->text();
    QString filepath = saveDirectory + "/" + filename;

    // 确保保存目录存在
    QDir dir(saveDirectory);
    if (!dir.exists()) {
        if (dir.mkpath(saveDirectory)) {
            std::cout << "MainWindow: Created save directory: " << saveDirectory.toStdString() << std::endl;
        } else {
            QMessageBox::warning(this, "警告", "创建保存目录失败！");
            camera_handler_.stopCapture();
            return;
        }
    }

    // 检查目录是否可写
    QFileInfo dirInfo(saveDirectory);
    if (!dirInfo.isWritable()) {
        QMessageBox::warning(this, "警告", "保存目录不可写！");
        camera_handler_.stopCapture();
        return;
    }

    // 采集图像
    cv::Mat frame;
    bool captured = false;
    int retry_count = 0;
    const int max_retries = 10;
    
    while (retry_count < max_retries && !captured) {
        if (camera_handler_.captureSingleFrame(frame)) {
            captured = true;
            std::cout << "MainWindow: Image capture successful" << std::endl;
            current_image_ = frame;
            displayImage(frame, camera_image_label_);

            // 保存图像
            std::cout << "MainWindow: Attempting to save image to: " << filepath.toStdString() << std::endl;
            std::cout << "MainWindow: Frame size: " << frame.cols << "x" << frame.rows << std::endl;
            std::cout << "MainWindow: Frame type: " << frame.type() << std::endl;
            
            if (cv::imwrite(filepath.toStdString(), frame)) {
                status_bar_label_->setText(QString("图像已保存: %1").arg(filepath));
                std::cout << "MainWindow: Image saved to: " << filepath.toStdString() << std::endl;
                
                // 验证文件是否存在
                QFile file(filepath);
                if (file.exists()) {
                    std::cout << "MainWindow: Verified image file exists" << std::endl;
                } else {
                    std::cout << "MainWindow: Image file does not exist after save" << std::endl;
                }
            } else {
                QMessageBox::warning(this, "警告", "保存图像失败！");
                status_bar_label_->setText("保存图像失败");
                std::cout << "MainWindow: Failed to save image to: " << filepath.toStdString() << std::endl;
            }
        } else {
            std::cout << "MainWindow: Image capture failed, retry: " << retry_count + 1 << "/" << max_retries << std::endl;
            retry_count++;
            // 等待一段时间后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    // 停止相机采集
    camera_handler_.stopCapture();
    std::cout << "MainWindow: Camera capture stopped" << std::endl;
    
    if (!captured) {
        std::cout << "MainWindow: Image capture failed after " << max_retries << " retries" << std::endl;
        status_bar_label_->setText("采集图像失败");
        QMessageBox::critical(this, "错误", "采集图像失败");
    }
}

void MainWindow::on_enumerateCamerasButton_clicked() {
    std::cout << "MainWindow: Starting camera enumeration..." << std::endl;
    camera_combo_->clear();
    
    std::vector<camera::CameraDevice> cameras = camera_handler_.enumerateCameras();
    std::cout << "MainWindow: Found " << cameras.size() << " cameras" << std::endl;
    
    for (const auto& camera : cameras) {
        std::cout << "MainWindow: Camera: " << camera.display_name << " (ID: " << camera.id << ")" << std::endl;
        camera_combo_->addItem(camera.display_name.c_str(), camera.id.c_str());
    }
    
    status_bar_label_->setText("相机枚举完成");
    std::cout << "MainWindow: Camera enumeration complete" << std::endl;
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

void MainWindow::on_autoExposureCheckbox_stateChanged(int state) {
    bool enabled = (state == Qt::Checked);
    
    exposure_spin_->setEnabled(!enabled);
    set_exposure_button_->setEnabled(!enabled);
    
    if (camera_handler_.isConnected()) {
        if (camera_handler_.setAutoExposure(enabled)) {
            status_bar_label_->setText(enabled ? "自动曝光已开启" : "自动曝光已关闭");
            std::cout << "MainWindow: Auto exposure " << (enabled ? "enabled" : "disabled") << std::endl;
        } else {
            status_bar_label_->setText("设置自动曝光失败");
            QMessageBox::warning(this, "警告", "设置自动曝光失败");
        }
    }
}

void MainWindow::on_rotationCombo_currentIndexChanged(int index) {
    int angle = 0;
    switch (index) {
        case 0: angle = 0; break;
        case 1: angle = 90; break;
        case 2: angle = 180; break;
        case 3: angle = 270; break;
    }
    
    camera_handler_.setImageRotation(angle);
    status_bar_label_->setText(QString("图像旋转设置为 %1°").arg(angle));
    std::cout << "MainWindow: Image rotation set to " << angle << " degrees" << std::endl;
}

void MainWindow::on_setCropButton_clicked() {
    int crop_width = crop_width_spin_->value();
    int crop_height = crop_height_spin_->value();
    
    camera_handler_.setCropSize(crop_width, crop_height);
    
    QString status_text;
    if (crop_width == 0 && crop_height == 0) {
        status_text = "截取已取消，使用原始像素";
    } else {
        status_text = QString("截取区域: %1x%2（中心对齐）").arg(crop_width).arg(crop_height);
    }
    status_bar_label_->setText(status_text);
    std::cout << "MainWindow: Crop size set to " << crop_width << "x" << crop_height << std::endl;
}

void MainWindow::on_setSaveDirectoryButton_clicked() {
    QString directory = QFileDialog::getExistingDirectory(
        this, "选择保存目录", save_directory_edit_->text());
    if (!directory.isEmpty()) {
        save_directory_edit_->setText(directory);
        status_bar_label_->setText(QString("保存目录: %1").arg(directory));
        std::cout << "MainWindow: Save directory set to: " << directory.toStdString() << std::endl;
    }
}

// Detection Slots
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
    // Open file dialog to select an image file
    QString file_path = QFileDialog::getOpenFileName(this, "选择图像文件", "./", 
                                                    "图像文件 (*.jpg *.jpeg *.png *.bmp);;所有文件 (*.*)");
    
    if (file_path.isEmpty()) {
        return;
    }
    
    // Load image using OpenCV
    cv::Mat image = cv::imread(file_path.toStdString());
    if (image.empty()) {
        QMessageBox::warning(this, "警告", "图像加载失败");
        return;
    }
    
    // Update current image and display it
    current_image_ = image;
    displayImage(image, detection_image_label_);
    
    // Clear previous detection results
    detection_result_ = cv::Mat();
    
    status_bar_label_->setText("图像加载成功");
    
    // Update status text
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
    
    // Update status text
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
    float threshold = confidence_threshold_spin_->value();
    yolo_detector_.setConfidenceThreshold(threshold);
    status_bar_label_->setText("置信度阈值设置完成");
}

void MainWindow::on_setNmsThresholdButton_clicked() {
    float threshold = nms_threshold_spin_->value();
    yolo_detector_.setNmsThreshold(threshold);
    status_bar_label_->setText("NMS阈值设置完成");
}

// Stitching Slots
void MainWindow::on_stitchImagesButton_clicked() {
    std::string input_dir = input_dir_edit_->text().toStdString();
    int grid_size_x = stitch_grid_size_x_spin_->value();
    int grid_size_y = stitch_grid_size_y_spin_->value();
    
    cv::Size grid_size(grid_size_x, grid_size_y);
    
    // Check if directory exists
    std::vector<cv::Mat> images;
    bool directory_exists = std::filesystem::exists(input_dir);
    
    if (!directory_exists) {
        // Let user select directory
        QString dir_path = QFileDialog::getExistingDirectory(this, "选择图像目录", "./", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (dir_path.isEmpty()) {
            status_bar_label_->setText("未选择图像目录");
            return;
        }
        
        input_dir = dir_path.toStdString();
        input_dir_edit_->setText(dir_path);
        images = image_stitcher_.loadImagesFromDirectory(input_dir);
    } else {
        // Load images from existing directory
        images = image_stitcher_.loadImagesFromDirectory(input_dir);
    }
    
    if (images.empty()) {
        QMessageBox::warning(this, "警告", "没有图像可以拼接");
        return;
    }
    
    // Sort images in S-curve order
    std::vector<cv::Mat> sorted_images = image_stitcher_.sortImagesInSCurveOrder(images, grid_size);
    
    // Start concurrent stitching
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
    // This function is just a placeholder for now
    status_bar_label_->setText("图像加载功能待实现");
}

void MainWindow::on_saveStitchedImageButton_clicked() {
    if (stitched_result_.empty()) {
        QMessageBox::warning(this, "警告", "没有拼接结果可以保存");
        return;
    }
    
    QString filename = QFileDialog::getSaveFileName(this, "保存拼接结果", "./stitched_result.jpg", "JPEG Files (*.jpg);;PNG Files (*.png)");
    if (!filename.isEmpty()) {
        cv::imwrite(filename.toStdString(), stitched_result_);
        status_bar_label_->setText("拼接结果保存成功");
    }
}

// Timer Slots
void MainWindow::updateArmStatus() {
    // Arm status is updated via callback
}

void MainWindow::updateCameraStatus() {
    // Camera status is updated via callback
}

void MainWindow::updateCameraImage() {
    if (!camera_handler_.isConnected() || !camera_handler_.isCapturing()) {
        return;
    }

    try {
        // Capture a single frame directly
        cv::Mat frame;
        if (camera_handler_.captureSingleFrame(frame)) {
            current_image_ = frame;
            
            if (image_detection_enabled_ && model_loaded_) {
                // Perform detection if enabled and model is loaded
                std::vector<detector::Detection> detections = yolo_detector_.detect(frame);
                cv::Mat result = yolo_detector_.drawDetections(frame, detections);
                displayImage(result, camera_image_label_);
            } else {
                // Just display the raw frame if detection is disabled or model not loaded
                displayImage(frame, camera_image_label_);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error updating camera image: " << e.what() << std::endl;
    }
}

// Image Callback Slot (deprecated - now using timer-based image pull)
void MainWindow::onImageCaptured(const cv::Mat& frame) {
    // This function is no longer used - image capture is handled by updateCameraImage
}

// Arm Status Callback Slot
void MainWindow::onArmStatusChanged(const arm::ArmStatus& status) {
    // Update status text
    std::string status_msg = "机械臂状态: " + status.status_message;
    status_bar_label_->setText(QString::fromStdString(status_msg));
    
    // Update position displays
    x_pos_spin_->setValue(status.current_positions[0]);
    y_pos_spin_->setValue(status.current_positions[1]);
    z_pos_spin_->setValue(status.current_positions[2]);
    a_pos_spin_->setValue(status.current_positions[3]);
    b_pos_spin_->setValue(status.current_positions[4]);
}

// Camera Status Callback Slot
void MainWindow::onCameraStatusChanged(const camera::CameraStatus& status) {
    std::string status_msg = "相机状态: " + status.status_message;
    status_bar_label_->setText(QString::fromStdString(status_msg));
    
    // Update camera status text
    camera_status_text_->append(QString::fromStdString(status_msg));
    camera_status_text_->append("分辨率: " + QString::number(status.width) + "x" + QString::number(status.height));
    camera_status_text_->append("帧率: " + QString::number(status.fps, 'f', 2) + " FPS");
}

// Movement Progress Callback Slot
void MainWindow::onMovementProgress(int current, int total) {
    movement_progress_bar_->setValue((current * 100) / total);
}

// Movement Status Callback Slot
void MainWindow::onMovementStatus(const arm::SMovementStatus& status) {
    movement_status_text_->append(QString::fromStdString(status.status_message));
    
    // Update progress bar if we have total points
    if (status.total_points > 0) {
        int progress = (status.current_point * 100) / status.total_points;
        movement_progress_bar_->setValue(progress);
    }
}

// Stitching Progress Callback Slot
void MainWindow::onStitchingProgress(int current, int total) {
    stitching_progress_bar_->setValue((current * 100) / total);
}

// Stitching Status Callback Slot
void MainWindow::onStitchingStatus(const std::string& status) {
    stitching_status_text_->append(QString::fromStdString(status));
}

// Detection Result Callback Slot
void MainWindow::onDetectionResult(const std::vector<detector::Detection>& detections, const cv::Mat& image) {
    // This function is just a placeholder for now
}

// Detection Toggle Slot
void MainWindow::on_enableDetectionCheckbox_stateChanged(int state) {
    image_detection_enabled_ = (state == Qt::Checked);
    
    if (image_detection_enabled_) {
        status_bar_label_->setText("图像检测已开启");
    } else {
        status_bar_label_->setText("图像检测已关闭");
    }
}

// Top Cells Click Slot
void MainWindow::on_topCellsTable_cellClicked(int row, int column) {
    // 检查是否有图片路径
    auto key = std::make_pair(row, column);
    auto it = s_movement_images_.find(key);
    
    if (it != s_movement_images_.end()) {
        std::string image_path = it->second;
        
        // 显示图片预览
        cv::Mat image = cv::imread(image_path);
        if (!image.empty()) {
            // 创建一个新窗口显示大图
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

// Bottom Cells Click Slot
void MainWindow::on_bottomCellsTable_cellClicked(int row, int column) {
    if (!arm_connected_) {
        status_bar_label_->setText("机械臂未连接");
        return;
    }
    
    double x_start = 0;
    double x_end = 387000;
    double y_start = 0;
    double y_end = 387000;
    int grid_width = 10;
    int grid_height = 10;
    
    double target_x = column * 43000.0;
    double target_y = row * 43000.0;
    
    int cap_radius = cap_radius_spin_->value();
    int cap_height = cap_height_spin_->value();
    int delta_h = delta_h_spin_->value();
    int z_base = 80000;
    
    double grid_center_x = (x_start + x_end) / 2.0;
    double grid_center_y = (y_start + y_end) / 2.0;
    double dx = target_x - grid_center_x;
    double dy = target_y - grid_center_y;
    double dist_sq = dx * dx + dy * dy;
    
    double R = cap_radius;
    double h = cap_height;
    double sphere_radius = (R * R + h * h) / (2.0 * h);
    double cap_base_radius_sq = R * R;
    
    double cap_height_value = 0.0;
    if (dist_sq <= cap_base_radius_sq) {
        cap_height_value = h - (sphere_radius - std::sqrt(sphere_radius * sphere_radius - dist_sq));
    }
    
    int target_z = static_cast<int>(z_base - cap_height_value - delta_h);
    if (target_z < 0) target_z = 0;
    if (target_z > z_base) target_z = z_base;
    
    arm_controller_.moveToPosition(0, static_cast<int>(target_x));
    arm_controller_.moveToPosition(1, static_cast<int>(target_y));
    arm_controller_.moveToPosition(2, target_z);
    
    status_bar_label_->setText(QString("移动到位置: (%1, %2, %3)").arg(target_x).arg(target_y).arg(target_z));
}


