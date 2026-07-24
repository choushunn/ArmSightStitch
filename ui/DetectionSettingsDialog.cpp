#include "DetectionSettingsDialog.h"
#include "YoloParamsWidget.h"
#include "DustParamsWidget.h"
#include "EdgeParamsWidget.h"

#include <QFileDialog>
#include <QPushButton>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QDateTime>
#include <QTabWidget>
#include <opencv2/opencv.hpp>
#include <regex>
#include <spdlog/spdlog.h>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QProgressDialog>

#include "ui/AppController.h"
#include "infra/config/ConfigManager.h"
#include "infra/report/ReportLayout.h"
#include "infra/report/ReportRenderer.h"
#include "infra/report/EdgeCrop.h"
#include "core/detector/IDetector.h"


DetectionSettingsDialog::DetectionSettingsDialog(AppController* app, QWidget* parent)
    : QDialog(parent)
    , app_(app)
{
    ui.setupUi(this);

    // ── Create parameter widgets and add to tab widget ──
    yoloWidget_ = new YoloParamsWidget(this);
    dustWidget_ = new DustParamsWidget(this);
    edgeWidget_ = new EdgeParamsWidget(this);

    ui.paramsTab->addTab(yoloWidget_, "YOLO");
    ui.paramsTab->addTab(dustWidget_, "灰尘");
    ui.paramsTab->addTab(edgeWidget_, "Sobel边缘");

    // ── Connections ──
    connect(ui.browseImageButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onBrowseImage);
    connect(ui.detectAndSaveButton, &QPushButton::clicked, this, &DetectionSettingsDialog::onDetectAndSave);
    connect(ui.algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DetectionSettingsDialog::onAlgorithmChanged);

    // Enable detect button only when path is set
    connect(ui.imagePathEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        ui.detectAndSaveButton->setEnabled(!text.isEmpty());
    });

    // ── Add batch mode selector above the path row ──
    auto* sl = qobject_cast<QVBoxLayout*>(ui.singleImageGroup->layout());
    if (sl) {
        modeCombo_ = new QComboBox(this);
        modeCombo_->addItem("单张图片", 0);
        modeCombo_->addItem("批量文件夹", 1);
        sl->insertWidget(0, modeCombo_);
        connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &DetectionSettingsDialog::onModeChanged);
    }
    // Repurpose path label (we'll update its text dynamically)
    pathLabel_ = ui.labelImage;

    onAlgorithmChanged(ui.algorithmCombo->currentIndex());
    onModeChanged(0);
}

// ── YOLO param accessors (delegated to YoloParamsWidget) ──

QString DetectionSettingsDialog::paramPath() const { return yoloWidget_->paramPath(); }
QString DetectionSettingsDialog::binPath() const { return yoloWidget_->binPath(); }
double DetectionSettingsDialog::confidenceThreshold() const { return yoloWidget_->confidenceThreshold(); }
double DetectionSettingsDialog::nmsThreshold() const { return yoloWidget_->nmsThreshold(); }

void DetectionSettingsDialog::setParamPath(const QString& path) { yoloWidget_->setParamPath(path); }
void DetectionSettingsDialog::setBinPath(const QString& path) { yoloWidget_->setBinPath(path); }
void DetectionSettingsDialog::setConfidenceThreshold(double val) { yoloWidget_->setConfidenceThreshold(val); }
void DetectionSettingsDialog::setNmsThreshold(double val) { yoloWidget_->setNmsThreshold(val); }

// ── Algorithm switching ──

int DetectionSettingsDialog::detectionAlgorithm() const { return ui.algorithmCombo->currentIndex(); }
void DetectionSettingsDialog::setDetectionAlgorithm(int algo) {
    ui.algorithmCombo->setCurrentIndex(algo >= 0 && algo <= 2 ? algo : 0);
}

// ── Dust / Edge param accessors (delegated to respective widgets) ──

detector::DustDetectionParams DetectionSettingsDialog::dustParams() const {
    return dustWidget_->dustParams();
}

void DetectionSettingsDialog::setDustParams(const detector::DustDetectionParams& p) {
    dustWidget_->setDustParams(p);
}

detector::EdgeDetectionParams DetectionSettingsDialog::edgeParams() const {
    return edgeWidget_->edgeParams();
}

void DetectionSettingsDialog::setEdgeParams(const detector::EdgeDetectionParams& p) {
    edgeWidget_->setEdgeParams(p);
}

// ── Algorithm / Mode changed ──

void DetectionSettingsDialog::onAlgorithmChanged(int index) {
    // Switch to the corresponding tab
    if (index >= 0 && index < ui.paramsTab->count())
        ui.paramsTab->setCurrentIndex(index);
}

void DetectionSettingsDialog::onModeChanged(int index) {
    const bool batch = (index == 1);
    if (pathLabel_)
        pathLabel_->setText(batch ? "文件夹路径:" : "图片路径:");
    ui.imagePathEdit->clear();
    ui.imagePathEdit->setPlaceholderText(
        batch ? "请选择包含检测图片的文件夹" : "请选择待检测的图片");
    ui.detectAndSaveButton->setText(batch ? "批量检测并保存" : "检测并保存");
    ui.singleImageGroup->setTitle(batch ? "批量图片检测" : "单张图片检测");
}

// ── Browse image ──

void DetectionSettingsDialog::onBrowseImage() {
    QString cur = ui.imagePathEdit->text();
    QString dir;
    if (!cur.isEmpty()) {
        dir = QFileInfo(cur).absolutePath();
    } else {
        // Default to the configured image save base path (work directory)
        dir = QString::fromStdString(ConfigManager::instance().imageSaveBasePath());
        if (dir.isEmpty() || !QFileInfo::exists(dir))
            dir = QCoreApplication::applicationDirPath();
    }
    if (modeCombo_ && modeCombo_->currentData().toInt() == 1) {
        // Batch folder mode
        QString folder = QFileDialog::getExistingDirectory(this, "选择检测文件夹", dir);
        if (!folder.isEmpty()) {
            // 扫描图像保存在 original/ 子目录，自动定位
            QString origPath = folder + "/original";
            if (QFileInfo::exists(origPath) && QFileInfo(origPath).isDir())
                folder = origPath;
            ui.imagePathEdit->setText(folder);
        }
    } else {
        // Single image mode
        QString path = QFileDialog::getOpenFileName(this, "选择待检测图片", dir,
            "图片文件 (*.jpg *.jpeg *.png *.bmp *.tif *.tiff);;所有文件 (*.*)");
        if (!path.isEmpty()) ui.imagePathEdit->setText(path);
    }
}

// ── Detect and save ──

void DetectionSettingsDialog::onDetectAndSave() {
    if (!app_) { QMessageBox::warning(this, "警告", "检测器未初始化"); return; }

    QString path = ui.imagePathEdit->text();
    if (path.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先选择图片或文件夹");
        return;
    }

    const int algo = ui.algorithmCombo->currentIndex();
    app_->setDetectorAlgorithm(algo);
    if (algo == 1) {
        app_->setDustParams(dustParams());
    } else if (algo == 2) {
        app_->setEdgeParams(edgeParams());
    } else {
        app_->detector().setConfidenceThreshold(static_cast<float>(yoloWidget_->confidenceThreshold()));
        app_->detector().setNmsThreshold(static_cast<float>(yoloWidget_->nmsThreshold()));
    }

    if (algo == 0 && !app_->isModelLoaded()) {
        QMessageBox::warning(this, "警告", "请先加载检测模型");
        return;
    }

    const detector::DustDetectionParams dp = dustParams();
    const detector::EdgeDetectionParams ep = edgeParams();
    const float conf = static_cast<float>(yoloWidget_->confidenceThreshold());
    const float nms  = static_cast<float>(yoloWidget_->nmsThreshold());
    const bool batch = (modeCombo_ && modeCombo_->currentData().toInt() == 1);

    if (batch)
        runBatchDetection(path, algo, dp, ep, conf, nms);
    else
        runSingleDetection(path, algo, dp, ep, conf, nms);
}

// ── Single detection ──

void DetectionSettingsDialog::runSingleDetection(
    const QString& imgPath, int algo,
    const detector::DustDetectionParams& dp,
    const detector::EdgeDetectionParams& ep,
    float conf, float nms)
{
    cv::Mat image = cv::imread(imgPath.toStdString(), cv::IMREAD_COLOR);
    if (image.empty()) {
        QMessageBox::warning(this, "警告", "无法读取图片: " + imgPath);
        return;
    }

    ui.detectAndSaveButton->setEnabled(false);
    ui.detectAndSaveButton->setText("检测中...");

    auto* progress = new QProgressDialog("正在执行检测...", QString(), 0, 0, this);
    progress->setWindowTitle("检测中");
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->show();

    // 边缘感知裁剪：尝试从文件名解析行列，边界图像保留外沿
    const int cropSize = ConfigManager::instance().centerCropSize();
    cv::Mat cropImage;
    if (cropSize > 0 && cropSize < image.cols && cropSize < image.rows) {
        QFileInfo fi(imgPath);
        QString stem = fi.completeBaseName();
        // 解析 row_col 格式（与 ImageStitcher 一致）
        std::regex re(R"(^(\d+)_(\d+))");
        std::string stemStd = stem.toStdString();
        std::smatch m;
        int stitchRow = -1, stitchCol = -1;
        int gridRows = -1, gridCols = -1;
        if (std::regex_search(stemStd, m, re)) {
            int dispRow = std::stoi(m[1].str());  // 1-indexed display
            int dispCol = std::stoi(m[2].str());
            gridRows = ConfigManager::instance().gridSizeY();
            gridCols = ConfigManager::instance().gridSizeX();
            if (gridRows > 0 && gridCols > 0) {
                stitchRow = dispRow - 1;           // 0-indexed stitch row
                stitchCol = gridCols - dispCol;    // RTL: disp col→stitch col
            }
        }
        cv::Rect roi = report::computeEdgeAwareCropRoi(
            cv::Size(image.cols, image.rows), cropSize,
            stitchRow, stitchCol, gridRows, gridCols);
        cropImage = image(roi).clone();
    } else {
        cropImage = image;
    }

    struct R { bool imgOk=false, pdfOk=false; int cnt=0;
               QString out, pdf, jp; };
    auto r = std::make_shared<R>();
    QFileInfo fi(imgPath);
    QString bn = fi.completeBaseName(), sf = fi.suffix();
    QString outDir = fi.absolutePath() + "/_detection_result";

    auto* w = new QFutureWatcher<void>(this);
    connect(w, &QFutureWatcher<void>::finished, this, [=]() {
        progress->close(); progress->deleteLater();
        ui.detectAndSaveButton->setEnabled(true);
        ui.detectAndSaveButton->setText("检测并保存");
        w->deleteLater();
        if (r->imgOk || r->pdfOk) {
            QStringList s;
            if (r->imgOk) s << "图片: " + r->out;
            if (r->pdfOk) s << "PDF: "  + r->pdf;
            if (!r->jp.isEmpty()) s << "JSON: " + r->jp;
            QMessageBox::information(this, "完成",
                QString("检测完成，共发现 %1 个目标\n\n%2")
                    .arg(r->cnt).arg(s.join("\n")));
        } else QMessageBox::warning(this, "错误", "保存失败: " + r->out);
    });

    w->setFuture(QtConcurrent::run([=]() {
        // ── Detection ──
        auto dets = app_->detect(cropImage);
        cv::Mat ann = app_->drawDetections(cropImage, dets);
        QDir().mkpath(outDir);
        r->out  = outDir + "/" + bn + "_detected." + sf;
        r->pdf  = outDir + "/" + bn + "_report.pdf";
        r->cnt  = static_cast<int>(dets.size());
        r->imgOk = cv::imwrite(r->out.toStdString(), ann);
        auto jp = app_->saveDetectionsJsonTo(dets, cropImage, outDir.toStdString(), bn.toStdString());
        if (!jp.empty()) r->jp = QString::fromStdString(jp);

        if (r->imgOk) {
            // ── PDF Report (worker thread, O(1) memory) ──
            SPDLOG_INFO("[DetectionDialog] DetectionDialog: generating single PDF report ({})", imgPath.toStdString());
            report::ReportInput in;
            in.imagePath  = imgPath;
            in.algorithm  = algo;
            in.confThreshold = conf;
            in.nmsThreshold  = nms;
            in.dustClaheClip  = dp.claheClip;
            in.dustBgBlur     = dp.bgBlurSize;
            in.dustMinArea    = dp.minArea;
            in.dustMaxArea    = dp.maxArea;
            in.dustDilateIter = dp.dilateIter;
            in.dustMaxIter    = dp.maxIter;
            in.dustNmsIou     = dp.nmsIou;
            in.edgeClaheClip       = ep.claheClip;
            in.edgeClaheTileGrid   = ep.claheTileGrid;
            in.edgeThreshold       = ep.edgeThreshold;
            in.edgeSobelKSize      = ep.sobelKSize;
            in.edgeDilateIter      = ep.dilateIter;
            in.edgeMinBboxArea     = ep.minBboxArea;
            in.edgeNmsIouThresh    = ep.nmsIouThresh;
            in.edgeNmsContainThresh = ep.nmsContainThresh;
            in.detections = dets;
            in.annotatedImage = report::Metrics::matToImage(
                ann, report::kContentW, report::kContentH / 2);

            auto pages = report::computeLayout(in);
            if (!pages.empty()) {
                report::renderReport(r->pdf, pages);
                r->pdfOk = QFileInfo::exists(r->pdf);
            }
        }
    }));
}

// ── Batch detection ──

void DetectionSettingsDialog::runBatchDetection(
    const QString& folderPath, int algo,
    const detector::DustDetectionParams& dp,
    const detector::EdgeDetectionParams& ep,
    float conf, float nms)
{
    QDir dir(folderPath);
    QStringList filters{"*.jpg", "*.jpeg", "*.png", "*.bmp", "*.tif", "*.tiff"};
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        QMessageBox::warning(this, "警告", "文件夹中没有图片文件");
        return;
    }

    const int total = files.size();
    ui.detectAndSaveButton->setEnabled(false);
    ui.detectAndSaveButton->setText(QString("批量检测 %1/%1 ...").arg(total));

    auto* progress = new QProgressDialog(
        QString("正在处理 0 / %1 ...").arg(total), QString(), 0, total, this);
    progress->setWindowTitle("批量检测中");
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->show();

    // 共享结果容器 — worker thread 写入，finished 回调主线程读取
    struct BatchResult {
        int totalImgs = 0, totalDets = 0, totalOk = 0;
        bool pdfOk = false;
        QStringList errors;
        QString outFolder;
        QString batchPdf;
    };
    auto br = std::make_shared<BatchResult>();
    br->totalImgs = total;
    br->outFolder = folderPath + "/_batch_result";
    br->batchPdf  = br->outFolder + "/_batch_report.pdf";
    QDir().mkpath(br->outFolder);

    auto* w = new QFutureWatcher<void>(this);
    connect(w, &QFutureWatcher<void>::finished, this, [=]() {
        progress->close(); progress->deleteLater();
        ui.detectAndSaveButton->setEnabled(true);
        ui.detectAndSaveButton->setText(modeCombo_->currentData().toInt()==1
            ? "批量检测并保存" : "检测并保存");
        w->deleteLater();

        QString msg = QString("检测完成\n\n共处理 %1 张图片，检出 %2 个目标\n结果目录: %3")
            .arg(br->totalImgs).arg(br->totalDets).arg(br->outFolder);
        if (br->pdfOk)
            msg += "\nPDF报告: " + br->batchPdf;
        if (!br->errors.isEmpty())
            msg += "\n\n失败: \n" + br->errors.join("\n");
        QMessageBox::information(this, "完成", msg);
    });

    w->setFuture(QtConcurrent::run([=]() {
        int batchDets = 0, okCnt = 0;
        QStringList errs;
        std::vector<report::BatchEntry> batchEntries;

        for (int idx = 0; idx < files.size(); ++idx) {
            QString fp = dir.absoluteFilePath(files[idx]);
            cv::Mat img = cv::imread(fp.toStdString(), cv::IMREAD_COLOR);
            if (img.empty()) {
                errs << files[idx] + ": 无法读取";
                continue;
            }

            // 边缘感知裁剪：从文件名解析行列
            int cropSz = ConfigManager::instance().centerCropSize();
            cv::Mat cropImg;
            if (cropSz > 0 && cropSz < img.cols && cropSz < img.rows) {
                int stitchRow = -1, stitchCol = -1;
                int gridRows = -1, gridCols = -1;
                std::string stemStd = files[idx].toStdString();
                std::regex re(R"(^(\d+)_(\d+))");
                std::smatch m;
                if (std::regex_search(stemStd, m, re)) {
                    int dispRow = std::stoi(m[1].str());
                    int dispCol = std::stoi(m[2].str());
                    gridRows = ConfigManager::instance().gridSizeY();
                    gridCols = ConfigManager::instance().gridSizeX();
                    if (gridRows > 0 && gridCols > 0) {
                        stitchRow = dispRow - 1;
                        stitchCol = gridCols - dispCol;
                    }
                }
                cv::Rect roi = report::computeEdgeAwareCropRoi(
                    cv::Size(img.cols, img.rows), cropSz,
                    stitchRow, stitchCol, gridRows, gridCols);
                cropImg = img(roi).clone();
            } else {
                cropImg = img;
            }

            auto dets = app_->detect(cropImg);
            cv::Mat ann = app_->drawDetections(cropImg, dets);
            batchDets += static_cast<int>(dets.size());

            QFileInfo fi(files[idx]);
            QString bn = fi.completeBaseName(), sf = fi.suffix();
            QString outImg = br->outFolder + "/" + bn + "_detected." + sf;

            bool ok = cv::imwrite(outImg.toStdString(), ann);
            app_->saveDetectionsJsonTo(dets, cropImg,
                br->outFolder.toStdString(), bn.toStdString());
            if (ok) {
                okCnt++;
                report::BatchEntry bentry;
                bentry.filePath = fp;
                bentry.annotatedImage = report::Metrics::matToImage(ann, 1200, 1200);
                bentry.detections = dets;
                batchEntries.push_back(std::move(bentry));
            } else {
                errs << files[idx] + ": 保存失败";
            }

            QMetaObject::invokeMethod(qApp, [=]() {
                progress->setValue(idx + 1);
                progress->setLabelText(
                    QString("正在处理 %1 / %2 ...").arg(idx + 1).arg(total));
            }, Qt::QueuedConnection);
        }

        br->totalDets = batchDets;
        br->totalOk   = okCnt;
        br->errors    = errs;

        // ── PDF Report — batch, single pass (worker thread) ──
        if (!batchEntries.empty()) {
            SPDLOG_INFO("[DetectionDialog] DetectionDialog: generating batch PDF report ({} entries)", batchEntries.size());
            QMetaObject::invokeMethod(qApp, [=]() {
                progress->setMaximum(0);
                progress->setLabelText("正在生成PDF报告...");
            }, Qt::QueuedConnection);

            report::ReportInput paramIn;
            paramIn.algorithm  = algo;
            paramIn.confThreshold = conf;
            paramIn.nmsThreshold  = nms;
            paramIn.dustClaheClip  = dp.claheClip;
            paramIn.dustBgBlur     = dp.bgBlurSize;
            paramIn.dustMinArea    = dp.minArea;
            paramIn.dustMaxArea    = dp.maxArea;
            paramIn.dustDilateIter = dp.dilateIter;
            paramIn.dustMaxIter    = dp.maxIter;
            paramIn.dustNmsIou     = dp.nmsIou;
            paramIn.edgeClaheClip       = ep.claheClip;
            paramIn.edgeClaheTileGrid   = ep.claheTileGrid;
            paramIn.edgeThreshold       = ep.edgeThreshold;
            paramIn.edgeSobelKSize      = ep.sobelKSize;
            paramIn.edgeDilateIter      = ep.dilateIter;
            paramIn.edgeMinBboxArea     = ep.minBboxArea;
            paramIn.edgeNmsIouThresh    = ep.nmsIouThresh;
            paramIn.edgeNmsContainThresh = ep.nmsContainThresh;

            auto pages = report::computeBatchLayout(algo, paramIn, batchEntries, errs);
            if (!pages.empty()) {
                report::renderReport(br->batchPdf, pages);
                br->pdfOk = QFileInfo::exists(br->batchPdf);
            }
        }
    }));
}
