#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFormLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QCoreApplication>
#include "ui_StitchingSettingsDialog.h"

class StitchingSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit StitchingSettingsDialog(QWidget* parent = nullptr);

    int gridSizeX() const;
    int gridSizeY() const;
    int centerCropSize() const;
    int stitchAlgorithm() const;
    int featherWidth() const;
    QString inputDir() const;
    int scaleMode() const;
    QString scaleMapFile() const;

    void setGridSizeX(int val);
    void setGridSizeY(int val);
    void setCenterCropSize(int val);
    void setStitchAlgorithm(int algo);
    void setFeatherWidth(int val);
    void setInputDir(const QString& dir);
    void setScaleMode(int mode);
    void setScaleMapFile(const QString& path);

private:
    Ui::StitchingSettingsDialog ui;
    QComboBox* scaleModeCombo_ = nullptr;
    QWidget* scaleMapRow_ = nullptr;
    QLineEdit* scaleMapEdit_ = nullptr;
};
