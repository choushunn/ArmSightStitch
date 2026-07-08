#pragma once

#include <QDialog>
#include "ui_StitchingSettingsDialog.h"

class StitchingSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit StitchingSettingsDialog(QWidget* parent = nullptr);

    int gridSizeX() const;
    int gridSizeY() const;
    int centerCropSize() const;
    int stitchAlgorithm() const;
    QString inputDir() const;

    void setGridSizeX(int val);
    void setGridSizeY(int val);
    void setCenterCropSize(int val);
    void setStitchAlgorithm(int algo);
    void setInputDir(const QString& dir);

private:
    Ui::StitchingSettingsDialog ui;
};
