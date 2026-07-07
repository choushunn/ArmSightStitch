#pragma once

#include <QDialog>
#include "ui_DetectionSettingsDialog.h"

namespace detector {
class IDetector;
}

class DetectionSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DetectionSettingsDialog(detector::IDetector* detector, QWidget* parent = nullptr);

    QString paramPath() const;
    QString binPath() const;
    double confidenceThreshold() const;
    double nmsThreshold() const;

    void setParamPath(const QString& path);
    void setBinPath(const QString& path);
    void setConfidenceThreshold(double val);
    void setNmsThreshold(double val);

private slots:
    void onBrowseParam();
    void onBrowseBin();
    void onBrowseImage();
    void onDetectAndSave();

private:
    Ui::DetectionSettingsDialog ui;
    detector::IDetector* detector_ = nullptr;
};
