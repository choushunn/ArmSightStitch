#pragma once

#include <vector>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <opencv2/opencv.hpp>

#include "ModbusArmController.h"

namespace arm {

struct SMovementPoint {
    int32_t x; // X axis position
    int32_t y; // Y axis position
    int32_t z; // Z axis position
    int32_t a; // A axis position
    int32_t b; // B axis position
    int row;   // Grid row index
    int col;   // Grid column index
};

struct SMovementStatus {
    bool running;           // Whether S movement is running
    bool paused;            // Whether S movement is paused
    int current_point;      // Current point index
    int total_points;       // Total number of points
    std::string status_message; // Status message
    std::string current_action;  // Current action
    SMovementPoint current_position; // Current position
};

class SMovementController {
public:
    SMovementController(ModbusArmController& arm_controller);
    ~SMovementController();
    
    void setSphericalCapParameters(int radius, int height, int delta_h);
    int getSphericalCapRadius() const { return spherical_cap_radius_; }
    int getSphericalCapHeight() const { return spherical_cap_height_; }
    int getDeltaH() const { return delta_h_; }

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
     * @brief Initialize S movement with grid parameters and boundary info for spherical cap
     * @param grid_size Grid size (width, height)
     * @param start_pos Start position
     * @param end_pos End position
     * @param x_start X boundary start
     * @param x_end X boundary end
     * @param y_start Y boundary start
     * @param y_end Y boundary end
     * @param step_size Step size between grid points
     * @return true if initialized successfully, false otherwise
     */
    bool initialize(const cv::Size& grid_size, 
                   const SMovementPoint& start_pos, 
                   const SMovementPoint& end_pos,
                   double x_start, double x_end, double y_start, double y_end,
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
    void setImageSaveCallback(std::function<bool(const cv::Mat&, const std::string&, int, int)> callback);

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
     * @brief Get saved images count
     * @return Number of saved images
     */
    int getSavedImagesCount() const;

private:
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
     * @brief Generate fixed point S movement path with spherical cap calculation
     * @param grid_size Grid size (width, height)
     * @param start_pos Start position
     * @param end_pos End position
     * @param x_start_boundary X boundary start
     * @param x_end_boundary X boundary end
     * @param y_start_boundary Y boundary start
     * @param y_end_boundary Y boundary end
     * @return Vector of movement points
     */
    std::vector<SMovementPoint> generateFixedPointSMovementPath(const cv::Size& grid_size, 
                                                             const SMovementPoint& start_pos, 
                                                             const SMovementPoint& end_pos,
                                                             double x_start_boundary, double x_end_boundary,
                                                             double y_start_boundary, double y_end_boundary);

    /**
     * @brief Movement thread function
     */
    void movementThread();

    /**
     * @brief Move to a single point
     * @param point Target point
     * @return true if moved successfully, false otherwise
     */
    bool moveToPoint(const SMovementPoint& point);

    /**
     * @brief Capture image at current position
     * @return true if captured successfully, false otherwise
     */
    bool captureImageAtPosition();

    /**
     * @brief Save captured image
     * @param image Captured image
     * @param point Current point
     * @return true if saved successfully, false otherwise
     */
    bool saveCapturedImage(const cv::Mat& image, const SMovementPoint& point);

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

private:
    ModbusArmController& arm_controller_;
    std::vector<SMovementPoint> movement_path_;
    std::thread movement_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    std::atomic<bool> stop_requested_;
    std::atomic<int> current_point_index_;
    std::atomic<int> run_number_;
    std::atomic<int> movement_speed_;
    SMovementStatus current_status_;
    std::string save_directory_;
    int saved_images_count_;
    std::mutex status_mutex_;
    std::mutex path_mutex_;
    
    int spherical_cap_radius_ = 230000;
    int spherical_cap_height_ = 50000;
    int delta_h_ = 0;
    int z_base_height_ = 80000;
    
    double computeSphericalCapAverageHeight(int cell_x, int cell_y, int grid_width, int grid_height, 
                                           double x_start, double x_end, double y_start, double y_end);
    
    // Callbacks
    std::function<bool(cv::Mat&)> image_capture_callback_;
    std::function<bool(const cv::Mat&, const std::string&, int, int)> image_save_callback_;
    std::function<void(const SMovementStatus&)> status_callback_;
};

} // namespace arm
