cmake_minimum_required(VERSION 3.16)

project(ArmSightStitch VERSION 2.0.0 LANGUAGES CXX C)

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

# OpenCV
find_package(OpenCV REQUIRED)
message(STATUS "Found OpenCV: ${OpenCV_VERSION} at ${OpenCV_DIR}")

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
    # Application entry
    app/main.cpp

    # UI layer
    ui/MainWindow.cpp

    # Core - Arm control
    core/arm/ModbusArmController.cpp
    core/arm/SMovementController.cpp

    # Core - Camera
    core/camera/CameraHandler.cpp

    # Core - Detector
    core/detector/YoloDetector.cpp

    # Core - Stitching
    core/stitch/ImageStitcher.cpp

    # Infrastructure - Config
    infra/config/ConfigManager.cpp

    # Infrastructure - Workflow
    infra/workflow/WorkflowManager.cpp

    # Infrastructure - Logging (header-only via spdlog)
)

set(RESOURCES
    res/qt/resources.qrc
)

# ============================================================
# Include directories - per target
# ============================================================

# Create output directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# ============================================================
# Executable
# ============================================================

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${RESOURCES}
)

# Target-specific include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/core/arm
    ${CMAKE_CURRENT_SOURCE_DIR}/core/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/core/detector
    ${CMAKE_CURRENT_SOURCE_DIR}/core/stitch
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/config
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/workflow
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/log
)

if(HAS_LIBMODBUS)
    target_sources(${PROJECT_NAME} PRIVATE ${LIBMODBUS_SOURCES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE DLLBUILD)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

# Compile definitions for optional modules
if(NOT HAS_NCNN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARM_SIGHT_STITCH_NO_DETECTOR)
endif()
if(NOT HAS_TOUPCAM)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARM_SIGHT_STITCH_NO_CAMERA)
endif()
if(NOT HAS_LIBMODBUS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARM_SIGHT_STITCH_NO_ARM)
endif()

# ============================================================
# Link libraries
# ============================================================

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

# ============================================================
# Post-build: copy model files and DLLs
# ============================================================

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/res/models $<TARGET_FILE_DIR:${PROJECT_NAME}>/models
)

if(HAS_TOUPCAM)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TOUPCAM_DLL}" $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying toupcam.dll to output directory"
    )
endif()

# Output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
)

# ============================================================
# Deploy target: windeployqt
# ============================================================

find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt windeployqt.exe
    PATHS "C:/Programs/Qt/6.10.1/mingw_64/bin"
    NO_DEFAULT_PATH
)
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_target(deploy
        COMMAND ${WINDEPLOYQT_EXECUTABLE}
            --dir $<TARGET_FILE_DIR:${PROJECT_NAME}>
            $<TARGET_FILE:${PROJECT_NAME}>
        COMMENT "Deploying Qt dependencies to $<TARGET_FILE_DIR:${PROJECT_NAME}>..."
        DEPENDS ${PROJECT_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
else()
    message(WARNING "windeployqt not found. Deploy target will not be available.")
endif()
