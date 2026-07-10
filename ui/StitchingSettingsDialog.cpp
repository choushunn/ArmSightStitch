#include "StitchingSettingsDialog.h"

StitchingSettingsDialog::StitchingSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    ui.setupUi(this);
}

int StitchingSettingsDialog::gridSizeX() const { return ui.gridXSpin->value(); }
int StitchingSettingsDialog::gridSizeY() const { return ui.gridYSpin->value(); }
int StitchingSettingsDialog::centerCropSize() const { return ui.centerCropSpin->value(); }
int StitchingSettingsDialog::stitchAlgorithm() const { return ui.algorithmCombo->currentIndex(); }
int StitchingSettingsDialog::featherWidth() const { return ui.featherWidthSpin->value(); }
QString StitchingSettingsDialog::inputDir() const { return ui.inputDirEdit->text(); }

void StitchingSettingsDialog::setGridSizeX(int val) { ui.gridXSpin->setValue(val); }
void StitchingSettingsDialog::setGridSizeY(int val) { ui.gridYSpin->setValue(val); }
void StitchingSettingsDialog::setCenterCropSize(int val) { ui.centerCropSpin->setValue(val); }
void StitchingSettingsDialog::setStitchAlgorithm(int algo) { ui.algorithmCombo->setCurrentIndex(algo); }
void StitchingSettingsDialog::setFeatherWidth(int val) { ui.featherWidthSpin->setValue(val); }
void StitchingSettingsDialog::setInputDir(const QString& dir) { ui.inputDirEdit->setText(dir); }
