# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ArmSightStitch is a Windows desktop application for bright-field microscope validation. It integrates a 5-axis robotic arm (Modbus TCP), ToupCam camera, YOLO object detection (NCNN inference), and dual-algorithm image stitching (grid-position + OpenCV feature matching). The primary build target is MinGW 13.1.0 with Qt 6.10.1.

## Build Commands

```powershell
# Configure (from repo root)
cmake --preset default -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build/vcpkg-mingw --config Release --parallel

# Deploy (windeployqt + MinGW runtime DLLs → bin/)
cmake --build build/vcpkg-mingw --config Release --target deploy

# Package NSIS installer (after deploy, from build dir)
cd build/vcpkg-mingw
cpack -G NSIS
```

The `default` preset uses Ninja + MinGW + vcpkg (`x64-mingw-static` triplet). An `msvc` preset also exists for Visual Studio 2022 + `x64-windows` triplet. Prerequisites: `VCPKG_ROOT` env var, Qt 6.10.1 at `C:/Programs/Qt/6.10.1/mingw_64/`.

Output executable: `bin/ArmSightStitch.exe`. The build also copies models (`res/models/`), stylesheet (`industrial_theme.qss`), config, and ToupCam DLL to `bin/`.

## Important Rules

- **NEVER run `cmake --preset` or modify `CMakeLists.txt` unless explicitly asked by the user.** Modifying `CMakeLists.txt` or reconfiguring cmake triggers vcpkg compiler re-detection, which hangs if MinGW is not in the current shell's PATH. Use `ninja` directly to build: `cd build/vcpkg-mingw && ninja`.
- If a build step triggers cmake regeneration (because `CMakeLists.txt` is newer than `build.ninja`), touch `build.ninja` to prevent it: `touch build/vcpkg-mingw/build.ninja`.
- **NSIS packaging workaround**: CPack's NSIS generator produces absolute forward-slash paths for MUI resources (`MUI_ICON`, `MUI_WELCOMEFINISHPAGE_BITMAP`, `MUI_PAGE_LICENSE`) which NSIS 3.12 cannot resolve inside MUI macros. Workaround: after `cpack -G NSIS` fails, copy `resources/{R.bmp,logo.ico,License.txt}` into `build/vcpkg-mingw/_CPack_Packages/win64/NSIS/`, edit `project.nsi` to use bare filenames instead of absolute paths, then run `makensis.exe project.nsi` directly.
- Before running `cpack`, delete `bin/logs/` (log file may be locked by spdlog) and `build/vcpkg-mingw/_CPack_Packages/` (stale state).

## Architecture

**Pattern overview:**
- **Strategy pattern** with abstract interfaces for all core modules (`IArmController`, `ISMovementController`, `ICameraHandler`, `IDetector`, `IStitcher`, `IStitchAlgorithm`). Each has exactly one concrete implementation.
- **Pimpl idiom** — `AppController` hides all five concrete implementations behind `struct Impl` + `std::unique_ptr<Impl>`, so `AppController.h` never exposes third-party types (libmodbus, ToupCam, NCNN, OpenCV).
- **Dependency injection** — `WorkflowManager` receives `IArmController&`, `ISMovementController&`, `ICameraHandler&`, `IStitcher&` via constructor. `AppController` owns the concrete instances and passes references.
- **Callback-based communication** — modules push status via `std::function` callbacks (`setStatusCallback`, `setImageCallback`, `setProgressCallback`) instead of observer pattern.
- **Async via QtConcurrent** — long operations (stitching, detection, arm connect) run on `QFuture`/`QFutureWatcher` background threads; results delivered via Qt signals to the UI thread.
- **S-movement** runs on its own `std::thread` with an internal state machine for pause/resume.

**Layer map:**
```
app/main.cpp          — entry point: ConfigManager → LogManager → AppController → MainWindow
ui/                   — Qt layer: MainWindow (signals/slots), AppController (pimpl coordinator),
                        WorkflowManager (auto-workflow FSM), dialog classes
core/arm/             — ModbusArmController (register/coil I/O), SMovementController (S-curve scan),
                        IArmController.h (both interfaces + shared structs)
core/camera/          — CameraHandler (ToupCam SDK wrapper)
core/detector/        — YoloDetector (NCNN inference, model load, bounding box drawing)
core/stitch/          — ImageStitcher (orchestrator), IStitchAlgorithm + 2 impls (grid/feature)
infra/config/         — ConfigManager (singleton, JSON load/save, env-var overrides), PathUtils
infra/log/            — LogManager (spdlog: console + rotating file)
```

**Optional modules** — if NCNN/ToupCam/libmodbus are unavailable at configure time, the corresponding module is compiled out via `ARM_SIGHT_STITCH_NO_DETECTOR`, `ARM_SIGHT_STITCH_NO_CAMERA`, `ARM_SIGHT_STITCH_NO_ARM` preprocessor defines.

**Image stitching strategy:** `ImageStitcher` (orchestrator) delegates to `IStitchAlgorithm`. Algorithm 1 (`GridStitchAlgorithm`, pure header) tiles images by grid position. Algorithm 2 (`FeatureStitchAlgorithm`, pure header) wraps `cv::Stitcher::PANORAMA`. Both are header-only and report progress via `IStitchAlgorithm`'s callbacks.

## Config System

`ConfigManager` is a singleton. On startup, `main.cpp` loads `config.json` from the executable directory (fallback: hardcoded defaults). Environment variables override file values:

| Variable | Config field |
|---|---|
| `ARM_SIGHT_STITCH_ARM_IP` | `arm_ip` |
| `ARM_SIGHT_STITCH_DEFAULT_SPEED` | `default_speed` |
| `ARM_SIGHT_STITCH_GRID_X` / `_Y` | `grid_size_x` / `_y` |
| `ARM_SIGHT_STITCH_STEP_SIZE` | `step_size` |
| `ARM_SIGHT_STITCH_GRID_HEIGHT` | `z_height` |
| `ARM_SIGHT_STITCH_DWELL_TIME_MS` | `dwell_time_ms` |

## Key Build Details

- **libmodbus** is fetched via `FetchContent` (v3.1.12), patched with a Windows `config.h`, compiled as C sources, and statically linked. Requires `FD_SETSIZE=32768` compile definition on Windows to handle socket handles > 1024 (see `docs/libmodbus-fix-summary.md`).
- **OpenCV** modules are limited to `core imgproc imgcodecs stitching`. TIFF linkage is forced to vcpkg's static `libtiff.a` to avoid MSVC `tiff.dll` dependency from conda installations.
- **Dead-code stripping**: `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` for MinGW.
- **CMake AUTOUIC/AUTOMOC/AUTORCC** are enabled — `.ui` files and `resources.qrc` are processed automatically.
- **Deploy target** runs `windeployqt`, copies MinGW runtime DLLs (`libgcc_s_seh-1`, `libstdc++-6`, `libwinpthread-1`, `libgomp-1`), removes `opengl32sw.dll`/`D3Dcompiler_47.dll`, and strips unused Qt translations.

## Important docs

- [docs/机械臂连接协议.md](docs/机械臂连接协议.md) — exhaustive Modbus TCP register map, coil addresses, data encoding for all 5 axes (~15K)
- [docs/libmodbus-fix-summary.md](docs/libmodbus-fix-summary.md) — root cause of `FD_SETSIZE=EINVAL` on Windows MinGW
- [docs/deploy-dll-summary.md](docs/deploy-dll-summary.md) — DLL inventory and sizes for the deploy output

## Branching

Git Flow simplified: `develop` (active) → `main` (releases). Commit messages are in Chinese.
