# ArmSightStitch

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Qt](https://img.shields.io/badge/Qt-6.10-green.svg)](https://www.qt.io/)
[![OpenCV](https://img.shields.io/badge/OpenCV-4.12-red.svg)](https://opencv.org/)

集成 YOLO 目标检测、Modbus TCP 五轴机械臂控制、ToupCam 相机采集与图像拼接。


## 功能特性

| 模块 | 功能 |
|------|------|
| **YOLO 目标检测** | 基于 NCNN 推理框架，支持实时检测与单帧检测，可调置信度/NMS 阈值 |
| **五轴机械臂控制** | Modbus TCP 协议，绝对定位、连续运动、速度设置、紧急停止 |
| **S 型路径规划** | 自定义网格大小与步进，蛇形扫描自动采集 |
| **相机采集** | ToupCam SDK，实时预览、曝光/增益/旋转/翻转调节、分辨率切换 |
| **双算法拼接** | 算法1：网格位置拼接；算法2：OpenCV 特征匹配拼接 |
| **实时预览** | 相机/检测/拼接三区预览，双击全屏，全屏持续更新 |
| **打包分发** | CPack + NSIS 生成 Windows 安装包 |

## 架构

```
ArmSightStitch/
├── app/                   # 应用入口
├── ui/                    # Qt 界面层 (MainWindow, AppController, dialogs)
├── core/
│   ├── arm/               # 机械臂控制 (ModbusArmController, SMovementController)
│   ├── camera/            # 相机采集 (CameraHandler)
│   ├── detector/          # YOLO 检测 (YoloDetector)
│   └── stitch/            # 图像拼接 (GridStitchAlgorithm, FeatureStitchAlgorithm)
├── infra/
│   ├── config/            # 配置管理 (ConfigManager, PathUtils)
│   └── log/               # 日志 (LogManager)
├── resources/             # 打包资源 (icon, license, installer images)
├── res/models/            # NCNN 模型文件
└── third_party/toupcam/   # ToupCam SDK
```

### 设计原则

- **策略模式** — 拼接算法可插拔 (`IStitchAlgorithm` → `GridStitchAlgorithm` / `FeatureStitchAlgorithm`)
- **依赖注入** — `IArmController` / `ICameraHandler` / `IStitcher` 接口隔离实现
- **pimpl 模式** — `AppController` 隐藏 5 个具体实现类，头文件不含第三方 SDK 类型

## 环境要求

| 组件 | 版本 | 说明 |
|------|------|------|
| Windows | 10/11 | |
| CMake | ≥ 3.16 | |
| Qt | 6.10.1 (mingw_64) | [下载](https://download.qt.io) |
| vcpkg | 2025.04+ | [下载](https://github.com/microsoft/vcpkg) |
| NSIS | 3.x | 仅打包需要 ([下载](https://nsis.sourceforge.io)) |

### vcpkg 依赖

| 库 | 用途 |
|----|------|
| opencv4[core,imgproc,imgcodecs,stitching] | 图像处理与拼接 |
| ncnn | YOLO 推理 |
| spdlog | 日志 |

## 快速开始

```powershell
# 1. 设置 vcpkg 路径
$env:VCPKG_ROOT = "C:/Programs/vcpkg"

# 2. 配置
cmake --preset default -DCMAKE_BUILD_TYPE=Release

# 3. 编译
cmake --build build/vcpkg-mingw --config Release

# 4. 启动
.\bin\ArmSightStitch.exe
```

### 打包

```powershell
# 部署 Qt 运行时
cmake --build build/vcpkg-mingw --target deploy

# 生成安装包
cd build/vcpkg-mingw
cpack -G NSIS
# → ArmSightStitch-2.0.0-win64.exe
```

## 使用说明

### 工作流程

1. **连接相机** → 左侧面板枚举并连接
2. **连接机械臂** → 输入 IP:端口，点击连接
3. **扫描采集** → 设置网格参数，点击"开始扫描"
4. **图像拼接** → 切换到拼接页，选择算法，点击"拼接"
5. **目标检测** → 加载模型，开启实时检测或点击单帧检测

### 快捷键

| 快捷键 | 功能 |
|--------|------|
| `F5` | 开始/停止扫描 |
| `F6` | 暂停/继续扫描 |
| `Ctrl+O` | 打开图像 |
| `Ctrl+S` | 保存拼接结果 |

### 鼠标操作

| 操作 | 功能 |
|------|------|
| 双击预览区 | 全屏显示 |
| ESC / 单击 | 退出全屏 |
| 点击网格单元格 | 预览该位置图像（勾选"允许位移"时同时移动机械臂） |

## 配置文件

程序启动时自动加载 `config.json`，默认路径为 `Documents/ArmSightStitch/`。

```json
{
    "arm_ip": "192.168.0.1",
    "arm_port": 502,
    "default_speed": 70000,
    "grid_size_x": 10,
    "grid_size_y": 10,
    "step_size": 43000,
    "z_height": 50000
}
```

环境变量覆盖：

| 变量 | 配置项 |
|------|--------|
| `ARM_SIGHT_STITCH_ARM_IP` | 机械臂 IP |
| `ARM_SIGHT_STITCH_DEFAULT_SPEED` | 默认速度 |
| `ARM_SIGHT_STITCH_GRID_X` / `_Y` | 网格大小 |

## 机械臂协议

详见 [docs/机械臂连接协议.md](docs/机械臂连接协议.md) — Modbus TCP 寄存器映射、线圈地址、数据编码规范。

## 许可证

MIT License
