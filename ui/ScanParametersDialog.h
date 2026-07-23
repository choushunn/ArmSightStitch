#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

/// 扫描参数设置对话框 — 设置扫描时每个位置的停留时间等参数
class ScanParametersDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScanParametersDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("扫描参数设置");
        setMinimumWidth(320);
        auto* form = new QFormLayout(this);

        step_size_ = new QSpinBox(this);
        step_size_->setRange(100, 999999);
        step_size_->setSuffix(" 脉冲");
        step_size_->setToolTip("相邻栅格点之间的位移量（脉冲数），决定扫描覆盖密度");
        form->addRow("步进步长:", step_size_);

        dwell_ms_ = new QSpinBox(this);
        dwell_ms_->setRange(0, 5000);
        dwell_ms_->setSuffix(" ms");
        dwell_ms_->setToolTip("每个扫描位置到达后的停留时间（毫秒），用于等待机械臂稳定");
        form->addRow("位置停留时间:", dwell_ms_);

        auto* btn = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(btn, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btn, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(btn);
    }

    int stepSize() const { return step_size_->value(); }
    void setStepSize(int v) { step_size_->setValue(v); }

    int dwellTimeMs() const { return dwell_ms_->value(); }
    void setDwellTimeMs(int v) { dwell_ms_->setValue(v); }

private:
    QSpinBox* step_size_;
    QSpinBox* dwell_ms_;
};
