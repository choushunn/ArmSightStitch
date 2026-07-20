# GetVersion.cmake — Derive project version from git tags
# Usage: include(GetVersion) → sets PROJECT_VERSION, PROJECT_VERSION_MAJOR/MINOR/PATCH/TWEAK
# Format: v<MAJOR>.<MINOR>.<PATCH> → MAJOR.MINOR.PATCH.TWEAK
#   v2.0.1                    → 2.0.1.0
#   v2.0.1-3-gabc1234         → 2.0.1.3   (3 commits ahead of tag)
#   v2.0.1-dirty              → 2.0.1.999 (uncommitted changes)

find_package(Git QUIET)

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_RESULT
    )
endif()

if(NOT GIT_RESULT EQUAL 0 OR GIT_DESCRIBE STREQUAL "")
    # Fallback: read from CMakeLists.txt project() call
    set(PROJECT_VERSION "${PROJECT_VERSION}")
    set(VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(VERSION_PATCH "${PROJECT_VERSION_PATCH}")
    set(VERSION_TWEAK "0")
    message(STATUS "Version: ${PROJECT_VERSION} (fallback, no git)")
    return()
endif()

# Parse git describe output: v2.0.1[-N-gHASH][-dirty]
string(REGEX MATCH "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9]+))?(-g[a-f0-9]+)?(-dirty)?$" _ "${GIT_DESCRIBE}")
if(NOT CMAKE_MATCH_1)
    message(WARNING "Cannot parse git describe '${GIT_DESCRIBE}', using fallback")
    return()
endif()

set(VERSION_MAJOR ${CMAKE_MATCH_1})
set(VERSION_MINOR ${CMAKE_MATCH_2})
set(VERSION_PATCH ${CMAKE_MATCH_3})
set(VERSION_TWEAK ${CMAKE_MATCH_5})

if(VERSION_TWEAK STREQUAL "")
    set(VERSION_TWEAK 0)
endif()

if(CMAKE_MATCH_7 STREQUAL "-dirty")
    set(VERSION_TWEAK 999)  # dirty marker
    set(VERSION_DIRTY "-dirty")
else()
    set(VERSION_DIRTY "")
endif()

set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_TWEAK}")
set(PROJECT_VERSION_MAJOR "${VERSION_MAJOR}")
set(PROJECT_VERSION_MINOR "${VERSION_MINOR}")
set(PROJECT_VERSION_PATCH "${VERSION_PATCH}")
set(PROJECT_VERSION_TWEAK "${VERSION_TWEAK}")

message(STATUS "Version: ${PROJECT_VERSION}${VERSION_DIRTY} (from git: ${GIT_DESCRIBE})")
