#include "DetectionSettingsDialog.h"
#include <QFileDialog>
#include <QPushButton>
#include <QFileInfo>
#include <QMessageBox>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>

#include "core/detector/IDetector.h"

DetectionSettingsDialog::DetectionSettingsDialog(detector::IDetector* detector, QWidget* parent)
    : QDialog(parent)
    , detector_(detector)
{
    ui.setupUi(this);

    connect(ui.browseParamButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onBrowseParam);
    connect(ui.browseBinButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onBrowseBin);
    connect(ui.browseImageButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onBrowseImage);
    connect(ui.detectAndSaveButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onDetectAndSave);

    // Enable detect button only when a valid image path is set
    connect(ui.imagePathEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        ui.detectAndSaveButton->setEnabled(!text.isEmpty());
    });
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

void DetectionSettingsDialog::onBrowseImage() {
    QString path = QFileDialog::getOpenFileName(this, "选择待检测图片", ui.imagePathEdit->text(),
        "图片文件 (*.jpg *.jpeg *.png *.bmp *.tif *.tiff);;所有文件 (*.*)");
    if (!path.isEmpty()) {
        ui.imagePathEdit->setText(path);
    }
}

void DetectionSettingsDialog::onDetectAndSave() {
    if (!detector_) {
        QMessageBox::warning(this, "警告", "检测器未初始化");
        return;
    }
    if (!detector_->isModelLoaded()) {
        QMessageBox::warning(this, "警告", "请先加载检测模型");
        return;
    }

    QString imagePath = ui.imagePathEdit->text();
    if (imagePath.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先选择一张图片");
        return;
    }

    // Load image
    cv::Mat image = cv::imread(imagePath.toStdString(), cv::IMREAD_COLOR);
    if (image.empty()) {
        QMessageBox::warning(this, "警告", "无法读取图片: " + imagePath);
        return;
    }

    // Apply current detection parameters
    detector_->setConfidenceThreshold(static_cast<float>(ui.confidenceSpin->value()));
    detector_->setNmsThreshold(static_cast<float>(ui.nmsSpin->value()));

    // Run detection
    auto detections = detector_->detect(image);

    // Draw result
    cv::Mat annotated = detector_->drawDetections(image, detections);

    // Build output path: original name + "_detected" suffix
    QFileInfo fileInfo(imagePath);
    QString dir = fileInfo.absolutePath();
    QString baseName = fileInfo.completeBaseName();
    QString suffix = fileInfo.suffix();
    QString outPath = dir + "/" + baseName + "_detected." + suffix;

    // Save
    bool ok = cv::imwrite(outPath.toStdString(), annotated);
    if (ok) {
        SPDLOG_INFO("Detection result saved to: {}", outPath.toStdString());
        QMessageBox::information(this, "完成",
            QString("检测完成，共发现 %1 个目标\n结果已保存至:\n%2")
                .arg(detections.size())
                .arg(outPath));
    } else {
        QMessageBox::warning(this, "错误", "保存图片失败: " + outPath);
    }
}
