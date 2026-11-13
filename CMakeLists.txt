cmake_minimum_required(VERSION 3.28)

project(GameScrcpy
    VERSION 2.1.1
    DESCRIPTION "Android screen mirroring, input mapping and automation"
    HOMEPAGE_URL "https://github.com/NuoRan/GameScrcpy"
    LANGUAGES C CXX
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "GameScrcpy v2 targets Windows 10/11 only for now (ADR-0010).")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(GS_BUILD_APP "Build the Qt Quick application (requires Qt 6.10)" ON)
option(GS_BUILD_TESTS "Build the unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

include(GsCompilerPolicy)

if(GS_BUILD_TESTS)
    enable_testing()
endif()

if(GS_BUILD_APP)
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickTemplates2 LinguistTools CorePrivate GuiPrivate
                 QuickPrivate)
    qt_standard_project_setup(REQUIRES 6.10)

    set(QT_QML_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/qml")
endif()

add_subdirectory(third_party)
add_subdirectory(src)

if(GS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
