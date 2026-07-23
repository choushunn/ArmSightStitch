#pragma once

// ── ReportLayout — 报告布局计算（纯数据，可任意线程）────────────
//
// 职责：将检测结果/参数/标注图编排为页面级元素列表，每个元素带有
// 精确的 QRectF（内容坐标系，左上角 = (0,0)，不含页边距）。
//
// 线程安全：布局仅依赖 QFontMetricsF（值语义）和 QImage（共享语义），
// 不使用任何 QPaintDevice，可在 worker 线程执行。
// ─────────────────────────────────────────────────────────────────

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QString>
#include <QColor>
#include <QRectF>
#include <QDateTime>
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>
#include <opencv2/core/mat.hpp>

// 前向声明 Detection（避免引入 OpenCV 头到纯布局模块）
namespace detector { struct Detection; }

namespace report {

// ═══════════════════════════════════════════════════════════════
// 常量
// ═══════════════════════════════════════════════════════════════

constexpr int kDpi       = 300;
constexpr int kPageW     = 2480;   // A4 210mm @300dpi
constexpr int kPageH     = 3508;   // A4 297mm @300dpi
constexpr int kMargin    = 140;    // ~12mm
constexpr int kContentW  = kPageW - kMargin * 2;   // 2200
constexpr int kContentH  = kPageH - kMargin * 2;   // 3228

// ═══════════════════════════════════════════════════════════════
// 字体集（自动添加 sans-serif fallback）
// ═══════════════════════════════════════════════════════════════

struct ReportFonts {
    QFont title;
    QFont section;
    QFont body;
    QFont tblHdr;
    QFont tblData;
    QFont hdrFtr;

    ReportFonts();
};

// ═══════════════════════════════════════════════════════════════
// DPI 感知的文本度量
// ═══════════════════════════════════════════════════════════════

class Metrics {
public:
    Metrics();

    /// 文本宽度（逻辑像素 @300dpi）
    double textWidth(const QFont& f, const QString& s) const;
    /// 行高（含 leading + padding）
    double lineHeight(const QFont& f) const;
    /// 上升部高度
    double ascent(const QFont& f) const;
    /// 截断文本使其宽度 ≤ maxW，返回被截断的文本
    QString elide(const QFont& f, const QString& s, double maxW) const;
    /// 将 cv::Mat BGR 转为缩放后的 QImage，适应给定最大宽高
    static QImage matToImage(const cv::Mat& bgr, int maxW, int maxH);

private:
    double scale_;
};

// ═══════════════════════════════════════════════════════════════
// 元素类型
// ═══════════════════════════════════════════════════════════════

enum class ElemType { Text, Hr, Table, Image };

struct Element {
    ElemType type = ElemType::Text;
    QRectF   rect;            // 内容坐标（不含页边距）

    // ── Text ──
    QString  text;
    QFont    font;
    QColor   color{Qt::black};
    int      alignment{Qt::AlignLeft | Qt::AlignVCenter};  // Qt::Alignment
    int      thickness{1};    // 仅 Hr 有效

    // ── Table ──
    struct Cell { QString text; QColor bg{Qt::transparent}; QColor fg{Qt::black}; };
    struct TableData {
        std::vector<double> colX;       // 每列 X 偏移（相对于 table rect.x）
        std::vector<double> colW;       // 每列宽度
        std::vector<std::vector<Cell>> rows;
        QFont font;
        double rowH;
    };
    std::shared_ptr<TableData> table;   // 仅 Table 有效（shared_ptr 允许拷贝）

    // ── Image ──
    QImage   image;          // 缩放后的 QImage（RGB 格式）
};

struct Page {
    std::vector<Element> elements;
};

// ═══════════════════════════════════════════════════════════════
// 输入
// ═══════════════════════════════════════════════════════════════

struct ReportInput {
    // 元信息
    QString  imagePath;       // 源图路径（仅显示用）
    int      algorithm = 0;   // 0=YOLO, 1=Dust, 2=Edge
    float    confThreshold = 0.3f;
    float    nmsThreshold  = 0.3f;

    // 灰尘参数
    double dustClaheClip  = 2.0;
    int    dustBgBlur     = 31;
    int    dustMinArea    = 50;
    int    dustMaxArea    = 20000;
    int    dustDilateIter = 0;
    int    dustMaxIter    = 4;
    double dustNmsIou     = 0.1;

    // 边缘参数
    double edgeClaheClip       = 2.0;
    int    edgeClaheTileGrid   = 8;
    int    edgeThreshold       = 30;
    int    edgeSobelKSize      = 3;
    int    edgeDilateIter      = 1;
    int    edgeMinBboxArea     = 25;
    double edgeNmsIouThresh    = 0.4;
    double edgeNmsContainThresh = 0.5;

    // 结果
    std::vector<detector::Detection> detections;  // 引用外部数据，布局期间只读
    QImage   annotatedImage;     // 标注图（已缩放为合适尺寸后传入）
};

/// 批量报告的一条记录
struct BatchEntry {
    QString  filePath;
    QImage   annotatedImage;  // 已缩放
    std::vector<detector::Detection> detections;
};

// ═══════════════════════════════════════════════════════════════
// 布局计算
// ═══════════════════════════════════════════════════════════════

/// 单张图片报告布局
std::vector<Page> computeLayout(const ReportInput& in);

/// 批量图片报告布局
std::vector<Page> computeBatchLayout(
    int algorithm,
    const ReportInput& params,          // 参数部分（算法类型 + 参数值）
    const std::vector<BatchEntry>& entries,
    const QStringList& errors);

} // namespace report
