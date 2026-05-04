@echo off
REM Build script for ArmSightStitch

REM Parse command line arguments and set build variables
set BUILD_TYPE=Debug
set BUILD_DIR=build\vcpkg-mingw

if "%~1"=="release" set BUILD_TYPE=Release
if "%~1"=="Release" set BUILD_TYPE=Release

if "%BUILD_TYPE%"=="Release" (
    set BUILD_DIR=build\vcpkg-mingw
)

REM First, make sure we're in the project root directory
cd /d %~dp0

REM Display build information
if "%BUILD_TYPE%"=="Debug" (
    echo Building Debug version...
) else (
    echo Building Release version...
)

REM Add MinGW to PATH
SET "PATH=C:/Programs/Qt/Tools/mingw1310_64/bin;C:/Programs/Qt/Tools/Ninja;%PATH%"

REM Configure project
echo Configuring project...
cmake -G "Ninja" -S . -B %BUILD_DIR% ^
  -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static ^
  -DVCPKG_HOST_TRIPLET=x64-mingw-static ^
  -DCMAKE_PREFIX_PATH="C:/Programs/Qt/6.10.1/mingw_64" ^
  -DQt6_DIR="C:/Programs/Qt/6.10.1/mingw_64/lib/cmake/Qt6" ^
  -DCMAKE_C_COMPILER="C:/Programs/Qt/Tools/mingw1310_64/bin/gcc.exe" ^
  -DCMAKE_CXX_COMPILER="C:/Programs/Qt/Tools/mingw1310_64/bin/g++.exe" ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake"

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
