#include "EdgeParamsWidget.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

EdgeParamsWidget::EdgeParamsWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    auto* group = new QGroupBox("Sobel边缘检测参数", this);
    auto* form = new QFormLayout(group);
    form->setVerticalSpacing(6);
    form->setHorizontalSpacing(4);
    form->setContentsMargins(4, 4, 4, 4);

    claheSpin_ = new QDoubleSpinBox(group);
    claheSpin_->setRange(0.5, 20.0);
    claheSpin_->setSingleStep(0.5);
    claheSpin_->setValue(2.0);
    form->addRow("CLAHE 对比度:", claheSpin_);

    tileSpin_ = new QSpinBox(group);
    tileSpin_->setRange(1, 64);
    tileSpin_->setSingleStep(2);
    tileSpin_->setValue(8);
    form->addRow("CLAHE 分块大小:", tileSpin_);

    edgeThreshSpin_ = new QSpinBox(group);
    edgeThreshSpin_->setRange(0, 255);
    edgeThreshSpin_->setValue(30);
    form->addRow("边缘阈值:", edgeThreshSpin_);

    sobelKSpin_ = new QSpinBox(group);
    sobelKSpin_->setRange(1, 7);
    sobelKSpin_->setSingleStep(2);
    sobelKSpin_->setValue(3);
    form->addRow("Sobel 核大小(奇数):", sobelKSpin_);

    dilateSpin_ = new QSpinBox(group);
    dilateSpin_->setRange(0, 10);
    dilateSpin_->setValue(1);
    form->addRow("膨胀次数:", dilateSpin_);

    minBboxSpin_ = new QSpinBox(group);
    minBboxSpin_->setRange(0, 100000);
    minBboxSpin_->setValue(25);
    form->addRow("最小框面积(px):", minBboxSpin_);

    nmsIouThreshSpin_ = new QDoubleSpinBox(group);
    nmsIouThreshSpin_->setRange(0.0, 1.0);
    nmsIouThreshSpin_->setSingleStep(0.05);
    nmsIouThreshSpin_->setValue(0.4);
    form->addRow("NMS IoU阈值:", nmsIouThreshSpin_);

    nmsContainThreshSpin_ = new QDoubleSpinBox(group);
    nmsContainThreshSpin_->setRange(0.0, 1.0);
    nmsContainThreshSpin_->setSingleStep(0.05);
    nmsContainThreshSpin_->setValue(0.5);
    form->addRow("NMS 包含阈值:", nmsContainThreshSpin_);

    mainLayout->addWidget(group);
    mainLayout->addStretch();
}

detector::EdgeDetectionParams EdgeParamsWidget::edgeParams() const
{
    detector::EdgeDetectionParams p;
    p.claheClip        = claheSpin_->value();
    p.claheTileGrid    = tileSpin_->value();
    p.edgeThreshold    = edgeThreshSpin_->value();
    p.sobelKSize       = sobelKSpin_->value() | 1;
    p.dilateIter       = dilateSpin_->value();
    p.minBboxArea      = minBboxSpin_->value();
    p.nmsIouThresh     = nmsIouThreshSpin_->value();
    p.nmsContainThresh = nmsContainThreshSpin_->value();
    return p;
}

void EdgeParamsWidget::setEdgeParams(const detector::EdgeDetectionParams& p)
{
    claheSpin_->setValue(p.claheClip);
    tileSpin_->setValue(p.claheTileGrid);
    edgeThreshSpin_->setValue(p.edgeThreshold);
    sobelKSpin_->setValue(p.sobelKSize);
    dilateSpin_->setValue(p.dilateIter);
    minBboxSpin_->setValue(p.minBboxArea);
    nmsIouThreshSpin_->setValue(p.nmsIouThresh);
    nmsContainThreshSpin_->setValue(p.nmsContainThresh);
}
