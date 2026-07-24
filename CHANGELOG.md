# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Google Test 测试框架 (3 个测试套件, 11 个用例)
- GitHub Actions CI/CD (MSVC + MinGW 双构建)
- `Result<T>` 错误处理模板 (`infra/util/Result.h`)
- `AppConstants.h` 统一编译时常量
- `ConfigSections.h` 领域配置结构体
- `QtLogSink` 自定义 spdlog sink
- `FolderSelectDialog` 可复用文件夹选择对话框
- 工具栏全屏切换按钮
- 应用图标 (`setWindowIcon`)

### Changed
- **架构重构**: MainWindow 拆分为 5 个子 Controller
- **架构重构**: ModbusArmController 拆分为 ModbusConnection + ModbusRegisterIO
- **架构重构**: DetectionSettingsDialog 拆分为 3 个独立 ParamWidget tab
- **架构重构**: ConfigManager 内部使用 ConfigSections 结构体
- **编译优化**: 4 个 stitch 算法 header-only → .h + .cpp
- **编译优化**: 3 个 report 模块 header-only → .h + .cpp
- **接口精简**: 移除 5 个单实现冗余接口，仅保留 IStitchAlgorithm
- 异步统一 Qt 原生 QFutureWatcher
- 日志统一 spdlog，消除 appendLog 双写
- 弹窗标题规范化
- 应用名称改为"明场显微成像验证组件"
- 启动默认全屏

### Fixed
- SMovementController `const_cast` 反模式 (mutable mutex)
- async imwrite fire-and-forget 无错误处理
- Z-stack arm move fire-and-forget 无状态追踪
- WinSock2 include 顺序 (MSVC `WIN32_LEAN_AND_MEAN`)

## [2.0.1] — 2026-07-22

### Added
- CI/CD (单 MSVC job)
- 默认全屏启动
- 工具栏全屏切换按钮

### Changed
- 弹窗标题规范化统一
- 应用名称修复: ScannerApp → 明场显微成像验证组件

### Fixed
- 多处弹窗标题用错 (警告/错误/完成/提示)

## [2.0.0] — 2026-07-05

### Added
- 5 轴机械臂 Modbus TCP 控制
- ToupCam 相机驱动 (曝光/增益/锐化/分辨率)
- YOLO 目标检测 (NCNN 推理)
- Dust 灰尘检测 (传统 CV)
- Sobel 边缘检测
- 4 种图像拼接算法 (Grid/Feature/ZScale/SeamFeather)
- S 型扫描运动控制
- Z 轴球冠补偿 + 手动 Z-Map + 径向 Z-Map
- 实时相机预览 + FPS 统计
- 负片显示 (扫描/拼接)
- 批量检测 + PDF 报告导出
- NSIS 安装器打包
- spdlog 日志系统 (控制台 + 滚动文件)
- JSON 配置管理 (环境变量覆盖)
