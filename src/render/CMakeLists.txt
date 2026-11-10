# gs_render: the shared Direct3D 11 device and the video item of Qt Quick (PLAN 4.5).

qt_add_library(gs_render STATIC)
qt_add_qml_module(gs_render
    URI GameScrcpy.Render
    VERSION 1.0
    SOURCES
        include/gs/render/FrameSlot.h
        include/gs/render/GraphicsDevice.h
        include/gs/render/VideoFilter.h
        include/gs/render/VideoItem.h
        src/FilterChain.cpp
        src/FilterChain.h
        src/FrameSlot.cpp
        src/GraphicsDevice.cpp
        src/VideoItem.cpp
        src/VideoRenderNode.cpp
        src/VideoRenderNode.h
)
add_library(gs::render ALIAS gs_render)

target_include_directories(gs_render
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/gs/render"
)
target_link_libraries(gs_render
    PUBLIC gs::media Qt6::Quick
    PRIVATE gs::ext Qt6::GuiPrivate d3d11 dxgi d3dcompiler
)
gs_apply_compiler_policy(gs_render)

find_program(GS_FXC fxc REQUIRED DOC "HLSL compiler of the Windows SDK")
set(gs_render_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${gs_render_shader_dir}")
function(gs_render_shader name entry profile variable)
    set(output "${gs_render_shader_dir}/${name}.h")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${GS_FXC}" /nologo /O3 /WX /T ${profile} /E ${entry} ${ARGN} /Vn ${variable} /Fh "${output}"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/video.hlsl"
        DEPENDS shaders/video.hlsl
        COMMENT "Compiling ${name}"
        VERBATIM
    )
    target_sources(gs_render PRIVATE "${output}")
endfunction()
gs_render_shader(video_vs vsMain vs_5_0 kVideoVertexShader)
gs_render_shader(video_vs_full vsFullScreen vs_5_0 kVideoVertexShaderFullScreen)
gs_render_shader(video_ps_nv12 psMain ps_5_0 kVideoPixelShaderNv12 /DNV12=1)
gs_render_shader(video_ps_planar psMain ps_5_0 kVideoPixelShaderPlanar /DPLANAR=1)
gs_render_shader(video_ps_copy psCopy ps_5_0 kVideoPixelShaderCopy /DCOPY=1)
target_include_directories(gs_render PRIVATE "${gs_render_shader_dir}")
