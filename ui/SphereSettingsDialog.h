#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

class SphereSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SphereSettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("球冠参数设置");
        setMinimumWidth(320);
        auto* form = new QFormLayout(this);

        radius_ = new QSpinBox(this);
        radius_->setRange(1, 9999999);
        form->addRow("球冠半径 R (脉冲数):", radius_);

        cap_h_ = new QSpinBox(this);
        cap_h_->setRange(0, 9999999);
        form->addRow("球冠高度 h (脉冲数):", cap_h_);

        dH_ = new QSpinBox(this);
        dH_->setRange(-9999999, 9999999);
        form->addRow("高度偏移 dH (脉冲数):", dH_);

        zBase_ = new QSpinBox(this);
        zBase_->setRange(0, 9999999);
        form->addRow("Z 基准高度 (脉冲数):", zBase_);

        auto* btn = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(btn, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btn, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(btn);
    }

    int sphereRadius() const { return radius_->value(); }
    int sphereCapHeight() const { return cap_h_->value(); }
    int sphereHeightOffset() const { return dH_->value(); }
    int zBaseHeight() const { return zBase_->value(); }

    void setSphereRadius(int v) { radius_->setValue(v); }
    void setSphereCapHeight(int v) { cap_h_->setValue(v); }
    void setSphereHeightOffset(int v) { dH_->setValue(v); }
    void setZBaseHeight(int v) { zBase_->setValue(v); }

private:
    QSpinBox* radius_;
    QSpinBox* cap_h_;
    QSpinBox* dH_;
    QSpinBox* zBase_;
};
