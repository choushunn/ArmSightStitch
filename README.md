# ArmSightStitch

明场显微镜自动扫描与图像拼接系统。

## 功能概览

窗口左侧为硬件控制面板。相机区域（`cameraConnGroup`）内通过下拉框选择设备、点击"连接"即可启动实时预览，支持分辨率切换、曝光调节、自动曝光和图像旋转/翻转/负片效果，预览画面显示在侧边栏下方的缩略图中，缩放系数等参数通过菜单栏"设置→球冠参数"调节。机械臂区域（`armConnGroup`）内输入 IP 和端口后点击"连接"建立 Modbus TCP 通信，连接成功后可在 X/Y/Z 轴输入框中设定目标位置并执行移动或归零，当前位置实时显示在绿色标签中，紧急停止按钮常驻侧边栏和顶部工具栏。

右侧为主工作区。顶部工具栏（`quickToolbar`）提供拍照、扫描、拼接、检测等常用操作的快捷按钮，其中"实时检测"复选框控制扫描过程中是否自动执行缺陷检测，"负片显示"切换网格缩略图的显示模式。工具栏下方是一个水平分割器，左侧为扫描网格缩略图表格（`topCellsTable`），扫描结束后每个网格点的图像以行列排列显示在表格中，点击缩略图可放大查看；右侧为垂直分割的两个预览窗口——上方为拼接结果显示区（`stitchImageLabel`），扫描完成后显示全幅拼接图，下方为检测结果显示区（`detectImageLabel`），在线或离线检测后显示标注结果。系统日志实时输出至侧边栏底部的日志面板（`statusLogEdit`），底部状态栏显示当前状态。

## 编译器要求

**必须使用** Qt 自带的 MinGW 13.1.0 编译器，禁止使用 MSYS2 或其他 MinGW 发行版，否则会出现 ABI 不兼容和链接错误。

| 配置项 | 路径 |
|--------|------|
| C 编译器 | `C:/Programs/Qt/Tools/mingw1310_64/bin/gcc.exe` |
| C++ 编译器 | `C:/Programs/Qt/Tools/mingw1310_64/bin/g++.exe` |
| Ninja | `C:/Programs/Qt/Tools/Ninja/ninja.exe` |
| Qt 6.10.1 | `C:/Programs/Qt/6.10.1/mingw_64/` |

## 完整编译命令

### PowerShell

```powershell
# 0. 前置条件
#   - 安装 Qt 6.10.1 minw_64 组件
#   - 设置环境变量 VCPKG_ROOT 指向 vcpkg 安装目录
#   - 确认前置目录存在: C:/Programs/Qt/Tools/mingw1310_64/bin
$env:PATH = "C:/Programs/Qt/Tools/mingw1310_64/bin;C:/Programs/Qt/Tools/Ninja;$env:PATH"

# 1. 配置（仅首次，或 CMakeLists.txt/vcpkg.json 变更后）
cmake --preset default -DCMAKE_BUILD_TYPE=Release

# 2. 编译
cmake --build build/vcpkg-mingw --config Release --parallel

# 3. 部署 Qt 运行时 DLL（windeployqt + MinGW 运行时）
cmake --build build/vcpkg-mingw --config Release --target deploy

# 4. 打包 NSIS 安装器（需安装 NSIS）
cd build/vcpkg-mingw
cpack -G NSIS
```

### Git Bash (MinGW64)

```bash
# 0. 确保 Qt MinGW 在 PATH 最前面
export PATH="C:/Programs/Qt/Tools/mingw1310_64/bin:C:/Programs/Qt/Tools/Ninja:$PATH"

# 1. 配置（仅首次）
cmake --preset default -DCMAKE_BUILD_TYPE=Release

# 2. 编译
cmake --build build/vcpkg-mingw --config Release --parallel

# 3. 部署
cmake --build build/vcpkg-mingw --config Release --target deploy
```

### 编译产物

| 产物 | 路径 |
|------|------|
| 可执行文件 | `bin/ArmSightStitch.exe` |
| 部署后完整目录 | `bin/`（含 Qt/MinGW DLL、模型、样式表、配置） |
| NSIS 安装包 | `build/vcpkg-mingw/ArmSightStitch-2.0.0-win64.exe` |

### 编译选项

| CMake Preset | 编译器 | triplet | 说明 |
|-------------|--------|---------|------|
| `default` | MinGW 13.1.0 (Ninja) | `x64-mingw-static` | **主要构建配置** |
| `msvc` | Visual Studio 2022 | `x64-windows` | MSVC 备选 |

### 注意事项

- **不要用 `cmake --preset` 重复配置**：修改代码后直接用 `ninja` 增量编译即可。若 CMakeLists.txt 被修改导致自动重配置，而当前 shell 未设置 MinGW PATH，会触发 vcpkg 编译器检测挂死。可用 `touch build/vcpkg-mingw/build.ninja` 阻止自动重配。
- **NSIS 打包**：CPack 生成的 NSIS 脚本中 MUI 资源（图标、位图、许可证）路径为绝对路径格式，NSIS 3.12 无法解析。需将 `resources/{R.bmp,logo.ico,License.txt}` 复制到 `build/vcpkg-mingw/_CPack_Packages/win64/NSIS/`，编辑 `project.nsi` 改为裸文件名，再运行 `makensis.exe project.nsi`。
- **打包前清理**：删除 `bin/logs/`（spdlog 可能锁文件）和 `build/vcpkg-mingw/_CPack_Packages/`（残留状态）。

### 增量编译（日常开发）

```bash
# 仅编译修改过的文件（在 build 目录直接运行 ninja）
cd build/vcpkg-mingw
ninja
```

## 依赖

| 组件 | 版本/说明 |
|------|-----------|
| Qt6 | 6.10.1 — Widgets, Core, Gui, Network, Concurrent |
| OpenCV | 4.x — core, imgproc, imgcodecs, stitching |
| ncnn | 2023+ — YOLO 模型推理 |
| libmodbus | 3.1.12（FetchContent 自动下载） |
| ToupCam SDK | `third_party/toupcam/` — 相机厂商 SDK |
| spdlog | 日志 |
| vcpkg | 包管理，triplet: `x64-mingw-static` |

## 目录结构

```
ArmSightStitch/
├── app/              # main.cpp 入口
├── core/
│   ├── arm/          # 机械臂 Modbus TCP 控制器 + S 运动控制
│   ├── camera/       # ToupCam SDK 封装
│   ├── detector/     # YOLO (NCNN) + Dust 灰尘传统 CV 检测
│   └── stitch/       # 4 种图像拼接算法 (Grid/Feature/ZScale/SeamFeather)
├── infra/
│   ├── config/       # 配置管理器（JSON + 环境变量覆盖）
│   └── log/          # spdlog 日志管理器
├── ui/               # Qt 界面（MainWindow + 对话框 + WorkflowManager）
├── res/              # 资源文件（模型、图标、样式表）
├── third_party/      # 第三方 SDK（ToupCam）
├── docs/             # 设计文档 + Python 算法原型
└── resources/        # 安装包资源（图标、位图、许可证）
```
