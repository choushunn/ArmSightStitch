#pragma once

#include <QDialog>
#include "ui_DetectionSettingsDialog.h"

class DetectionSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DetectionSettingsDialog(QWidget* parent = nullptr);

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

private:
    Ui::DetectionSettingsDialog ui;
};
