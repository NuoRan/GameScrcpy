include_guard(GLOBAL)

option(GS_WARNINGS_AS_ERRORS "Treat warnings in first-party code as errors" ON)
option(GS_ENABLE_ASAN "Build with AddressSanitizer" OFF)

if(GS_ENABLE_ASAN)
    if(NOT MSVC)
        message(FATAL_ERROR "GS_ENABLE_ASAN is only wired up for MSVC")
    endif()
    add_compile_options(/fsanitize=address)
    add_link_options(/INCREMENTAL:NO)
    foreach(lang IN ITEMS C CXX)
        string(REPLACE "/RTC1" "" CMAKE_${lang}_FLAGS_DEBUG "${CMAKE_${lang}_FLAGS_DEBUG}")
    endforeach()
endif()

function(gs_apply_compiler_policy target)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_features(${target} INTERFACE cxx_std_20)
        return()
    endif()

    target_compile_features(${target} PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /Zc:inline
            /utf-8
            /external:W0
            /diagnostics:caret
            /wd4702
        )
        if(GS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        target_compile_definitions(${target} PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            UNICODE
            _UNICODE
        )
    endif()
endfunction()
