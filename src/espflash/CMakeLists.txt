add_library(gs_espflash STATIC
    include/gs/espflash/Chips.h
    include/gs/espflash/Firmware.h
    include/gs/espflash/Flasher.h
    include/gs/espflash/Loader.h
    include/gs/espflash/Md5.h
    include/gs/espflash/Slip.h
    src/Chips.cpp
    src/Firmware.cpp
    src/Flasher.cpp
    src/Loader.cpp
    src/Md5.cpp
    src/Slip.cpp
)
add_library(gs::espflash ALIAS gs_espflash)

target_include_directories(gs_espflash PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(gs_espflash PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(gs_espflash
    PUBLIC gs::base gs::esp32
    PRIVATE gs::miniz gs::nlohmann_json gs::platform bcrypt)
gs_apply_compiler_policy(gs_espflash)
