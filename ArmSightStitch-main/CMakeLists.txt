cmake_minimum_required(VERSION 3.16)

project(ArmLiteCPlusPlus VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6
set(Qt6_DIR C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6)
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Network)

# OpenCV
set(OpenCV_DIR C:/opencv/opencv/build)
find_package(OpenCV REQUIRED)

# NCNN
set(NCNN_DIR C:/Users/SP/Desktop/ArmSightStitch-main/third_party/ncnn/ncnn-master)
include_directories(${NCNN_DIR}/src)
include_directories(${NCNN_DIR}/build/src) # For generated platform.h
link_directories(${NCNN_DIR}/build/src/Release)

# libmodbus (include source directly)
set(LIBMODBUS_DIR C:/Users/SP/Desktop/ArmSightStitch-main/third_party/modbus/libmodbus-master)
include_directories(${LIBMODBUS_DIR}/src)

# Add libmodbus source files to project
set(LIBMODBUS_SOURCES
    ${LIBMODBUS_DIR}/src/modbus.c
    ${LIBMODBUS_DIR}/src/modbus-tcp.c
    ${LIBMODBUS_DIR}/src/modbus-rtu.c
    ${LIBMODBUS_DIR}/src/modbus-data.c
)

# Set the language of libmodbus source files to C
set_source_files_properties(${LIBMODBUS_SOURCES} PROPERTIES LANGUAGE C)



# ToupCam SDK
set(TOUPCAM_DIR C:/Users/SP/Desktop/ArmSightStitch-main/third_party/toupcam/toupcamsdk.20250623)
include_directories(${TOUPCAM_DIR}/inc)
link_directories(${TOUPCAM_DIR}/win/x64)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/detector
    ${CMAKE_CURRENT_SOURCE_DIR}/include/arm
    ${CMAKE_CURRENT_SOURCE_DIR}/include/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/include/stitch
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)

# Source files
file(GLOB_RECURSE SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.ui
)

# Header files
file(GLOB_RECURSE HEADERS 
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

# Resource files
set(RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources/resources.qrc)

# Create directory structure if it doesn't exist
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/models)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Executable
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Add libmodbus source files using target_sources to ensure they are compiled as C source files
target_sources(${PROJECT_NAME} PRIVATE
    ${LIBMODBUS_SOURCES}
)

# Define DLLBUILD for libmodbus source files to ensure functions are exported correctly
target_compile_definitions(${PROJECT_NAME} PRIVATE DLLBUILD)

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    ${OpenCV_LIBS}
    ncnn
    toupcam
    ws2_32 # For libmodbus TCP support
)

# Copy model files to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/models $<TARGET_FILE_DIR:${PROJECT_NAME}>/models
)

# Copy ToupCam DLLs to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${TOUPCAM_DIR}/win/x64/toupcam.dll $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy
    ${TOUPCAM_DIR}/win/x64/toupcam.lib $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
)
