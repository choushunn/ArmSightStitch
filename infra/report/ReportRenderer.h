#pragma once

// ── ReportRenderer — 将 Page 列表按页渲染到 QPdfWriter ─────────
//
// 线程：可在 worker 线程执行（内部创建独立的 QPdfWriter + QPainter）。
// 每个文字单元格使用 QPainter::drawText → PDF 中是矢量文字，可选可搜索。
// 内存 O(1)：逐页渲染，不缓存全量内容。
// ─────────────────────────────────────────────────────────────────

#include "ReportLayout.h"

#include <QString>
#include <vector>
#include <functional>

namespace report {

void renderReport(const QString& pdfPath,
                  const std::vector<Page>& pages,
                  const ReportFonts* fontsOverride = nullptr,
                  std::function<void(int cur, int total)> progressCb = {});

} // namespace report
