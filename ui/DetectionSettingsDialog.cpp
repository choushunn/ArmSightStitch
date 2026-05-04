#include "DetectionSettingsDialog.h"
#include <QFileDialog>
#include <QPushButton>

DetectionSettingsDialog::DetectionSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    ui.setupUi(this);

    connect(ui.browseParamButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onBrowseParam);
    connect(ui.browseBinButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onBrowseBin);
}

QString DetectionSettingsDialog::paramPath() const { return ui.paramPathEdit->text(); }
QString DetectionSettingsDialog::binPath() const { return ui.binPathEdit->text(); }
double DetectionSettingsDialog::confidenceThreshold() const { return ui.confidenceSpin->value(); }
double DetectionSettingsDialog::nmsThreshold() const { return ui.nmsSpin->value(); }

void DetectionSettingsDialog::setParamPath(const QString& path) { ui.paramPathEdit->setText(path); }
void DetectionSettingsDialog::setBinPath(const QString& path) { ui.binPathEdit->setText(path); }
void DetectionSettingsDialog::setConfidenceThreshold(double val) { ui.confidenceSpin->setValue(val); }
void DetectionSettingsDialog::setNmsThreshold(double val) { ui.nmsSpin->setValue(val); }

void DetectionSettingsDialog::onBrowseParam() {
    QString path = QFileDialog::getOpenFileName(this, "选择 Param 文件", ui.paramPathEdit->text(),
        "NCNN Param (*.param);;所有文件 (*.*)");
    if (!path.isEmpty()) ui.paramPathEdit->setText(path);
}

void DetectionSettingsDialog::onBrowseBin() {
    QString path = QFileDialog::getOpenFileName(this, "选择 Bin 文件", ui.binPathEdit->text(),
        "NCNN Bin (*.bin);;所有文件 (*.*)");
    if (!path.isEmpty()) ui.binPathEdit->setText(path);
}
