#pragma once

#include <QWidget>
#include "core/detector/EdgeDetectionParams.h"

class QDoubleSpinBox;
class QSpinBox;

class EdgeParamsWidget : public QWidget {
    Q_OBJECT
public:
    explicit EdgeParamsWidget(QWidget* parent = nullptr);

    detector::EdgeDetectionParams edgeParams() const;
    void setEdgeParams(const detector::EdgeDetectionParams& params);

private:
    QDoubleSpinBox* claheSpin_ = nullptr;
    QSpinBox* tileSpin_ = nullptr;
    QSpinBox* edgeThreshSpin_ = nullptr;
    QSpinBox* sobelKSpin_ = nullptr;
    QSpinBox* dilateSpin_ = nullptr;
    QSpinBox* minBboxSpin_ = nullptr;
    QDoubleSpinBox* nmsIouThreshSpin_ = nullptr;
    QDoubleSpinBox* nmsContainThreshSpin_ = nullptr;
};
