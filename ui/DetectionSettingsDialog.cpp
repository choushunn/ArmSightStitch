#include "DetectionSettingsDialog.h"
#include <QFileDialog>
#include <QPushButton>
#include <QFileInfo>
#include <QMessageBox>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>

#include "ui/AppController.h"
#include "core/detector/IDetector.h"

DetectionSettingsDialog::DetectionSettingsDialog(AppController* app, QWidget* parent)
    : QDialog(parent)
    , app_(app)
{
    ui.setupUi(this);

    connect(ui.browseParamButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onBrowseParam);
    connect(ui.browseBinButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onBrowseBin);
    connect(ui.browseImageButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onBrowseImage);
    connect(ui.detectAndSaveButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onDetectAndSave);
    connect(ui.algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DetectionSettingsDialog::onAlgorithmChanged);

    // Enable detect button only when a valid image path is set
    connect(ui.imagePathEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        ui.detectAndSaveButton->setEnabled(!text.isEmpty());
    });

    onAlgorithmChanged(ui.algorithmCombo->currentIndex());
}

QString DetectionSettingsDialog::paramPath() const { return ui.paramPathEdit->text(); }
QString DetectionSettingsDialog::binPath() const { return ui.binPathEdit->text(); }
double DetectionSettingsDialog::confidenceThreshold() const { return ui.confidenceSpin->value(); }
double DetectionSettingsDialog::nmsThreshold() const { return ui.nmsSpin->value(); }

void DetectionSettingsDialog::setParamPath(const QString& path) { ui.paramPathEdit->setText(path); }
void DetectionSettingsDialog::setBinPath(const QString& path) { ui.binPathEdit->setText(path); }
void DetectionSettingsDialog::setConfidenceThreshold(double val) { ui.confidenceSpin->setValue(val); }
void DetectionSettingsDialog::setNmsThreshold(double val) { ui.nmsSpin->setValue(val); }

int DetectionSettingsDialog::detectionAlgorithm() const { return ui.algorithmCombo->currentIndex(); }
void DetectionSettingsDialog::setDetectionAlgorithm(int algo) {
    ui.algorithmCombo->setCurrentIndex(algo == 1 ? 1 : 0);
}

detector::DustDetectionParams DetectionSettingsDialog::dustParams() const {
    detector::DustDetectionParams p;
    p.claheClip  = ui.claheSpin->value();
    p.bgBlurSize = ui.bgBlurSpin->value() | 1;  // 强制奇数核
    p.minArea    = ui.minAreaSpin->value();
    p.maxArea    = ui.maxAreaSpin->value();
    p.dilateIter = ui.dilateSpin->value();
    p.maxIter    = ui.maxIterSpin->value();
    p.nmsIou     = ui.nmsIouSpin->value();
    return p;
}

void DetectionSettingsDialog::setDustParams(const detector::DustDetectionParams& p) {
    ui.claheSpin->setValue(p.claheClip);
    ui.bgBlurSpin->setValue(p.bgBlurSize);
    ui.minAreaSpin->setValue(p.minArea);
    ui.maxAreaSpin->setValue(p.maxArea);
    ui.dilateSpin->setValue(p.dilateIter);
    ui.maxIterSpin->setValue(p.maxIter);
    ui.nmsIouSpin->setValue(p.nmsIou);
}

void DetectionSettingsDialog::onAlgorithmChanged(int index) {
    const bool dust = (index == 1);
    // YOLO 用模型/阈值组，灰尘用参数组；单张检测组两者共用
    ui.modelGroup->setVisible(!dust);
    ui.thresholdsGroup->setVisible(!dust);
    ui.dustGroup->setVisible(dust);
}

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
    if (!app_) {
        QMessageBox::warning(this, "警告", "检测器未初始化");
        return;
    }

    QString imagePath = ui.imagePathEdit->text();
    if (imagePath.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先选择一张图片");
        return;
    }

    // Apply the algorithm + parameters chosen in this dialog (live preview).
    // MainWindow snapshots/restores on Cancel, so this does not persist unless accepted.
    const int algo = ui.algorithmCombo->currentIndex();
    app_->setDetectorAlgorithm(algo);
    if (algo == 1) {
        app_->setDustParams(dustParams());
    } else {
        app_->detector().setConfidenceThreshold(static_cast<float>(ui.confidenceSpin->value()));
        app_->detector().setNmsThreshold(static_cast<float>(ui.nmsSpin->value()));
    }

    if (!app_->isModelLoaded()) {
        QMessageBox::warning(this, "警告", "请先加载检测模型");
        return;
    }

    // Load image
    cv::Mat image = cv::imread(imagePath.toStdString(), cv::IMREAD_COLOR);
    if (image.empty()) {
        QMessageBox::warning(this, "警告", "无法读取图片: " + imagePath);
        return;
    }

    // Run detection + draw result
    auto detections = app_->detect(image);
    cv::Mat annotated = app_->drawDetections(image, detections);

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
        // 导出检测结果（类型/尺寸/位置）到扫描目录下的 JSON
        std::string jsonPath = app_->saveDetectionsJson(detections, image, imagePath.toStdString());
        QString extra = jsonPath.empty()
                            ? QString()
                            : QString("\nJSON: %1").arg(QString::fromStdString(jsonPath));
        QMessageBox::information(this, "完成",
            QString("检测完成，共发现 %1 个目标\n结果已保存至:\n%2%3")
                .arg(detections.size())
                .arg(outPath)
                .arg(extra));
    } else {
        QMessageBox::warning(this, "错误", "保存图片失败: " + outPath);
    }
}
