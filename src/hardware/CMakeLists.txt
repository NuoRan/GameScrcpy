add_library(gs_hardware STATIC
    include/gs/hardware/Backend.h
    include/gs/hardware/Devices.h
    include/gs/hardware/FakeBackend.h
    src/BleLink.cpp
    src/Devices.cpp
    src/FakeBackend.cpp
    src/HidLink.cpp
    src/Links.h
    src/SerialLink.cpp
    src/Win32.cpp
    src/WindowsBackend.cpp
    src/Worker.cpp
    src/Worker.h
)
add_library(gs::hardware ALIAS gs_hardware)

target_include_directories(gs_hardware PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_hardware
    PUBLIC gs::base
    PRIVATE setupapi cfgmgr32 hid ole32 windowsapp)
gs_apply_compiler_policy(gs_hardware)
if(MSVC)
    target_compile_options(gs_hardware PRIVATE /bigobj)
endif()
