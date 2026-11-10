add_library(gs_esp32 STATIC
    include/gs/esp32/Boards.h
    include/gs/esp32/Contacts.h
    include/gs/esp32/Diagnosis.h
    include/gs/esp32/Discovery.h
    include/gs/esp32/Keys.h
    include/gs/esp32/Link.h
    include/gs/esp32/Port.h
    include/gs/esp32/TouchMethod.h
    include/gs/esp32/UdpPort.h
    "${PROJECT_SOURCE_DIR}/protocol/esp32/gs_esp32_protocol.h"
    src/Boards.cpp
    src/Contacts.cpp
    src/Diagnosis.cpp
    src/Discovery.cpp
    src/Keys.cpp
    src/Link.cpp
    src/SerialPort.cpp
    src/TouchMethod.cpp
    src/UdpPort.cpp
)
add_library(gs::esp32 ALIAS gs_esp32)

target_include_directories(gs_esp32 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/protocol/esp32"
)
target_compile_definitions(gs_esp32 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(gs_esp32
    PUBLIC gs::base
    PRIVATE gs::nlohmann_json gs::platform setupapi cfgmgr32 advapi32 dnsapi ws2_32)
gs_apply_compiler_policy(gs_esp32)

add_subdirectory("${PROJECT_SOURCE_DIR}/examples/extensions/esp32" "${PROJECT_BINARY_DIR}/examples/esp32")
