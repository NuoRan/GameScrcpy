add_library(gs_record STATIC
    include/gs/record/MediaFile.h
    include/gs/record/Mp4Export.h
    include/gs/record/Operations.h
    include/gs/record/RecordedAudio.h
    include/gs/record/RecordedVision.h
    include/gs/record/Recorder.h
    include/gs/record/Recording.h
    src/MediaFile.cpp
    src/Mp4Export.cpp
    src/Operations.cpp
    src/Pictures.cpp
    src/Pictures.h
    src/RecordedAudio.cpp
    src/RecordedVision.cpp
    src/Recorder.cpp
    src/Recording.cpp
)
add_library(gs::record ALIAS gs_record)

target_include_directories(gs_record PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_record
    PUBLIC gs::base gs::protocol gs::script gs::media
    PRIVATE gs::platform gs::vision FFmpeg::avformat FFmpeg::avcodec windowscodecs ole32
)
gs_apply_compiler_policy(gs_record)
