#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>

class QPushButton;

class YoloParamsWidget : public QWidget {
    Q_OBJECT
public:
    explicit YoloParamsWidget(QWidget* parent = nullptr);

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
    QLineEdit* paramPathEdit_ = nullptr;
    QLineEdit* binPathEdit_ = nullptr;
    QDoubleSpinBox* confidenceSpin_ = nullptr;
    QDoubleSpinBox* nmsSpin_ = nullptr;
};
