#pragma once

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#include <opencv2/opencv.hpp>

#include "core/arm/SMovementController.h"

class AppController;
namespace Ui { class MainWindow; }

class ScanController : public QObject {
    Q_OBJECT

public:
    explicit ScanController(Ui::MainWindow* ui, AppController& ctrl,
                            QObject* parent = nullptr);
    ~ScanController() override = default;

public slots:
    void onToggleScanStartStop();
    void onTogglePauseScan();
    void onMovementStatus(const arm::SMovementStatus& status);
    void onZeroProgressFinished();

signals:
    void logMessage(const QString& msg, const QString& level = QStringLiteral("INFO"));
    void scanStarted();
    void scanFinished();
    void scanStopped();
    void scanProgress(int current, int total);

public:
    // Shared state accessors for MainWindow (const + non-const)
    std::map<std::pair<int,int>, std::string>& movementImages() { return s_movement_images_; }
    std::mutex& movementImagesMutex() { return s_movement_images_mutex_; }
    std::string& scanBaseDir() { return scan_base_dir_; }  // caller must lock scanStateMutex
    std::mutex& scanStateMutex() { return scan_state_mutex_; }
    std::atomic<bool>& scanShowingNegative() { return scan_showing_negative_; }
    std::set<std::pair<int,int>>& scannedCells() { return scanned_cells_; }
    const std::set<std::pair<int,int>>& scannedCells() const { return scanned_cells_; }
    int currentScanRow() const { return current_scan_row_; }
    int currentScanCol() const { return current_scan_col_; }
    std::map<std::pair<int,int>, std::pair<QPixmap,QPixmap>>& gridThumbnails() { return grid_thumbnails_; }
    std::mutex& gridThumbnailsMutex() { return grid_thumbnails_mutex_; }
    std::mutex& sMovementImagesMutex() { return s_movement_images_mutex_; }

private:
    void startScanSequence();
    void applyCellBorder(int row, int col);

    Ui::MainWindow* ui_;
    AppController& ctrl_;

    // Scan state
    std::map<std::pair<int, int>, std::string> s_movement_images_;
    mutable std::mutex s_movement_images_mutex_;
    std::map<std::pair<int,int>, std::pair<QPixmap, QPixmap>> grid_thumbnails_;
    mutable std::mutex grid_thumbnails_mutex_;
    std::string scan_base_dir_;
    mutable std::mutex scan_state_mutex_;
    std::atomic<bool> scan_showing_negative_{false};
    std::set<std::pair<int, int>> scanned_cells_;
    int current_scan_row_ = -1;
    int current_scan_col_ = -1;
    bool scanning_ = false;
    bool scan_was_running_ = false;
    bool scan_stopped_by_user_ = false;
};
