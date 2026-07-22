#include "StitchingSettingsDialog.h"
#include <QLabel>
#include <QHBoxLayout>

StitchingSettingsDialog::StitchingSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    ui.setupUi(this);

    // Add scale mode UI to the existing form layout
    auto* form = qobject_cast<QFormLayout*>(ui.settingsGroup->layout());
    if (form) {
        // Scale mode combo
        scaleModeCombo_ = new QComboBox(this);
        scaleModeCombo_->addItem("Z 轴自动缩放 (Z-based)", 0);
        scaleModeCombo_->addItem("手动缩放映射 (Scale Map)", 1);
        form->addRow("缩放模式:", scaleModeCombo_);

        // Scale map file row (hidden when Z-based mode)
        scaleMapRow_ = new QWidget(this);
        auto* hbox = new QHBoxLayout(scaleMapRow_);
        hbox->setContentsMargins(0, 0, 0, 0);
        scaleMapEdit_ = new QLineEdit(this);
        scaleMapEdit_->setPlaceholderText("选择 scale-map JSON 文件...");
        hbox->addWidget(scaleMapEdit_);
        auto* browse = new QPushButton("浏览...", this);
        connect(browse, &QPushButton::clicked, this, [this]() {
            QString startDir = scaleMapEdit_->text().isEmpty()
                ? QCoreApplication::applicationDirPath()
                : QFileInfo(scaleMapEdit_->text()).absolutePath();
            QString path = QFileDialog::getOpenFileName(this, "选择 Scale-Map 文件",
                startDir, "JSON Files (*.json);;All Files (*)");
            if (!path.isEmpty()) scaleMapEdit_->setText(path);
        });
        hbox->addWidget(browse);
        form->addRow("缩放映射文件:", scaleMapRow_);

        // Toggle visibility
        connect(scaleModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) { scaleMapRow_->setVisible(idx == 1); });
        scaleMapRow_->setVisible(false);

        // ── Z correction coefficient row ──
        z_correction_coef_row_ = new QWidget(this);
        {
            auto* hb = new QHBoxLayout(z_correction_coef_row_);
            hb->setContentsMargins(0, 0, 0, 0);
            zCorrectionCoefSpin_ = new QDoubleSpinBox(this);
            zCorrectionCoefSpin_->setRange(0.50, 2.00);
            zCorrectionCoefSpin_->setDecimals(2);
            zCorrectionCoefSpin_->setSingleStep(0.01);
            zCorrectionCoefSpin_->setValue(1.00);
            zCorrectionCoefSpin_->setToolTip("Z 轴缩放系数全局乘数");
            hb->addWidget(zCorrectionCoefSpin_);
            form->addRow("Z 校正系数:", z_correction_coef_row_);
        }

        // ── Crop offset file row ──
        crop_offset_file_row_ = new QWidget(this);
        {
            auto* hb = new QHBoxLayout(crop_offset_file_row_);
            hb->setContentsMargins(0, 0, 0, 0);
            cropOffsetEdit_ = new QLineEdit(this);
            cropOffsetEdit_->setPlaceholderText("选择 crop-offset JSON 文件...");
            hb->addWidget(cropOffsetEdit_);
            auto* browseCo = new QPushButton("浏览...", this);
            connect(browseCo, &QPushButton::clicked, this, [this]() {
                QString startDir = cropOffsetEdit_->text().isEmpty()
                    ? QCoreApplication::applicationDirPath()
                    : QFileInfo(cropOffsetEdit_->text()).absolutePath();
                QString path = QFileDialog::getOpenFileName(this, "选择 Crop Offset 文件",
                    startDir, "JSON Files (*.json);;All Files (*)");
                if (!path.isEmpty()) cropOffsetEdit_->setText(path);
            });
            hb->addWidget(browseCo);
            form->addRow("裁剪偏移文件:", crop_offset_file_row_);
        }

        // ── Algorithm-aware visibility ──
        connect(ui.algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
            bool isFeather = (idx == 3);  // SeamFeather with all advanced features
            z_correction_coef_row_->setVisible(isFeather);
            crop_offset_file_row_->setVisible(isFeather);
            scaleModeCombo_->setVisible(isFeather);
            ui.featherWidthSpin->setVisible(isFeather);
            if (!isFeather) scaleMapRow_->setVisible(false);
        });

        // Initial state: hide advanced controls
        z_correction_coef_row_->setVisible(false);
        crop_offset_file_row_->setVisible(false);
        scaleModeCombo_->setVisible(false);
        scaleMapRow_->setVisible(false);
        ui.featherWidthSpin->setVisible(false);
    }
}

int StitchingSettingsDialog::gridSizeX() const { return ui.gridXSpin->value(); }
int StitchingSettingsDialog::gridSizeY() const { return ui.gridYSpin->value(); }
int StitchingSettingsDialog::centerCropSize() const { return ui.centerCropSpin->value(); }
int StitchingSettingsDialog::stitchAlgorithm() const { return ui.algorithmCombo->currentIndex(); }
int StitchingSettingsDialog::featherWidth() const { return ui.featherWidthSpin->value(); }
QString StitchingSettingsDialog::inputDir() const { return ui.inputDirEdit->text(); }
int StitchingSettingsDialog::scaleMode() const {
    return scaleModeCombo_ ? scaleModeCombo_->currentData().toInt() : 0;
}
QString StitchingSettingsDialog::scaleMapFile() const {
    return scaleMapEdit_ ? scaleMapEdit_->text() : QString();
}

void StitchingSettingsDialog::setGridSizeX(int val) { ui.gridXSpin->setValue(val); }
void StitchingSettingsDialog::setGridSizeY(int val) { ui.gridYSpin->setValue(val); }
void StitchingSettingsDialog::setCenterCropSize(int val) { ui.centerCropSpin->setValue(val); }
void StitchingSettingsDialog::setStitchAlgorithm(int algo) {
    // algo 4 merged into algo 3; map for backward compat
    ui.algorithmCombo->setCurrentIndex(algo == 4 ? 3 : algo);
}
void StitchingSettingsDialog::setFeatherWidth(int val) { ui.featherWidthSpin->setValue(val); }
void StitchingSettingsDialog::setInputDir(const QString& dir) { ui.inputDirEdit->setText(dir); }
void StitchingSettingsDialog::setScaleMode(int mode) {
    if (scaleModeCombo_) scaleModeCombo_->setCurrentIndex(mode == 1 ? 1 : 0);
}
void StitchingSettingsDialog::setScaleMapFile(const QString& path) {
    if (scaleMapEdit_) scaleMapEdit_->setText(path);
}

double StitchingSettingsDialog::zCorrectionCoef() const {
    return zCorrectionCoefSpin_ ? zCorrectionCoefSpin_->value() : 1.0;
}
QString StitchingSettingsDialog::cropOffsetFile() const {
    return cropOffsetEdit_ ? cropOffsetEdit_->text() : QString();
}
void StitchingSettingsDialog::setZCorrectionCoef(double val) {
    if (zCorrectionCoefSpin_) zCorrectionCoefSpin_->setValue(val);
}
void StitchingSettingsDialog::setCropOffsetFile(const QString& path) {
    if (cropOffsetEdit_) cropOffsetEdit_->setText(path);
}
