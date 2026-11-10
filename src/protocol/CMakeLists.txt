# gs_protocol: GameScrcpy wire protocol v1 (protocol/SPEC.md). Pure C++, no Qt, no platform headers.

add_library(gs_protocol STATIC
    include/gs/protocol/Codec.h
    include/gs/protocol/Constants.h
    include/gs/protocol/Datagram.h
    include/gs/protocol/DirectMedia.h
    include/gs/protocol/Enums.h
    include/gs/protocol/Errors.h
    include/gs/protocol/Messages.h
    include/gs/protocol/ReedSolomon.h
    include/gs/protocol/Reliable.h
    src/Codec.cpp
    src/Datagram.cpp
    src/DirectMedia.cpp
    src/Errors.cpp
    src/ReedSolomon.cpp
    src/Reliable.cpp
    src/Utf8.cpp
)
add_library(gs::protocol ALIAS gs_protocol)

target_include_directories(gs_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_protocol PUBLIC gs::base)
gs_apply_compiler_policy(gs_protocol)
