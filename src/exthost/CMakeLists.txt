add_library(gs_exthost STATIC
    include/gs/exthost/Hosted.h
    include/gs/exthost/InProcess.h
    include/gs/exthost/Media.h
    include/gs/exthost/Module.h
    include/gs/exthost/Server.h
    src/Channel.cpp
    src/Channel.h
    src/Container.cpp
    src/Container.h
    src/Hosted.cpp
    src/ImageSection.cpp
    src/ImageSection.h
    src/InProcess.cpp
    src/MediaRing.cpp
    src/MediaRing.h
    src/Module.cpp
    src/Server.cpp
    src/Store.cpp
    src/Store.h
    src/TapSection.cpp
    src/TapSection.h
    src/Wire.cpp
    src/Wire.h
)
add_library(gs::exthost ALIAS gs_exthost)

target_include_directories(gs_exthost PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/sdk/include")
target_link_libraries(gs_exthost PUBLIC gs::base PRIVATE gs::nlohmann_json gs::platform gs::ext userenv advapi32)
gs_apply_compiler_policy(gs_exthost)

add_executable(gs_exthost_program main.cpp)
set_target_properties(gs_exthost_program PROPERTIES OUTPUT_NAME gs-exthost)
target_link_libraries(gs_exthost_program PRIVATE gs::exthost)
gs_apply_compiler_policy(gs_exthost_program)

add_subdirectory("${PROJECT_SOURCE_DIR}/examples/extensions/pc-clock" "${PROJECT_BINARY_DIR}/examples/pc-clock")
