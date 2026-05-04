# Tasks

- [x] Task 1: 重构 MainWindow.ui 布局 — 移除冗余 Action 和 Toolbar
  - [x] 删除冗余 Action：actionScanSettings、actionCameraSettings、actionArmSettings、actionDeviceConnect、actionDeviceDisconnect、actionLoadImageDir
  - [x] 删除 menuSettings 中对应 addaction、menuTools 中对应 addaction、menuFile 中对应 addaction
  - [x] 删除整个 mainToolBar widget 定义
  - [x] 删除 actionViewToolbar、actionViewGridPanel、actionResetLayout
  - [x] 简化 menuView 只保留 actionViewArmPanel

- [x] Task 2: 重构 MainWindow.ui 布局 — 紧凑化中央区域布局
  - [x] 将 centralWidget 的主布局从 3 行 QVBoxLayout 改为左右 QSplitter（horizontal）
  - [x] 左侧面板：创建垂直 QScrollArea 包含相机控制、机械臂面板（无 GroupBox）、扫描面板、网格面板
  - [x] 右侧面板：上/下分栏（QSplitter vertical），上为 cameraImageLabel、下为 stitchImageLabel
  - [x] 移除 cameraPanel、armPanel、gridPanel、stitchPanel、movementPanel 中的 GroupBox 标题，改为无框布局
  - [x] 减小所有 spacing 和 margin 到最小
  - [x] 窗口默认尺寸从 1400×900 改为 1200×750

- [x] Task 3: 重构 MainWindow.ui 布局 — 统一状态展示
  - [x] 将 movementProgressBar、stitchProgressBar 替换为状态栏中的一个统一 QProgressBar
  - [x] 将 stitchStatusText、movementStatusText 收敛为状态栏中的一个 QPlainTextEdit（readOnly, 最大高度 60）
  - [x] 移除网格面板中的 gridLabel、gridClickLabel，将提示文字移到 tooltip

- [x] Task 4: 更新 MainWindow.cpp — 适配新 UI 布局
  - [x] 移除 connectSignals() 中已删除 Action 的 connect 代码（actionScanSettings→on_stitchSettings、actionCameraSettings、actionArmSettings、actionDeviceConnect、actionDeviceDisconnect、actionLoadImageDir）
  - [x] 移除 actionViewToolbar→QToolBar::setVisible 的 connect
  - [x] 移除 actionViewGridPanel→on_toggleGridPanel、actionResetLayout 的 connect
  - [x] 更新 statusLabel 引用为新的统一状态控件名称
  - [x] 更新 stitchProgressBar、movementProgressBar、stitchStatusText、movementStatusText 引用为新的统一控件名称
  - [x] 移除 on_toggleGridPanel slot 定义和实现
  - [x] 移除 MainWindow.h 中 on_toggleGridPanel 声明

- [x] Task 5: 紧凑化 StitchingSettingsDialog 和 DetectionSettingsDialog
  - [x] StitchingSettingsDialog.ui：尺寸从 480×280 改为 400×180，layout spacing 和 margin 减小
  - [x] DetectionSettingsDialog.ui：尺寸从 520×380 改为 460×280，layout spacing 和 margin 减小

# Task Dependencies
- Task 2 依赖 Task 1（先清理冗余元素，再做布局调整）
- Task 4 依赖 Task 2 和 Task 3（UI 变更后适配 C++ 代码）
- Task 3 和 Task 5 可并行
