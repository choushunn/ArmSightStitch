find_package(GTest REQUIRED)

# ── Test runner ──
add_executable(armsightstitch_tests
    test_main.cpp
    test_ConfigManager.cpp
    test_StitchAlgorithm.cpp
    # Shared sources needed by tests
    ${CMAKE_SOURCE_DIR}/infra/config/ConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/core/stitch/GridStitchAlgorithm.cpp
    ${CMAKE_SOURCE_DIR}/core/stitch/SeamFeatherStitchAlgorithm.cpp
    ${CMAKE_SOURCE_DIR}/infra/report/EdgeCrop.cpp
)

target_include_directories(armsightstitch_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/infra/config
    ${CMAKE_SOURCE_DIR}/infra/log
    ${CMAKE_SOURCE_DIR}/core/stitch
    ${CMAKE_SOURCE_DIR}/core/detector
    ${CMAKE_SOURCE_DIR}/core/arm
    ${CMAKE_SOURCE_DIR}/core/camera
)

target_link_libraries(armsightstitch_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    ${OpenCV_LIBS}
    spdlog::spdlog
)

# Qt is needed for ConfigManager test (QString in paths)
if(Qt6_FOUND)
    target_link_libraries(armsightstitch_tests PRIVATE Qt6::Core Qt6::Concurrent Qt6::Network)
elseif(Qt5_FOUND)
    target_link_libraries(armsightstitch_tests PRIVATE Qt5::Core Qt5::Concurrent Qt5::Network)
endif()

# Match main project MSVC compile definitions to avoid WinSock2 conflicts
target_compile_definitions(armsightstitch_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# ── Test registration ──
# gtest_discover_tests requires the exe to run at build time (needs Qt DLLs on PATH).
# Use basic add_test instead; run with ctest --test-dir build/vcpkg-msvc -C Release
# after setting PATH to include Qt bin dir and OpenCV bin dir.
add_test(NAME armsightstitch_tests
    COMMAND armsightstitch_tests
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:armsightstitch_tests>")
