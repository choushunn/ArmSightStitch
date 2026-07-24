#pragma once

#include <QWidget>
#include "core/detector/DustDetectionParams.h"

class QDoubleSpinBox;
class QSpinBox;

class DustParamsWidget : public QWidget {
    Q_OBJECT
public:
    explicit DustParamsWidget(QWidget* parent = nullptr);

    detector::DustDetectionParams dustParams() const;
    void setDustParams(const detector::DustDetectionParams& params);

private:
    QDoubleSpinBox* claheSpin_ = nullptr;
    QSpinBox* bgBlurSpin_ = nullptr;
    QSpinBox* minAreaSpin_ = nullptr;
    QSpinBox* maxAreaSpin_ = nullptr;
    QSpinBox* dilateSpin_ = nullptr;
    QSpinBox* maxIterSpin_ = nullptr;
    QDoubleSpinBox* nmsIouSpin_ = nullptr;
};
