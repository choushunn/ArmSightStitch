# 国际化 (i18n) 策略

## 当前状态

项目尚未启用 Qt 翻译系统（`QTranslator` + `.ts` 文件）。所有字符串以硬编码方式存在：
- **UI 字符串** — 主要使用中文字面量，部分 `QStringLiteral()` 包裹
- **日志字符串** — spdlog 使用英文，appendLog 使用中文

## 临时策略

在正式 i18n 实施之前：

| 层面 | 语言 | 理由 |
|------|------|------|
| UI 用户可见文本 | **中文** | 当前用户群体为中文使用者 |
| spdlog 日志 | **英文** | 开发者 / 技术支持可读，便于 grep / 国际化协作 |
| appendLog (UI 日志面板) | **中文** | 最终用户可见 |

## 未来计划

### 阶段 1: 字符串提取
- 所有 `QStringLiteral("...")` / `tr("...")` / `setText("...")` 统一改为 `tr("...")`
- 运行 `lupdate` 生成 `.ts` 文件
- 提供 `zh_CN.ts` (简体中文) 和 `en_US.ts` (英文) 两个翻译文件

### 阶段 2: 翻译加载
```cpp
QTranslator translator;
translator.load("armsightstitch_" + QLocale::system().name(), ":/i18n");
a.installTranslator(&translator);
```

### 阶段 3: 持续维护
- 新增 UI 字符串必须用 `tr()` 包裹
- PR review 检查项：无裸中文字符串出现在可翻译路径

## 注意事项
- `QComboBox::addItem()` 的显示文本需要翻译
- `QMessageBox` 的标题和正文都需要翻译
- spdlog 格式字符串保持英文（日志格式 + 字段名国际化）
- 拼接/检测算法的 progress/status 回调文本需要二选一（当前用中文）
