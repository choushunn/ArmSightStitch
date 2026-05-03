cmake_minimum_required(VERSION 3.16)

project(ArmSightStitch VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ============================================================
# Qt - try system-installed Qt6 first, then vcpkg Qt, then Qt5
# ============================================================
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

# ============================================================
# OpenCV - via vcpkg (opencv4)
# ============================================================
find_package(OpenCV REQUIRED)
message(STATUS "Found OpenCV: ${OpenCV_VERSION} at ${OpenCV_DIR}")

# ============================================================
# Third-party: libmodbus (bundled source in third_party/)
# ============================================================
set(LIBMODBUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/modbus/libmodbus-master")
if(EXISTS "${LIBMODBUS_DIR}/src/modbus.c")
    include_directories("${LIBMODBUS_DIR}/src")
    set(LIBMODBUS_SOURCES
        "${LIBMODBUS_DIR}/src/modbus.c"
        "${LIBMODBUS_DIR}/src/modbus-tcp.c"
        "${LIBMODBUS_DIR}/src/modbus-rtu.c"
        "${LIBMODBUS_DIR}/src/modbus-data.c"
    )
    set_source_files_properties(${LIBMODBUS_SOURCES} PROPERTIES LANGUAGE C)
    message(STATUS "Found libmodbus: ${LIBMODBUS_DIR}")
else()
    message(WARNING "libmodbus not found at ${LIBMODBUS_DIR}. "
                    "Arm communication will be disabled. "
                    "Download libmodbus to third_party/modbus/")
    set(LIBMODBUS_SOURCES "")
endif()

# ============================================================
# Third-party: NCNN - via vcpkg
# ============================================================
find_package(ncnn QUIET)
if(ncnn_FOUND)
    message(STATUS "Found NCNN via vcpkg: ${ncnn_DIR}")
    set(HAS_NCNN TRUE)
else()
    message(WARNING "NCNN not found via vcpkg. "
                    "Detection module will be disabled. "
                    "Add ncnn to vcpkg.json")
    set(HAS_NCNN FALSE)
endif()

# ============================================================
# Third-party: ToupCam SDK (bundled in third_party/)
# ============================================================
set(TOUPCAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/toupcam/toupcamsdk.20250623")
if(EXISTS "${TOUPCAM_DIR}/inc/toupcam.h")
    include_directories("${TOUPCAM_DIR}/inc")
    link_directories("${TOUPCAM_DIR}/win/x64")
    message(STATUS "Found ToupCam SDK: ${TOUPCAM_DIR}")
    set(HAS_TOUPCAM TRUE)
else()
    message(WARNING "ToupCam SDK not found at ${TOUPCAM_DIR}. "
                    "Camera module will be disabled. "
                    "Download ToupCam SDK to third_party/toupcam/")
    set(HAS_TOUPCAM FALSE)
endif()

# ============================================================
# Include directories - layered architecture
# ============================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/core/arm
    ${CMAKE_CURRENT_SOURCE_DIR}/core/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/core/detector
    ${CMAKE_CURRENT_SOURCE_DIR}/core/stitch
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/config
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/workflow
)

# ============================================================
# Source files - scan layered directories
# ============================================================
file(GLOB_RECURSE SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/*.cpp
)

file(GLOB_RECURSE HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/*.h
)

file(GLOB_RECURSE RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/res/qt/*.qrc
)

# ============================================================
# Create output directory
# ============================================================
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# ============================================================
# Executable
# ============================================================
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Add libmodbus source files if available
if(LIBMODBUS_SOURCES)
    target_sources(${PROJECT_NAME} PRIVATE ${LIBMODBUS_SOURCES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE DLLBUILD)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

# ============================================================
# Compile definitions for optional modules
# ============================================================
if(NOT HAS_NCNN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARM_SIGHT_STITCH_NO_DETECTOR)
endif()
if(NOT HAS_TOUPCAM)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARM_SIGHT_STITCH_NO_CAMERA)
endif()
if(NOT LIBMODBUS_SOURCES)
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

if(HAS_TOUPCAM AND EXISTS "${TOUPCAM_DIR}/win/x64/toupcam.dll")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${TOUPCAM_DIR}/win/x64/toupcam.dll" $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy
        "${TOUPCAM_DIR}/win/x64/toupcam.lib" $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

# Output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
)
