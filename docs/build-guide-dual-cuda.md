# ArmSightStitch 双版本构建指南

## 概述

ArmSightStitch 支持两种编译器/加速方案的并行构建，输出到不同目录，互不干扰。

| | MinGW CPU 版本 | MSVC + CUDA 版本 |
|---|---|---|
| **编译器** | MinGW 13.1.0 (Qt 自带) | Visual Studio 2022/2026 (MSVC) |
| **vcpkg triplet** | `x64-mingw-static` | `x64-windows` |
| **vcpkg 模式** | manifest (`vcpkg.json`) | classic（`vcpkg install` 预装） |
| **OpenCV GPU** | ❌ CPU 回退 | ✅ CUDA (`opencv4[cuda,cudnn]`) |
| **YOLO 推理** | NCNN Vulkan | NCNN Vulkan |
| **输出目录** | `bin/` | `bin-msvc/` |
| **deploy target** | `deploy` | `deploy-msvc` |
| **cmake preset** | `default` | `msvc` |
| **build 目录** | `build/vcpkg-mingw/` | `build/vcpkg-msvc/` |

## 前置条件

### 共同需求

- Qt 6.10.1 安装于 `C:/Programs/Qt/6.10.1/`
- vcpkg 已克隆，`VCPKG_ROOT` 环境变量已设置
- CMake ≥ 3.16

### MinGW 版本额外需求

- Qt 自带 MinGW 13.1.0：`C:/Programs/Qt/Tools/mingw1310_64/`
- Ninja：`C:/Programs/Qt/Tools/Ninja/ninja.exe`

### MSVC + CUDA 版本额外需求

- Visual Studio 2022 或 2026（含 "使用 C++ 的桌面开发" 工作负载）
- CUDA Toolkit ≥ 12.0（推荐 13.2）：`C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2/`
- NVIDIA 显卡驱动 ≥ 525
- vcpkg 已预装 OpenCV CUDA 包：
  ```powershell
  vcpkg install opencv4[cuda,cudnn,calib3d,dshow,fs,gapi,highgui,intrinsics,jpeg,png,quirc,thread,tiff,webp,win32ui]:x64-windows
  vcpkg install spdlog:x64-windows
  vcpkg install ncnn[vulkan]:x64-windows
  ```

## 构建命令

### 1. MinGW CPU 版本

```bash
# 确保 PATH 优先使用 mingw1310_64（防止 MSYS2 等其他 MinGW 被误检测）
export PATH="C:/Programs/Qt/Tools/mingw1310_64/bin:C:/Programs/Qt/Tools/Ninja:$PATH"

# 配置
cmake --preset default

# 编译
cmake --build build/vcpkg-mingw --config Release --parallel

# 部署 Qt 运行时 + MinGW DLL → bin/
cmake --build build/vcpkg-mingw --config Release --target deploy

# (可选) 打包 NSIS 安装包
cd build/vcpkg-mingw
cpack -G NSIS
```

**输出**: `bin/ArmSightStitch.exe`

**构建日志关键信息**:
```
-- Found OpenCV: 4.12.0
-- WARNING: OpenCV CUDA modules not available — GPU-accelerated features disabled
-- Found NCNN via vcpkg
```

### 2. MSVC + CUDA 版本

```powershell
# 从 Developer PowerShell for VS 2022/2026 或 Developer Command Prompt 运行

# 配置
cmake --preset msvc

# 编译
cmake --build build/vcpkg-msvc --config Release --parallel

# 部署 Qt MSVC 运行时 → bin-msvc/
cmake --build build/vcpkg-msvc --config Release --target deploy-msvc
```

**输出**: `bin-msvc/Release/ArmSightStitch.exe`

**构建日志关键信息**:
```
-- Found CUDA Toolkit: 13.2.78 at C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2/bin
-- Found OpenCV: 4.12.0
-- Found NCNN via vcpkg
```

> **注意**: 不应出现 `"OpenCV CUDA modules not available"` 警告。如果出现，说明 vcpkg 未安装 `opencv4[cuda,cudnn]`。

## CUDA 加速范围

MSVC 版本中，以下模块走 CUDA GPU 加速（由 `HAVE_OPENCV_CUDA` / `HAVE_OPENCV_CUDAFILTERS` 预编译宏控制）：

| 模块 | CUDA 加速内容 | 源码位置 |
|------|-------------|---------|
| **DustDetector** | `cv::cuda::createGaussianFilter` 背景高斯模糊 + `cv::cuda::createMorphologyFilter` 膨胀 | `core/detector/DustDetector.h` |
| **SeamFeatherStitchAlgorithm** | `cv::cuda::GpuMat` 加权累积、`cv::cuda::multiply/add/divide` 归一化 | `core/stitch/SeamFeatherStitchAlgorithm.h` |
| **YoloDetector** | NCNN Vulkan Compute（与 MinGW 相同，不受 OpenCV CUDA 影响） | `core/detector/YoloDetector.cpp` |

MinGW 版本中以上 CUDA 路径编译为 CPU 回退（`cv::GaussianBlur`、`cv::dilate`、`cv::Mat`），功能一致但性能较低。

## 验证 CUDA 已激活

### 编译时验证

在 `.vcxproj` 中搜索 `PreprocessorDefinitions`，确认包含：
```
HAVE_OPENCV_CUDA;HAVE_OPENCV_CUDAFILTERS
```

### 运行时验证

1. 使用 NVIDIA Nsight Systems / Nsight Graphics 监控 GPU 利用率
2. 运行 DustDetector 检测时应观察到 CUDA kernel 调用
3. 部署目录 `bin-msvc/Release/` 应包含：
   - `opencv_cudaarithm4.dll`
   - `opencv_cudafilters4.dll`
   - `opencv_cudaimgproc4.dll`
   - `opencv_cudawarping4.dll`

## 项目文件结构

```
ArmSightStitch/
├── bin/                          # MinGW 输出
│   └── ArmSightStitch.exe
├── bin-msvc/                     # MSVC 输出
│   └── Release/
│       └── ArmSightStitch.exe
├── build/
│   ├── vcpkg-mingw/              # MinGW 构建产物
│   └── vcpkg-msvc/               # MSVC 构建产物
├── vcpkg.json                    # MinGW manifest
├── CMakePresets.json             # 双 preset 定义
├── CMakeLists.txt                # 条件化双构建逻辑
└── docs/
    └── build-guide-dual-cuda.md  # 本文档
```

## 常见问题

### Q: MinGW 版本能否启用 CUDA？
**不能。** NVIDIA CUDA Toolkit 不支持 MinGW 编译器。如需 GPU 加速请使用 MSVC 版本。

### Q: 两个版本可以同时构建吗？
可以。它们使用不同的 build 目录和输出目录，完全独立。

### Q: MSVC 构建时 vcpkg 重新编译 OpenCV 怎么办？
使用 classic 模式（`VCPKG_MANIFEST_MODE: OFF`）配合 `vcpkg install` 预安装，避免 manifest 模式触发重编译。首次 `vcpkg install opencv4[cuda,cudnn]` 需要 1~2 小时编译 OpenCV + CUDA 模块，后续配置秒级完成。

### Q: 部署后 CUDA DLL 找不到？
确保 `bin-msvc/Release/` 中包含了 opencv_cuda*.dll。运行 `cmake --build build/vcpkg-msvc --config Release --target deploy-msvc` 会自动部署。如果手动运行，需要将 CUDA Runtime DLL（如 `cudart64_132.dll`）也加入 PATH 或复制到 exe 同目录。
