#include "SMovementController.h"
#include "core/arm/ArmUtils.h"

#include <spdlog/spdlog.h>
#include <cmath>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <ctime>

namespace arm {

SMovementController::SMovementController(IArmController& arm_controller) 
    : arm_controller_(arm_controller),
      running_(false),
      paused_(false),
      stop_requested_(false),
      current_point_index_(0),
      run_number_(0),
      movement_speed_(26000), // 默认速度设置为26000
      saved_images_count_(0) {
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

SMovementController::~SMovementController() {
    stop();
    // Ensure save thread is joined
    save_thread_running_ = false;
    save_queue_cv_.notify_all();
    if (save_thread_.joinable()) {
        save_thread_.join();
    }
}

bool SMovementController::initialize(const cv::Size& grid_size, 
                                   const SMovementPoint& start_pos, 
                                   const SMovementPoint& end_pos, 
                                   int step_size) {
    try {
        // 直接使用固定点的方式实现S型运动
        // 生成S型路径的固定点
        movement_path_ = generateFixedPointSMovementPath(grid_size, start_pos, end_pos);
        
        if (movement_path_.empty()) {
            updateStatus("Failed to generate movement path");
            return false;
        }
        
        // Update status
        current_status_.total_points = static_cast<int>(movement_path_.size());
        current_status_.current_point = 0;
        
        updateStatus("Initialized successfully");
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error initializing S-movement controller: {}", e.what());
        updateStatus(std::string("Initialization error: ") + e.what());
        return false;
    }
}

bool SMovementController::start() {
    if (running_ || movement_path_.empty()) {
        return false;
    }

    // Clean up any previous threads (e.g. after natural completion)
    if (movement_thread_.joinable()) movement_thread_.join();
    if (save_thread_.joinable()) save_thread_.join();

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
        
        // Stop auto-read during scanning to avoid modbus_mutex_ contention
        arm_controller_.stopAutoRead();

        // Start movement thread
        movement_thread_ = std::thread(&SMovementController::movementThread, this);

        // Start background save consumer thread
        save_thread_running_ = true;
        save_thread_ = std::thread(&SMovementController::saveConsumerThread, this);

        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error starting S-movement: {}", e.what());
        updateStatus(std::string("Failed to start: ") + e.what());
        running_ = false;
        arm_controller_.startAutoRead(); // restore auto-read on failure
        return false;
    }
}

void SMovementController::stop() {
    stop_requested_ = true;
    running_ = false;
    paused_ = false;
    current_status_.running = false;
    current_status_.paused = false;

    if (movement_thread_.joinable()) {
        movement_thread_.join();
    } else {
        // Movement thread never started but auto-read was stopped in start()
        arm_controller_.startAutoRead();
    }

    // Stop save thread and flush remaining items
    save_thread_running_ = false;
    save_queue_cv_.notify_all();
    if (save_thread_.joinable()) {
        save_thread_.join();
    }

    updateStatus("Movement stopped");
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
    
    if (!save_directory_.empty() && !std::filesystem::exists(save_directory_)) {
        std::filesystem::create_directories(save_directory_);
        SPDLOG_INFO("Created save directory: {}", save_directory_);
    }
}

void SMovementController::setPositionTolerance(double tolerance) {
    if (tolerance > 0) position_tolerance_ = tolerance;
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
    return saved_images_count_.load();
}

std::vector<SMovementPoint> SMovementController::generateFixedPointSMovementPath(const cv::Size& grid_size, 
                                                                              const SMovementPoint& start_pos, 
                                                                              const SMovementPoint& end_pos) {
    std::vector<SMovementPoint> path;
    
    try {
        // Calculate grid dimensions
        int width = grid_size.width;
        int height = grid_size.height;
        
        if (width <= 0 || height <= 0) {
            return path;
        }
        
        // Calculate step sizes between grid points
        double x_step = static_cast<double>(end_pos.x - start_pos.x) / (width - 1);
        double y_step = static_cast<double>(end_pos.y - start_pos.y) / (height - 1);
        
        SPDLOG_INFO("Grid size: {}x{}", width, height);
        SPDLOG_INFO("Start position: ({}, {})", start_pos.x, start_pos.y);
        SPDLOG_INFO("End position: ({}, {})", end_pos.x, end_pos.y);
        SPDLOG_INFO("Step sizes: X={}, Y={}", x_step, y_step);
        
        // Generate grid points in S-curve order (zig-zag pattern)
        for (int y_idx = 0; y_idx < height; ++y_idx) {
            for (int x_idx = 0; x_idx < width; ++x_idx) {
                SMovementPoint point;
                
                // Calculate X position (zig-zag pattern)
                int actual_x_idx = (y_idx % 2 == 0) ? x_idx : (width - 1 - x_idx);
                
                point.x = static_cast<int>(start_pos.x + actual_x_idx * x_step);
                point.y = static_cast<int>(start_pos.y + y_idx * y_step);
                point.z = start_pos.z; // Use same Z height for all points
                point.a = start_pos.a; // Use same A angle for all points
                point.b = start_pos.b; // Use same B angle for all points
                point.row = y_idx;
                point.col = actual_x_idx;
                
                path.push_back(point);
                SPDLOG_DEBUG("Added point ({}, {}) at row {}, col {}", point.x, point.y, point.row, point.col);
            }
        }
        
        SPDLOG_INFO("Generated {} points for S-movement", path.size());
        
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error generating S-movement path: {}", e.what());
    }
    
    return path;
}

void SMovementController::movementThread() {
    try {
        updateAction("Starting movement");
        SPDLOG_INFO("Starting S-movement thread");
        
        // Set movement speed for X/Y/Z only (A/B disabled)
        for (int axis = 0; axis < 3; ++axis) {
            try {
                arm_controller_.setSpeed(axis, movement_speed_);
                SPDLOG_INFO("Set speed for axis {} to {}", axis, movement_speed_.load());
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Error setting speed for axis {}: {}", axis, e.what());
            }
        }

        // ── One-time: move Z to scan height ──
        if (!movement_path_.empty()) {
            const auto& firstPt = movement_path_.front();
            SPDLOG_INFO("One-time Z move to {}", firstPt.z);
            arm_controller_.moveToPosition(2, firstPt.z);
        }

        // Process each point in the movement path
        for (size_t i = 0; i < movement_path_.size() && !stop_requested_; ++i) {
            try {
                if (stop_requested_) {
                    SPDLOG_DEBUG("Movement stopped by user");
                    break;
                }
                
                // Update current point index
                current_point_index_ = static_cast<int>(i);
                current_status_.current_point = current_point_index_;
                
                // Get current point
                const SMovementPoint& point = movement_path_[i];
                
                // Update current position
                current_status_.current_position = point;
                
                SPDLOG_INFO("Moving to point {} of {}", i + 1, movement_path_.size());
                updateAction("Moving");
                
                // 直接向机械臂发送固定点
                SPDLOG_DEBUG("Sending fixed point {} to arm: ({}, {}, {})", i + 1, point.x, point.y, point.z);
                
                bool all_ok = arm::moveXYAxes(arm_controller_, point.x, point.y);
                if (!all_ok) {
                    updateStatus("Failed to move to point " + std::to_string(i + 1) + ", skipping to next point");
                    SPDLOG_ERROR("Failed to move to point {}, skipping to next point", i + 1);
                    continue;
                }

                // moveToPosition() already confirmed X/Y arrival internally;
                // check stop/pause, then proceed directly to capture.
                if (stop_requested_) break;
                while (paused_ && !stop_requested_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (stop_requested_) break;

                SPDLOG_INFO("Arrived at point {}, capturing", i + 1);
                updateAction("Capturing");

                // 机械臂稳定等待时间
                std::this_thread::sleep_for(std::chrono::milliseconds(150));

                // 确认到达目标位置后，拍摄照片
                bool image_captured = false;
                cv::Mat frame;

                // 快速图像捕获，减少重试（captureSingleFrame内部已有3次重试）
                int capture_attempts = 0;
                const int max_capture_attempts = 3;

                while (!image_captured && capture_attempts < max_capture_attempts) {
                    capture_attempts++;
                    if (image_capture_callback_) {
                        try {
                            if (image_capture_callback_(frame)) {
                                image_captured = true;
                            } else {
                                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                            }
                        } catch (const std::exception& e) {
                            SPDLOG_ERROR("Error capturing image: {}", e.what());
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    } else {
                        break;
                    }
                }

                // 如果成功捕获图像，立即入队列（后台线程异步保存，不阻塞移动）
                if (image_captured && !save_directory_.empty()) {
                    SPDLOG_DEBUG("Queuing frame for async save at [{}, {}]", point.row, point.col);
                    {
                        std::lock_guard<std::mutex> lock(save_queue_mutex_);
                        save_queue_.push({frame.clone(), save_directory_, point.row, point.col});
                    }
                    save_queue_cv_.notify_one();
                    saved_images_count_++;
                } else if (!image_captured) {
                    updateStatus("Failed to capture image at point " + std::to_string(i + 1) + ", skipping");
                }

                // 最小化停留时间（仅检查暂停/停止）
                auto stay_start = std::chrono::steady_clock::now();
                while (std::chrono::duration<double>(std::chrono::steady_clock::now() - stay_start).count() < 0.02) {
                    // 首先检查是否已经被停止
                    if (stop_requested_) {
                        SPDLOG_DEBUG("Movement stopped by user");
                        break;
                    }

                    // 检查是否暂停
                    while (paused_ && !stop_requested_) {
                        SPDLOG_DEBUG("Movement paused");
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    if (stop_requested_) {
                        break;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                
                // 检查是否暂停
                while (paused_ && !stop_requested_) {
                    SPDLOG_DEBUG("Movement paused");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                // Minimal gap before next move
                SPDLOG_DEBUG("Waiting 5ms before next move");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Error processing point {}: {}", i + 1, e.what());
                updateStatus("Error processing point " + std::to_string(i + 1) + ", skipping to next point");
                // 继续处理下一个点
                continue;
            }
        }
        
        if (stop_requested_) {
            updateStatus("Movement stopped by user");
            SPDLOG_INFO("Movement stopped by user");
        } else {
            updateStatus("Movement completed successfully");
            SPDLOG_INFO("Movement completed successfully");
            SPDLOG_INFO("Saved {} images out of {} points", saved_images_count_.load(), movement_path_.size());
        }
        
        updateAction("Idle");
        
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error in movement thread: {}", e.what());
        updateStatus(std::string("Movement error: ") + e.what());
        updateAction("Error");
    } catch (...) {
        SPDLOG_ERROR("Unknown error in movement thread");
        updateStatus("Unknown movement error");
        updateAction("Error");
    }
    
    // Update final status
    running_ = false;
    current_status_.running = false;
    current_status_.paused = false;
    // Resume auto-read after scanning completes
    arm_controller_.startAutoRead();
    SPDLOG_INFO("Exiting movement thread");
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

void SMovementController::saveConsumerThread() {
    SPDLOG_INFO("Save consumer thread started");
    while (save_thread_running_) {
        FrameSaveTask task;
        {
            std::unique_lock<std::mutex> lock(save_queue_mutex_);
            save_queue_cv_.wait(lock, [this] {
                return !save_queue_.empty() || !save_thread_running_;
            });
            if (!save_thread_running_ && save_queue_.empty()) break;
            if (save_queue_.empty()) continue;
            task = std::move(save_queue_.front());
            save_queue_.pop();
        }

        // Process save in background (imwrite + thumbnail → UI)
        if (image_save_callback_) {
            try {
                image_save_callback_(task.frame, task.directory, task.row, task.col);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Save callback error for [{},{}]: {}", task.row, task.col, e.what());
            }
        }
    }
    SPDLOG_INFO("Save consumer thread exiting, {} items left in queue", save_queue_.size());
}

} // namespace arm
