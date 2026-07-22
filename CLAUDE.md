# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ArmSightStitch is a Windows desktop application for bright-field microscope validation. It integrates a 5-axis robotic arm (Modbus TCP), ToupCam camera, YOLO object detection (NCNN inference), and multi-algorithm image stitching. Dual build targets: MinGW 13.1.0 (no CUDA) and MSVC 2022/2026 (with CUDA).

## Build Commands

### MinGW（日/常/编/译 — 强制使用此命令）

已配置完成后，**严禁重新运行 cmake**，直接使用 ninja 增量编译：

```bash
export PATH="C:/Programs/Qt/Tools/mingw1310_64/bin:C:/Programs/Qt/Tools/Ninja:$PATH" && cd d:/CPP/ArmSightStitch/build/vcpkg-mingw && ninja
```

输出：`bin/ArmSightStitch.exe`

### MinGW（首次配置 — 仅在新机器或 build 目录损坏时使用）

```bash
export PATH="C:/Programs/Qt/Tools/mingw1310_64/bin:C:/Programs/Qt/Tools/Ninja:$PATH"
export CC="C:/Programs/Qt/Tools/mingw1310_64/bin/gcc.exe"
export CXX="C:/Programs/Qt/Tools/mingw1310_64/bin/g++.exe"

cmake --preset default -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="C:/Programs/Qt/Tools/mingw1310_64/bin/gcc.exe" \
    -DCMAKE_CXX_COMPILER="C:/Programs/Qt/Tools/mingw1310_64/bin/g++.exe" \
    -DCMAKE_MAKE_PROGRAM="C:/Programs/Qt/Tools/Ninja/ninja.exe"
```

| 参数 | 路径 |
|------|------|
| C 编译器 | `C:/Programs/Qt/Tools/mingw1310_64/bin/gcc.exe` |
| C++ 编译器 | `C:/Programs/Qt/Tools/mingw1310_64/bin/g++.exe` |
| Ninja | `C:/Programs/Qt/Tools/Ninja/ninja.exe` |
| Qt6 | `C:/Programs/Qt/6.10.1/mingw_64` |

**PATH 必须把 mingw1310_64/bin 放在最前面**，并显式指定 CC/CXX 和 CMAKE_C_COMPILER/CMAKE_CXX_COMPILER，防止 vcpkg 检测到 MSYS2 的编译器（会导致 ABI 不兼容的链接错误）。

### MinGW Deploy & 打包

```bash
# Deploy（windeployqt + MinGW 运行时 DLL → bin/）
cmake --build build/vcpkg-mingw --config Release --target deploy

# NSIS 打包（从 build 目录）
cd build/vcpkg-mingw && cpack -G NSIS
```

NSIS 打包注意事项：cpack 失败后需手动修复 `.nsi` 中的绝对路径，详见 Important Rules。

---

### MSVC + CUDA（日/常/编/译）

```bash
cmake --build build/vcpkg-msvc --config Release --parallel
```

输出：`bin-msvc/Release/ArmSightStitch.exe`

### MSVC + CUDA（首次配置）

```bash
cmake --preset msvc
```

前置条件：CUDA Toolkit ≥12.0、NVIDIA 驱动 ≥525、Visual Studio 2022/2026、Qt 6.10.1 MSVC。

### MSVC Deploy

```bash
cmake --build build/vcpkg-msvc --config Release --target deploy-msvc
```

### CUDA 加速模块

MSVC 构建使用独立 vcpkg manifest（`vcpkg-msvc/vcpkg.json`，额外含 `opencv4[cuda,cudnn]`）和独立输出目录（`bin-msvc/`），与 MinGW 完全解耦。

| 模块 | GPU 操作 | 宏 |
|------|----------|----|
| `EdgeDetector` | CLAHE + Sobel + magnitude + threshold + 形态学膨胀 | `HAVE_OPENCV_CUDAFILTERS` |
| `DustDetector` | 高斯模糊 + 形态学膨胀 | `HAVE_OPENCV_CUDAFILTERS` |
| `SeamFeatherStitchAlgorithm` | 加权累积与归一化 | `HAVE_OPENCV_CUDA` |
| `YoloDetector` | NCNN Vulkan | 独立于 OpenCV CUDA |

CUDA 架构目标：`75-real;86-real;89-real`（RTX 20/30/40 系列）。

---

## Important Rules

1. **MinGW 编译必须严格执行上方命令**，不得修改、不得重新配置 cmake、不得添加额外参数。
2. **严禁在未明确要求时运行 `cmake --preset` 或修改 `CMakeLists.txt`**。配置 cmake 会触发 vcpkg 编译器检测，PATH 不对会误用 MSYS2 编译器导致全量重编依赖包。
3. **如果 CMakeLists.txt 比 build.ninja 新**，ninja 会自动触发 cmake 重新生成。阻止方法：
   ```bash
   touch build/vcpkg-mingw/build.ninja
   ```
4. **NSIS 打包 workaround**：CPack NSIS generator 对 MUI 资源（`MUI_ICON`、`MUI_WELCOMEFINISHPAGE_BITMAP`、`MUI_PAGE_LICENSE`）生成了绝对正斜杠路径，NSIS 3.12 无法在 MUI 宏中解析。workaround：
   ```bash
   cp resources/{R.bmp,logo.ico,License.txt} build/vcpkg-mingw/_CPack_Packages/win64/NSIS/
   # 编辑 project.nsi，将绝对路径替换为裸文件名
   cd build/vcpkg-mingw/_CPack_Packages/win64/NSIS && makensis.exe project.nsi
   ```
5. **打包前清理**：`rm -rf bin/logs build/vcpkg-mingw/_CPack_Packages`（日志文件可能被 spdlog 锁定）。

---

## Architecture

**核心模式：**
- **Strategy pattern** — 所有核心模块通过抽象接口解耦（`IArmController`、`ICameraHandler`、`IDetector`、`IStitcher`、`IStitchAlgorithm`），各有一个具体实现。
- **Pimpl idiom** — `AppController` 将所有具体实现隐藏在 `struct Impl` + `std::unique_ptr<Impl>` 中，头文件不暴露第三方类型。
- **Dependency injection** — `WorkflowManager` 通过构造函数接收引用；`AppController` 持有实例并传递。
- **Callback 通信** — 模块通过 `std::function` 回调推送状态，非观察者模式。
- **Async** — 耗时操作（拼接、检测、连接）通过 `QtConcurrent::run` / `QFutureWatcher` 异步执行，结果通过 Qt signal 回传 UI 线程。
- **S-movement** 运行在独立 `std::thread` 上，内部状态机控制 pause/resume。

**Layer map：**
```
app/main.cpp          — 入口: ConfigManager → LogManager → AppController → MainWindow
ui/                   — Qt 层: MainWindow, AppController (pimpl), WorkflowManager (FSM), dialogs
core/arm/             — ModbusArmController (Modbus I/O), SMovementController (S曲线扫描)
core/camera/          — CameraHandler (ToupCam SDK)
core/detector/        — YoloDetector (NCNN), DustDetector (传统CV), EdgeDetector (Sobel),
                        IDetector.h (接口), DetectionParams (POD 参数)
core/stitch/          — ImageStitcher (orchestrator), IStitchAlgorithm + 多种实现
infra/config/         — ConfigManager (singleton, JSON, 环境变量覆盖)
infra/log/            — LogManager (spdlog: console + rotating file)
```

**可选模块**：NCNN/ToupCam/libmodbus 不可用时通过 CMake define 排除（`ARM_SIGHT_STITCH_NO_*`）。

**检测算法选择**：0=YOLO, 1=Dust(传统CV迭代法), 2=Edge(Sobel边缘检测)。

---

## Config System

`ConfigManager` 单例。启动时从 exe 目录加载 `config.json`（fallback: 硬编码默认值）。环境变量可覆盖：

| 环境变量 | 覆盖字段 |
|----------|---------|
| `ARM_SIGHT_STITCH_ARM_IP` | `arm_ip` |
| `ARM_SIGHT_STITCH_DEFAULT_SPEED` | `default_speed` |
| `ARM_SIGHT_STITCH_GRID_X` / `_Y` | `grid_size_x` / `_y` |
| `ARM_SIGHT_STITCH_STEP_SIZE` | `step_size` |
| `ARM_SIGHT_STITCH_GRID_HEIGHT` | `z_height` |
| `ARM_SIGHT_STITCH_DWELL_TIME_MS` | `dwell_time_ms` |

---

## Key Build Details

- **libmodbus**：FetchContent（v3.1.12），Windows `config.h` patch，C 源码静态链接。MinGW 需 `FD_SETSIZE=32768` 处理高编号 socket。
- **OpenCV**：模块限定 `core imgproc imgcodecs stitching`。MinGW 强制链接 vcpkg 静态 `libtiff.a` 避免 conda 的 `tiff.dll` 依赖。
- **MinGW 优化**：`-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` 死代码裁剪。
- **CMake**：AUTOUIC/AUTOMOC/AUTORCC 自动处理 `.ui` / `.qrc`。
- **Deploy**：windeployqt + MinGW 运行时 DLL（`libgcc_s_seh-1`、`libstdc++-6`、`libwinpthread-1`、`libgomp-1`），移除 `opengl32sw.dll` / `D3Dcompiler_47.dll`，裁剪多余 Qt 翻译文件。

---

## Important Docs

- [docs/机械臂连接协议.md](docs/机械臂连接协议.md) — Modbus TCP 寄存器映射、线圈地址、5轴数据编码
- [docs/libmodbus-fix-summary.md](docs/libmodbus-fix-summary.md) — MinGW `FD_SETSIZE=EINVAL` 根因
- [docs/deploy-dll-summary.md](docs/deploy-dll-summary.md) — 部署 DLL 清单与体积

---

## Branching

Git Flow 简化版：`develop`（开发）→ `main`（发布）。Commit message 使用中文。
