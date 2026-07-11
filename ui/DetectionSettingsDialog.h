#pragma once

#include <QDialog>
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

private:
    Ui::DetectionSettingsDialog ui;
    AppController* app_ = nullptr;
};
