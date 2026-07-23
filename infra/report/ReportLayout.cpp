// ReportLayout.cpp — 报告布局计算实现

#include "ReportLayout.h"
#include "core/detector/IDetector.h"
#include <spdlog/spdlog.h>

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>
#include <opencv2/imgproc.hpp>

namespace report {

// ═══════════════════════════════════════════════════════════════
// ReportFonts
// ═══════════════════════════════════════════════════════════════

static void setFontFamilies(QFont& f, const QStringList& families) {
    f.setFamilies(families);
}

ReportFonts::ReportFonts() {
    title   = QFont("Microsoft YaHei", 36, QFont::Bold);
    section = QFont("Microsoft YaHei", 22, QFont::Bold);
    body    = QFont("Microsoft YaHei", 18);
    tblHdr  = QFont("Microsoft YaHei", 16);
    tblData = QFont("Microsoft YaHei", 14);
    hdrFtr  = QFont("Microsoft YaHei", 12);

    // Fallback: sans-serif on non-Windows platforms
    QStringList families{"Microsoft YaHei", "sans-serif"};
    setFontFamilies(title,   families);
    setFontFamilies(section, families);
    setFontFamilies(body,    families);
    setFontFamilies(tblHdr,  families);
    setFontFamilies(tblData, families);
    setFontFamilies(hdrFtr,  families);
}

// ═══════════════════════════════════════════════════════════════
// Metrics
// ═══════════════════════════════════════════════════════════════

Metrics::Metrics() {
    double sdpi = 96.0;
    if (auto* screen = QGuiApplication::primaryScreen())
        sdpi = screen->logicalDotsPerInch();
    if (sdpi <= 0) sdpi = 96.0;
    scale_ = static_cast<double>(kDpi) / sdpi;
}

double Metrics::textWidth(const QFont& f, const QString& s) const {
    if (s.isEmpty()) return 0;
    QFontMetricsF fm(f);
    return fm.horizontalAdvance(s) * scale_;
}

double Metrics::lineHeight(const QFont& f) const {
    QFontMetricsF fm(f);
    // Qt returns height in screen pixels; scale to target DPI + small padding
    return (fm.height() + fm.leading()) * scale_ + 3;
}

double Metrics::ascent(const QFont& f) const {
    QFontMetricsF fm(f);
    return fm.ascent() * scale_;
}

QString Metrics::elide(const QFont& f, const QString& s, double maxW) const {
    if (s.isEmpty() || maxW <= 0) return s;
    QFontMetricsF fm(f);
    return fm.elidedText(s, Qt::ElideRight, static_cast<int>(maxW / scale_));
}

QImage Metrics::matToImage(const cv::Mat& bgr, int maxW, int maxH) {
    if (bgr.empty()) return {};
    cv::Mat rgb;
    if (bgr.channels() == 3)
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    else
        rgb = bgr;

    QImage qi(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
              QImage::Format_RGB888);
    QImage qic = qi.copy();  // deep copy — cv::Mat may go out of scope

    if (qic.width() <= maxW && qic.height() <= maxH)
        return qic;

    double s = std::min(1.0,
        std::min(static_cast<double>(maxW) / qic.width(),
                 static_cast<double>(maxH) / qic.height()));
    return qic.scaled(static_cast<int>(qic.width() * s),
                      static_cast<int>(qic.height() * s),
                      Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// ═══════════════════════════════════════════════════════════════
// Layout cursor — 管理页面创建和光标推进
// ═══════════════════════════════════════════════════════════════

class Cursor {
public:
    explicit Cursor(const Metrics& m, const ReportFonts& f)
        : metrics(m), fonts(f) {}

    std::vector<Page> pages;

    /// 添加元素并推进光标。若剩余空间不足则自动换页。
    void place(Element elem) {
        if (elem.rect.bottom() > kContentH) {
            newPage();
            elem.rect.moveTop(0);
        }
        curPage().elements.push_back(std::move(elem));
    }

    /// 当前光标 Y（内容坐标系）
    double y() const { return y_; }

    /// 设置光标 Y（用于精确控制垂直位置）
    void setY(double ny) { y_ = ny; }

    /// 添加文本行并推进光标。
    double addText(const QString& text, const QFont& font,
                   QColor color = Qt::black, bool center = false) {
        double h = metrics.lineHeight(font);
        ensureSpace(h);
        Element e;
        e.type  = ElemType::Text;
        e.font  = font;
        e.color = color;
        e.text  = text;
        e.alignment = center ? (Qt::AlignHCenter | Qt::AlignVCenter)
                             : (Qt::AlignLeft   | Qt::AlignVCenter);
        e.rect  = QRectF(0, y_, kContentW, h);
        place(std::move(e));
        y_ += h;
        return y_;
    }

    void addHr(int thickness = 1) {
        double y = y_;
        double h = 16;  // HR 行高（分割线 + 上下间距）
        ensureSpace(h);
        // 分割线自身
        double lineY = y + h / 2.0;
        Element e;
        e.type      = ElemType::Hr;
        e.thickness = thickness;
        e.rect      = QRectF(0, lineY, kContentW, static_cast<double>(thickness));
        place(std::move(e));
        // 光标推到 HR 下方
        ensureSpace(h);
        // 实际上需要一个 spacer — 让我们简化：HR 后光标移动到 y + h
        newPageIfNeeded(y + h);
        y_ = y + h;
    }

    void addSpacer(double dy) {
        ensureSpace(dy);
        y_ += dy;
    }

    // ══ 参数行（左右两列）══
    void addParamRow(const QString& a, const QString& b,
                     const QString& c, const QString& d) {
        double h = metrics.lineHeight(fonts.body);
        ensureSpace(h);

        double col1w = metrics.textWidth(fonts.body, a + ":");
        double col2w = metrics.textWidth(fonts.body, b);
        double col3w = c.isEmpty() ? 0 : metrics.textWidth(fonts.body, c + ":");
        double col4w = d.isEmpty() ? 0 : metrics.textWidth(fonts.body, d);

        // 简单布局：label:value  |  label:value
        const double gap = 60;
        double x1 = 0;
        double x2 = x1 + col1w + 10 + col2w;
        double x3 = x2 + gap;
        double x4 = x3 + col3w + 10 + col4w;

        Element ea;
        ea.type  = ElemType::Text;
        ea.font  = fonts.body;
        ea.rect  = QRectF(x1, y_, col1w, h);
        ea.text  = a + ":";
        ea.alignment = Qt::AlignRight | Qt::AlignVCenter;
        place(std::move(ea));

        Element eb;
        eb.type  = ElemType::Text;
        eb.font  = fonts.body;
        eb.rect  = QRectF(x1 + col1w + 10, y_, col2w + gap, h);
        eb.text  = b;
        eb.alignment = Qt::AlignLeft | Qt::AlignVCenter;
        place(std::move(eb));

        if (!c.isEmpty()) {
            Element ec;
            ec.type  = ElemType::Text;
            ec.font  = fonts.body;
            ec.rect  = QRectF(x2 + gap, y_, col3w, h);
            ec.text  = c + ":";
            ec.alignment = Qt::AlignRight | Qt::AlignVCenter;
            place(std::move(ec));

            Element ed;
            ed.type  = ElemType::Text;
            ed.font  = fonts.body;
            ed.rect  = QRectF(x2 + gap + col3w + 10, y_, col4w, h);
            ed.text  = d;
            ed.alignment = Qt::AlignLeft | Qt::AlignVCenter;
            place(std::move(ed));
        }
        y_ += h;
    }

    // ══ 检测结果表格 ══
    void addDetectionTable(const std::vector<detector::Detection>& detections,
                           int startId = 1)
    {
        const auto& hdrFont = fonts.tblHdr;
        const auto& datFont = fonts.tblData;
        double rowH = std::max(metrics.lineHeight(hdrFont),
                               metrics.lineHeight(datFont));

        // ── 计算列宽 ──
        QStringList headers{"#", "缺陷类别", "位置 (x, y)", "尺寸 (w × h)",
                           "面积 (px²)", "置信度", "说明"};
        std::vector<double> colW(headers.size());

        colW[0] = metrics.textWidth(hdrFont, "000") + 16;
        colW[1] = metrics.textWidth(hdrFont, headers[1]) + 16;
        colW[2] = metrics.textWidth(hdrFont, "(00000, 00000)") + 16;
        colW[3] = metrics.textWidth(hdrFont, "00000 × 00000") + 16;
        colW[4] = metrics.textWidth(hdrFont, "00000000") + 16;
        colW[5] = metrics.textWidth(hdrFont, "0.00") + 16;
        colW[6] = 0;  // 说明列，剩余空间

        for (const auto& d : detections) {
            colW[1] = std::max(colW[1], metrics.textWidth(datFont,
                QString::fromStdString(d.class_name)) + 20);
            colW[2] = std::max(colW[2], metrics.textWidth(datFont,
                QString("(%1, %2)").arg(d.bounding_box.x).arg(d.bounding_box.y)) + 20);
            int w = d.bounding_box.width, h = d.bounding_box.height;
            colW[3] = std::max(colW[3], metrics.textWidth(datFont,
                QString("%1 × %2").arg(w).arg(h)) + 20);
            colW[4] = std::max(colW[4], metrics.textWidth(datFont,
                QString::number(w * h)) + 20);
            colW[5] = std::max(colW[5], metrics.textWidth(datFont,
                QString::number(d.confidence, 'f', 2)) + 20);
        }

        double totalW = 0;
        for (int i = 0; i < 6; ++i) totalW += colW[i];
        colW[6] = std::max(0.0, kContentW - totalW);

        // 如果仍然溢出，等比缩放
        if (colW[6] < 0) {
            double scale = kContentW / totalW;
            for (auto& w : colW) w = std::floor(w * scale);
            colW[6] = 0;
        }

        // ── 计算列偏移 ──
        std::vector<double> colX(headers.size());
        double cx = 0;
        for (size_t i = 0; i < colX.size(); ++i) {
            colX[i] = cx;
            cx += colW[i];
        }

        // ── 构建表格行 ──
        // Header
        {
            ensureSpace(rowH);
            std::vector<Element::Cell> hdrRow;
            for (int i = 0; i < (int)headers.size(); ++i) {
                Element::Cell cell;
                cell.text = headers[i];
                cell.bg   = QColor(80, 80, 80);
                cell.fg   = Qt::white;
                hdrRow.push_back(cell);
            }

            auto td = std::make_shared<Element::TableData>();
            td->colX  = colX;
            td->colW  = colW;
            td->font  = hdrFont;
            td->rowH  = rowH;
            td->rows.push_back(std::move(hdrRow));

            // Data rows
            int id = startId;
            for (const auto& d : detections) {
                ensureSpace(rowH);
                int area = d.bounding_box.width * d.bounding_box.height;
                QString desc;
                if (area < 500)       desc = "微小缺陷";
                else if (area < 2000) desc = "小缺陷";
                else if (area < 8000) desc = "中等缺陷";
                else                  desc = "大型缺陷";

                std::vector<Element::Cell> dataRow(7);
                dataRow[0] = { QString::number(id++), {}, Qt::black };
                dataRow[1] = { QString::fromStdString(d.class_name), {}, Qt::black };
                dataRow[2] = { QString("(%1, %2)").arg(d.bounding_box.x)
                                                     .arg(d.bounding_box.y), {}, Qt::black };
                dataRow[3] = { QString("%1 × %2").arg(d.bounding_box.width)
                                                  .arg(d.bounding_box.height), {}, Qt::black };
                dataRow[4] = { QString::number(area), {}, Qt::black };
                dataRow[5] = { QString::number(d.confidence, 'f', 2), {}, Qt::black };
                dataRow[6] = { desc, {}, Qt::black };
                td->rows.push_back(std::move(dataRow));
            }

            // 表格元素
            Element te;
            te.type  = ElemType::Table;
            te.table = td;
            te.rect  = QRectF(0, y_, kContentW, rowH * td->rows.size());
            double tableH = rowH * td->rows.size();
            place(std::move(te));
            y_ += tableH;
        }
    }

    // ══ 标注图 ══
    // 返回实际图像高度（用于后续间距）
    double addImage(const QImage& img) {
        if (img.isNull()) return 0;
        int iw = img.width(), ih = img.height();

        // 如果图片太大，缩放到 CONTENT_H * 0.4
        int maxH = static_cast<int>(kContentH * 0.4);
        if (ih > maxH) {
            double s = static_cast<double>(maxH) / ih;
            iw = static_cast<int>(iw * s);
            ih = maxH;
        }

        // 如果图片加标注行超过剩余空间，换页
        double needed = metrics.lineHeight(fonts.section) + 8 + ih + 12;
        if (y_ + needed > kContentH) {
            newPage();
        }

        // 图片以当前 Y 为起点，居中
        double x = std::max(0.0, (kContentW - iw) / 2.0);
        ensureSpace(ih);

        Element e;
        e.type  = ElemType::Image;
        e.rect  = QRectF(x, y_, static_cast<double>(iw), static_cast<double>(ih));
        e.image = (iw == img.width() && ih == img.height())
                      ? img
                      : img.scaled(iw, ih, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        place(std::move(e));
        y_ += ih;
        return ih;
    }

    const Metrics& metrics;
    const ReportFonts& fonts;

private:
    double y_ = 0;   // 内容坐标系，左上角原点

    Page& curPage() {
        if (pages.empty()) newPage();
        return pages.back();
    }

    void newPage() {
        pages.push_back({});
        y_ = 0;
    }

    void newPageIfNeeded(double nextY) {
        if (nextY > kContentH) {
            newPage();
        } else {
            y_ = nextY;
        }
    }

    void ensureSpace(double neededH) {
        if (y_ + neededH > kContentH) {
            newPage();
        }
    }
};

// ═══════════════════════════════════════════════════════════════
// 算法名称
// ═══════════════════════════════════════════════════════════════

static const char* algoName(int algo) {
    switch (algo) {
        case 2:  return "Sobel边缘检测 (传统CV)";
        case 1:  return "灰尘颗粒检测 (传统CV)";
        default: return "YOLO 目标检测";
    }
}

// ═══════════════════════════════════════════════════════════════
// computeLayout — 单张图片 — 向量化渲染
// ═══════════════════════════════════════════════════════════════

std::vector<Page> computeLayout(const ReportInput& in) {
    SPDLOG_INFO("[Report] ReportLayout: starting single report (algo={}, dets={})",
                in.algorithm, in.detections.size());
    ReportFonts fonts;
    Metrics metrics;
    Cursor cur(metrics, fonts);

    // ── 标题 ──
    cur.addText("明场显微成像组件缺陷检测报告", fonts.title, Qt::black, true);
    cur.addSpacer(10);
    cur.addHr(3);
    cur.addSpacer(10);

    // ── 元信息 ──
    cur.addText(QString("生成时间: %1").arg(
        QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")), fonts.body);
    cur.addText(QString("检测算法: %1").arg(algoName(in.algorithm)), fonts.body);
    cur.addText(QString("源图片: %1").arg(in.imagePath), fonts.body);
    cur.addSpacer(8);
    cur.addHr();
    cur.addSpacer(8);

    // ── 1. 检测参数 ──
    cur.addText("1. 检测参数", fonts.section);
    cur.addSpacer(4);

    if (in.algorithm == 0) {
        cur.addParamRow("置信度阈值",
                        QString::number(in.confThreshold, 'f', 2),
                        "NMS 阈值",
                        QString::number(in.nmsThreshold, 'f', 2));
    } else if (in.algorithm == 1) {
        cur.addParamRow("CLAHE 对比度",
                        QString::number(in.dustClaheClip, 'f', 1),
                        "背景估计核",
                        QString::number(in.dustBgBlur));
        cur.addParamRow("最小面积",
                        QString("%1 px²").arg(in.dustMinArea),
                        "最大面积",
                        in.dustMaxArea > 0 ? QString("%1 px²").arg(in.dustMaxArea) : "不限");
        cur.addParamRow("膨胀次数",
                        QString::number(in.dustDilateIter),
                        "最大迭代",
                        QString::number(in.dustMaxIter));
        cur.addParamRow("NMS 合并",
                        in.dustNmsIou > 0 ? QString::number(in.dustNmsIou, 'f', 2) : "关闭",
                        "", "");
    } else if (in.algorithm == 2) {
        cur.addParamRow("CLAHE 对比度",
                        QString::number(in.edgeClaheClip, 'f', 1),
                        "CLAHE 分块",
                        QString::number(in.edgeClaheTileGrid));
        cur.addParamRow("边缘阈值",
                        QString::number(in.edgeThreshold),
                        "Sobel 核",
                        QString::number(in.edgeSobelKSize));
        cur.addParamRow("膨胀次数",
                        QString::number(in.edgeDilateIter),
                        "最小框面积",
                        QString("%1 px²").arg(in.edgeMinBboxArea));
        cur.addParamRow("NMS IoU",
                        QString::number(in.edgeNmsIouThresh, 'f', 2),
                        "NMS 包含",
                        QString::number(in.edgeNmsContainThresh, 'f', 2));
    }
    cur.addSpacer(4);
    cur.addHr();
    cur.addSpacer(8);

    // ── 2. 检测结果 ──
    cur.addText(QString("2. 检测结果（共 %1 个目标缺陷）")
                    .arg(in.detections.size()), fonts.section);
    cur.addSpacer(8);

    if (in.detections.empty()) {
        cur.addText("未检测到目标缺陷。", fonts.body);
    } else {
        cur.addDetectionTable(in.detections);
    }
    cur.addSpacer(8);
    cur.addHr();

    // ── 3. 标注结果图 ──
    if (!in.annotatedImage.isNull()) {
        cur.addSpacer(12);
        cur.addText("3. 标注结果图", fonts.section);
        cur.addSpacer(4);
        double imgH = cur.addImage(in.annotatedImage);
        // Add spacer after image for visual breathing room
        (void)imgH;
        cur.addSpacer(12);
        cur.addHr();
    }

    SPDLOG_INFO("[Report] ReportLayout: single report done ({} pages)", cur.pages.size());
    return std::move(cur.pages);
}

// ═══════════════════════════════════════════════════════════════
// computeBatchLayout — 批量 — 向量化渲染
// ═══════════════════════════════════════════════════════════════

std::vector<Page> computeBatchLayout(
    int algorithm,
    const ReportInput& params,
    const std::vector<BatchEntry>& entries,
    const QStringList& errors)
{
    int entryDets = 0;
    for (const auto& e : entries)
        entryDets += static_cast<int>(e.detections.size());
    SPDLOG_INFO("[Report] ReportLayout: starting batch report (algo={}, entries={}, dets={})",
                algorithm, entries.size(), entryDets);
    ReportFonts fonts;
    Metrics metrics;
    Cursor cur(metrics, fonts);

    // ── 标题 ──
    cur.addText("明场显微成像组件缺陷检测报告", fonts.title, Qt::black, true);
    cur.addSpacer(10);
    cur.addHr(3);
    cur.addSpacer(10);

    // ── 汇总 ──
    cur.addText(QString("生成时间: %1").arg(
        QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")), fonts.body);
    cur.addText(QString("检测算法: %1").arg(algoName(algorithm)), fonts.body);

    int totalDets = 0;
    for (const auto& e : entries)
        totalDets += static_cast<int>(e.detections.size());
    cur.addText(QString("共检测 %1 张图片，检出 %2 个目标缺陷")
                    .arg(entries.size()).arg(totalDets), fonts.body);

    // 参数摘要一行
    QString paramLine;
    if (algorithm == 0) {
        paramLine = QString("参数: 置信度=%1  NMS=%2")
                        .arg(params.confThreshold, 0, 'f', 2)
                        .arg(params.nmsThreshold, 0, 'f', 2);
    } else if (algorithm == 1) {
        paramLine = QString("参数: CLAHE=%1  核=%2  面积=%3-%4  膨胀=%5  迭代=%6  NMS合并=%7")
                        .arg(params.dustClaheClip, 0, 'f', 1)
                        .arg(params.dustBgBlur)
                        .arg(params.dustMinArea)
                        .arg(params.dustMaxArea > 0 ? QString::number(params.dustMaxArea) : "不限")
                        .arg(params.dustDilateIter)
                        .arg(params.dustMaxIter)
                        .arg(params.dustNmsIou > 0 ? QString::number(params.dustNmsIou, 'f', 2) : "关闭");
    } else if (algorithm == 2) {
        paramLine = QString("参数: CLAHE=%1  块=%2  阈值=%3  Sobel=%4  膨胀=%5  最小框=%6  IoU=%7  包含=%8")
                        .arg(params.edgeClaheClip, 0, 'f', 1)
                        .arg(params.edgeClaheTileGrid)
                        .arg(params.edgeThreshold)
                        .arg(params.edgeSobelKSize)
                        .arg(params.edgeDilateIter)
                        .arg(params.edgeMinBboxArea)
                        .arg(params.edgeNmsIouThresh, 0, 'f', 2)
                        .arg(params.edgeNmsContainThresh, 0, 'f', 2);
    }
    cur.addText(paramLine, fonts.body);

    if (!errors.isEmpty()) {
        cur.addText("失败: " + errors.join(", "), fonts.tblData, QColor(200, 50, 50));
    }
    cur.addSpacer(8);
    cur.addHr();
    cur.addSpacer(12);

    // ── 逐图明细 ──
    for (size_t ei = 0; ei < entries.size(); ++ei) {
        const auto& entry = entries[ei];
        QString fname = entry.filePath;
        int lastSlash = std::max(fname.lastIndexOf('/'), fname.lastIndexOf('\\'));
        if (lastSlash >= 0) fname = fname.mid(lastSlash + 1);

        cur.addText(QString("图片 %1/%2: %3")
                        .arg(ei + 1).arg(entries.size()).arg(fname), fonts.section);
        cur.addSpacer(6);

        if (entry.detections.empty()) {
            cur.addText("  未检出缺陷", fonts.body);
        } else {
            Cursor tableCur(metrics, fonts);
            // 继承已积累的页面，但重新开始光标
            tableCur.pages = std::move(cur.pages);
            tableCur.setY(cur.y());

            tableCur.addDetectionTable(entry.detections);

            cur.pages = std::move(tableCur.pages);
            cur.setY(tableCur.y());
        }
        cur.addSpacer(8);

        if (!entry.annotatedImage.isNull()) {
            double imgH = cur.addImage(entry.annotatedImage);
            (void)imgH;
            cur.addSpacer(12);
        }
        cur.addHr();
        cur.addSpacer(8);
    }

    SPDLOG_INFO("[Report] ReportLayout: batch report done ({} pages)", cur.pages.size());
    return std::move(cur.pages);
}

} // namespace report
