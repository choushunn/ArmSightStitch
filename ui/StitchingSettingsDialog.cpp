#include "StitchingSettingsDialog.h"

StitchingSettingsDialog::StitchingSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    ui.setupUi(this);
}

int StitchingSettingsDialog::gridSizeX() const { return ui.gridXSpin->value(); }
int StitchingSettingsDialog::gridSizeY() const { return ui.gridYSpin->value(); }
QString StitchingSettingsDialog::inputDir() const { return ui.inputDirEdit->text(); }

void StitchingSettingsDialog::setGridSizeX(int val) { ui.gridXSpin->setValue(val); }
void StitchingSettingsDialog::setGridSizeY(int val) { ui.gridYSpin->setValue(val); }
void StitchingSettingsDialog::setInputDir(const QString& dir) { ui.inputDirEdit->setText(dir); }
