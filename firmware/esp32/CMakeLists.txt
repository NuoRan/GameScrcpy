cmake_minimum_required(VERSION 3.22)

file(READ "${CMAKE_CURRENT_LIST_DIR}/version.txt" GS_FIRMWARE_VERSION)
string(STRIP "${GS_FIRMWARE_VERSION}" GS_FIRMWARE_VERSION)
set(PROJECT_VER "${GS_FIRMWARE_VERSION}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gs-esp32)
