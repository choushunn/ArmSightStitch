#include "YoloParamsWidget.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

YoloParamsWidget::YoloParamsWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    // ── Model file group ──
    auto* modelGroup = new QGroupBox("模型文件", this);
    auto* modelLayout = new QGridLayout(modelGroup);
    modelLayout->setHorizontalSpacing(4);
    modelLayout->setVerticalSpacing(4);
    modelLayout->setContentsMargins(4, 4, 4, 4);

    // Param path row
    auto* labelParam = new QLabel("Param 路径:", modelGroup);
    paramPathEdit_ = new QLineEdit(modelGroup);
    paramPathEdit_->setPlaceholderText("请选择 .param 模型文件");
    auto* browseParamBtn = new QPushButton("...", modelGroup);
    browseParamBtn->setMaximumWidth(36);
    modelLayout->addWidget(labelParam, 0, 0);
    modelLayout->addWidget(paramPathEdit_, 0, 1);
    modelLayout->addWidget(browseParamBtn, 0, 2);

    // Bin path row
    auto* labelBin = new QLabel("Bin 路径:", modelGroup);
    binPathEdit_ = new QLineEdit(modelGroup);
    binPathEdit_->setPlaceholderText("请选择 .bin 模型文件");
    auto* browseBinBtn = new QPushButton("...", modelGroup);
    browseBinBtn->setMaximumWidth(36);
    modelLayout->addWidget(labelBin, 1, 0);
    modelLayout->addWidget(binPathEdit_, 1, 1);
    modelLayout->addWidget(browseBinBtn, 1, 2);

    mainLayout->addWidget(modelGroup);

    // ── Thresholds group ──
    auto* thresholdsGroup = new QGroupBox("检测参数", this);
    auto* thresholdsLayout = new QFormLayout(thresholdsGroup);
    thresholdsLayout->setVerticalSpacing(6);
    thresholdsLayout->setHorizontalSpacing(4);
    thresholdsLayout->setContentsMargins(4, 4, 4, 4);

    confidenceSpin_ = new QDoubleSpinBox(thresholdsGroup);
    confidenceSpin_->setRange(0.0, 1.0);
    confidenceSpin_->setSingleStep(0.05);
    confidenceSpin_->setValue(0.3);
    thresholdsLayout->addRow("置信度阈值:", confidenceSpin_);

    nmsSpin_ = new QDoubleSpinBox(thresholdsGroup);
    nmsSpin_->setRange(0.0, 1.0);
    nmsSpin_->setSingleStep(0.05);
    nmsSpin_->setValue(0.3);
    thresholdsLayout->addRow("NMS 阈值:", nmsSpin_);

    mainLayout->addWidget(thresholdsGroup);
    mainLayout->addStretch();

    connect(browseParamBtn, &QPushButton::clicked, this, &YoloParamsWidget::onBrowseParam);
    connect(browseBinBtn, &QPushButton::clicked, this, &YoloParamsWidget::onBrowseBin);
}

QString YoloParamsWidget::paramPath() const { return paramPathEdit_->text(); }
QString YoloParamsWidget::binPath() const { return binPathEdit_->text(); }
double YoloParamsWidget::confidenceThreshold() const { return confidenceSpin_->value(); }
double YoloParamsWidget::nmsThreshold() const { return nmsSpin_->value(); }

void YoloParamsWidget::setParamPath(const QString& path) { paramPathEdit_->setText(path); }
void YoloParamsWidget::setBinPath(const QString& path) { binPathEdit_->setText(path); }
void YoloParamsWidget::setConfidenceThreshold(double val) { confidenceSpin_->setValue(val); }
void YoloParamsWidget::setNmsThreshold(double val) { nmsSpin_->setValue(val); }

void YoloParamsWidget::onBrowseParam()
{
    QString cur = paramPathEdit_->text();
    QString dir = cur.isEmpty() ? QCoreApplication::applicationDirPath()
                                : QFileInfo(cur).absolutePath();
    QString path = QFileDialog::getOpenFileName(this, "选择 Param 文件", dir,
        "NCNN Param (*.param);;所有文件 (*.*)");
    if (!path.isEmpty())
        paramPathEdit_->setText(path);
}

void YoloParamsWidget::onBrowseBin()
{
    QString cur = binPathEdit_->text();
    QString dir = cur.isEmpty() ? QCoreApplication::applicationDirPath()
                                : QFileInfo(cur).absolutePath();
    QString path = QFileDialog::getOpenFileName(this, "选择 Bin 文件", dir,
        "NCNN Bin (*.bin);;所有文件 (*.*)");
    if (!path.isEmpty())
        binPathEdit_->setText(path);
}
