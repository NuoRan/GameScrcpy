# gs_media: decoding of the media channels (PLAN 4.4) with FFmpeg and D3D11VA (ADR-0015, ADR-0046),
# and audio playback through WASAPI.

add_library(gs_media STATIC
    include/gs/media/AudioPlayer.h
    include/gs/media/AudioRegulator.h
    include/gs/media/ColorMatrix.h
    include/gs/media/FfmpegLog.h
    include/gs/media/FrameSnapshot.h
    include/gs/media/FrameUploader.h
    include/gs/media/LumaSampler.h
    include/gs/media/Thumbnailer.h
    include/gs/media/VideoDecoder.h
    src/AudioOutput.cpp
    src/AudioOutput.h
    src/AudioPlayer.cpp
    src/AudioRegulator.cpp
    src/ColorMatrix.cpp
    src/FfmpegLog.cpp
    src/FrameSnapshot.cpp
    src/FrameUploader.cpp
    src/LumaSampler.cpp
    src/PlaneReader.cpp
    src/PlaneReader.h
    src/Thumbnailer.cpp
    src/VideoDecoder.cpp
)
add_library(gs::media ALIAS gs_media)

target_include_directories(gs_media PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_media
    PUBLIC gs::base gs::protocol FFmpeg::avutil
    PRIVATE FFmpeg::avcodec FFmpeg::swresample gs::platform d3d11 ole32 avrt
)
gs_apply_compiler_policy(gs_media)

find_program(GS_FXC fxc REQUIRED DOC "HLSL compiler of the Windows SDK")
set(gs_media_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${gs_media_shader_dir}")
function(gs_media_shader name entry profile variable)
    set(output "${gs_media_shader_dir}/${name}.h")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${GS_FXC}" /nologo /O3 /WX /T ${profile} /E ${entry} ${ARGN} /Vn ${variable} /Fh "${output}"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/convert.hlsl"
        DEPENDS shaders/convert.hlsl
        COMMENT "Compiling ${name}"
        VERBATIM
    )
    target_sources(gs_media PRIVATE "${output}")
endfunction()
gs_media_shader(convert_vs vsQuad vs_5_0 kConvertVertexShader)
gs_media_shader(convert_yuy2_luma psLuma ps_5_0 kYuy2LumaShader /DYUY2=1)
gs_media_shader(convert_yuy2_chroma psChroma ps_5_0 kYuy2ChromaShader /DYUY2=1)
gs_media_shader(convert_scale_luma psLuma ps_5_0 kScaleLumaShader)
gs_media_shader(convert_scale_chroma psChroma ps_5_0 kScaleChromaShader)
target_include_directories(gs_media PRIVATE "${gs_media_shader_dir}")
