# gs_platform: Windows integration below every other module (sockets, CNG cryptography, files).
# Public headers stay free of Windows headers.

add_library(gs_platform STATIC
    include/gs/platform/Accessibility.h
    include/gs/platform/AtomicFile.h
    include/gs/platform/CrashHandler.h
    include/gs/platform/Credentials.h
    include/gs/platform/Crypto.h
    include/gs/platform/Cursor.h
    include/gs/platform/Foreground.h
    include/gs/platform/InputCapture.h
    include/gs/platform/KeyboardLayout.h
    include/gs/platform/RunningMarker.h
    include/gs/platform/Speech.h
    include/gs/platform/TcpStream.h
    include/gs/platform/TextReader.h
    include/gs/platform/Thread.h
    include/gs/platform/TrayNotice.h
    include/gs/platform/UdpSocket.h
    include/gs/platform/Usb.h
    src/Accessibility.cpp
    src/AtomicFile.cpp
    src/CrashHandler.cpp
    src/Credentials.cpp
    src/Crypto.cpp
    src/Cursor.cpp
    src/Foreground.cpp
    src/InputCapture.cpp
    src/KeyboardLayout.cpp
    src/RunningMarker.cpp
    src/Speech.cpp
    src/TcpStream.cpp
    src/TextReader.cpp
    src/Thread.cpp
    src/TrayNotice.cpp
    src/UdpSocket.cpp
    src/Usb.cpp
)
add_library(gs::platform ALIAS gs_platform)

target_include_directories(gs_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(gs_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(gs_platform PUBLIC gs::base
    PRIVATE SDL3::SDL3 libusb::libusb ws2_32 bcrypt crypt32 advapi32 user32 shell32 dbghelp setupapi cfgmgr32 ole32
            sapi windowsapp)
gs_apply_compiler_policy(gs_platform)
