#pragma once

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QPixmap>

#include <opencv2/opencv.hpp>

#include "core/stitch/ImageStitcher.h"

class AppController;
class QPushButton;
namespace Ui { class MainWindow; }

class StitchController : public QObject {
    Q_OBJECT

public:
    explicit StitchController(Ui::MainWindow* ui, AppController& ctrl,
                              QObject* parent = nullptr);
    ~StitchController() override;

public slots:
    void onStitchRun();
    void onStitchingFinished();
    void onStitchingFinishedExternal(const cv::Mat& result);
    void onStitchSettings();
    void onSaveStitch();
    void onNegativeStitchingFinished();

signals:
    void logMessage(const QString& msg, const QString& level = QStringLiteral("INFO"));
    void stitchingComplete(const cv::Mat& result);
    void negativeStitchingComplete(const cv::Mat& negResult);

public:
    // Shared state accessors for MainWindow
    cv::Mat& stitchedResult() { return stitched_result_; }
    cv::Mat& stitchedResultNegative() { return stitched_result_negative_; }
    const std::string& lastScanDir() const { return last_scan_dir_; }
    void setLastScanDir(const std::string& dir) { last_scan_dir_ = dir; }
    QPushButton* negativeToggleBtn() { return negative_toggle_btn_; }
    void setNegativeToggleBtn(QPushButton* btn) { negative_toggle_btn_ = btn; }
    bool stitchShowingNegative() const { return stitch_showing_negative_; }
    void setStitchShowingNegative(bool v) { stitch_showing_negative_ = v; }
    QPixmap& stitchedPixmap() { return stitched_pixmap_; }
    const QPixmap& stitchedPixmap() const { return stitched_pixmap_; }

    // Stitcher access for progress callbacks
    QProgressDialog* stitchProgressDlg() { return stitch_progress_dlg_; }

    // Negative helper (needed by external signal handlers)
    bool hasNegativeImages(const std::string& directory) const;

private:
    void startNegativeStitching(const std::string& directory, const cv::Size& grid_size);

    Ui::MainWindow* ui_;
    AppController& ctrl_;

    // Stitch results
    cv::Mat stitched_result_;
    cv::Mat stitched_result_negative_;
    QPixmap stitched_pixmap_;
    std::string last_scan_dir_;

    // Negative toggle button
    QPushButton* negative_toggle_btn_ = nullptr;
    bool stitch_showing_negative_ = false;

    // Two-phase stitching — inter-stage staging of load results
    std::vector<stitch::PositionedImage> stitch_positioned_;
    std::vector<cv::Mat> stitch_raw_images_;
    cv::Size stitch_load_grid_{0, 0};
    int stitch_load_count_ = 0;

    QProgressDialog* stitch_progress_dlg_ = nullptr;

    QFuture<cv::Mat> stitching_future_;
    QFutureWatcher<cv::Mat> stitching_watcher_;
    QFuture<cv::Mat> negative_stitching_future_;
    QFutureWatcher<cv::Mat> negative_stitching_watcher_;

    // Flag for external result injection
    bool use_external_result_ = false;
};
