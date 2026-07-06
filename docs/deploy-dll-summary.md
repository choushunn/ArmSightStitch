# ArmSightStitch 部署依赖 DLL 清单

> 生成日期: 2026-07-06 | 编译器: Qt MinGW 13.1.0 (MSVCRT) | 构建类型: Release

## 执行文件

| 文件 | 大小 | 说明 |
|------|------|------|
| `ArmSightStitch.exe` | 1.3 MB | 主程序 |

## Qt 运行时 (6.11.1)

| DLL | 大小 | 说明 |
|-----|------|------|
| `Qt6Core.dll` | 11.0 MB | Qt 核心库 |
| `Qt6Gui.dll` | 11.3 MB | Qt GUI 库 |
| `Qt6Widgets.dll` | 7.0 MB | Qt Widgets 库 |
| `Qt6Network.dll` | 1.9 MB | Qt 网络库 |
| `Qt6Svg.dll` | 620 KB | Qt SVG 支持 |

### Qt 插件目录

| 目录 | 包含 | 说明 |
|------|------|------|
| `platforms/` | `qwindows.dll` | Windows 平台插件 |
| `styles/` | `qmodernwindowsstyle.dll` | 界面样式 |
| `imageformats/` | `qgif.dll`, `qico.dll`, `qjpeg.dll`, `qsvg.dll` | 图像格式支持 |
| `iconengines/` | `qsvgicon.dll` | SVG 图标引擎 |
| `tls/` | `qcertonlybackend.dll`, `qschannelbackend.dll` | SSL/TLS 后端 |
| `networkinformation/` | `qnetworklistmanager.dll` | 网络信息 |
| `generic/` | `qtuiotouchplugin.dll` | 触摸插件 |

## OpenCV (4.12.0)

| DLL | 大小 | 说明 |
|-----|------|------|
| `libopencv_core4.dll` | 28.5 MB | 核心模块 |
| `libopencv_imgproc4.dll` | 32.7 MB | 图像处理 |
| `libopencv_imgcodecs4.dll` | 4.7 MB | 图像编解码 (`cv::imread`/`cv::imwrite`) |
| `libopencv_stitching4.dll` | 7.8 MB | 图像拼接 (`cv::Stitcher`) |
| `libopencv_calib3d4.dll` | 14.6 MB | 相机标定 (stitching 依赖) |
| `libopencv_features2d4.dll` | 5.9 MB | 特征检测 (stitching 依赖) |
| `libopencv_flann4.dll` | 3.1 MB | 特征匹配 (stitching 依赖) |

## 图像格式库 (OpenCV imgcodecs 依赖)

| DLL | 大小 | 说明 |
|-----|------|------|
| `libjpeg-62.dll` | 894 KB | JPEG 编解码 |
| `libpng16.dll` | 346 KB | PNG 编解码 |
| `libtiff-6.dll` | 725 KB | TIFF 编解码 |
| `libwebp.dll` | 838 KB | WebP 编解码 |
| `libwebpdecoder.dll` | 510 KB | WebP 解码器 |
| `libwebpdemux.dll` | 48 KB | WebP 解复用 |
| `libwebpmux.dll` | 111 KB | WebP 复用 |
| `libsharpyuv.dll` | 83 KB | SharpYUV 转换 |
| `libz.dll` | 177 KB | zlib 压缩 |
| `liblzma.dll` | 244 KB | LZMA 压缩 |

## 其他第三方库

| DLL | 大小 | 说明 |
|-----|------|------|
| `libncnn.dll` | 16.5 MB | NCNN 神经网络推理 (YOLO 检测) |
| `libspdlog.dll` | 813 KB | 日志框架 |
| `libfmt.dll` | 263 KB | 格式化库 (spdlog 依赖) |
| `toupcam.dll` | 28.5 MB | ToupCam 相机 SDK |

## MinGW 运行时 (必须)

| DLL | 大小 | 来源 | 说明 |
|-----|------|------|------|
| `libgcc_s_seh-1.dll` | 107 KB | Qt MinGW | GCC 异常处理 |
| `libstdc++-6.dll` | 2.2 MB | Qt MinGW | C++ 标准库 |
| `libwinpthread-1.dll` | 52 KB | Qt MinGW | POSIX 线程 |
| `libgomp-1.dll` | 273 KB | Qt MinGW | OpenMP (NCNN 需要) |

## libmodbus

**静态链接** — 编译进 `ArmSightStitch.exe`，无需额外 DLL。

来源: `https://github.com/stephane/libmodbus` (v3.1.12, FetchContent)

---

## 部署步骤

```powershell
# 1. windeployqt (自动复制 Qt DLL + MinGW 运行时 + 插件)
C:\Qt\6.11.1\mingw_64\bin\windeployqt.exe --dir bin bin\ArmSightStitch.exe --no-translations

# 2. 复制 vcpkg DLL
$vcpkg = "vcpkg_installed\x64-mingw-dynamic\bin"
Copy-Item $vcpkg\libfmt.dll, $vcpkg\libncnn.dll, $vcpkg\libopencv_*.dll, $vcpkg\libspdlog.dll bin\
Copy-Item $vcpkg\libjpeg-62.dll, $vcpkg\libpng16.dll, $vcpkg\libtiff-6.dll, $vcpkg\libwebp*.dll bin\
Copy-Item $vcpkg\libsharpyuv.dll, $vcpkg\libz.dll, $vcpkg\liblzma.dll bin\

# 3. 复制 OpenMP 运行时
Copy-Item C:\Qt\Tools\mingw1310_64\bin\libgomp-1.dll bin\
```

> ⚠️ `config.json` 和 `industrial_theme.qss` 需要放在 `bin\` 父目录或可执行文件同级目录。
