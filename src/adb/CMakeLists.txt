# gs_adb: adb host protocol client (device tracking, shell v2, sync push, forward/reverse).
# Talks to the adb server over loopback TCP; no adb.exe child processes except start-server.

add_library(gs_adb STATIC
    include/gs/adb/AdbClient.h
    include/gs/adb/DeviceIdentity.h
    include/gs/adb/DeviceList.h
    include/gs/adb/DeviceTracker.h
    include/gs/adb/ForwardList.h
    include/gs/adb/ServerLauncher.h
    include/gs/adb/ShellProtocol.h
    include/gs/adb/WirelessServices.h
    src/AdbClient.cpp
    src/DeviceIdentity.cpp
    src/DeviceList.cpp
    src/DeviceTracker.cpp
    src/ForwardList.cpp
    src/HostProtocol.cpp
    src/HostProtocol.h
    src/ServerLauncher.cpp
    src/ShellProtocol.cpp
    src/WirelessServices.cpp
    src/Wire.h
)
add_library(gs::adb ALIAS gs_adb)

target_include_directories(gs_adb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_adb
    PUBLIC gs::base
    PRIVATE gs::platform
)
gs_apply_compiler_policy(gs_adb)
