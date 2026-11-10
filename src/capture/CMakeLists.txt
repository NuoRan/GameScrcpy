add_library(gs_capture STATIC
    include/gs/capture/AudioCapture.h
    include/gs/capture/AudioPairing.h
    include/gs/capture/CaptureDevices.h
    include/gs/capture/CaptureFormat.h
    include/gs/capture/CaptureSource.h
    include/gs/capture/CropDetector.h
    include/gs/capture/Encoders.h
    include/gs/capture/FrameBuilder.h
    include/gs/capture/MediaFoundationSource.h
    src/AudioCapture.cpp
    src/AudioPairing.cpp
    src/CaptureDevices.cpp
    src/CaptureFormat.cpp
    src/CropDetector.cpp
    src/Encoders.cpp
    src/FrameBuilder.cpp
    src/MediaFoundation.cpp
    src/MediaFoundation.h
    src/MediaFoundationSource.cpp
)
add_library(gs::capture ALIAS gs_capture)

target_include_directories(gs_capture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_capture
    PUBLIC gs::base gs::protocol gs::media
    PRIVATE gs::platform FFmpeg::swresample d3d11 mfplat mfreadwrite mf mfuuid ole32 cfgmgr32 avrt
)
gs_apply_compiler_policy(gs_capture)
