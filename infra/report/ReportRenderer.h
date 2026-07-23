#pragma once

// ── ReportRenderer — 将 Page 列表按页渲染到 QPdfWriter ─────────
//
// 线程：可在 worker 线程执行（内部创建独立的 QPdfWriter + QPainter）。
// 每个文字单元格使用 QPainter::drawText → PDF 中是矢量文字，可选可搜索。
// 内存 O(1)：逐页渲染，不缓存全量内容。
// ─────────────────────────────────────────────────────────────────

#include "ReportLayout.h"

#include <QPainter>
#include <QPdfWriter>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <spdlog/spdlog.h>
#include <functional>

namespace report {

static void renderReport(const QString& pdfPath,
                         const std::vector<Page>& pages,
                         const ReportFonts* fontsOverride = nullptr,
                         std::function<void(int cur, int total)> progressCb = {})
{
    ReportFonts fonts;
    if (fontsOverride) fonts = *fontsOverride;

    const int total = static_cast<int>(pages.size());
    if (total == 0) return;

    SPDLOG_INFO("[Report] ReportRenderer: writing {} pages to {}", total, pdfPath.toStdString());
    QDir().mkpath(QFileInfo(pdfPath).absolutePath());

    QPdfWriter writer(pdfPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(kDpi);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    writer.setTitle("明场显微成像组件缺陷检测报告");

    QPainter p(&writer);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::Antialiasing);

    const QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    for (int pi = 0; pi < total; ++pi) {
        if (pi > 0) {
            bool ok = writer.newPage();
            if (!ok) break;
        }

        const auto& page = pages[pi];

        // ── 渲染内容（加上页边距偏移）──
        for (const auto& el : page.elements) {
            p.save();
            QRectF r = el.rect.translated(kMargin, kMargin);

            switch (el.type) {
            case ElemType::Text: {
                p.setFont(el.font);
                p.setPen(el.color);
                p.drawText(r, el.alignment, el.text);
                break;
            }
            case ElemType::Hr: {
                p.setPen(QPen(QColor(100, 100, 100), el.thickness));
                p.drawLine(QPointF(r.left(), r.top()),
                           QPointF(r.right(), r.top()));
                break;
            }
            case ElemType::Table: {
                if (!el.table) break;
                const auto& td = *el.table;
                double rowH = td.rowH;
                double ty = r.top();
                for (const auto& row : td.rows) {
                    // Background
                    if (!row.empty() && row[0].bg != Qt::transparent) {
                        p.fillRect(QRectF(r.left(), ty, r.width(), rowH), row[0].bg);
                    }
                    // Cells
                    for (size_t ci = 0; ci < row.size() && ci < td.colX.size(); ++ci) {
                        double cx = r.left() + td.colX[ci];
                        double cw = td.colW[ci];
                        if (cw <= 0) continue;
                        QRectF cellR(cx, ty, cw, rowH);
                        p.setFont(td.font);
                        p.setPen(row[ci].fg);
                        // Use QFontMetrics to elide text that doesn't fit
                        QFontMetricsF fm(td.font);
                        QString display = row[ci].text;
                        // Convert cell rect width from logical (300dpi) to screen-space for elide
                        double screenW = cw * 96.0 / static_cast<double>(kDpi);
                        if (fm.horizontalAdvance(display) > screenW && cw > 30) {
                            display = fm.elidedText(display, Qt::ElideRight,
                                                    static_cast<int>(screenW));
                        }
                        p.drawText(cellR.adjusted(ci > 1 ? 6 : (ci == 0 ? 8 : 4), 0, -2, 0),
                                   Qt::AlignVCenter | Qt::AlignLeft, display);
                    }
                    ty += rowH;
                }
                break;
            }
            case ElemType::Image: {
                if (!el.image.isNull()) {
                    p.drawImage(r.topLeft(), el.image);
                }
                break;
            }
            }
            p.restore();
        }

        // ── 页眉 ──
        {
            p.save();
            p.setFont(fonts.hdrFtr);
            p.setPen(QColor(130, 130, 130));
            p.drawText(kMargin, 35, kContentW, 30,
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString("缺陷检测报告  第 %1 页 / 共 %2 页").arg(pi + 1).arg(total));
            p.setPen(QPen(QColor(130, 130, 130), 0.5));
            p.drawLine(kMargin, 68, kMargin + kContentW, 68);
            p.restore();
        }

        // ── 页脚 ──
        {
            p.save();
            p.setPen(QPen(QColor(130, 130, 130), 0.5));
            const int ftrLineY = kPageH - kMargin + 40;
            p.drawLine(kMargin, ftrLineY, kMargin + kContentW, ftrLineY);
            p.setFont(fonts.hdrFtr);
            p.drawText(kMargin, ftrLineY + 8, kContentW, 30,
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString("生成时间: %1").arg(timeStr));
            p.drawText(kMargin, ftrLineY + 8, kContentW, 30,
                       Qt::AlignRight | Qt::AlignVCenter,
                       "明场显微成像组件");
            p.restore();
        }

        if (progressCb)
            progressCb(pi + 1, total);
    }

    p.end();
    SPDLOG_INFO("[Report] ReportRenderer: done ({} pages, {})", total, pdfPath.toStdString());
}

} // namespace report
