#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include "ui_DetectionSettingsDialog.h"
#include "core/detector/DustDetectionParams.h"

class AppController;

class DetectionSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DetectionSettingsDialog(AppController* app, QWidget* parent = nullptr);

    QString paramPath() const;
    QString binPath() const;
    double confidenceThreshold() const;
    double nmsThreshold() const;

    void setParamPath(const QString& path);
    void setBinPath(const QString& path);
    void setConfidenceThreshold(double val);
    void setNmsThreshold(double val);

    // Detection algorithm: 0=YOLO, 1=Dust
    int detectionAlgorithm() const;
    void setDetectionAlgorithm(int algo);

    // Dust detection parameters (bgBlur coerced to odd in the getter)
    detector::DustDetectionParams dustParams() const;
    void setDustParams(const detector::DustDetectionParams& params);

private slots:
    void onBrowseParam();
    void onBrowseBin();
    void onBrowseImage();
    void onDetectAndSave();
    void onAlgorithmChanged(int index);
    void onModeChanged(int index);

private:
    void runSingleDetection(const QString& imagePath, int algo,
                            const detector::DustDetectionParams& dp,
                            float conf, float nms);
    void runBatchDetection(const QString& folderPath, int algo,
                           const detector::DustDetectionParams& dp,
                           float conf, float nms);

    Ui::DetectionSettingsDialog ui;
    AppController* app_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QLabel* pathLabel_ = nullptr;
};
