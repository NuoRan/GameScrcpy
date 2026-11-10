# gs_session: device sessions (PLAN 4.3, ADR-0044). Brings the server up through a host, then
# handshakes, opens the channels, keeps the connection alive, resumes it and ends it.

add_library(gs_session STATIC
    include/gs/session/AdbServerHost.h
    include/gs/session/ServerHost.h
    include/gs/session/Session.h
    include/gs/session/SessionState.h
    include/gs/session/Transport.h
    src/AdbServerHost.cpp
    src/DirectHandshake.cpp
    src/DirectHandshake.h
    src/DirectTransport.cpp
    src/DirectTransport.h
    src/FallbackTransport.h
    src/Handshake.cpp
    src/Handshake.h
    src/KeyFrameGate.h
    src/ReceiverStats.cpp
    src/ReceiverStats.h
    src/ReliableStream.cpp
    src/ReliableStream.h
    src/Session.cpp
    src/SessionState.cpp
    src/TunnelTransport.cpp
    src/TunnelTransport.h
)
add_library(gs::session ALIAS gs_session)

target_include_directories(gs_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_session
    PUBLIC gs::adb gs::base gs::protocol gs::platform
)
gs_apply_compiler_policy(gs_session)
