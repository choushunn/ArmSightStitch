#pragma once

#include <vector>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <opencv2/opencv.hpp>

#include "IArmController.h"

namespace arm {

class SMovementController : public ISMovementController {
public:
    explicit SMovementController(IArmController& arm_controller);
    ~SMovementController();

    /**
     * @brief Initialize S movement with grid parameters
     * @param grid_size Grid size (width, height)
     * @param start_pos Start position
     * @param end_pos End position
     * @param step_size Step size between grid points
     * @return true if initialized successfully, false otherwise
     */
    bool initialize(const cv::Size& grid_size, 
                   const SMovementPoint& start_pos, 
                   const SMovementPoint& end_pos, 
                   int step_size = 10000);

    /**
     * @brief Start S movement
     * @return true if started successfully, false otherwise
     */
    bool start();

    /**
     * @brief Stop S movement
     */
    void stop();

    /**
     * @brief Pause S movement
     */
    void pause();

    /**
     * @brief Resume S movement
     */
    void resume();

    /**
     * @brief Set image capture callback
     * @param callback Callback function to be called when image capture is needed
     */
    void setImageCaptureCallback(std::function<bool(cv::Mat&)> callback);

    /**
     * @brief Set image save callback
     * @param callback Callback function to be called when image needs to be saved
     */
    void setImageSaveCallback(std::function<bool(const cv::Mat&, const std::string&, int, int, int)> callback);

    /**
     * @brief Set status callback
     * @param callback Callback function to be called when movement status changes
     */
    void setStatusCallback(std::function<void(const SMovementStatus&)> callback);

    /**
     * @brief Set save directory
     * @param save_dir Save directory path
     */
    void setSaveDirectory(const std::string& save_dir);
    void setPositionTolerance(double tolerance);

    /**
     * @brief Get current movement status
     * @return Movement status
     */
    SMovementStatus getStatus() const;

    /**
     * @brief Get S movement path
     * @return Vector of movement points
     */
    std::vector<SMovementPoint> getMovementPath() const;

    /**
     * @brief Set current run number
     * @param run_number Run number
     */
    void setRunNumber(int run_number);

    /**
     * @brief Set movement speed
     * @param speed Speed value
     */
    void setMovementSpeed(int speed);

    /**
     * @brief Set spherical cap compensation parameters
     * @param radius Base radius of spherical cap (pulses)
     * @param capHeight Height of spherical cap (pulses)
     * @param heightOffset Constant offset from cap surface (pulses)
     * @param zBase Ground plane Z height (pulses)
     */
    void setSphereParams(int radius, int capHeight, int heightOffset, int zBase);

    /**
     * @brief Set dwell time at each scan position
     * @param ms Dwell time in milliseconds (0–5000)
     */
    void setDwellTimeMs(int ms);

    /**
     * @brief Get saved images count
     * @return Number of saved images
     */
    int getSavedImagesCount() const;

private:
    struct FrameSaveTask {
        cv::Mat frame;
        std::string directory;
        int row;
        int col;
        int z = 0;
    };
    /**
     * @brief Generate fixed point S movement path
     * @param grid_size Grid size (width, height)
     * @param start_pos Start position
     * @param end_pos End position
     * @return Vector of movement points
     */
    std::vector<SMovementPoint> generateFixedPointSMovementPath(const cv::Size& grid_size, 
                                                             const SMovementPoint& start_pos, 
                                                             const SMovementPoint& end_pos);

    /**
     * @brief Movement thread function
     */
    void movementThread();

    /**
     * @brief Update movement status
     * @param status New status message
     */
    void updateStatus(const std::string& status);

    /**
     * @brief Update movement action
     * @param action New action description
     */
    void updateAction(const std::string& action);

    /**
     * @brief Consumer thread that saves queued frames to disk in the background
     */
    void saveConsumerThread();

    IArmController& arm_controller_;
    std::vector<SMovementPoint> movement_path_;
    std::thread movement_thread_;
    std::thread save_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    std::atomic<bool> stop_requested_;
    std::atomic<bool> save_thread_running_{false};

    // Async save queue
    std::queue<FrameSaveTask> save_queue_;
    std::mutex save_queue_mutex_;
    std::condition_variable save_queue_cv_;
    std::atomic<int> current_point_index_;
    std::atomic<int> run_number_;
    std::atomic<int> movement_speed_;
    SMovementStatus current_status_;
    std::string save_directory_;
    std::atomic<int> saved_images_count_{0};
    double position_tolerance_ = 100.0;
    std::mutex status_mutex_;
    std::mutex path_mutex_;

    // Spherical cap compensation parameters
    int sphere_radius_ = 230000;
    int sphere_cap_height_ = 50000;
    int sphere_height_offset_ = 0;
    int z_base_height_ = 80000;

    // Scan timing
    int dwell_time_ms_ = 20;
    
    // Callbacks
    std::function<bool(cv::Mat&)> image_capture_callback_;
    std::function<bool(const cv::Mat&, const std::string&, int, int, int)> image_save_callback_;
    std::function<void(const SMovementStatus&)> status_callback_;
};

} // namespace arm
