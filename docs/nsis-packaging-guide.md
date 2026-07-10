# NSIS 安装包打包指南

## 概览

ArmSightStitch 使用 **CPack + NSIS** 生成 Windows 安装程序 (`.exe`)。

- 安装包名称：`ArmSightStitch-<版本号>-win64.exe`
- 安装位置：`C:\Program Files\ArmSightStitch\`
- 生成快捷方式：开始菜单 + 桌面
- 支持卸载（控制面板可找到）

## 前置条件

| 工具 | 用途 | 安装方式 |
|------|------|----------|
| NSIS 3.x | 编译安装包脚本 | `winget install NSIS.NSIS` 或 [nsis.sourceforge.io](https://nsis.sourceforge.io) |
| MinGW 13.1.0 | 编译器 | Qt 自带 (`C:\Qt\Tools\mingw1310_64\`) |
| CMake 3.30+ | 构建系统 | Qt 自带或独立安装 |
| Ninja | 构建工具 | vcpkg 下载 |

## 完整打包流程

### 1. 编译项目

```powershell
# 配置（仅首次）
cmake --preset default -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build/vcpkg-mingw --config Release --parallel
```

### 2. Deploy（收集运行时依赖）

```powershell
cmake --build build/vcpkg-mingw --config Release --target deploy
```

这一步会：
- 运行 `windeployqt` 收集 Qt DLL、插件、翻译文件
- 复制 MinGW 运行时 DLL（`libgcc_s_seh-1`, `libstdc++-6`, `libwinpthread-1`, `libgomp-1`）
- 清理无用文件（`opengl32sw.dll`, `D3Dcompiler_47.dll`, 多余翻译）
- 清理嵌套 `bin/` 目录和残留日志

### 3. 生成安装包

```powershell
cd build/vcpkg-mingw
cpack -G NSIS
```

输出文件在：`build/vcpkg-mingw/_CPack_Packages/win64/NSIS/ArmSightStitch-<版本>-win64.exe`

安装包约 **70MB**（LZMA 压缩后）。

## 已知问题与解决

### NSIS 3.12 路径兼容性问题

**症状**：CPack 调用 NSIS 时报错：
```
File: "C:/Users/.../resources/R.bmp" -> no files found.
Error in macro MUI_WELCOMEFINISHPAGE_INIT on macroline 5
```

**根因**：CPack 生成的 `project.nsi` 使用正斜杠绝对路径（如 `C:/Users/.../R.bmp`），但 NSIS 3.12 的 MUI 宏内部 `File` 指令无法正确解析这种格式的路径。

**修复方案（CMakeLists.txt 已更新）**：
```cmake
# 使用 CMake 的 TO_NATIVE_PATH 转换路径格式（反斜杠）
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/logo.ico" _NSIS_LOGO_ICO)
set(CPACK_NSIS_MUI_ICON "${_NSIS_LOGO_ICO}")
```

**手动应急修复**（当 cmake 配置无法重新生成时）：

```powershell
# 1. 把资源文件复制到 NSIS 临时目录
$nsisDir = "build/vcpkg-mingw/_CPack_Packages/win64/NSIS"
Copy-Item resources\R.bmp "$nsisDir\R.bmp"
Copy-Item resources\logo.ico "$nsisDir\logo.ico"
Copy-Item resources\License.txt "$nsisDir\License.txt"

# 2. 修改 project.nsi 的绝对路径为相对路径
#    MUI_ICON "C:/Users/.../logo.ico"     → MUI_ICON "logo.ico"
#    MUI_WELCOMEFINISHPAGE_BITMAP "C:/.../R.bmp" → MUI_WELCOMEFINISHPAGE_BITMAP "R.bmp"
#    MUI_PAGE_LICENSE "C:/.../License.txt" → MUI_PAGE_LICENSE "License.txt"

# 3. 手动运行 makensis
& "C:\Program Files (x86)\NSIS\makensis.exe" "$nsisDir\project.nsi"
```

### cmake 重配置触发 vcpkg 编译器检测

**症状**：修改 `CMakeLists.txt` 后运行 `cmake --build`，触发 vcpkg 重新检测编译器并失败。

**根因**：当前 shell 的 `PATH` 中没有 MinGW，vcpkg 找不到 `g++.exe`。

**解决**：确保运行 cmake 命令时 `PATH` 包含：
```
C:\Qt\Tools\mingw1310_64\bin
C:\Qt\Tools\CMake_64\bin
```

## 打包内容清单

安装包包含以下文件和目录：

```
C:\Program Files\ArmSightStitch\
└── bin\
    ├── ArmSightStitch.exe        # 主程序
    ├── config.json               # 配置文件
    ├── industrial_theme.qss      # 样式表
    ├── toupcam.dll               # 相机 SDK
    ├── Qt6Core.dll               # Qt 运行时
    ├── Qt6Gui.dll
    ├── Qt6Widgets.dll
    ├── Qt6Network.dll
    ├── Qt6Svg.dll
    ├── libopencv_*.dll           # OpenCV 模块
    ├── libncnn.dll               # NCNN 推理引擎
    ├── libspdlog.dll             # 日志库
    ├── libgcc_s_seh-1.dll        # MinGW 运行时
    ├── libstdc++-6.dll
    ├── libwinpthread-1.dll
    ├── libgomp-1.dll
    ├── models\                   # YOLO 模型
    │   ├── best-sim-opt.ncnn.param
    │   └── best-sim-opt.ncnn.bin
    ├── platforms\                # Qt 平台插件
    │   └── qwindows.dll
    ├── styles\                   # Qt 样式插件
    ├── imageformats\             # 图片格式插件
    ├── iconengines\
    ├── networkinformation\
    ├── tls\
    └── translations\             # 仅保留 qt_zh_CN.qm
```

## NSIS 安装程序特性

| 特性 | 说明 |
|------|------|
| 图标 | `resources/logo.ico` |
| 欢迎/完成页位图 | `resources/R.bmp` |
| 许可证 | `resources/License.txt` |
| 安装路径 | `$PROGRAMFILES64\ArmSightStitch`（用户可选） |
| 快捷方式 | 开始菜单 + 桌面 |
| 卸载 | 控制面板 / 开始菜单 / 安装目录 `Uninstall.exe` |
| 权限 | 需要管理员权限 |
| 覆盖安装 | 支持检测旧版本并先卸载 |
| 压缩 | LZMA |
