#include "DetectionSettingsDialog.h"
#include <QFileDialog>
#include <QPushButton>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QDateTime>
#include <QPdfWriter>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QProgressDialog>

#include "ui/AppController.h"
#include "infra/config/ConfigManager.h"
#include "core/detector/IDetector.h"

// ── PDF Report: render to QImage at 300 DPI, then wrap in PDF ─────────
namespace {

constexpr int REPORT_DPI = 300;
constexpr int REPORT_W   = 2480;  // A4 width at 300dpi (210mm)
constexpr int REPORT_H   = 3508;  // A4 height at 300dpi (297mm)
constexpr int M   = 140;          // margin
constexpr int W   = REPORT_W - M * 2;  // content width
constexpr int LINE_H = 52;        // row height

/// Draw a centered line of text and advance y.
void drawLine(QPainter& p, int& y, const QString& text,
              const QFont& f, const QColor& c = Qt::black) {
    p.setFont(f);
    p.setPen(c);
    p.drawText(M, y, W, LINE_H, Qt::AlignLeft | Qt::AlignVCenter, text);
    y += LINE_H;
}

/// Draw a horizontal rule.
void drawHr(QPainter& p, int& y, int thick = 1) {
    p.setPen(QPen(QColor(100,100,100), thick));
    p.drawLine(M, y, M + W, y);
    p.setPen(Qt::black);
    y += 16;
}

bool generatePdfReport(const QString& pdfPath,
                       const QString& imagePath, int algo,
                       const detector::DustDetectionParams& dp,
                       const detector::EdgeDetectionParams& ep,
                       float confThresh, float nmsThresh,
                       const cv::Mat& annotated,
                       const std::vector<detector::Detection>& detections)
{
    // ── Step 1: render to QImage ──
    QImage img(REPORT_W, REPORT_H, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::Antialiasing);

    QFont titleF("Microsoft YaHei", 36, QFont::Bold);
    QFont secF("Microsoft YaHei", 22, QFont::Bold);
    QFont bodyF("Microsoft YaHei", 18);
    QFont tblF("Microsoft YaHei", 16);
    QFont smallF("Microsoft YaHei", 14);

    int y = M;

    // ── Title ──
    p.setFont(titleF);
    p.drawText(M, y, W, 80, Qt::AlignHCenter | Qt::AlignVCenter,
               "明场显微成像组件缺陷检测报告");
    y += 90;
    drawHr(p, y, 3);
    y += 10;

    // ── Meta info ──
    drawLine(p, y, QString("生成时间: %1").arg(
        QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")), bodyF);
    drawLine(p, y, QString("检测算法: %1").arg(
        algo == 0 ? "YOLO 目标检测"
                  : (algo == 2 ? "Sobel边缘检测 (传统CV)"
                               : "灰尘颗粒检测 (传统CV)")), bodyF);
    drawLine(p, y, QString("源图片: %1").arg(imagePath), bodyF);
    y += 8;
    drawHr(p, y);
    y += 8;

    // ── Section 1: Parameters ──
    drawLine(p, y, "1. 检测参数", secF);
    y += 4;

    auto paramRow = [&](const QString& a, const QString& b,
                        const QString& c, const QString& d) {
        p.setFont(bodyF);
        p.drawText(M, y, 320, LINE_H, Qt::AlignLeft | Qt::AlignVCenter, a + ":");
        p.drawText(M + 330, y, 360, LINE_H, Qt::AlignLeft | Qt::AlignVCenter, b);
        if (!c.isEmpty()) {
            p.drawText(M + 750, y, 320, LINE_H, Qt::AlignLeft | Qt::AlignVCenter, c + ":");
            p.drawText(M + 1080, y, 500, LINE_H, Qt::AlignLeft | Qt::AlignVCenter, d);
        }
        y += LINE_H;
    };

    if (algo == 0) {
        paramRow("置信度阈值", QString::number(confThresh, 'f', 2),
                 "NMS 阈值", QString::number(nmsThresh, 'f', 2));
    } else if (algo == 2) {
        paramRow("CLAHE 对比度", QString::number(ep.claheClip, 'f', 1),
                 "CLAHE 分块", QString::number(ep.claheTileGrid));
        paramRow("边缘阈值", QString::number(ep.edgeThreshold),
                 "Sobel 核", QString::number(ep.sobelKSize));
        paramRow("膨胀次数", QString::number(ep.dilateIter),
                 "最小框面积", QString("%1 px²").arg(ep.minBboxArea));
        paramRow("NMS IoU",
                 QString::number(ep.nmsIouThresh, 'f', 2),
                 "NMS 包含", QString::number(ep.nmsContainThresh, 'f', 2));
    } else {
        paramRow("CLAHE 对比度", QString::number(dp.claheClip, 'f', 1),
                 "背景估计核", QString::number(dp.bgBlurSize));
        paramRow("最小面积", QString("%1 px²").arg(dp.minArea),
                 "最大面积", dp.maxArea > 0 ? QString("%1 px²").arg(dp.maxArea) : "不限");
        paramRow("膨胀次数", QString::number(dp.dilateIter),
                 "最大迭代", QString::number(dp.maxIter));
        paramRow("NMS 合并",
                 dp.nmsIou > 0 ? QString::number(dp.nmsIou, 'f', 2) : "关闭",
                 "", "");
    }
    y += 4;
    drawHr(p, y);
    y += 8;

    // ── Section 2: Results table ──
    drawLine(p, y, QString("2. 检测结果（共 %1 个目标缺陷）").arg(detections.size()), secF);
    y += 8;

    if (detections.empty()) {
        drawLine(p, y, "未检测到目标缺陷。", bodyF);
    } else {
        // Column positions
        const int cx[7] = {M, M+60, M+240, M+620, M+900, M+1120, M+1300};
        const int cw[7] = {60, 180, 380, 280, 220, 180, W - 1300};

        // Table header
        p.fillRect(cx[0], y, cw[0]+cw[1]+cw[2]+cw[3]+cw[4]+cw[5]+cw[6], LINE_H,
                   QColor(80, 80, 80));
        p.setFont(tblF);
        p.setPen(Qt::white);
        p.drawText(cx[0]+8, y, cw[0], LINE_H, Qt::AlignVCenter, "#");
        p.drawText(cx[1],    y, cw[1], LINE_H, Qt::AlignVCenter, "缺陷类别");
        p.drawText(cx[2],    y, cw[2], LINE_H, Qt::AlignVCenter, "位置 (x, y)");
        p.drawText(cx[3],    y, cw[3], LINE_H, Qt::AlignVCenter, "尺寸 (w × h)");
        p.drawText(cx[4],    y, cw[4], LINE_H, Qt::AlignVCenter, "面积 (px²)");
        p.drawText(cx[5],    y, cw[5], LINE_H, Qt::AlignVCenter, "置信度");
        p.drawText(cx[6],    y, cw[6], LINE_H, Qt::AlignVCenter, "说明");
        p.setPen(Qt::black);
        y += LINE_H;

        // Data rows
        for (size_t i = 0; i < detections.size(); ++i) {
            const auto& d = detections[i];
            int area = d.bounding_box.width * d.bounding_box.height;
            QString desc;
            if (area < 500)       desc = "微小缺陷";
            else if (area < 2000) desc = "小缺陷";
            else if (area < 8000) desc = "中等缺陷";
            else                  desc = "大型缺陷";

            // Alternating row background
            if (i % 2 == 0)
                p.fillRect(cx[0], y, cw[0]+cw[1]+cw[2]+cw[3]+cw[4]+cw[5]+cw[6],
                           LINE_H, QColor(245, 245, 248));

            p.setFont(smallF);
            p.setPen(Qt::black);
            p.drawText(cx[0]+8, y, cw[0], LINE_H, Qt::AlignVCenter, QString::number(i+1));
            p.drawText(cx[1],    y, cw[1], LINE_H, Qt::AlignVCenter,
                       QString::fromStdString(d.class_name.empty()?"-":d.class_name));
            p.drawText(cx[2],    y, cw[2], LINE_H, Qt::AlignVCenter,
                       QString("(%1, %2)").arg(d.bounding_box.x).arg(d.bounding_box.y));
            p.drawText(cx[3],    y, cw[3], LINE_H, Qt::AlignVCenter,
                       QString("%1 × %2").arg(d.bounding_box.width).arg(d.bounding_box.height));
            p.drawText(cx[4],    y, cw[4], LINE_H, Qt::AlignVCenter, QString::number(area));
            p.drawText(cx[5],    y, cw[5], LINE_H, Qt::AlignVCenter,
                       QString::number(d.confidence, 'f', 2));
            p.drawText(cx[6],    y, cw[6], LINE_H, Qt::AlignVCenter, desc);
            y += LINE_H;
        }
    }
    y += 8;
    drawHr(p, y);
    y += 8;

    // ── Section 3: Annotated image ──
    drawLine(p, y, "3. 标注结果图", secF);
    y += 8;

    if (!annotated.empty()) {
        cv::Mat rgb;
        if (annotated.channels() == 3)
            cv::cvtColor(annotated, rgb, cv::COLOR_BGR2RGB);
        else
            rgb = annotated;
        QImage qi(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                  QImage::Format_RGB888);
        QImage qic = qi.copy();  // detach from cv::Mat buffer

        int maxW = W, maxH = REPORT_H - y - M;
        double s = std::min(1.0,
            std::min(static_cast<double>(maxW) / qic.width(),
                     static_cast<double>(maxH) / qic.height()));
        int iw = static_cast<int>(qic.width() * s);
        int ih = static_cast<int>(qic.height() * s);
        p.drawImage(M + (W - iw) / 2, y,
                    qic.scaled(iw, ih, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // ── Footer ──
    p.setFont(smallF);
    p.setPen(QColor(150, 150, 150));
    p.drawText(M, REPORT_H - M, W, 30, Qt::AlignRight | Qt::AlignVCenter,
               "明场显微成像组件缺陷检测报告");
    p.setPen(Qt::black);

    p.end();

    // ── Step 2: wrap QImage in PDF ──
    QPdfWriter writer(pdfPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(REPORT_DPI);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    writer.setTitle("明场显微成像组件缺陷检测报告");

    QPainter pp(&writer);
    pp.drawImage(0, 0, img);
    pp.end();

    return true;
}

/// One entry in a batch report.
struct BatchEntry {
    QString filePath;
    cv::Mat annotated;
    std::vector<detector::Detection> detections;
};

/// Generate combined batch PDF report — all results in one file.
bool generateBatchPdfReport(const QString& pdfPath,
                            int algo,
                            const detector::DustDetectionParams& dp,
                            const detector::EdgeDetectionParams& ep,
                            float confThresh, float nmsThresh,
                            const std::vector<BatchEntry>& entries,
                            const QStringList& errors)
{
    // ── Step 1: prepare data ──
    int totalDets = 0;
    for (const auto& e : entries) totalDets += static_cast<int>(e.detections.size());

    // Estimate pages per entry (image height scaled to ~1/3 page) + table
    const int imgPageH = REPORT_H / 3;
    int tableRows = 0;
    for (const auto& e : entries) tableRows += static_cast<int>(e.detections.size()) + 3; // header + rows

    const int headerH = 300;
    const int entryHeaderH = 120;
    int totalH = headerH + static_cast<int>(entries.size()) * (entryHeaderH + imgPageH + 60)
                 + tableRows * LINE_H + M;
    if (totalH < REPORT_H) totalH = REPORT_H;

    // ── Step 2: render to large QImage (multi-page content stacked vertically) ──
    QImage img(REPORT_W, totalH, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::Antialiasing);

    QFont titleF("Microsoft YaHei", 36, QFont::Bold);
    QFont secF("Microsoft YaHei", 22, QFont::Bold);
    QFont bodyF("Microsoft YaHei", 18);
    QFont tblF("Microsoft YaHei", 16);
    QFont smallF("Microsoft YaHei", 14);

    auto drawLine = [&](int& y, const QString& t, const QFont& f, const QColor& c = Qt::black) {
        p.setFont(f); p.setPen(c);
        p.drawText(M, y, W, LINE_H, Qt::AlignLeft | Qt::AlignVCenter, t);
        y += LINE_H;
    };
    auto drawHr = [&](int& y, int thick = 1) {
        p.setPen(QPen(QColor(100,100,100), thick));
        p.drawLine(M, y, M + W, y); p.setPen(Qt::black); y += 16;
    };

    int y = M;

    // ── Header ──
    p.setFont(titleF);
    p.drawText(M, y, W, 80, Qt::AlignHCenter | Qt::AlignVCenter,
               "明场显微成像组件缺陷检测报告");
    y += 90;
    drawHr(y, 3); y += 10;

    drawLine(y, QString("生成时间: %1").arg(
        QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")), bodyF);
    drawLine(y, QString("检测算法: %1").arg(
        algo == 0 ? "YOLO 目标检测"
                  : (algo == 2 ? "Sobel边缘检测 (传统CV)"
                               : "灰尘颗粒检测 (传统CV)")), bodyF);
    drawLine(y, QString("共检测 %1 张图片，检出 %2 个目标缺陷")
        .arg(entries.size()).arg(totalDets), bodyF);

    if (algo == 0) {
        drawLine(y, QString("参数: 置信度=%1  NMS=%2")
            .arg(confThresh, 0, 'f', 2).arg(nmsThresh, 0, 'f', 2), bodyF);
    } else if (algo == 2) {
        drawLine(y, QString("参数: CLAHE=%1/%2  边缘阈值=%3  Sobel核=%4  膨胀=%5  最小框=%6  NMS(IoU=%7, 包含=%8)")
            .arg(ep.claheClip, 0, 'f', 1).arg(ep.claheTileGrid)
            .arg(ep.edgeThreshold).arg(ep.sobelKSize)
            .arg(ep.dilateIter).arg(ep.minBboxArea)
            .arg(ep.nmsIouThresh, 0, 'f', 2).arg(ep.nmsContainThresh, 0, 'f', 2), bodyF);
    } else {
        drawLine(y, QString("参数: CLAHE=%1  核=%2  面积=%3-%4  膨胀=%5  迭代=%6  NMS合并=%7")
            .arg(dp.claheClip, 0, 'f', 1).arg(dp.bgBlurSize)
            .arg(dp.minArea).arg(dp.maxArea > 0 ? QString::number(dp.maxArea) : "不限")
            .arg(dp.dilateIter).arg(dp.maxIter)
            .arg(dp.nmsIou > 0 ? QString::number(dp.nmsIou, 'f', 2) : "关闭"), bodyF);
    }

    if (!errors.isEmpty()) {
        p.setPen(QColor(200, 50, 50));
        drawLine(y, "失败: " + errors.join(", "), smallF);
        p.setPen(Qt::black);
    }
    y += 8;
    drawHr(y); y += 12;

    // ── Per-image sections ──
    const int cols[7] = {M, M+60, M+240, M+620, M+900, M+1120, M+1300};
    const int cw[7] = {60, 180, 380, 280, 220, 180, W - 1300};

    for (size_t ei = 0; ei < entries.size(); ++ei) {
        const auto& e = entries[ei];
        drawLine(y, QString("图片 %1/%2: %3").arg(ei+1).arg(entries.size())
            .arg(QFileInfo(e.filePath).fileName()), secF);
        y += 6;

        // Table for this entry
        if (e.detections.empty()) {
            drawLine(y, "  未检出缺陷", bodyF);
        } else {
            // Header
            p.fillRect(cols[0], y, cols[6]+cw[6]-cols[0], LINE_H, QColor(80,80,80));
            p.setFont(tblF); p.setPen(Qt::white);
            p.drawText(cols[0]+8,y,cw[0],LINE_H,Qt::AlignVCenter,"#");
            p.drawText(cols[1],y,cw[1],LINE_H,Qt::AlignVCenter,"类别");
            p.drawText(cols[2],y,cw[2],LINE_H,Qt::AlignVCenter,"位置");
            p.drawText(cols[3],y,cw[3],LINE_H,Qt::AlignVCenter,"尺寸");
            p.drawText(cols[4],y,cw[4],LINE_H,Qt::AlignVCenter,"面积");
            p.drawText(cols[5],y,cw[5],LINE_H,Qt::AlignVCenter,"置信度");
            p.drawText(cols[6],y,cw[6],LINE_H,Qt::AlignVCenter,"说明");
            p.setPen(Qt::black); y += LINE_H;

            for (size_t i = 0; i < e.detections.size(); ++i) {
                if (i % 2 == 0)
                    p.fillRect(cols[0], y, cols[6]+cw[6]-cols[0], LINE_H, QColor(245,245,248));
                const auto& d = e.detections[i];
                int area = d.bounding_box.width * d.bounding_box.height;
                QString desc = area<500?"微小":area<2000?"小":area<8000?"中":"大";
                p.setFont(smallF);
                p.drawText(cols[0]+8,y,cw[0],LINE_H,Qt::AlignVCenter,QString::number(i+1));
                p.drawText(cols[1],y,cw[1],LINE_H,Qt::AlignVCenter,
                    QString::fromStdString(d.class_name.empty()?"-":d.class_name));
                p.drawText(cols[2],y,cw[2],LINE_H,Qt::AlignVCenter,
                    QString("(%1,%2)").arg(d.bounding_box.x).arg(d.bounding_box.y));
                p.drawText(cols[3],y,cw[3],LINE_H,Qt::AlignVCenter,
                    QString("%1×%2").arg(d.bounding_box.width).arg(d.bounding_box.height));
                p.drawText(cols[4],y,cw[4],LINE_H,Qt::AlignVCenter,QString::number(area));
                p.drawText(cols[5],y,cw[5],LINE_H,Qt::AlignVCenter,
                    QString::number(d.confidence,'f',2));
                p.drawText(cols[6],y,cw[6],LINE_H,Qt::AlignVCenter,desc+"缺陷");
                y += LINE_H;
            }
        }
        y += 8;

        // Annotated image (avoid page break through image)
        if (!e.annotated.empty()) {
            cv::Mat rgb;
            cv::cvtColor(e.annotated, rgb, cv::COLOR_BGR2RGB);
            QImage qi(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
            QImage qic = qi.copy();
            int maxW = W, maxH = REPORT_H / 3;
            double s = std::min(1.0, std::min((double)maxW/qic.width(), (double)maxH/qic.height()));
            int iw = static_cast<int>(qic.width()*s), ih = static_cast<int>(qic.height()*s);

            // If image would cross a page boundary, pad to next page
            int pageRemain = REPORT_H - (y % REPORT_H);
            if (ih + 20 > pageRemain)
                y = ((y / REPORT_H) + 1) * REPORT_H;

            p.drawImage(M + (W-iw)/2, y, qic.scaled(iw, ih, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            y += ih + 12;
        }
        drawHr(y); y += 8;
    }

    // Footer
    p.setFont(smallF);
    p.setPen(QColor(150,150,150));
    p.drawText(M, totalH - M, W, 30, Qt::AlignRight | Qt::AlignVCenter,
               "明场显微成像组件缺陷检测报告");
    p.end();

    // ── Step 3: split into A4 pages and write PDF ──
    QPdfWriter writer(pdfPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(REPORT_DPI);
    writer.setPageMargins(QMarginsF(0,0,0,0), QPageLayout::Millimeter);
    writer.setTitle("明场显微成像组件缺陷检测报告");

    QPainter pp(&writer);
    int pageY = 0;
    while (pageY < totalH) {
        int pageH = std::min(REPORT_H, totalH - pageY);
        QImage page = img.copy(0, pageY, REPORT_W, pageH);
        pp.drawImage(0, 0, page);
        pageY += REPORT_H;
        if (pageY < totalH) writer.newPage();
    }
    pp.end();

    return true;
}

} // anonymous namespace

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
    ui.algorithmCombo->setCurrentIndex(std::clamp(algo, 0, 2));
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

detector::EdgeDetectionParams DetectionSettingsDialog::edgeParams() const {
    detector::EdgeDetectionParams p;
    p.claheClip         = ui.edgeClaheSpin->value();
    p.claheTileGrid     = ui.edgeTileSpin->value();
    p.edgeThreshold     = ui.edgeThreshSpin->value();
    p.sobelKSize        = ui.sobelKSpin->value() | 1;
    p.dilateIter        = ui.edgeDilateSpin->value();
    p.minBboxArea       = ui.minBboxSpin->value();
    p.nmsIouThresh      = ui.nmsIouThreshSpin->value();
    p.nmsContainThresh  = ui.nmsContainThreshSpin->value();
    return p;
}

void DetectionSettingsDialog::setEdgeParams(const detector::EdgeDetectionParams& p) {
    ui.edgeClaheSpin->setValue(p.claheClip);
    ui.edgeTileSpin->setValue(p.claheTileGrid);
    ui.edgeThreshSpin->setValue(p.edgeThreshold);
    ui.sobelKSpin->setValue(p.sobelKSize);
    ui.edgeDilateSpin->setValue(p.dilateIter);
    ui.minBboxSpin->setValue(p.minBboxArea);
    ui.nmsIouThreshSpin->setValue(p.nmsIouThresh);
    ui.nmsContainThreshSpin->setValue(p.nmsContainThresh);
}

void DetectionSettingsDialog::onAlgorithmChanged(int index) {
    const bool yolo = (index == 0);
    const bool dust = (index == 1);
    const bool edge = (index == 2);
    ui.modelGroup->setVisible(yolo);
    ui.thresholdsGroup->setVisible(yolo);
    ui.dustGroup->setVisible(dust);
    ui.edgeGroup->setVisible(edge);
    // Collapse/expand to exactly fit visible widgets
    layout()->setSizeConstraint(QLayout::SetFixedSize);
    adjustSize();
    layout()->setSizeConstraint(QLayout::SetDefaultConstraint);
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

void DetectionSettingsDialog::onBrowseParam() {
    QString cur = ui.paramPathEdit->text();
    QString dir = cur.isEmpty() ? QCoreApplication::applicationDirPath()
                                : QFileInfo(cur).absolutePath();
    QString path = QFileDialog::getOpenFileName(this, "选择 Param 文件", dir,
        "NCNN Param (*.param);;所有文件 (*.*)");
    if (!path.isEmpty()) ui.paramPathEdit->setText(path);
}

void DetectionSettingsDialog::onBrowseBin() {
    QString cur = ui.binPathEdit->text();
    QString dir = cur.isEmpty() ? QCoreApplication::applicationDirPath()
                                : QFileInfo(cur).absolutePath();
    QString path = QFileDialog::getOpenFileName(this, "选择 Bin 文件", dir,
        "NCNN Bin (*.bin);;所有文件 (*.*)");
    if (!path.isEmpty()) ui.binPathEdit->setText(path);
}

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
        if (!folder.isEmpty()) ui.imagePathEdit->setText(folder);
    } else {
        // Single image mode
        QString path = QFileDialog::getOpenFileName(this, "选择待检测图片", dir,
            "图片文件 (*.jpg *.jpeg *.png *.bmp *.tif *.tiff);;所有文件 (*.*)");
        if (!path.isEmpty()) ui.imagePathEdit->setText(path);
    }
}

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
        app_->detector().setConfidenceThreshold(static_cast<float>(ui.confidenceSpin->value()));
        app_->detector().setNmsThreshold(static_cast<float>(ui.nmsSpin->value()));
    }

    if (algo == 0 && !app_->isModelLoaded()) {
        QMessageBox::warning(this, "警告", "请先加载检测模型");
        return;
    }

    const detector::DustDetectionParams dp = dustParams();
    const detector::EdgeDetectionParams ep = edgeParams();
    const float conf = static_cast<float>(ui.confidenceSpin->value());
    const float nms  = static_cast<float>(ui.nmsSpin->value());
    const bool batch = (modeCombo_ && modeCombo_->currentData().toInt() == 1);

    if (batch)
        runBatchDetection(path, algo, dp, ep, conf, nms);
    else
        runSingleDetection(path, algo, dp, ep, conf, nms);
}

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

    struct R { bool imgOk=false, pdfOk=false; int cnt=0;
               QString out, pdf, jp; };
    auto r = std::make_shared<R>();

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
        auto dets = app_->detect(image);
        cv::Mat ann = app_->drawDetections(image, dets);
        QFileInfo fi(imgPath);
        QString d = fi.absolutePath(), bn = fi.completeBaseName(), sf = fi.suffix();
        r->out  = d + "/" + bn + "_detected." + sf;
        r->pdf  = d + "/" + bn + "_report.pdf";
        r->cnt  = static_cast<int>(dets.size());
        r->imgOk = cv::imwrite(r->out.toStdString(), ann);
        r->pdfOk = generatePdfReport(r->pdf, imgPath, algo, dp, ep, conf, nms, ann, dets);
        auto jp = app_->saveDetectionsJson(dets, image, imgPath.toStdString());
        r->jp = QString::fromStdString(jp);
    }));
}

void DetectionSettingsDialog::runBatchDetection(
    const QString& folderPath, int algo,
    const detector::DustDetectionParams& dp,
    const detector::EdgeDetectionParams& ep,
    float conf, float nms)
{
    // ── Collect image files ──
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

    // Accumulate results across all images
    struct BatchResult {
        int totalImgs = 0, totalDets = 0, totalOk = 0;
        QStringList errors;
        QString outFolder;
    };
    auto br = std::make_shared<BatchResult>();
    br->totalImgs = total;
    br->outFolder = folderPath + "/_batch_result";
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
        if (!br->errors.isEmpty()) {
            msg += "\n\n失败: \n" + br->errors.join("\n");
        }
        QMessageBox::information(this, "完成", msg);
    });

    w->setFuture(QtConcurrent::run([=]() {
        int batchDets = 0, okCnt = 0;
        QStringList errs;
        std::vector<BatchEntry> batchEntries;

        for (int idx = 0; idx < files.size(); ++idx) {
            QString fp = dir.absoluteFilePath(files[idx]);
            cv::Mat img = cv::imread(fp.toStdString(), cv::IMREAD_COLOR);
            if (img.empty()) {
                errs << files[idx] + ": 无法读取";
                continue;
            }
            auto dets = app_->detect(img);
            cv::Mat ann = app_->drawDetections(img, dets);
            batchDets += static_cast<int>(dets.size());

            QFileInfo fi(files[idx]);
            QString bn = fi.completeBaseName(), sf = fi.suffix();
            QString outImg = br->outFolder + "/" + bn + "_detected." + sf;

            bool ok = cv::imwrite(outImg.toStdString(), ann);
            app_->saveDetectionsJson(dets, img, fp.toStdString());
            if (ok) {
                okCnt++;
                batchEntries.push_back({fp, ann, dets});
            } else {
                errs << files[idx] + ": 保存失败";
            }

            // Update progress on UI thread
            QMetaObject::invokeMethod(qApp, [=]() {
                progress->setValue(idx + 1);
                progress->setLabelText(
                    QString("正在处理 %1 / %2 ...").arg(idx + 1).arg(total));
            }, Qt::QueuedConnection);
        }

        // Generate combined batch PDF
        if (!batchEntries.empty()) {
            QString batchPdf = br->outFolder + "/_batch_report.pdf";
            generateBatchPdfReport(batchPdf, algo, dp, ep, conf, nms, batchEntries, errs);
        }

        br->totalDets = batchDets;
        br->totalOk   = okCnt;
        br->errors    = errs;
    }));
}
