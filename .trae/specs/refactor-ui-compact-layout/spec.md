# UI界面重构 — 移除冗余元素 + 紧凑布局 Spec

## Why
当前 `g:\CPP\ArmSightStitch\ui\` 下的 Qt UI 界面存在大量冗余元素：菜单中重复的 Action、未实现的空 handler、重复的文本标签、过大的窗口尺寸以及分散的进度/状态组件。布局采用三行垂直堆叠，导致控件密度低、信息密度不足，操作时需要频繁滚动。需要通过移除冗余和紧凑化布局，使界面更高效、更专业。

## What Changes
- 移除菜单栏和工具栏中的冗余 Action（重复项 & 未实现项）
- 合并/删除多余的标签文本和 GroupBox，减少视觉噪音
- 重构 MainWindow 中央区域布局，从 3 行堆叠改为更紧凑的左右分栏 + 底部状态栏结构
- 减小窗口默认尺寸
- 将分散的进度指示器和状态文本统一收敛到状态栏
- 优化 Grid 面板的空间利用（移除冗余标签）
- 统一 Dialog 的尺寸和间距

## Impact
- Affected specs: 无（新建）
- Affected code:
  - `ui/MainWindow.ui` — 主要布局重构
  - `ui/MainWindow.cpp` — 调整 signal/slot 连接逻辑
  - `ui/MainWindow.h` — 可能的成员变量调整
  - `ui/StitchingSettingsDialog.ui` — 紧凑化
  - `ui/DetectionSettingsDialog.ui` — 紧凑化

## ADDED Requirements

### Requirement: Compact Central Layout
主窗口中央区域 SHALL 采用左侧面板 + 右侧图像区域的紧凑二分栏布局，代替原有的三行垂直堆叠布局。

#### Scenario: 窗口初始化
- **WHEN** 应用程序启动
- **THEN** 中央区域显示为左右结构：左侧为相机控制+机械臂控制+扫描控制+网格面板的垂直堆叠面板（使用 QSplitter），右侧为相机预览图像 + 拼接预览图像的上/下分栏区域
- **AND** 窗口默认尺寸从 1400×900 缩减为 1200×750

### Requirement: Remove Redundant Menu Actions
菜单栏 SHALL 移除所有冗余或未实现的 Action。

#### Scenario: 移除重复设置项
- **WHEN** 用户打开"设置"菜单
- **THEN** `actionScanSettings`（扫描参数）被移除（功能与 `actionStitchSettings` 完全重复）
- **AND** `actionCameraSettings` 被移除（handler 为空，无实现）
- **AND** `actionArmSettings` 被移除（handler 为空，无实现）

#### Scenario: 移除冗余工具项
- **WHEN** 用户打开"工具"菜单
- **THEN** `actionDeviceConnect` 和 `actionDeviceDisconnect` 被移除（面板已有独立的连接/断开按钮，功能重复）

#### Scenario: 移除冗余文件项
- **WHEN** 用户打开"文件"菜单
- **THEN** `actionLoadImageDir` 被移除（仅设置状态栏文本，无实际功能且拼接设置中已有目录选择）

### Requirement: Remove Redundant Toolbar
工具栏 SHALL 被移除，所有操作已通过面板按钮提供。

#### Scenario: 工具栏不可见
- **WHEN** 应用程序启动
- **THEN** 工具栏不显示
- **AND** "视图"菜单中的 `actionViewToolbar` 和相关 toggle 逻辑被移除

### Requirement: Merge View Menu Items
"视图"菜单 SHALL 简化为更少的可控项。

#### Scenario: 视图菜单简化
- **WHEN** 用户打开"视图"菜单
- **THEN** 只保留 `actionViewArmPanel` 切换机械臂面板可见性
- **AND** `actionViewGridPanel`、`actionViewToolbar`、`actionResetLayout` 被移除
- **AND** 对应的 C++ slot/connect 代码被清理

### Requirement: Compact Controls Panel
左侧控制面板 SHALL 使用紧凑的布局，去除所有 GroupBox 标题，减小内边距，使用无框分组替代 GroupBox。

#### Scenario: 紧凑面板布局
- **WHEN** 应用程序启动
- **THEN** 机械臂连接区域以无框布局展示（IP、端口、连接按钮在一行）
- **AND** 机械臂位置控制区域以无框布局展示（X/Y/Z 坐标 + 移动/读取/归零按钮在一行）
- **AND** S型扫描控制区域以无框布局展示（开始/停止/暂停/恢复按钮在一行）
- **AND** 所有面板间距从 2px 减小为 1px，内边距减小

### Requirement: Unified Status Display
所有进度条和状态文本 SHALL 收敛到状态栏区域。

#### Scenario: 统一状态展示
- **WHEN** 扫描或拼接进行中
- **THEN** 进度信息显示在底部状态栏的统一进度区域中
- **AND** `movementProgressBar` 和 `stitchProgressBar` 不再作为固定控件占据中央空间，改为状态栏内的动态元素
- **AND** `stitchStatusText` 和 `movementStatusText` 收敛为状态栏中的一个合并日志控件

### Requirement: Compact Grid Panel
网格面板 SHALL 移除冗余标签文本，使用更紧凑的 table 展示。

#### Scenario: 网格面板
- **WHEN** 网格面板可见
- **THEN** "图像采集 10x10" 标签被移除（硬编码文本，不反映实际配置）
- **AND** "点击下方网格移动至目标位置" 标签被替换为 tooltip
- **AND** table 的 minimumSize 和 maximumSize 约束放宽以自适应布局

### Requirement: Compact Dialog Layouts
StitchingSettingsDialog 和 DetectionSettingsDialog SHALL 减小默认尺寸和内边距。

#### Scenario: Dialog 紧凑化
- **WHEN** 打开拼接设置对话框
- **THEN** 对话框尺寸从 480×280 缩减为 400×180
- **AND** 间距统一减小

- **WHEN** 打开检测设置对话框
- **THEN** 对话框尺寸从 520×380 缩减为 460×280
- **AND** 间距统一减小

## MODIFIED Requirements
（无 — 全新重构）

## REMOVED Requirements
### Requirement: Redundant Settings Actions
**Reason**: `actionScanSettings` 和 `actionStitchSettings` 在 C++ 代码中连接到了同一个 slot；`actionCameraSettings` 和 `actionArmSettings` 的 handler 为空
**Migration**: 不需要迁移，功能继续通过保留的 `actionStitchSettings` 和 `actionDetSettings` 提供

### Requirement: Empty Menu Handlers
**Reason**: `actionCameraSettings`、`actionArmSettings`、`actionLoadImageDir` 无实际功能
**Migration**: 仅有的 `actionLoadImageDir` 效果（设置状态栏文本）由拼接设置中的输入目录选择替代

### Requirement: Toolbar
**Reason**: 所有工具栏操作在面板中已有对应按钮，工具栏造成视觉冗余
**Migration**: 无需迁移，面板按钮保持原有功能
