# QtScrcpy Client - 重构后的 CMakeLists.txt
# 目录结构:
#   src/app       - 应用入口
#   src/ui        - 用户界面
#   src/control   - 控制模块
#   src/transport - 传输模块 (KCP/TCP/ADB)
#   src/decoder   - 解码模块
#   src/render    - 渲染模块
#   src/common    - 通用模块
#   env/          - 环境依赖 (FFmpeg, ADB)

cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

#
# 项目配置
#
set(QC_PROJECT_NAME "QtScrcpy")
set(QC_PROJECT_VERSION "2.2.1")

project(${QC_PROJECT_NAME} VERSION ${QC_PROJECT_VERSION} LANGUAGES C CXX)
message(STATUS "[${PROJECT_NAME}] Project ${PROJECT_NAME} ${PROJECT_VERSION}")

# 架构检测
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(QC_CPU_ARCH x64)
else()
    set(QC_CPU_ARCH x86)
endif()

# MacOS 架构
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES arm64)
    endif()
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(QC_CPU_ARCH arm64)
    endif()
endif()

message(STATUS "[${PROJECT_NAME}] CPU_ARCH: ${QC_CPU_ARCH}")

#
# 编译器配置
#
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
message(STATUS "[${PROJECT_NAME}] BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# MSVC 配置
if(MSVC)
    add_compile_options(/W3 /WX /wd4566 /utf-8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    add_link_options(/SAFESEH:NO)
endif()

# GCC/Clang 配置
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -pedantic -Werror)
    add_compile_options(-Wno-nested-anon-types -Wno-c++17-extensions -Wno-overloaded-virtual)
endif()

#
# Qt 配置
#
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
set(QT_DESIRED_VERSION ${QT_VERSION_MAJOR})
message(STATUS "[${PROJECT_NAME}] Qt version: ${QT_DESIRED_VERSION}")

set(qt_required_components Widgets Network Multimedia Qml Quick Gui)

if(QT_DESIRED_VERSION MATCHES 6)
    list(APPEND qt_required_components OpenGL OpenGLWidgets)
else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND qt_required_components X11Extras)
    endif()
endif()

find_package(Qt${QT_DESIRED_VERSION} REQUIRED COMPONENTS ${qt_required_components})

set(LINK_LIBS
    Qt${QT_DESIRED_VERSION}::Widgets
    Qt${QT_DESIRED_VERSION}::Network
    Qt${QT_DESIRED_VERSION}::Multimedia
    Qt${QT_DESIRED_VERSION}::Qml
    Qt${QT_DESIRED_VERSION}::Quick
    Qt${QT_DESIRED_VERSION}::Gui
)

if(QT_DESIRED_VERSION MATCHES 6)
    list(APPEND LINK_LIBS Qt${QT_DESIRED_VERSION}::OpenGL)
    list(APPEND LINK_LIBS Qt${QT_DESIRED_VERSION}::OpenGLWidgets)
else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND LINK_LIBS Qt${QT_DESIRED_VERSION}::X11Extras)
    endif()
endif()

#
# OpenCV 配置 (可选)
#
set(OPENCV_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../opencv/build")
if(EXISTS "${OPENCV_LOCAL_DIR}/OpenCVConfig.cmake")
    set(OpenCV_DIR "${OPENCV_LOCAL_DIR}")
endif()

find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    include_directories(${OpenCV_INCLUDE_DIRS})
    message(STATUS "[${PROJECT_NAME}] OpenCV version: ${OpenCV_VERSION}")
    set(ENABLE_IMAGE_MATCHING ON)
    list(APPEND LINK_LIBS ${OpenCV_LIBS})
else()
    message(WARNING "[${PROJECT_NAME}] OpenCV not found, image matching disabled")
    set(ENABLE_IMAGE_MATCHING OFF)
endif()

#
# 源文件
#

# app - 应用入口
set(SRC_APP
    src/app/main.cpp
    src/app/config.cpp
    src/app/config.h
    src/app/path.h
)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND SRC_APP
        src/app/winutils.cpp
        src/app/winutils.h
        src/app/mousetap/mousetap.cpp
        src/app/mousetap/mousetap.h
        src/app/mousetap/winmousetap.cpp
        src/app/mousetap/winmousetap.h
    )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND SRC_APP
        src/app/path.mm
        src/app/mousetap/mousetap.cpp
        src/app/mousetap/mousetap.h
        src/app/mousetap/cocoamousetap.h
        src/app/mousetap/cocoamousetap.mm
    )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SRC_APP
        src/app/mousetap/mousetap.cpp
        src/app/mousetap/mousetap.h
        src/app/mousetap/xmousetap.cpp
        src/app/mousetap/xmousetap.h
    )
endif()

# ui - 用户界面
set(SRC_UI
    src/ui/dialog.cpp
    src/ui/dialog.h
    src/ui/dialog.ui
    src/ui/videoform.cpp
    src/ui/videoform.h
    src/ui/videoform.ui
    src/ui/toolform.cpp
    src/ui/toolform.h
    src/ui/toolform.ui
    src/ui/settingsdialog.cpp
    src/ui/settingsdialog.h
    src/ui/terminaldialog.cpp
    src/ui/terminaldialog.h
    src/ui/ConnectionProgressWidget.cpp
    src/ui/ConnectionProgressWidget.h
    src/ui/KeyMapEditView.cpp
    src/ui/KeyMapEditView.h
    src/ui/imagecapturedialog.h
    src/ui/scripteditordialog.h
    src/ui/KeyMapBase.h
    src/ui/KeyMapItems.h
    # widgets
    src/ui/widgets/keepratiowidget.cpp
    src/ui/widgets/keepratiowidget.h
    src/ui/widgets/magneticwidget.cpp
    src/ui/widgets/magneticwidget.h
    src/ui/widgets/iconhelper.cpp
    src/ui/widgets/iconhelper.h
)

# control - 控制模块
set(SRC_CONTROL
    src/control/controller.cpp
    src/control/controller.h
    src/control/controlsender.cpp
    src/control/controlsender.h
    src/control/bufferutil.cpp
    src/control/bufferutil.h
    # input
    src/control/input/controlmsg.cpp
    src/control/input/controlmsg.h
    src/control/input/controlmsgpool.h
    src/control/input/fastmsg.cpp
    src/control/input/fastmsg.h
    src/control/input/inputconvertbase.cpp
    src/control/input/inputconvertbase.h
    src/control/input/inputconvertgame.cpp
    src/control/input/inputconvertgame.h
    src/control/input/keymap.cpp
    src/control/input/keymap.h
    src/control/input/scriptapi.cpp
    src/control/input/scriptapi.h
    # receiver
    src/control/receiver/devicemsg.cpp
    src/control/receiver/devicemsg.h
    src/control/receiver/receiver.cpp
    src/control/receiver/receiver.h
)

# transport - 传输模块
set(SRC_TRANSPORT
    # server
    src/transport/server/server.cpp
    src/transport/server/server.h
    src/transport/server/device.cpp
    src/transport/server/device.h
    src/transport/server/devicemanage.cpp
    src/transport/server/devicemanage.h
    # tcp
    src/transport/tcp/tcpserverhandler.cpp
    src/transport/tcp/tcpserverhandler.h
    src/transport/tcp/videosocket.cpp
    src/transport/tcp/videosocket.h
    src/transport/tcp/tcpserver.cpp
    src/transport/tcp/tcpserver.h
    # kcp
    src/transport/kcp/ikcp.c
    src/transport/kcp/ikcp.h
    src/transport/kcp/KcpCore.cpp
    src/transport/kcp/KcpCore.h
    src/transport/kcp/KcpTransport.cpp
    src/transport/kcp/KcpTransport.h
    src/transport/kcp/KcpClient.cpp
    src/transport/kcp/KcpClient.h
    src/transport/kcp/kcpcontrolsocket.cpp
    src/transport/kcp/kcpcontrolsocket.h
    src/transport/kcp/kcpserver.cpp
    src/transport/kcp/kcpserver.h
    src/transport/kcp/kcpvideosocket.cpp
    src/transport/kcp/kcpvideosocket.h
    # adb
    src/transport/adb/adbprocess.cpp
    src/transport/adb/adbprocess.h
    src/transport/adb/adbprocessimpl.cpp
    src/transport/adb/adbprocessimpl.h
    src/transport/adb/IAdbExecutor.h
)

# 确保 ikcp.c 使用 C 编译器
set_source_files_properties(src/transport/kcp/ikcp.c PROPERTIES LANGUAGE C)

# decoder - 解码模块
set(SRC_DECODER
    src/decoder/demuxer.cpp
    src/decoder/demuxer.h
    src/decoder/decoder.cpp
    src/decoder/decoder.h
    src/decoder/videobuffer.cpp
    src/decoder/videobuffer.h
    src/decoder/fpscounter.cpp
    src/decoder/fpscounter.h
    src/decoder/avframeconvert.cpp
    src/decoder/avframeconvert.h
    src/decoder/IDecoder.h
)

# render - 渲染模块
set(SRC_RENDER
    src/render/qyuvopenglwidget.cpp
    src/render/qyuvopenglwidget.h
    src/render/IVideoRenderer.h
)

# common - 通用模块
set(SRC_COMMON
    src/common/compat.h
    src/common/input.h
    src/common/keycodes.h
    src/common/qscrcpyevent.h
    src/common/QtScrcpyCore.h
    src/common/QtScrcpyCoreDef.h
    src/common/ConfigCenter.cpp
    src/common/ConfigCenter.h
    src/common/Constants.h
    src/common/ErrorCode.h
    src/common/SPSCQueue.h
)

# imagematch (总是编译，辅助函数不依赖 OpenCV)
set(SRC_IMAGEMATCH
    src/common/imagematcher.cpp
    src/common/imagematcher.h
)

# 资源文件
set(SRC_RESOURCES
    src/ui/resources/res.qrc
)

# 平台特定文件
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SRC_PLATFORM src/ui/resources/QtScrcpy.rc)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(SRC_PLATFORM src/ui/resources/QtScrcpy.icns)
endif()

# 所有源文件
set(ALL_SOURCES
    ${SRC_APP}
    ${SRC_UI}
    ${SRC_CONTROL}
    ${SRC_TRANSPORT}
    ${SRC_DECODER}
    ${SRC_RENDER}
    ${SRC_COMMON}
    ${SRC_IMAGEMATCH}
    ${SRC_RESOURCES}
    ${SRC_PLATFORM}
)

# 源文件分组 (IDE 中显示)
source_group(app FILES ${SRC_APP})
source_group(ui FILES ${SRC_UI})
source_group(control FILES ${SRC_CONTROL})
source_group(transport FILES ${SRC_TRANSPORT})
source_group(decoder FILES ${SRC_DECODER})
source_group(render FILES ${SRC_RENDER})
source_group(common FILES ${SRC_COMMON})

#
# 可执行文件
#
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(QC_RUNTIME_TYPE MACOSX_BUNDLE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(QC_RUNTIME_TYPE WIN32)
endif()

add_executable(${PROJECT_NAME} ${QC_RUNTIME_TYPE} ${ALL_SOURCES})

#
# Include 路径
#
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/mousetap
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/input
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/tcp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/kcp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/adb
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# FFmpeg include
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/env/ffmpeg/include)

#
# 输出目录
#
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../output/${QC_CPU_ARCH}/${CMAKE_BUILD_TYPE}/$<0:>"
)

get_target_property(QSC_BIN_OUTPUT_PATH ${PROJECT_NAME} RUNTIME_OUTPUT_DIRECTORY)
set(QSC_DEPLOY_PATH ${QSC_BIN_OUTPUT_PATH})

#
# 平台特定配置
#

# Windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(FFMPEG_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/env/ffmpeg/lib/${QC_CPU_ARCH}")
    target_link_directories(${PROJECT_NAME} PUBLIC ${FFMPEG_LIB_PATH})
    target_link_libraries(${PROJECT_NAME} PRIVATE avformat avcodec avutil swscale)

    # 复制 DLL 和工具
    set(FFMPEG_BIN_PATH "${CMAKE_CURRENT_SOURCE_DIR}/env/ffmpeg/bin/${QC_CPU_ARCH}")
    set(ADB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/env/adb/win")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/avcodec-58.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/avformat-58.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/avutil-56.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/swscale-5.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/swresample-3.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ADB_PATH}/adb.exe" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ADB_PATH}/AdbWinApi.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ADB_PATH}/AdbWinUsbApi.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/env/scrcpy-server" "${QSC_DEPLOY_PATH}"
    )
endif()

# MacOS
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
    set(FFMPEG_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/env/ffmpeg/lib/${QC_CPU_ARCH}")
    target_link_directories(${PROJECT_NAME} PUBLIC ${FFMPEG_LIB_PATH})
    target_link_libraries(${PROJECT_NAME} PRIVATE avformat.58 avcodec.58 avutil.56 swscale.5)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework AppKit")

    set(MACOS_BUNDLE_PATH ${QSC_DEPLOY_PATH}/${PROJECT_NAME}.app/Contents)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_LIB_PATH}/libavcodec.58.dylib" "${MACOS_BUNDLE_PATH}/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_LIB_PATH}/libavformat.58.dylib" "${MACOS_BUNDLE_PATH}/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_LIB_PATH}/libavutil.56.dylib" "${MACOS_BUNDLE_PATH}/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_LIB_PATH}/libswscale.5.dylib" "${MACOS_BUNDLE_PATH}/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/env/scrcpy-server" "${MACOS_BUNDLE_PATH}/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/env/adb/mac/adb" "${MACOS_BUNDLE_PATH}/MacOS"
    )
endif()

# Linux
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(FFMPEG_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/env/ffmpeg/lib")
    target_link_directories(${PROJECT_NAME} PUBLIC ${FFMPEG_LIB_PATH})
    target_link_libraries(${PROJECT_NAME} PRIVATE avformat avcodec avutil swscale z)

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE xcb Threads::Threads)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/env/adb/linux/adb" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/env/scrcpy-server" "${QSC_DEPLOY_PATH}"
    )
endif()

#
# 链接库
#
target_link_libraries(${PROJECT_NAME} PRIVATE ${LINK_LIBS})

# 编译定义
if(ENABLE_IMAGE_MATCHING)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_IMAGE_MATCHING)
endif()
