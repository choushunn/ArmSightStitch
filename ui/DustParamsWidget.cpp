#include "DustParamsWidget.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

DustParamsWidget::DustParamsWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    auto* group = new QGroupBox("灰尘检测参数", this);
    auto* form = new QFormLayout(group);
    form->setVerticalSpacing(6);
    form->setHorizontalSpacing(4);
    form->setContentsMargins(4, 4, 4, 4);

    claheSpin_ = new QDoubleSpinBox(group);
    claheSpin_->setRange(0.5, 20.0);
    claheSpin_->setSingleStep(0.5);
    claheSpin_->setValue(2.0);
    form->addRow("CLAHE 对比度:", claheSpin_);

    bgBlurSpin_ = new QSpinBox(group);
    bgBlurSpin_->setRange(3, 199);
    bgBlurSpin_->setSingleStep(2);
    bgBlurSpin_->setValue(31);
    form->addRow("背景核(奇数):", bgBlurSpin_);

    minAreaSpin_ = new QSpinBox(group);
    minAreaSpin_->setRange(0, 100000);
    minAreaSpin_->setValue(50);
    form->addRow("最小面积(px):", minAreaSpin_);

    maxAreaSpin_ = new QSpinBox(group);
    maxAreaSpin_->setRange(0, 2000000);
    maxAreaSpin_->setValue(20000);
    form->addRow("最大面积(0=不限):", maxAreaSpin_);

    dilateSpin_ = new QSpinBox(group);
    dilateSpin_->setRange(0, 10);
    dilateSpin_->setValue(0);
    form->addRow("膨胀次数:", dilateSpin_);

    maxIterSpin_ = new QSpinBox(group);
    maxIterSpin_->setRange(1, 20);
    maxIterSpin_->setValue(4);
    form->addRow("最大迭代次数:", maxIterSpin_);

    nmsIouSpin_ = new QDoubleSpinBox(group);
    nmsIouSpin_->setRange(0.0, 1.0);
    nmsIouSpin_->setSingleStep(0.05);
    nmsIouSpin_->setValue(0.1);
    form->addRow("NMS 合并(0=关闭):", nmsIouSpin_);

    mainLayout->addWidget(group);
    mainLayout->addStretch();
}

detector::DustDetectionParams DustParamsWidget::dustParams() const
{
    detector::DustDetectionParams p;
    p.claheClip  = claheSpin_->value();
    p.bgBlurSize = bgBlurSpin_->value() | 1;  // force odd kernel
    p.minArea    = minAreaSpin_->value();
    p.maxArea    = maxAreaSpin_->value();
    p.dilateIter = dilateSpin_->value();
    p.maxIter    = maxIterSpin_->value();
    p.nmsIou     = nmsIouSpin_->value();
    return p;
}

void DustParamsWidget::setDustParams(const detector::DustDetectionParams& p)
{
    claheSpin_->setValue(p.claheClip);
    bgBlurSpin_->setValue(p.bgBlurSize);
    minAreaSpin_->setValue(p.minArea);
    maxAreaSpin_->setValue(p.maxArea);
    dilateSpin_->setValue(p.dilateIter);
    maxIterSpin_->setValue(p.maxIter);
    nmsIouSpin_->setValue(p.nmsIou);
}
