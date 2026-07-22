cmake_minimum_required(VERSION 3.16)

project(ArmSightStitch VERSION 2.0.0 LANGUAGES CXX C)

# Auto-detect version from git tags (overrides project() fallback above)
include(cmake/GetVersion.cmake)
configure_file(cmake/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ============================================================
# Dependencies
# ============================================================

# Qt
find_package(Qt6 QUIET COMPONENTS Widgets Core Gui Network Concurrent)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    message(STATUS "Found Qt6: ${Qt6_DIR}")
else()
    find_package(Qt5 5.15 QUIET COMPONENTS Widgets Core Gui Network Concurrent)
    if(Qt5_FOUND)
        set(QT_VERSION_MAJOR 5)
        message(STATUS "Found Qt5: ${Qt5_DIR}")
    else()
        message(FATAL_ERROR "Qt6 or Qt5 (5.15+) with Widgets,Core,Gui,Network,Concurrent required. "
                           "Set Qt6_DIR or Qt5_DIR or CMAKE_PREFIX_PATH to Qt installation.")
    endif()
endif()

# OpenCV — only link modules we actually use
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # MinGW: Force TIFF to use FindTIFF.cmake (MODULE mode) with vcpkg's static libtiff.a,
    # instead of finding miniforge3's TiffConfig.cmake (CONFIG mode) which brings MSVC tiff.dll dependency.
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
    set(Tiff_DIR "Tiff_DIR-NOTFOUND" CACHE PATH "" FORCE)
    set(TIFF_INCLUDE_DIR "${CMAKE_PREFIX_PATH}/include" CACHE PATH "" FORCE)
    set(TIFF_LIBRARY "${CMAKE_PREFIX_PATH}/lib/libtiff.a" CACHE FILEPATH "" FORCE)
    set(TIFF_LIBRARY_RELEASE "${CMAKE_PREFIX_PATH}/lib/libtiff.a" CACHE FILEPATH "" FORCE)
    set(TIFF_LIBRARY_DEBUG "${CMAKE_PREFIX_PATH}/debug/lib/libtiffd.a" CACHE FILEPATH "" FORCE)
endif()
# Required: core OpenCV modules (CPU build)
# CUDA modules are optional — guarded by HAS_OPENCV_CUDA at compile time
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs stitching
             OPTIONAL_COMPONENTS cudaarithm cudawarping cudafilters cudaimgproc)
set(HAS_OPENCV_CUDA TRUE)
foreach(comp cudaarithm cudawarping cudafilters cudaimgproc)
    if(NOT OpenCV_${comp}_FOUND)
        set(HAS_OPENCV_CUDA FALSE)
        break()
    endif()
endforeach()
if(HAS_OPENCV_CUDA)
    find_package(CUDAToolkit REQUIRED)
    message(STATUS "Found CUDA Toolkit: ${CUDAToolkit_VERSION} at ${CUDAToolkit_BIN_DIR}")
else()
    message(WARNING "OpenCV CUDA modules not available — GPU-accelerated features disabled")
endif()
message(STATUS "Found OpenCV: ${OpenCV_VERSION} at ${OpenCV_DIR}")

# Optimize: strip dead code from statically linked libraries
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()

# spdlog
find_package(spdlog REQUIRED)
message(STATUS "Found spdlog: ${spdlog_VERSION}")

# NCNN
find_package(ncnn QUIET)
if(ncnn_FOUND)
    message(STATUS "Found NCNN via vcpkg: ${ncnn_DIR}")
    set(HAS_NCNN TRUE)
else()
    message(WARNING "NCNN not found via vcpkg. Detection module will be disabled. Add ncnn to vcpkg.json")
    set(HAS_NCNN FALSE)
endif()

# ============================================================
# Third-party: libmodbus
# ============================================================
include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_Declare(libmodbus
    GIT_REPOSITORY  https://github.com/stephane/libmodbus.git
    GIT_TAG         v3.1.12
    GIT_SHALLOW     TRUE
)
FetchContent_GetProperties(libmodbus)
if(NOT libmodbus_POPULATED)
    FetchContent_Populate(libmodbus)
    file(COPY "${libmodbus_SOURCE_DIR}/src/win32/config.h.win32"
         DESTINATION "${libmodbus_SOURCE_DIR}/src")
    file(RENAME "${libmodbus_SOURCE_DIR}/src/config.h.win32"
         "${libmodbus_SOURCE_DIR}/src/config.h")
    file(READ "${libmodbus_SOURCE_DIR}/src/config.h" CONFIG_H_CONTENT)
    string(REPLACE "@LIBMODBUS_VERSION@" "3.1.12" CONFIG_H_CONTENT "${CONFIG_H_CONTENT}")
    file(WRITE "${libmodbus_SOURCE_DIR}/src/config.h" "${CONFIG_H_CONTENT}")
    set(LIBMODBUS_VERSION_MAJOR 3)
    set(LIBMODBUS_VERSION_MINOR 1)
    set(LIBMODBUS_VERSION_MICRO 12)
    configure_file(
        "${libmodbus_SOURCE_DIR}/src/modbus-version.h.in"
        "${libmodbus_SOURCE_DIR}/src/modbus-version.h"
        @ONLY
    )
    set(LIBMODBUS_SOURCES
        "${libmodbus_SOURCE_DIR}/src/modbus.c"
        "${libmodbus_SOURCE_DIR}/src/modbus-tcp.c"
        "${libmodbus_SOURCE_DIR}/src/modbus-rtu.c"
        "${libmodbus_SOURCE_DIR}/src/modbus-data.c"
    )
    set_source_files_properties(${LIBMODBUS_SOURCES} PROPERTIES LANGUAGE C)
    include_directories("${libmodbus_SOURCE_DIR}/src")
    message(STATUS "Found libmodbus: ${libmodbus_SOURCE_DIR}")
    set(HAS_LIBMODBUS TRUE)
else()
    set(HAS_LIBMODBUS FALSE)
endif()

# ToupCam SDK
set(HAS_TOUPCAM FALSE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/toupcam/inc/toupcam.h")
    include("${CMAKE_CURRENT_SOURCE_DIR}/third_party/toupcam/toupcam.cmake")
    set(HAS_TOUPCAM TRUE)
    message(STATUS "Found ToupCam SDK: ${CMAKE_CURRENT_SOURCE_DIR}/third_party/toupcam")
else()
    message(WARNING "ToupCam SDK not found at third_party/toupcam/. Camera module will be disabled.")
endif()

# ============================================================
# Source files - explicitly listed
# ============================================================

set(SOURCES
    app/main.cpp
    ui/MainWindow.cpp
    ui/AppController.cpp
    ui/DetectionSettingsDialog.cpp
    ui/SphereSettingsDialog.h
    ui/ScanParametersDialog.h
    ui/StitchingSettingsDialog.cpp
    core/arm/ModbusArmController.cpp
    core/arm/SMovementController.cpp
    core/camera/CameraHandler.cpp
    core/detector/YoloDetector.cpp
    core/stitch/ImageStitcher.cpp
    core/stitch/GridStitchAlgorithm.h
    core/stitch/FeatureStitchAlgorithm.h
    core/stitch/ZScaleGridStitchAlgorithm.h
    core/stitch/SeamFeatherStitchAlgorithm.h
    core/stitch/IStitchAlgorithm.h
    infra/config/ConfigManager.cpp
    ui/WorkflowManager.cpp
)

set(RESOURCES
    res/qt/resources.qrc
)

# Create output directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# ============================================================
# Executable
# ============================================================

add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
    ${RESOURCES}
    ui/MainWindow.ui
    ui/DetectionSettingsDialog.ui
    ui/StitchingSettingsDialog.ui
    resources/app.rc
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}        # for generated version.h
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/core/arm
    ${CMAKE_CURRENT_SOURCE_DIR}/core/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/core/detector
    ${CMAKE_CURRENT_SOURCE_DIR}/core/stitch
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/config
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/log
)

# Compile definitions and optional module support
if(HAS_LIBMODBUS)
    target_sources(${PROJECT_NAME} PRIVATE ${LIBMODBUS_SOURCES})
    # libmodbus is statically linked — no DLLBUILD needed
    # Windows SOCKET handles can be any kernel handle value (often > 2000),
    # but libmodbus checks `fd >= FD_SETSIZE` in many places. Use a large
    # FD_SETSIZE so all valid handles pass the check. (32768 = 256KB per fd_set)
    # OS_WIN32 is required by libmodbus to enable Windows-specific code paths
    # (WSAStartup, non-blocking connect, etc.)
    target_compile_definitions(${PROJECT_NAME} PRIVATE FD_SETSIZE=32768 OS_WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

if(HAS_OPENCV_CUDA)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_OPENCV_CUDA HAVE_OPENCV_CUDAFILTERS)
endif()
if(NOT HAS_NCNN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARM_SIGHT_STITCH_NO_DETECTOR)
endif()
if(NOT HAS_TOUPCAM)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARM_SIGHT_STITCH_NO_CAMERA)
endif()
if(NOT HAS_LIBMODBUS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARM_SIGHT_STITCH_NO_ARM)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Concurrent
    ${OpenCV_LIBS}
    spdlog::spdlog
)

if(HAS_NCNN)
    target_link_libraries(${PROJECT_NAME} PRIVATE ncnn)
endif()
if(HAS_TOUPCAM)
    target_link_libraries(${PROJECT_NAME} PRIVATE toupcam)
endif()

# Output directory — separate for MinGW vs MSVC+CUDA to avoid conflicts
if(HAS_OPENCV_CUDA)
    set(OUTPUT_BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin-msvc)
else()
    set(OUTPUT_BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_BIN_DIR}
)

# ============================================================
# Post-build: copy model files and DLLs
# ============================================================

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/res/models $<TARGET_FILE_DIR:${PROJECT_NAME}>/models
    COMMENT "Copying model files..."
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/industrial_theme.qss $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.json $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/res/z_map_template.json $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/res/scale_map_template.json $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/res/z_radial_template.json $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/res/crop_offset_template.json $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying stylesheet and config..."
)

if(HAS_TOUPCAM)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TOUPCAM_DLL}" $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying toupcam.dll to output directory"
    )
endif()

# ============================================================
# Deploy targets: windeployqt + runtime DLLs
# ============================================================

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # --- MinGW deploy ---
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt windeployqt.exe
        PATHS "C:/Programs/Qt/6.10.1/mingw_64/bin"
        NO_DEFAULT_PATH
    )
    if(WINDEPLOYQT_EXECUTABLE)
        get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

        add_custom_target(deploy
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 0: Clean nested bin dirs + logs ==="
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/bin
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/logs
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 1: Deploying Qt runtime ==="
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                --dir $<TARGET_FILE_DIR:${PROJECT_NAME}>
                $<TARGET_FILE:${PROJECT_NAME}>
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 2: Copying MinGW runtime DLLs ==="
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"    $<TARGET_FILE_DIR:${PROJECT_NAME}>
                "${MINGW_BIN_DIR}/libstdc++-6.dll"       $<TARGET_FILE_DIR:${PROJECT_NAME}>
                "${MINGW_BIN_DIR}/libwinpthread-1.dll"   $<TARGET_FILE_DIR:${PROJECT_NAME}>
                "${MINGW_BIN_DIR}/libgomp-1.dll"         $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 3: Clean up unnecessary files ==="
            COMMAND ${CMAKE_COMMAND} -E rm -f
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/opengl32sw.dll
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/D3Dcompiler_47.dll
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 4: Remove unused translations ==="
            COMMAND ${CMAKE_COMMAND} -E rm -f
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ar.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_bg.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ca.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_cs.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_da.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_de.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_en.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_es.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_fa.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_fi.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_fr.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_gd.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_he.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_hr.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_hu.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_it.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ja.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ka.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ko.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_lg.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_lv.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_nl.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_nn.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_pl.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_pt_BR.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ru.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_sk.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_sv.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_tr.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_uk.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_zh_TW.qm
            COMMAND ${CMAKE_COMMAND} -E echo "=== Deploy complete ==="
            COMMENT "Deploy: Qt + MinGW runtime DLLs"
            DEPENDS ${PROJECT_NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        message(STATUS "Found windeployqt (MinGW): ${WINDEPLOYQT_EXECUTABLE}")
    else()
        message(WARNING "windeployqt (MinGW) not found. Deploy target will not be available.")
    endif()

elseif(MSVC)
    # --- MSVC deploy ---
    find_program(WINDEPLOYQT_MSVC_EXECUTABLE
        NAMES windeployqt windeployqt.exe
        PATHS "C:/Programs/Qt/6.10.1/msvc2022_64/bin"
        NO_DEFAULT_PATH
    )
    if(WINDEPLOYQT_MSVC_EXECUTABLE)
        # Derive VCINSTALLDIR from the compiler path.
        # cl.exe is at .../VC/Tools/MSVC/<ver>/bin/Hostx64/x64/cl.exe → VCINSTALLDIR = .../VC/
        get_filename_component(_cl_d1 "${CMAKE_CXX_COMPILER}" DIRECTORY)   # Hostx64/x64
        get_filename_component(_cl_d2 "${_cl_d1}" DIRECTORY)               # Hostx64
        get_filename_component(_cl_d3 "${_cl_d2}" DIRECTORY)               # bin
        get_filename_component(_cl_ver "${_cl_d3}" DIRECTORY)              # <ver> (e.g. 14.50.35717)
        get_filename_component(_cl_d5  "${_cl_ver}" DIRECTORY)             # MSVC
        get_filename_component(_cl_d6  "${_cl_d5}" DIRECTORY)              # Tools
        get_filename_component(_vc_dir "${_cl_d6}" DIRECTORY)              # VC
        # Redist version may differ from Tools version — glob for vcruntime140.dll
        file(GLOB_RECURSE _vcruntime_dlls "${_vc_dir}/Redist/MSVC/*/x64/Microsoft.VC*/vcruntime140.dll")
        if(_vcruntime_dlls)
            list(GET _vcruntime_dlls 0 _first_vcruntime)
            get_filename_component(_vcredist_dir "${_first_vcruntime}" DIRECTORY)
        else()
            set(_vcredist_dir "")
        endif()
        # Find cuDNN (installed separately from CUDA Toolkit)
        file(GLOB_RECURSE _cudnn_candidates
            "C:/Program Files/NVIDIA/CUDNN/*/cudnn64_*.dll"
            "$ENV{ProgramFiles}/NVIDIA/CUDNN/*/cudnn64_*.dll")
        if(_cudnn_candidates)
            list(GET _cudnn_candidates 0 CUDNN_DLL)
            message(STATUS "Found cuDNN: ${CUDNN_DLL}")
            set(_cudnn_copy
                COMMAND ${CMAKE_COMMAND} -E echo "=== Step 2.5: Copying cuDNN ==="
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CUDNN_DLL}" $<TARGET_FILE_DIR:${PROJECT_NAME}>)
        else()
            set(CUDNN_DLL "")
            set(_cudnn_copy)
            message(WARNING "cuDNN not found — opencv_dnn4.dll may fail to load")
        endif()

        add_custom_target(deploy-msvc
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 0: Clean nested bin dirs + logs ==="
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/bin
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/logs
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 1: Deploying Qt runtime (MSVC) ==="
            COMMAND ${CMAKE_COMMAND} -E env "VCINSTALLDIR=${_vc_dir}"
                ${WINDEPLOYQT_MSVC_EXECUTABLE}
                --dir $<TARGET_FILE_DIR:${PROJECT_NAME}>
                $<TARGET_FILE:${PROJECT_NAME}>
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 2: Copying CUDA runtime DLLs (NPPI + cuBLAS + cuFFT) ==="
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CUDAToolkit_BIN_DIR}/x64/nppc64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/nppial64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/nppicc64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/nppidei64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/nppif64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/nppig64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/nppim64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/nppist64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/nppisu64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/nppitc64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/npps64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/cublas64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/cublasLt64_13.dll"
                "${CUDAToolkit_BIN_DIR}/x64/cufft64_12.dll"
                "${CUDAToolkit_BIN_DIR}/x64/cudart64_13.dll"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            ${_cudnn_copy}
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 3: Copying MSVC redist DLLs ==="
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_vcredist_dir}/vcruntime140.dll"
                "${_vcredist_dir}/vcruntime140_1.dll"
                "${_vcredist_dir}/msvcp140.dll"
                "${_vcredist_dir}/msvcp140_1.dll"
                "${_vcredist_dir}/msvcp140_2.dll"
                "${_vcredist_dir}/concrt140.dll"
                "${_vcredist_dir}/msvcp140_atomic_wait.dll"
                "${_vcredist_dir}/msvcp140_codecvt_ids.dll"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 4: Clean up unnecessary files ==="
            COMMAND ${CMAKE_COMMAND} -E rm -f
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/opengl32sw.dll
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/D3Dcompiler_47.dll
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 5: Remove unused translations ==="
            COMMAND ${CMAKE_COMMAND} -E rm -f
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ar.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_bg.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ca.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_cs.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_da.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_de.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_en.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_es.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_fa.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_fi.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_fr.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_gd.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_he.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_hr.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_hu.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_it.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ja.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ka.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ko.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_lg.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_lv.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_nl.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_nn.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_pl.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_pt_BR.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_ru.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_sk.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_sv.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_tr.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_uk.qm
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/qt_zh_TW.qm
            COMMAND ${CMAKE_COMMAND} -E echo "=== Step 6: Deploy (MSVC) complete ==="
            COMMENT "Deploy: Qt MSVC runtime"
            DEPENDS ${PROJECT_NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        message(STATUS "Found windeployqt (MSVC): ${WINDEPLOYQT_MSVC_EXECUTABLE}")
    else()
        message(WARNING "windeployqt (MSVC) not found. deploy-msvc target will not be available.")
    endif()
endif()

# ============================================================
# Packaging — CPack + NSIS
# ============================================================
# Usage:
#   1. cmake --build . --target deploy   (or just build)
#   2. cmake --build . --target package  (or: cpack -G NSIS)
#
# Requires: NSIS installed (https://nsis.sourceforge.io)

set(CPACK_PACKAGE_NAME             "ArmSightStitch")
set(CPACK_PACKAGE_VERSION          ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR           "ArmSight")
set(CPACK_PACKAGE_DESCRIPTION      "明场显微成像验证组件")
set(CPACK_PACKAGE_HOMEPAGE_URL     "https://github.com/armsight/armsightstitch")
set(CPACK_PACKAGE_CONTACT          "support@armsight.com")

set(CPACK_GENERATOR                "NSIS")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ArmSightStitch")
set(CPACK_NSIS_DISPLAY_NAME        "ArmSightStitch ${PROJECT_VERSION}")
set(CPACK_NSIS_INSTALL_ROOT        "$PROGRAMFILES64")
set(CPACK_NSIS_PACKAGE_NAME        "ArmSightStitch")
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$DESKTOP\\\\ArmSightStitch.lnk' '$INSTDIR\\\\bin\\\\ArmSightStitch.exe'")
set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$DESKTOP\\\\ArmSightStitch.lnk'")
set(CPACK_NSIS_MENU_LINKS
    "bin/ArmSightStitch.exe" "ArmSightStitch")
set(CPACK_NSIS_URL_INFO_ABOUT      "https://github.com/armsight/armsightstitch")
set(CPACK_NSIS_CONTACT             "support@armsight.com")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH         OFF)
set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
set(CPACK_NSIS_INSTALLED_ICON_NAME "ArmSightStitch.exe")
set(CPACK_NSIS_MUI_ICON            "${CMAKE_CURRENT_SOURCE_DIR}/resources/logo.ico")
set(CPACK_NSIS_MUI_UNIICON         "${CMAKE_CURRENT_SOURCE_DIR}/resources/logo.ico")
set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/R.bmp")
set(CPACK_NSIS_MUI_UNWELCOMEFINISHPAGE_BITMAP
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/R.bmp")
set(CPACK_RESOURCE_FILE_LICENSE    "${CMAKE_CURRENT_SOURCE_DIR}/resources/License.txt")

# Include entire deploy output (exclude transient logs to avoid locked-file errors)
install(DIRECTORY ${OUTPUT_BIN_DIR}/
        DESTINATION bin
        USE_SOURCE_PERMISSIONS
        PATTERN "logs" EXCLUDE
        PATTERN "bin" EXCLUDE)

include(CPack)
