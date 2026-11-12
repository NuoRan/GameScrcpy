# gs_base: standard-library-only foundations shared by every module (no Qt, no platform headers).

set(GS_SOURCE_REVISION "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE git_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE git_result
    )
    if(git_result EQUAL 0)
        set(GS_SOURCE_REVISION "${git_revision}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE git_branch
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    foreach(git_name IN ITEMS HEAD ${git_branch})
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --git-path "${git_name}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE git_file
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE git_found
        )
        if(git_found EQUAL 0)
            get_filename_component(git_file "${git_file}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
            if(EXISTS "${git_file}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_file}")
            endif()
        endif()
    endforeach()
endif()

configure_file(src/BuildInfo.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp" @ONLY)

add_library(gs_base STATIC
    include/gs/base/BuildInfo.h
    include/gs/base/ByteOrder.h
    include/gs/base/Clock.h
    include/gs/base/Expected.h
    include/gs/base/InplaceVector.h
    include/gs/base/JsonNesting.h
    include/gs/base/Log.h
    include/gs/base/SpscQueue.h
    include/gs/base/ThreadChecker.h
    src/Log.cpp
    src/ThreadChecker.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp"
)
add_library(gs::base ALIAS gs_base)

target_include_directories(gs_base PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_base PUBLIC gs::tl_expected)
gs_apply_compiler_policy(gs_base)
