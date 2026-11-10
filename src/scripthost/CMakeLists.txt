add_library(gs_scripthost_wire STATIC
    src/Pipe.cpp
    src/Pipe.h
    src/Wire.cpp
    src/Wire.h
)
target_include_directories(gs_scripthost_wire PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(gs_scripthost_wire PUBLIC gs::script gs::base)
gs_apply_compiler_policy(gs_scripthost_wire)

add_library(gs_scripthost STATIC
    include/gs/scripthost/RemoteLogicHost.h
    src/RemoteLogicHost.cpp
    src/Sandbox.cpp
    src/Sandbox.h
)
add_library(gs::scripthost ALIAS gs_scripthost)

target_include_directories(gs_scripthost PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_scripthost PUBLIC gs::script gs::base PRIVATE gs_scripthost_wire gs::ext userenv advapi32)
gs_apply_compiler_policy(gs_scripthost)

add_executable(gs_scripthost_program main.cpp src/Server.cpp src/Server.h)
set_target_properties(gs_scripthost_program PROPERTIES OUTPUT_NAME gs-scripthost)
target_include_directories(gs_scripthost_program PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(gs_scripthost_program PRIVATE gs_scripthost_wire gs::script ws2_32)
gs_apply_compiler_policy(gs_scripthost_program)
