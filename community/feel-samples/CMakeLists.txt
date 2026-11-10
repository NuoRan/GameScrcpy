cmake_minimum_required(VERSION 3.28)

project(GameScrcpyFeelSamples VERSION 1.0.0 LANGUAGES CXX)

if(NOT GS_SDK_INCLUDE)
    set(GS_SDK_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../../sdk/include")
endif()
if(NOT EXISTS "${GS_SDK_INCLUDE}/gamescrcpy_ext.h")
    message(FATAL_ERROR "gamescrcpy_ext.h is not in ${GS_SDK_INCLUDE}: pass -DGS_SDK_INCLUDE=<GameScrcpy>/sdk/include")
endif()

set(GS_FEEL_SAMPLES_OUTPUT "${CMAKE_BINARY_DIR}/community/feel-samples")

add_library(gs_feel_samples SHARED
    src/feel_samples.cpp
    src/params.cpp
    src/params.h
)
set_target_properties(gs_feel_samples PROPERTIES
    OUTPUT_NAME feel-samples
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${GS_FEEL_SAMPLES_OUTPUT}$<0:>"
    LIBRARY_OUTPUT_DIRECTORY "${GS_FEEL_SAMPLES_OUTPUT}$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib$<0:>"
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols$<0:>"
)
target_compile_features(gs_feel_samples PRIVATE cxx_std_20)
target_include_directories(gs_feel_samples PRIVATE "${GS_SDK_INCLUDE}")
if(COMMAND gs_apply_compiler_policy)
    gs_apply_compiler_policy(gs_feel_samples)
elseif(MSVC)
    target_compile_options(gs_feel_samples PRIVATE /W4 /permissive- /utf-8)
endif()
configure_file(manifest.json "${GS_FEEL_SAMPLES_OUTPUT}/manifest.json" COPYONLY)
