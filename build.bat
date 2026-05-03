@echo off
REM Build script for SerialCCDMonitor

REM Parse command line arguments and set build variables
set BUILD_TYPE=Debug
set BUILD_DIR=build\Debug
set WINDEPLOYQT_MODE=--debug
set CMAKE_PREFIX_PATH_ADD=

if "%~1"=="release" set BUILD_TYPE=Release
if "%~1"=="Release" set BUILD_TYPE=Release

if "%BUILD_TYPE%"=="Release" (
    set BUILD_DIR=build\Release
    set WINDEPLOYQT_MODE=--release
) else (
    set CMAKE_PREFIX_PATH_ADD=;%VCPKG_INSTALL_DIR%/debug
)

REM First, make sure we're in the project root directory
cd /d %~dp0

REM Display build information
if "%BUILD_TYPE%"=="Debug" (
    echo Building Debug version...
) else (
    echo Building Release version...
)

REM Clean old build directories if they exist
echo Cleaning old build directories...
@REM if exist build rmdir /s /q build

REM Create build directory if it doesn't exist
mkdir build

REM Add MinGW to PATH
SET "PATH=C:/Programs/Qt/Tools/mingw1310_64/bin;%PATH%"

REM Configure project
echo Configuring project...
cmake -G "Ninja" -S . -B %BUILD_DIR% ^
  -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static ^
  -DVCPKG_HOST_TRIPLET=x64-mingw-static ^
  -DCMAKE_PREFIX_PATH="C:/Programs/Qt/6.9.1/mingw_64;%VCPKG_INSTALL_DIR%%CMAKE_PREFIX_PATH_ADD%" ^
  -DQt6_DIR="C:/Programs/Qt/6.9.1/mingw_64/lib/cmake/Qt6" ^
  -DCMAKE_TOOLCHAIN_FILE="C:/Programs/vcpkg-2025.04.09/scripts/buildsystems/vcpkg.cmake"

if errorlevel 1 (
    echo Project configuration failed!
    exit /b 1
)

REM Build project
echo Building project...
cmake --build %BUILD_DIR% --config %BUILD_TYPE%

if errorlevel 1 (
    echo Project build failed!
    exit /b 1
)
