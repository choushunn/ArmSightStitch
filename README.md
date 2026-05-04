# ArmSightStitch

机械臂视觉控制系统 — 集成 YOLO 目标检测、五轴机械臂控制、相机采集与图像拼接

## 功能特性

- **YOLO 目标检测** — 基于 NCNN 推理框架部署，支持自定义置信度/NMS 阈值
- **五轴机械臂控制** — 基于 Modbus TCP 协议，支持位置控制与连续运动
- **S 型路径规划** — 网格点图像采集，支持自定义网格大小和范围
- **相机采集** — 基于 ToupCam SDK，支持枚举、连接、连续/单帧采集
- **图像拼接** — 基于 OpenCV Stitcher，S 型图像排序与自动拼接
- **GUI 界面** — 基于 Qt6，多标签页设计，实时图像显示

## 架构

```
ArmSightStitch/
├── app/                # 应用入口
│   └── main.cpp
├── ui/                 # Qt 界面层
│   ├── MainWindow.h
│   └── MainWindow.cpp
├── core/               # 核心业务层
│   ├── arm/            # 机械臂控制 (Modbus)
│   ├── camera/         # 相机采集 (ToupCam SDK)
│   ├── detector/       # YOLO 检测 (NCNN)
│   └── stitch/         # 图像拼接 (OpenCV)
├── infra/              # 基础设施层
│   ├── config/         # 配置管理
│   └── workflow/       # 工作流管理
├── res/
│   ├── models/         # NCNN 模型文件
│   └── qt/             # Qt 资源文件
├── third_party/        # 第三方库 (暂未使用)
├── build/              # 构建输出目录
├── bin/                # 可执行文件输出
├── CMakeLists.txt
├── CMakePresets.json
└── vcpkg.json
```

## 环境要求

| 组件 | 版本 | 来源 |
|------|------|------|
| Windows | 10/11 | — |
| CMake | >= 3.16 | 系统安装或 Qt Tools 自带 |
| Qt | 6.10.1 (mingw_64 / msvc2022_64) | [Qt 在线安装](https://download.qt.io) |
| MinGW | 13.1.0 (x86_64-posix-seh) | Qt Tools 自带 (mingw1310_64) |
| Visual Studio | 17 2022 (可选) | MSVC 方案 |
| vcpkg | 2025.04.09 | [vcpkg](https://github.com/microsoft/vcpkg) |

### vcpkg 托管的依赖

项目通过 `vcpkg.json` 声明依赖，首次构建时自动下载编译：

| 依赖 | 用途 |
|------|------|
| opencv4 | 图像处理与拼接（core, imgproc, imgcodecs, highgui） |
| ncnn | YOLO 深度学习推理 |
| fmt | 格式化输出 |
| spdlog | 日志记录 |

### 需要手动安装的依赖

| 依赖 | 用途 | 安装方式 |
|------|------|----------|
| Qt 6.10.1 | GUI 框架 | 系统安装，设置 `CMAKE_PREFIX_PATH` |

> **注意**：所有 C++ 库均通过 vcpkg 管理，首次构建自动下载编译。

## 快速开始

### 1. 配置环境变量

```powershell
# vcpkg 根目录
$env:VCPKG_ROOT = "C:/Programs/vcpkg-2025.04.09"
```

### 2. 构建

```powershell
# MinGW 方案（默认，Debug 模式）
cmake --preset default
cmake --build --preset default

# MSVC 方案（Release 模式）
cmake --preset msvc
cmake --build --preset msvc
```

构建完成后，可执行文件输出至 `bin/` 目录。

### 首次构建说明

vcpkg 管理的依赖（opencv4、ncnn 等）在首次配置时会**自动下载并编译**，耗时较长（取决于网络和机器性能，约 30-60 分钟）。后续构建将使用缓存，无需重复编译。

如需加速首次构建：

- 设置 `HTTP_PROXY` / `HTTPS_PROXY` 加速源码下载
- 使用预置的 vcpkg 二进制缓存（默认启用，位于 `%LOCALAPPDATA%\vcpkg\archives`）

## 构建预设

| 预设 | 生成器 | 编译器 | 构建类型 | 适用场景 |
|------|--------|--------|----------|----------|
| `default` | Ninja | MinGW 13.1.0 | Debug | 日常开发调试 |
| `msvc` | Visual Studio 17 2022 | MSVC | Release | 发布构建 |

## 可选模块

项目通过编译宏控制可选模块。缺失依赖时自动禁用：

| 宏定义 | 缺失依赖 | 禁用模块 |
|--------|----------|----------|
| `ARM_SIGHT_STITCH_NO_DETECTOR` | ncnn | YOLO 目标检测 |
| `ARM_SIGHT_STITCH_NO_CAMERA` | ToupCam SDK（third_party/toupcam/） | 相机采集 |
| `ARM_SIGHT_STITCH_NO_ARM` | libmodbus（FetchContent） | 机械臂控制 |

## 依赖管理策略

| 类型 | 管理方式 | 示例 |
|------|----------|------|
| 标准库 | vcpkg（`vcpkg.json`） | opencv4, ncnn, fmt, spdlog |
| 系统库 | 系统安装 + `CMAKE_PREFIX_PATH` | Qt6 |
| 源码库 | FetchContent（CMake 配置时自动下载） | libmodbus |
| 模型文件 | Git 管理（`res/models/`） | ncnn .param / .bin |

## 常见问题

### vcpkg 构建失败

确保 vcpkg 与 vcpkg 端口为最新：

```powershell
cd $env:VCPKG_ROOT
git pull
.\bootstrap-vcpkg.bat
```

### Qt6 未找到

检查 `CMAKE_PREFIX_PATH` 是否指向 Qt6 安装目录：

```powershell
# MinGW 方案
$env:CMAKE_PREFIX_PATH = "C:/Programs/Qt/6.10.1/mingw_64"

# MSVC 方案
$env:CMAKE_PREFIX_PATH = "C:/Programs/Qt/6.10.1/msvc2022_64"
```
