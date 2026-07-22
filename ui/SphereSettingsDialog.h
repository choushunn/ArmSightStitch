#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QWidget>
#include <QHBoxLayout>

class SphereSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SphereSettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Z 轴参数设置");
        setMinimumWidth(420);
        auto* form = new QFormLayout(this);

        // ---- Z-axis mode selector ----
        zMode_ = new QComboBox(this);
        zMode_->addItem("球冠补偿 (Spherical Cap)", 0);
        zMode_->addItem("手动 Z-Map (Manual Per-Position)", 1);
        zMode_->addItem("径向 Z-Map (Radial Z-Map)", 2);
        form->addRow("Z 轴模式:", zMode_);

        // ---- Spherical cap group ----
        capGroup_ = new QWidget(this);
        auto* capForm = new QFormLayout(capGroup_);
        capForm->setContentsMargins(0, 0, 0, 0);

        radius_ = new QSpinBox(this);
        radius_->setRange(1, 9999999);
        capForm->addRow("球冠半径 R (脉冲数):", radius_);

        cap_h_ = new QSpinBox(this);
        cap_h_->setRange(0, 9999999);
        capForm->addRow("球冠高度 h (脉冲数):", cap_h_);

        dH_ = new QSpinBox(this);
        dH_->setRange(-9999999, 9999999);
        capForm->addRow("高度偏移 dH (脉冲数):", dH_);

        zBase_ = new QSpinBox(this);
        zBase_->setRange(0, 9999999);
        capForm->addRow("Z 基准高度 (脉冲数):", zBase_);

        form->addRow(capGroup_);

        // ---- Manual Z-map group ----
        mapGroup_ = new QWidget(this);
        auto* mapForm = new QFormLayout(mapGroup_);
        mapForm->setContentsMargins(0, 0, 0, 0);

        auto* fileRow = new QHBoxLayout();
        zMapFile_ = new QLineEdit(this);
        zMapFile_->setPlaceholderText("选择 Z-Map JSON 文件...");
        fileRow->addWidget(zMapFile_);
        auto* browseBtn = new QPushButton("浏览...", this);
        connect(browseBtn, &QPushButton::clicked, this, [this]() {
            QString startDir = zMapFile_->text().isEmpty()
                ? QCoreApplication::applicationDirPath()
                : QFileInfo(zMapFile_->text()).absolutePath();
            QString path = QFileDialog::getOpenFileName(this, "选择 Z-Map 文件",
                startDir, "JSON Files (*.json);;All Files (*)");
            if (!path.isEmpty()) zMapFile_->setText(path);
        });
        fileRow->addWidget(browseBtn);
        mapForm->addRow("Z-Map 文件:", fileRow);

        mapForm->addRow("格式说明:",
            new QLabel("JSON: {\"z_values\": [[row0_z0, row0_z1, ...], [row1_z0, ...], ...]}", this));

        form->addRow(mapGroup_);

        // ---- Radial Z-map group ----
        radialGroup_ = new QWidget(this);
        auto* radialForm = new QFormLayout(radialGroup_);
        radialForm->setContentsMargins(0, 0, 0, 0);

        auto* radialFileRow = new QHBoxLayout();
        zRadialFile_ = new QLineEdit(this);
        zRadialFile_->setPlaceholderText("选择径向 Z-Map JSON 文件...");
        radialFileRow->addWidget(zRadialFile_);
        auto* radialBrowseBtn = new QPushButton("浏览...", this);
        connect(radialBrowseBtn, &QPushButton::clicked, this, [this]() {
            QString startDir = zRadialFile_->text().isEmpty()
                ? QCoreApplication::applicationDirPath()
                : QFileInfo(zRadialFile_->text()).absolutePath();
            QString path = QFileDialog::getOpenFileName(this, "选择径向 Z-Map 文件",
                startDir, "JSON Files (*.json);;All Files (*)");
            if (!path.isEmpty()) zRadialFile_->setText(path);
        });
        radialFileRow->addWidget(radialBrowseBtn);
        radialForm->addRow("径向 Z-Map 文件:", radialFileRow);

        radialForm->addRow("格式说明:",
            new QLabel("JSON: {\"z_radial\": [{\"r\":0.0,\"z\":80000}, {\"r\":0.5,\"z\":78000}, ...]}\nr=归一化半径(0→0.5→1.0), z=Z脉冲值", this));

        form->addRow(radialGroup_);

        // ---- Toggle visibility based on mode ----
        connect(zMode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
            capGroup_->setVisible(idx == 0);
            mapGroup_->setVisible(idx == 1);
            radialGroup_->setVisible(idx == 2);
        });
        capGroup_->setVisible(true);
        mapGroup_->setVisible(false);
        radialGroup_->setVisible(false);

        auto* btn = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(btn, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btn, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(btn);
    }

    // Z mode
    int zMode() const { return zMode_->currentData().toInt(); }
    void setZMode(int v) { zMode_->setCurrentIndex(v >= 0 && v <= 2 ? v : 0); }

    // Spherical cap
    int sphereRadius() const { return radius_->value(); }
    int sphereCapHeight() const { return cap_h_->value(); }
    int sphereHeightOffset() const { return dH_->value(); }
    int zBaseHeight() const { return zBase_->value(); }

    void setSphereRadius(int v) { radius_->setValue(v); }
    void setSphereCapHeight(int v) { cap_h_->setValue(v); }
    void setSphereHeightOffset(int v) { dH_->setValue(v); }
    void setZBaseHeight(int v) { zBase_->setValue(v); }

    // Manual Z-map file
    std::string zMapFile() const { return zMapFile_->text().toStdString(); }
    void setZMapFile(const std::string& path) { zMapFile_->setText(QString::fromStdString(path)); }

    // Radial Z-map file
    std::string zRadialFile() const { return zRadialFile_->text().toStdString(); }
    void setZRadialFile(const std::string& path) { zRadialFile_->setText(QString::fromStdString(path)); }

private:
    QComboBox* zMode_;
    QWidget* capGroup_;
    QWidget* mapGroup_;
    QWidget* radialGroup_ = nullptr;
    QSpinBox* radius_;
    QSpinBox* cap_h_;
    QSpinBox* dH_;
    QSpinBox* zBase_;
    QLineEdit* zMapFile_;
    QLineEdit* zRadialFile_ = nullptr;
};
