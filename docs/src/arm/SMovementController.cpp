#include "SMovementController.h"

#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <ctime>

namespace arm {

SMovementController::SMovementController(ModbusArmController& arm_controller) 
    : arm_controller_(arm_controller),
      running_(false),
      paused_(false),
      stop_requested_(false),
      current_point_index_(0),
      run_number_(0),
      movement_speed_(70000), // 默认速度设置为70000
      saved_images_count_(0),
      spherical_cap_radius_(230000),
      spherical_cap_height_(50000),
      delta_h_(0),
      z_base_height_(80000) {
    // Initialize status
    current_status_.running = false;
    current_status_.paused = false;
    current_status_.current_point = 0;
    current_status_.total_points = 0;
    current_status_.status_message = "Initialized";
    current_status_.current_action = "Idle";
    
    // Initialize current position
    current_status_.current_position = {0, 0, 0, 0, 0, 0, 0};
}

void SMovementController::setSphericalCapParameters(int radius, int height, int delta_h) {
    spherical_cap_radius_ = radius;
    spherical_cap_height_ = height;
    delta_h_ = delta_h;
    std::cout << "SMovementController: Spherical cap parameters set - R=" << radius 
              << ", h=" << height << ", dH=" << delta_h << std::endl;
}

SMovementController::~SMovementController() {
    stop();
}

bool SMovementController::initialize(const cv::Size& grid_size, 
                                   const SMovementPoint& start_pos, 
                                   const SMovementPoint& end_pos, 
                                   int step_size) {
    return initialize(grid_size, start_pos, end_pos, 
                      start_pos.x, end_pos.x, start_pos.y, end_pos.y, step_size);
}

bool SMovementController::initialize(const cv::Size& grid_size, 
                                   const SMovementPoint& start_pos, 
                                   const SMovementPoint& end_pos,
                                   double x_start_boundary, double x_end_boundary,
                                   double y_start_boundary, double y_end_boundary,
                                   int step_size) {
    try {
        movement_path_ = generateFixedPointSMovementPath(grid_size, start_pos, end_pos,
                                                          x_start_boundary, x_end_boundary,
                                                          y_start_boundary, y_end_boundary);
        
        if (movement_path_.empty()) {
            updateStatus("Failed to generate movement path");
            return false;
        }
        
        current_status_.total_points = static_cast<int>(movement_path_.size());
        current_status_.current_point = 0;
        
        updateStatus("Initialized successfully");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error initializing S-movement controller: " << e.what() << std::endl;
        updateStatus(std::string("Initialization error: ") + e.what());
        return false;
    }
}

bool SMovementController::start() {
    if (running_ || movement_path_.empty()) {
        return false;
    }
    
    try {
        running_ = true;
        paused_ = false;
        stop_requested_ = false;
        current_point_index_ = 0;
        saved_images_count_ = 0;
        
        // Update status
        current_status_.running = true;
        current_status_.paused = false;
        updateStatus("Starting S-curve movement");
        updateAction("Generating path");
        
        // Start movement thread
        movement_thread_ = std::thread(&SMovementController::movementThread, this);
        movement_thread_.detach();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error starting S-movement: " << e.what() << std::endl;
        updateStatus(std::string("Failed to start: ") + e.what());
        running_ = false;
        return false;
    }
}

void SMovementController::stop() {
    if (running_) {
        stop_requested_ = true;
        running_ = false;
        paused_ = false;
        
        // Wait for thread to finish
        if (movement_thread_.joinable()) {
            movement_thread_.join();
        }
        
        updateStatus("Movement stopped");
    }
}

void SMovementController::pause() {
    if (running_ && !paused_) {
        paused_ = true;
        current_status_.paused = true;
        updateStatus("Movement paused");
    }
}

void SMovementController::resume() {
    if (running_ && paused_) {
        paused_ = false;
        current_status_.paused = false;
        updateStatus("Movement resumed");
    }
}

void SMovementController::setImageCaptureCallback(std::function<bool(cv::Mat&)> callback) {
    image_capture_callback_ = callback;
}

void SMovementController::setImageSaveCallback(std::function<bool(const cv::Mat&, const std::string&, int, int)> callback) {
    image_save_callback_ = callback;
}

void SMovementController::setStatusCallback(std::function<void(const SMovementStatus&)> callback) {
    status_callback_ = callback;
}

void SMovementController::setSaveDirectory(const std::string& save_dir) {
    save_directory_ = save_dir;
    
    // Create directory if it doesn't exist
    if (!save_directory_.empty() && !std::filesystem::exists(save_directory_)) {
        std::filesystem::create_directories(save_directory_);
        std::cout << "SMovementController: Created save directory: " << save_directory_ << std::endl;
    } else if (!save_directory_.empty() && std::filesystem::exists(save_directory_)) {
        std::cout << "SMovementController: Save directory already exists: " << save_directory_ << std::endl;
    }
}

SMovementStatus SMovementController::getStatus() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(status_mutex_));
    return current_status_;
}

std::vector<SMovementPoint> SMovementController::getMovementPath() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(path_mutex_));
    return movement_path_;
}

void SMovementController::setRunNumber(int run_number) {
    run_number_ = run_number;
}

void SMovementController::setMovementSpeed(int speed) {
    movement_speed_ = speed;
}

int SMovementController::getSavedImagesCount() const {
    return saved_images_count_;
}

std::vector<SMovementPoint> SMovementController::generateFixedPointSMovementPath(const cv::Size& grid_size, 
                                                                              const SMovementPoint& start_pos, 
                                                                              const SMovementPoint& end_pos) {
    return generateFixedPointSMovementPath(grid_size, start_pos, end_pos, 
                                           start_pos.x, end_pos.x, start_pos.y, end_pos.y);
}

std::vector<SMovementPoint> SMovementController::generateFixedPointSMovementPath(const cv::Size& grid_size, 
                                                                              const SMovementPoint& start_pos, 
                                                                              const SMovementPoint& end_pos,
                                                                              double x_start_boundary, double x_end_boundary,
                                                                              double y_start_boundary, double y_end_boundary) {
    std::vector<SMovementPoint> path;
    
    try {
        int width = grid_size.width;
        int height = grid_size.height;
        
        if (width <= 0 || height <= 0) {
            return path;
        }
        
        double x_step = static_cast<double>(end_pos.x - start_pos.x) / (width - 1);
        double y_step = static_cast<double>(end_pos.y - start_pos.y) / (height - 1);
        
        std::cout << "SMovementController: Grid size: " << width << "x" << height << std::endl;
        std::cout << "SMovementController: Start position: (" << start_pos.x << ", " << start_pos.y << ")" << std::endl;
        std::cout << "SMovementController: End position: (" << end_pos.x << ", " << end_pos.y << ")" << std::endl;
        std::cout << "SMovementController: Step sizes: X=" << x_step << ", Y=" << y_step << std::endl;
        std::cout << "SMovementController: Spherical cap: R=" << spherical_cap_radius_ 
                  << ", h=" << spherical_cap_height_ << ", dH=" << delta_h_ << std::endl;
        
        for (int y_idx = 0; y_idx < height; ++y_idx) {
            for (int x_idx = 0; x_idx < width; ++x_idx) {
                SMovementPoint point;
                
                int actual_x_idx = (y_idx % 2 == 0) ? x_idx : (width - 1 - x_idx);
                
                point.x = static_cast<int>(start_pos.x + actual_x_idx * x_step);
                point.y = static_cast<int>(start_pos.y + y_idx * y_step);
                
                double cap_height = computeSphericalCapAverageHeight(actual_x_idx, y_idx, width, height,
                                                                     x_start_boundary, x_end_boundary,
                                                                     y_start_boundary, y_end_boundary);
                
                int z_value = static_cast<int>(z_base_height_ - cap_height - delta_h_);
                
                if (z_value < 0) z_value = 0;
                if (z_value > z_base_height_) z_value = z_base_height_;
                
                point.z = z_value;
                point.a = start_pos.a;
                point.b = start_pos.b;
                point.row = y_idx;
                point.col = actual_x_idx;
                
                path.push_back(point);
                std::cout << "SMovementController: Added point (" << point.x << ", " << point.y << ", " << point.z 
                          << ") at row " << point.row << ", col " << point.col 
                          << " (cap height: " << cap_height << ")" << std::endl;
            }
        }
        
        std::cout << "SMovementController: Generated " << path.size() << " points for S-movement" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error generating S-movement path: " << e.what() << std::endl;
    }
    
    return path;
}

void SMovementController::movementThread() {
    try {
        updateAction("Starting movement");
        std::cout << "SMovementController: Starting S-movement thread" << std::endl;
        
        // Set movement speed for all axes
        for (int axis = 0; axis < 5; ++axis) {
            try {
                arm_controller_.setSpeed(axis, movement_speed_);
                std::cout << "SMovementController: Set speed for axis " << axis << " to " << movement_speed_ << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "SMovementController: Error setting speed for axis " << axis << ": " << e.what() << std::endl;
            }
        }
        
        // Process each point in the movement path
        for (size_t i = 0; i < movement_path_.size() && !stop_requested_; ++i) {
            try {
                if (stop_requested_) {
                    std::cout << "SMovementController: Movement stopped by user" << std::endl;
                    break;
                }
                
                // Update current point index
                current_point_index_ = static_cast<int>(i);
                current_status_.current_point = current_point_index_;
                
                // Get current point
                const SMovementPoint& point = movement_path_[i];
                
                // Update current position
                current_status_.current_position = point;
                
                // Update status
                updateStatus("Moving to point " + std::to_string(i + 1) + " of " + std::to_string(movement_path_.size()));
                updateAction("Moving");
                
                // 直接向机械臂发送固定点
                std::cout << "SMovementController: Sending fixed point " << i + 1 << " to arm: (" << point.x << ", " << point.y << ", " << point.z << ")" << std::endl;
                
                // 并行移动X轴和Y轴
                bool success_x = false, success_y = false, success_z = false, success_a = false, success_b = false;
                try {
                    success_x = arm_controller_.moveToPosition(0, point.x); // X轴是轴ID 0
                    success_y = arm_controller_.moveToPosition(1, point.y); // Y轴是轴ID 1
                    success_z = arm_controller_.moveToPosition(2, point.z); // Z轴是轴ID 2
                    success_a = arm_controller_.moveToPosition(3, point.a); // A轴是轴ID 3
                    success_b = arm_controller_.moveToPosition(4, point.b); // B轴是轴ID 4
                } catch (const std::exception& e) {
                    std::cerr << "SMovementController: Error sending movement commands: " << e.what() << std::endl;
                }
                
                // 检查是否有轴移动失败
                if (!success_x || !success_y || !success_z || !success_a || !success_b) {
                    if (!success_x) std::cerr << "SMovementController: X axis movement failed" << std::endl;
                    if (!success_y) std::cerr << "SMovementController: Y axis movement failed" << std::endl;
                    if (!success_z) std::cerr << "SMovementController: Z axis movement failed" << std::endl;
                    if (!success_a) std::cerr << "SMovementController: A axis movement failed" << std::endl;
                    if (!success_b) std::cerr << "SMovementController: B axis movement failed" << std::endl;
                    updateStatus("Failed to move to point " + std::to_string(i + 1) + ", skipping to next point");
                    std::cerr << "SMovementController: Failed to move to point " << i + 1 << ", skipping to next point" << std::endl;
                    continue;
                }
                
                std::cout << "SMovementController: All axis movement commands sent successfully" << std::endl;
                
                // 等待机械臂到达目标位置
                double max_wait_time = 5.0; // 最大等待时间5秒
                auto wait_start = std::chrono::steady_clock::now();
                bool arrived = false;
                
                // 快速轮询检查位置，提高检测精度
                while (std::chrono::duration<double>(std::chrono::steady_clock::now() - wait_start).count() < max_wait_time) {
                    // 首先检查是否已经被停止
                    if (stop_requested_) {
                        std::cout << "SMovementController: Movement stopped by user" << std::endl;
                        arrived = false;
                        break;
                    }
                    
                    // 检查是否暂停
                    while (paused_ && !stop_requested_) {
                        std::cout << "SMovementController: Movement paused" << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    if (stop_requested_) {
                        arrived = false;
                        break;
                    }
                    
                    // 验证机械臂是否到达目标位置
                    double current_x = 0, current_y = 0;
                    try {
                        current_x = arm_controller_.readPosition(0);
                        current_y = arm_controller_.readPosition(1);
                    } catch (const std::exception& e) {
                        std::cerr << "SMovementController: Error reading position: " << e.what() << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }
                    
                    // 检查位置是否在合理范围内
                    const double tolerance = 50.0; // 位置容差
                    if (std::abs(current_x - point.x) <= tolerance && std::abs(current_y - point.y) <= tolerance) {
                        arrived = true;
                        std::cout << "SMovementController: Arm reached target position" << std::endl;
                        break;
                    }
                    
                    // 50ms检查一次，比之前更频繁
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
                // 如果成功到达目标位置，保存相机画面并停留
                if (arrived) {
                    // 更新状态栏显示到达目标
                    updateStatus("Arrived at target, saving image...");
                    updateAction("Saving image");
                    
                    // 最小化机械臂稳定等待时间
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    
                    // 确认到达目标位置后，拍摄照片
                    bool image_captured = false;
                    bool image_saved = false;
                    cv::Mat frame;
                    
                    // 增加图像捕获的重试次数，确保能够成功捕获图像
                    int capture_attempts = 0;
                    const int max_capture_attempts = 10;
                    
                    while (!image_captured && capture_attempts < max_capture_attempts) {
                        capture_attempts++;
                        if (image_capture_callback_) {
                            try {
                                if (image_capture_callback_(frame)) {
                                    image_captured = true;
                                } else {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                                }
                            } catch (const std::exception& e) {
                                std::cerr << "SMovementController: Error capturing image: " << e.what() << std::endl;
                                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                            }
                        } else {
                            break;
                        }
                    }
                    
                    // 如果成功捕获图像，保存图像
                    if (image_captured) {
                        // 增加图像保存的重试次数，确保能够成功保存图像
                        int save_attempts = 0;
                        const int max_save_attempts = 5;
                        
                        while (!image_saved && save_attempts < max_save_attempts) {
                            save_attempts++;
                            if (image_save_callback_ && !save_directory_.empty()) {
                                try {
                                    // 只使用回调函数来保存图像和更新UI
                                    // 这样可以确保所有操作都是线程安全的
                                    if (image_save_callback_(frame, save_directory_, point.row, point.col)) {
                                        saved_images_count_++;
                                        image_saved = true;
                                    } else {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                                    }
                                } catch (const std::exception& e) {
                                    std::cerr << "SMovementController: Error saving image: " << e.what() << std::endl;
                                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                                }
                            } else {
                                break;
                            }
                        }
                    }
                    
                    if (!image_saved) {
                        updateStatus("Failed to capture and save image at point " + std::to_string(i + 1) + " after multiple attempts, skipping to next point");
                        continue;
                    }
                    
                    // 最小化停留时间，只需要足够的时间保存图像
                    auto stay_start = std::chrono::steady_clock::now();
                    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - stay_start).count() < 0.1) {
                        // 首先检查是否已经被停止
                        if (stop_requested_) {
                            std::cout << "SMovementController: Movement stopped by user" << std::endl;
                            break;
                        }
                        
                        // 检查是否暂停
                        while (paused_ && !stop_requested_) {
                            std::cout << "SMovementController: Movement paused" << std::endl;
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                        if (stop_requested_) {
                            break;
                        }
                        
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }
                
                // 检查是否暂停
                while (paused_ && !stop_requested_) {
                    std::cout << "SMovementController: Movement paused" << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                // Wait before next move (minimized to 20ms)
                std::cout << "SMovementController: Waiting 20ms before next move" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } catch (const std::exception& e) {
                std::cerr << "SMovementController: Error processing point " << i + 1 << ": " << e.what() << std::endl;
                updateStatus("Error processing point " + std::to_string(i + 1) + ", skipping to next point");
                // 继续处理下一个点
                continue;
            }
        }
        
        if (stop_requested_) {
            updateStatus("Movement stopped by user");
            std::cout << "SMovementController: Movement stopped by user" << std::endl;
        } else {
            updateStatus("Movement completed successfully");
            std::cout << "SMovementController: Movement completed successfully" << std::endl;
            std::cout << "SMovementController: Saved " << saved_images_count_ << " images out of " << movement_path_.size() << " points" << std::endl;
        }
        
        updateAction("Idle");
        
    } catch (const std::exception& e) {
        std::cerr << "Error in movement thread: " << e.what() << std::endl;
        updateStatus(std::string("Movement error: ") + e.what());
        updateAction("Error");
    } catch (...) {
        std::cerr << "Unknown error in movement thread" << std::endl;
        updateStatus("Unknown movement error");
        updateAction("Error");
    }
    
    // Update final status
    running_ = false;
    current_status_.running = false;
    current_status_.paused = false;
    std::cout << "SMovementController: Exiting movement thread" << std::endl;
}

bool SMovementController::moveToPoint(const SMovementPoint& point) {
    try {
        std::cout << "SMovementController: Moving to point (" << point.x << ", " << point.y << ", " << point.z << ")" << std::endl;
        
        // 检查机械臂连接状态
        if (!arm_controller_.isConnected()) {
            std::cerr << "SMovementController: Arm is not connected" << std::endl;
            return false;
        }
        
        // 直接向机械臂发送固定点命令
        std::cout << "SMovementController: Sending fixed position to arm" << std::endl;
        
        // 按照顺序发送各轴位置命令
        // 先发送X轴
        std::cout << "SMovementController: Sending X axis position: " << point.x << std::endl;
        bool x_ok = arm_controller_.moveToPosition(0, point.x);
        
        // 再发送Y轴
        std::cout << "SMovementController: Sending Y axis position: " << point.y << std::endl;
        bool y_ok = arm_controller_.moveToPosition(1, point.y);
        
        // 再发送Z轴
        std::cout << "SMovementController: Sending Z axis position: " << point.z << std::endl;
        bool z_ok = arm_controller_.moveToPosition(2, point.z);
        
        // 再发送A轴
        std::cout << "SMovementController: Sending A axis position: " << point.a << std::endl;
        bool a_ok = arm_controller_.moveToPosition(3, point.a);
        
        // 最后发送B轴
        std::cout << "SMovementController: Sending B axis position: " << point.b << std::endl;
        bool b_ok = arm_controller_.moveToPosition(4, point.b);
        
        if (!x_ok || !y_ok || !z_ok || !a_ok || !b_ok) {
            std::cerr << "SMovementController: Failed to send position to arm" << std::endl;
            std::cerr << "SMovementController: X: " << (x_ok ? "OK" : "FAILED") << ", Y: " << (y_ok ? "OK" : "FAILED") << ", Z: " << (z_ok ? "OK" : "FAILED") << ", A: " << (a_ok ? "OK" : "FAILED") << ", B: " << (b_ok ? "OK" : "FAILED") << std::endl;
            return false;
        }
        
        std::cout << "SMovementController: All target positions sent successfully" << std::endl;
        
        // 最小化等待时间，确保机械臂到达目标位置
        std::cout << "SMovementController: Waiting for arm to reach position..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 最小化等待时间，确保机械臂到达位置
        
        // 验证机械臂是否到达目标位置
        double current_x = arm_controller_.readPosition(0);
        double current_y = arm_controller_.readPosition(1);
        double current_z = arm_controller_.readPosition(2);
        double current_a = arm_controller_.readPosition(3);
        double current_b = arm_controller_.readPosition(4);
        
        std::cout << "SMovementController: Current position: (" << current_x << ", " << current_y << ", " << current_z << ", " << current_a << ", " << current_b << ")" << std::endl;
        
        // 检查位置是否在合理范围内
        const double tolerance = 10.0; // 位置容差
        bool position_reached = true;
        
        if (std::abs(current_x - point.x) > tolerance) {
            std::cerr << "SMovementController: X axis did not reach target position" << std::endl;
            position_reached = false;
        }
        if (std::abs(current_y - point.y) > tolerance) {
            std::cerr << "SMovementController: Y axis did not reach target position" << std::endl;
            position_reached = false;
        }
        if (std::abs(current_z - point.z) > tolerance) {
            std::cerr << "SMovementController: Z axis did not reach target position" << std::endl;
            position_reached = false;
        }
        if (std::abs(current_a - point.a) > tolerance) {
            std::cerr << "SMovementController: A axis did not reach target position" << std::endl;
            position_reached = false;
        }
        if (std::abs(current_b - point.b) > tolerance) {
            std::cerr << "SMovementController: B axis did not reach target position" << std::endl;
            position_reached = false;
        }
        
        if (!position_reached) {
            std::cerr << "SMovementController: Arm did not reach target position" << std::endl;
            std::cerr << "SMovementController: Target: (" << point.x << ", " << point.y << ", " << point.z << ", " << point.a << ", " << point.b << ")" << std::endl;
            std::cerr << "SMovementController: Current: (" << current_x << ", " << current_y << ", " << current_z << ", " << current_a << ", " << current_b << ")" << std::endl;
            return false;
        }
        
        std::cout << "SMovementController: Successfully moved to point" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error moving to point: " << e.what() << std::endl;
        return false;
    }
}

bool SMovementController::captureImageAtPosition() {
    if (!image_capture_callback_) {
        return false;
    }
    
    try {
        cv::Mat frame;
        return image_capture_callback_(frame);
    } catch (const std::exception& e) {
        std::cerr << "Error capturing image: " << e.what() << std::endl;
        return false;
    }
}

bool SMovementController::saveCapturedImage(const cv::Mat& image, const SMovementPoint& point) {
    if (!image_save_callback_ || save_directory_.empty()) {
        return false;
    }
    
    try {
        std::string filename = std::to_string(point.row) + "_" + std::to_string(point.col) + ".jpg";
        
        return image_save_callback_(image, save_directory_, point.row, point.col);
    } catch (const std::exception& e) {
        std::cerr << "Error saving image: " << e.what() << std::endl;
        return false;
    }
}

void SMovementController::updateStatus(const std::string& status_message) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_status_.status_message = status_message;
    
    if (status_callback_) {
        status_callback_(current_status_);
    }
}

void SMovementController::updateAction(const std::string& action) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_status_.current_action = action;
    
    if (status_callback_) {
        status_callback_(current_status_);
    }
}

double SMovementController::computeSphericalCapAverageHeight(int cell_x, int cell_y, int grid_width, int grid_height,
                                                             double x_start, double x_end, double y_start, double y_end) {
    if (spherical_cap_radius_ <= 0 || spherical_cap_height_ <= 0) {
        return 0.0;
    }
    
    double grid_center_x = (x_start + x_end) / 2.0;
    double grid_center_y = (y_start + y_end) / 2.0;
    
    double cell_width = (x_end - x_start) / (grid_width - 1);
    double cell_height = (y_end - y_start) / (grid_height - 1);
    
    double cell_center_x = x_start + cell_x * cell_width;
    double cell_center_y = y_start + cell_y * cell_height;
    
    double dx = cell_center_x - grid_center_x;
    double dy = cell_center_y - grid_center_y;
    double dist_sq = dx * dx + dy * dy;
    
    double R = spherical_cap_radius_;
    double h = spherical_cap_height_;
    
    double sphere_radius = (R * R + h * h) / (2.0 * h);
    double cap_base_radius_sq = R * R;
    
    if (dist_sq > cap_base_radius_sq) {
        return 0.0;
    }
    
    double cell_radius = (cell_width < cell_height ? cell_width : cell_height) / 2.0;
    
    const int sample_count = 5;
    double total_height = 0.0;
    int valid_samples = 0;
    
    for (int i = -sample_count; i <= sample_count; ++i) {
        for (int j = -sample_count; j <= sample_count; ++j) {
            double sample_x = cell_center_x + i * cell_radius / sample_count;
            double sample_y = cell_center_y + j * cell_radius / sample_count;
            
            double sample_dx = sample_x - grid_center_x;
            double sample_dy = sample_y - grid_center_y;
            double sample_dist_sq = sample_dx * sample_dx + sample_dy * sample_dy;
            
            if (sample_dist_sq <= cap_base_radius_sq) {
                double z = h - (sphere_radius - std::sqrt(sphere_radius * sphere_radius - sample_dist_sq));
                total_height += z;
                valid_samples++;
            }
        }
    }
    
    if (valid_samples == 0) {
        return 0.0;
    }
    
    return total_height / valid_samples;
}

} // namespace arm
