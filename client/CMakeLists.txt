# GameScrcpy Client - éćĺç CMakeLists.txt
# çŽĺ˝çťć:
#   src/app       - ĺşç¨ĺĽĺŁ
#   src/ui        - ç¨ćˇçé˘
#   src/control   - ć§ĺść¨Ąĺ
#   src/transport - äź čžć¨Ąĺ (KCP/TCP/ADB)
#   src/decoder   - č§Łç ć¨Ąĺ
#   src/render    - ć¸˛ćć¨Ąĺ
#   src/common    - éç¨ć¨Ąĺ
#   env/          - çŻĺ˘äžčľ (FFmpeg, ADB)

cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

#
# éĄšçŽéç˝Ž
#
set(QC_PROJECT_NAME "GameScrcpy")
set(QC_PROJECT_VERSION "1.3.4")

project(${QC_PROJECT_NAME} VERSION ${QC_PROJECT_VERSION} LANGUAGES C CXX)
message(STATUS "[${PROJECT_NAME}] Project ${PROJECT_NAME} ${PROJECT_VERSION}")

# ćśććŁćľ?
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(QC_CPU_ARCH x64)
else()
    set(QC_CPU_ARCH x86)
endif()

# MacOS ćść
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
# çźčŻĺ¨éç˝?
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

# MSVC éç˝Ž
if(MSVC)
    add_compile_options(/W3 /WX /wd4566 /utf-8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
    add_link_options(/SAFESEH:NO /MAP)
endif()

# GCC/Clang éç˝Ž
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -pedantic -Werror)
    add_compile_options(-Wno-nested-anon-types -Wno-c++17-extensions -Wno-overloaded-virtual)
endif()

#
# Qt éç˝Ž
#
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
set(QT_DESIRED_VERSION ${QT_VERSION_MAJOR})
message(STATUS "[${PROJECT_NAME}] Qt version: ${QT_DESIRED_VERSION}")

set(qt_required_components Widgets Gui Svg SvgWidgets SerialPort Network)

if(NOT QT_DESIRED_VERSION MATCHES 6)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND qt_required_components X11Extras)
    endif()
endif()

find_package(Qt${QT_DESIRED_VERSION} REQUIRED COMPONENTS ${qt_required_components})

set(LINK_LIBS
    Qt${QT_DESIRED_VERSION}::Widgets
    Qt${QT_DESIRED_VERSION}::Gui
    Qt${QT_DESIRED_VERSION}::Svg
    Qt${QT_DESIRED_VERSION}::SvgWidgets
    Qt${QT_DESIRED_VERSION}::SerialPort
    Qt${QT_DESIRED_VERSION}::Network
)

if(NOT QT_DESIRED_VERSION MATCHES 6)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND LINK_LIBS Qt${QT_DESIRED_VERSION}::X11Extras)
    endif()
endif()

#
# OpenCV éç˝Ž (ĺŻé?
#
set(OPENCV_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/env/opencv/build/x64/vc16/lib")
if(EXISTS "${OPENCV_LOCAL_DIR}/OpenCVConfig.cmake")
    set(OpenCV_DIR "${OPENCV_LOCAL_DIR}")
endif()

find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    include_directories(${OpenCV_INCLUDE_DIRS})
    message(STATUS "[${PROJECT_NAME}] OpenCV version: ${OpenCV_VERSION}")
    set(ENABLE_IMAGE_MATCHING ON)
    list(APPEND LINK_LIBS ${OpenCV_LIBS})

    # opencv_matching ĺş?(ć¨Ąćżĺšé)
    set(OPENCV_MATCHING_DIR "${CMAKE_CURRENT_SOURCE_DIR}/env/opencv_matching")
    if(EXISTS "${OPENCV_MATCHING_DIR}/include/matcher.h")
        include_directories(${OPENCV_MATCHING_DIR}/include)
        include_directories(${OPENCV_MATCHING_DIR}/matcher)
        include_directories(${OPENCV_MATCHING_DIR}/matcher/base_matcher)
        include_directories(${OPENCV_MATCHING_DIR}/matcher/Pattern_Matching)
        # spdlog ćĽĺżĺş?(opencv_matching äžčľ)
        include_directories(${OPENCV_MATCHING_DIR}/3rdParty/spdlog/include)
        message(STATUS "[${PROJECT_NAME}] opencv_matching found")

        # ćˇťĺ  opencv_matching ćşćäť?
        set(SRC_OPENCV_MATCHING
            ${OPENCV_MATCHING_DIR}/matcher/matcher.cpp
            ${OPENCV_MATCHING_DIR}/matcher/base_matcher/base_matcher.cpp
            ${OPENCV_MATCHING_DIR}/matcher/Pattern_Matching/PatternMatching.cpp
        )
        # çŚç¨ opencv_matching çč­Śĺĺ˝éčŻŻďźçŹŹä¸ćšäťŁç ďź?
        if(MSVC)
            set_source_files_properties(${SRC_OPENCV_MATCHING} PROPERTIES COMPILE_FLAGS "/W0")
        endif()
    else()
        message(WARNING "[${PROJECT_NAME}] opencv_matching not found, using basic matching")
        set(SRC_OPENCV_MATCHING "")
    endif()
else()
    message(WARNING "[${PROJECT_NAME}] OpenCV not found, image matching disabled")
    set(ENABLE_IMAGE_MATCHING OFF)
    set(SRC_OPENCV_MATCHING "")
endif()

#
# ćşćäť?
#

# app - ĺşç¨ĺĽĺŁ
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

# ui - ç¨ćˇçé˘
set(SRC_UI
    # ć§ćäť?(é¨ĺäżçç¨äş VideoForm ç­?
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
    src/ui/KeyMapOverlay.cpp
    src/ui/KeyMapOverlay.h
    src/ui/ScriptTipWidget.cpp
    src/ui/ScriptTipWidget.h
    src/ui/imagecapturedialog.h
    src/ui/scripteditordialog.h
    src/ui/selectionregionmanager.h
    src/ui/selectioneditordialog.h
    src/ui/scriptbuttonmanager.h
    src/ui/scriptswipemanager.h
    src/ui/KeyMapBase.h
    src/ui/KeyMapItems.h
    # widgets
    src/ui/widgets/keepratiowidget.cpp
    src/ui/widgets/keepratiowidget.h
    src/ui/widgets/magneticwidget.cpp
    src/ui/widgets/magneticwidget.h
    src/ui/widgets/iconhelper.cpp
    src/ui/widgets/iconhelper.h
    # ===== Fluent UI ć°ćść?=====
    # ä¸ťçŞĺ?
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    # ä¸ťé˘ĺşçĄčŽžć˝
    src/ui/theme/DesignTokens.h
    src/ui/theme/MotionTokens.h
    src/ui/theme/ThemeManager.h
    src/ui/theme/ThemeManager.cpp
    # çťäťśĺş?
    src/ui/components/FluentCard.h
    src/ui/components/FluentCard.cpp
    src/ui/components/FluentButton.h
    src/ui/components/FluentButton.cpp
    src/ui/components/FluentToggle.h
    src/ui/components/FluentToggle.cpp
    src/ui/components/FluentDialog.h
    src/ui/components/FluentDialog.cpp
    src/ui/components/FluentInfoBar.h
    src/ui/components/FluentInfoBar.cpp
    src/ui/components/SettingRow.h
    src/ui/components/SettingRow.cpp
    src/ui/components/NavigationView.h
    src/ui/components/NavigationView.cpp
    src/ui/components/FluentSlider.h
    src/ui/components/FluentSlider.cpp
    src/ui/components/FluentProgressRing.h
    src/ui/components/FluentProgressRing.cpp
    src/ui/components/FluentBadge.h
    src/ui/components/FluentBadge.cpp
    src/ui/components/FluentToolWindow.h
    src/ui/components/FluentToolWindow.cpp
    src/ui/components/DeviceCard.h
    src/ui/components/DeviceCard.cpp
    src/ui/components/ActivityLog.h
    src/ui/components/ActivityLog.cpp
    src/ui/components/VideoBottomBar.h
    src/ui/components/VideoBottomBar.cpp
    src/ui/components/VideoSettingsPopup.h
    src/ui/components/VideoSettingsPopup.cpp
    src/ui/components/KeyMapSidePanel.h
    src/ui/components/KeyMapSidePanel.cpp
    src/ui/components/FluentComboBox.h
    src/ui/components/FluentComboBox.cpp
    src/ui/components/FluentInput.h
    src/ui/components/FluentInput.cpp
    src/ui/components/OnboardingOverlay.h
    src/ui/components/OnboardingOverlay.cpp
    src/ui/components/HelpDialog.h
    src/ui/components/HelpDialog.cpp
    # éŽä˝çźčž
    src/ui/keymap/KeyMapPropertyPanel.h
    src/ui/keymap/KeyMapPropertyPanel.cpp
    src/ui/keymap/KeyConflictIndicator.h
    src/ui/keymap/KeyConflictIndicator.cpp
    # éĄľé˘
    src/ui/pages/HomePage.h
    src/ui/pages/HomePage.cpp
    src/ui/pages/SettingsPage.h
    src/ui/pages/SettingsPage.cpp
    src/ui/pages/TerminalPage.h
    src/ui/pages/TerminalPage.cpp
    src/ui/pages/DeviceDetailPage.h
    src/ui/pages/DeviceDetailPage.cpp
)

# control - ć§ĺść¨Ąĺ
set(SRC_CONTROL
    src/control/controller.cpp
    src/control/controller.h
    src/control/controlsender.cpp
    src/control/controlsender.h
    # session
    src/control/session/SessionContext.cpp
    src/control/session/SessionContext.h
    src/control/session/SessionVars.cpp
    src/control/session/SessionVars.h
    src/control/session/ScriptBridge.cpp
    src/control/session/ScriptBridge.h
    src/control/session/InputDispatcher.cpp
    src/control/session/InputDispatcher.h
    # handlers
    src/control/handlers/IInputHandler.h
    src/control/handlers/HandlerChain.cpp
    src/control/handlers/HandlerChain.h
    src/control/handlers/SteerWheelHandler.cpp
    src/control/handlers/SteerWheelHandler.h
    src/control/handlers/ViewportHandler.cpp
    src/control/handlers/ViewportHandler.h
    src/control/handlers/FreeLookHandler.cpp
    src/control/handlers/FreeLookHandler.h
    src/control/handlers/CursorHandler.cpp
    src/control/handlers/CursorHandler.h
    src/control/handlers/KeyboardHandler.cpp
    src/control/handlers/KeyboardHandler.h
    # input
    src/control/input/fastmsg.cpp
    src/control/input/fastmsg.h
    src/control/input/keymap.cpp
    src/control/input/keymap.h
    # script - čćŹĺźć
    src/control/script/ScriptEngine.cpp
    src/control/script/ScriptEngine.h
    src/control/script/ScriptSandbox.cpp
    src/control/script/ScriptSandbox.h
    src/control/script/ScriptWatchdog.cpp
    src/control/script/ScriptWatchdog.h
    # receiver
    src/control/receiver/devicemsg.cpp
    src/control/receiver/devicemsg.h
    src/control/receiver/receiver.cpp
    src/control/receiver/receiver.h
    # hid - ?????? (TouchRouter ????)
    src/control/hid/TouchRouter.h
    src/control/hid/TouchRouter.cpp
    src/control/hid/ITouchBackend.h
    src/control/hid/HidReportDescriptor.h
    src/control/hid/UhidTouchBackend.h
    src/control/hid/UhidTouchBackend.cpp
)

# transport - äź čžć¨Ąĺ
set(SRC_TRANSPORT
    # server
    src/transport/server/server.cpp
    src/transport/server/server.h
    # device.cpp/h ĺˇ˛ĺ é¤ďźč˘?core/service/DeviceAdapter ćżäťŁ
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
    src/transport/kcp/UdpVideoClient.cpp
    src/transport/kcp/UdpVideoClient.h
    # adb
    src/transport/adb/adbprocess.cpp
    src/transport/adb/adbprocess.h
    src/transport/adb/adbprocessimpl.cpp
    src/transport/adb/adbprocessimpl.h
    src/transport/adb/IAdbExecutor.h
    # auxiliary channel
    src/transport/auxiliary/AuxChannelClient.cpp
    src/transport/auxiliary/AuxChannelClient.h
    # native transport (Winsock2)
    src/transport/native/NativeTcpSocket.cpp
    src/transport/native/NativeTcpSocket.h
    src/transport/native/NativeTcpServer.cpp
    src/transport/native/NativeTcpServer.h
    # companion app client
    src/transport/companion/CompanionClient.cpp
    src/transport/companion/CompanionClient.h
)

# çĄŽäż ikcp.c ä˝żç¨ C çźčŻĺ?
set_source_files_properties(src/transport/kcp/ikcp.c PROPERTIES LANGUAGE C)

# decoder - č§Łç ć¨Ąĺ
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
    src/decoder/AudioStreamManager.h
    src/decoder/AudioStreamManager.cpp
    src/decoder/WasapiPlayer.h
    src/decoder/WasapiPlayer.cpp
)

# render - ć¸˛ćć¨Ąĺ
set(SRC_RENDER
    src/render/IVideoRenderer.h
    src/render/d3d11/D3D11Renderer.h
    src/render/d3d11/D3D11Renderer.cpp
    src/render/d3d11/D3D11VideoWidget.h
    src/render/d3d11/D3D11VideoWidget.cpp
)

# common - éç¨ć¨Ąĺ
set(SRC_COMMON
    src/common/compat.h
    src/common/input.h
    src/common/keycodes.h
    src/common/qscrcpyevent.h
    src/common/GameScrcpyCore.h
    src/common/GameScrcpyCoreDef.h
    src/common/ConfigCenter.cpp
    src/common/ConfigCenter.h
    src/common/Constants.h
    src/common/ErrorCode.h
    src/common/SPSCQueue.h
    src/common/Logger.h
    src/common/GameSignal.h
    src/common/StringUtils.h
    src/common/ThreadDispatcher.h
    src/common/ThreadDispatcher.cpp
    src/common/IniConfig.h
    src/common/IniConfig.cpp
    src/common/NativeTimer.h
    src/common/NativeTimer.cpp
)

# QuickJS - ĺľĺĽĺź?JavaScript ĺźć (C ćşç )
# QuickJS C files compiled as a separate static library with relaxed flags
set(QUICKJS_SOURCES
    env/quickjs/quickjs.c
    env/quickjs/cutils.c
    env/quickjs/libregexp.c
    env/quickjs/libunicode.c
    env/quickjs/dtoa.c
)
set(QUICKJS_HEADERS
    env/quickjs/quickjs.h
    env/quickjs/quickjs-libc.h
    env/quickjs/quickjs-atom.h
    env/quickjs/quickjs-opcode.h
    env/quickjs/cutils.h
    env/quickjs/libregexp.h
    env/quickjs/libregexp-opcode.h
    env/quickjs/libunicode.h
    env/quickjs/libunicode-table.h
    env/quickjs/dtoa.h
    env/quickjs/list.h
)

# Build QuickJS as a static library with its own compile flags
add_library(quickjs STATIC ${QUICKJS_SOURCES} ${QUICKJS_HEADERS})
set_target_properties(quickjs PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(quickjs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/env/quickjs
)
target_compile_definitions(quickjs PRIVATE
    CONFIG_VERSION="2025-09-13"
    _CRT_SECURE_NO_WARNINGS
)
if(MSVC)
    # Compile as pure C11 with all warnings suppressed (third-party code)
    # /WX- overrides the parent /WX (warnings-as-errors)
    target_compile_options(quickjs PRIVATE /TC /W0 /WX- /utf-8 /std:c11)
endif()

# JsEngine wrapper (C++ code, part of main project)
set(SRC_QUICKJS
    src/common/JsEngine.h
    src/common/JsEngine.cpp
    src/control/script/JsBindings.h
    src/control/script/JsBindings.cpp
)

# core - ć ¸ĺżĺą?(ć°ćść?
set(SRC_CORE
    # infra - ĺşçĄčŽžć˝
    src/core/infra/FrameData.h
    src/core/infra/SessionParams.h
    src/core/infra/FramePool.h
    src/core/infra/FramePool.cpp
    src/core/infra/FrameQueue.h
    # interfaces - ćĽĺŁĺŽäš
    src/core/interfaces/IDecoder.h
    src/core/interfaces/IVideoChannel.h
    src/core/interfaces/IControlChannel.h
    src/core/interfaces/IRenderer.h
    src/core/interfaces/IInputProcessor.h
    # impl - ćĽĺŁĺŽç° (ééĺ?
    src/core/impl/TcpVideoChannel.h
    src/core/impl/TcpVideoChannel.cpp
    src/core/impl/KcpVideoChannel.h
    src/core/impl/KcpVideoChannel.cpp
    src/core/impl/TcpControlChannel.h
    src/core/impl/TcpControlChannel.cpp
    src/core/impl/KcpControlChannel.h
    src/core/impl/KcpControlChannel.cpp
    src/core/impl/FFmpegDecoderImpl.h
    src/core/impl/FFmpegDecoderImpl.cpp
    src/core/impl/GameInputProcessor.h
    src/core/impl/GameInputProcessor.cpp
    # impl - éśćˇč´çťäť?
    src/core/impl/ZeroCopyDecoder.h
    src/core/impl/ZeroCopyDecoder.cpp
    # service - ćĺĄĺą?
    src/core/service/ConnectionManager.h
    src/core/service/ConnectionManager.cpp
    src/core/service/DeviceSession.h
    src/core/service/DeviceSession.cpp
    src/core/service/SessionFactory.h
    src/core/service/SessionFactory.cpp
    src/core/service/StreamManager.h
    src/core/service/StreamManager.cpp
    src/core/service/InputManager.h
    src/core/service/ScriptEngine.h
    src/core/service/ScriptEngine.cpp
    src/core/service/InputManager.cpp
    # service - éśćˇč´ćľçŽĄç
    src/core/service/ZeroCopyStreamManager.h
    src/core/service/ZeroCopyStreamManager.cpp
)

# imagematch (ćťćŻçźčŻďźčžĺŠĺ˝ć°ä¸äžčľ OpenCV)
set(SRC_IMAGEMATCH
    src/common/imagematcher.cpp
    src/common/imagematcher.h
)

# čľćşćäťś
set(SRC_RESOURCES
    src/ui/resources/res.qrc
)

# ĺšłĺ°çšĺŽćäťś
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SRC_PLATFORM src/ui/resources/GameScrcpy.rc)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(SRC_PLATFORM src/ui/resources/GameScrcpy.icns)
endif()

# ćććşćäťś
set(ALL_SOURCES
    ${SRC_APP}
    ${SRC_UI}
    ${SRC_CONTROL}
    ${SRC_TRANSPORT}
    ${SRC_DECODER}
    ${SRC_RENDER}
    ${SRC_COMMON}
    ${SRC_CORE}
    ${SRC_QUICKJS}
    ${SRC_IMAGEMATCH}
    ${SRC_OPENCV_MATCHING}
    ${SRC_RESOURCES}
    ${SRC_PLATFORM}
)

# ćşćäťśĺçť?(IDE ä¸­ćžç¤?
source_group(app FILES ${SRC_APP})
source_group(ui FILES ${SRC_UI})
source_group(control FILES ${SRC_CONTROL})
source_group(transport FILES ${SRC_TRANSPORT})
source_group(decoder FILES ${SRC_DECODER})
source_group(render FILES ${SRC_RENDER})
source_group(common FILES ${SRC_COMMON})
source_group(core FILES ${SRC_CORE})
source_group(quickjs FILES ${SRC_QUICKJS})

#
# ĺŻć§čĄćäť?
#
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(QC_RUNTIME_TYPE MACOSX_BUNDLE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(QC_RUNTIME_TYPE WIN32)
endif()

add_executable(${PROJECT_NAME} ${QC_RUNTIME_TYPE} ${ALL_SOURCES})

# Link QuickJS static library
target_link_libraries(${PROJECT_NAME} PRIVATE quickjs)

#
# Include čˇŻĺž
#
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/mousetap
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/theme
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/session
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/handlers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/input
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/script
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/hid
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/tcp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/kcp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/adb
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/auxiliary
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/companion
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/d3d11
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/env/quickjs
    # core - ć°ćść?
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/interfaces
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/infra
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/impl
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/service
)

# FFmpeg include
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/env/ffmpeg/include)

#
# čžĺşçŽĺ˝
#
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../output/${QC_CPU_ARCH}/${CMAKE_BUILD_TYPE}/$<0:>"
)

get_target_property(QSC_BIN_OUTPUT_PATH ${PROJECT_NAME} RUNTIME_OUTPUT_DIRECTORY)
set(QSC_DEPLOY_PATH ${QSC_BIN_OUTPUT_PATH})

#
# ĺšłĺ°çšĺŽéç˝Ž
#

# Windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(FFMPEG_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/env/ffmpeg/lib")
    target_link_directories(${PROJECT_NAME} PUBLIC ${FFMPEG_LIB_PATH})
    target_link_libraries(${PROJECT_NAME} PRIVATE avformat avcodec avutil swscale swresample)

    # d3d11/dxgi/d3dcompiler: D3D11 ć¸˛ć, winmm: éŤç˛žĺşŚĺŽćśĺ¨, avrt: MMCSS ĺŽćśč°ĺşŚ
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 dxgi d3dcompiler winmm avrt)

    # ĺ¤ĺś DLL ĺĺˇĽĺ?
    set(FFMPEG_BIN_PATH "${CMAKE_CURRENT_SOURCE_DIR}/env/ffmpeg/bin")
    set(ADB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/env/adb/win")
    set(OPENCV_BIN_PATH "${CMAKE_CURRENT_SOURCE_DIR}/env/opencv/build/x64/vc16/bin")

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        # ============ FFmpeg DLLs ============
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/avcodec-62.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/avformat-62.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/avutil-60.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/swscale-9.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_BIN_PATH}/swresample-6.dll" "${QSC_DEPLOY_PATH}"
        # ============ ADB ĺˇĽĺˇ ============
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ADB_PATH}/adb.exe" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ADB_PATH}/AdbWinApi.dll" "${QSC_DEPLOY_PATH}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ADB_PATH}/AdbWinUsbApi.dll" "${QSC_DEPLOY_PATH}"
        # ============ scrcpy-server ============
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/env/scrcpy-server" "${QSC_DEPLOY_PATH}"
        # ============ OpenCV DLLs (ĺŚćĺ­ĺ¨) ============
        COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:${OpenCV_FOUND}>,copy_if_different,true> "$<IF:$<BOOL:${OpenCV_FOUND}>,${OPENCV_BIN_PATH}/opencv_world4120.dll,>" "$<IF:$<BOOL:${OpenCV_FOUND}>,${QSC_DEPLOY_PATH},>"
        COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:${OpenCV_FOUND}>,copy_if_different,true> "$<IF:$<BOOL:${OpenCV_FOUND}>,${OPENCV_BIN_PATH}/opencv_videoio_ffmpeg4120_64.dll,>" "$<IF:$<BOOL:${OpenCV_FOUND}>,${QSC_DEPLOY_PATH},>"
        COMMENT "Deploying runtime dependencies to ${QSC_DEPLOY_PATH}"
    )

    # ============ windeployqt: čŞĺ¨ćśé Qt čżčĄć?DLL ============
    # ćĽćž windeployqt ĺˇĽĺˇ
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}" "${QT${QT_DESIRED_VERSION}_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Running windeployqt to gather Qt runtime DLLs..."
        )
        message(STATUS "[${PROJECT_NAME}] windeployqt found: ${WINDEPLOYQT_EXECUTABLE}")
    else()
        message(WARNING "[${PROJECT_NAME}] windeployqt NOT found! Qt DLLs will not be auto-deployed. "
            "Make sure Qt bin directory is in PATH, or set CMAKE_PREFIX_PATH correctly.")
    endif()
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
# éžćĽĺş?
#
target_link_libraries(${PROJECT_NAME} PRIVATE ${LINK_LIBS})

# çźčŻĺŽäš
if(ENABLE_IMAGE_MATCHING)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_IMAGE_MATCHING)
endif()

#
# AOA HID touch backend (optional, requires libusb)
#
option(ENABLE_AOA_HID "Enable AOA HID touch backend (requires libusb)" ON)
set(LIBUSB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/env/libusb")
if(ENABLE_AOA_HID AND EXISTS "${LIBUSB_DIR}/include/libusb-1.0/libusb.h")
    message(STATUS "[${PROJECT_NAME}] AOA HID: enabled (libusb found)")
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_AOA_HID)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LIBUSB_DIR}/include/libusb-1.0)
    target_sources(${PROJECT_NAME} PRIVATE
        src/control/hid/AoaHidDevice.h
        src/control/hid/AoaHidDevice.cpp
        src/control/hid/AoaHidBackend.h
        src/control/hid/AoaHidBackend.cpp
        src/control/hid/WinUsbDriverHelper.h
        src/control/hid/WinUsbDriverHelper.cpp
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_directories(${PROJECT_NAME} PRIVATE ${LIBUSB_DIR}/lib)
        target_link_libraries(${PROJECT_NAME} PRIVATE libusb-1.0)
        set(LIBUSB_DLL "${LIBUSB_DIR}/bin/libusb-1.0.dll")
        if(EXISTS "${LIBUSB_DLL}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBUSB_DLL}" "${QSC_DEPLOY_PATH}"
                COMMENT "Deploying libusb-1.0.dll"
            )
        endif()
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
            target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBUSB_LIBRARIES})
            target_include_directories(${PROJECT_NAME} PRIVATE ${LIBUSB_INCLUDE_DIRS})
        else()
            target_link_libraries(${PROJECT_NAME} PRIVATE usb-1.0)
        endif()
    endif()
else()
    if(ENABLE_AOA_HID)
        message(WARNING "[${PROJECT_NAME}] AOA HID: libusb not found at ${LIBUSB_DIR}, disabling")
    else()
        message(STATUS "[${PROJECT_NAME}] AOA HID: disabled by option")
    endif()
endif()

#
# ESP32 HID touch backend (optional, requires Qt SerialPort)
#
option(ENABLE_ESP32_HID "Enable ESP32 HID touch backend (serial)" ON)
if(ENABLE_ESP32_HID)
    message(STATUS "[${PROJECT_NAME}] ESP32 HID: enabled")
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_ESP32_HID)
    target_sources(${PROJECT_NAME} PRIVATE
        src/control/hid/Esp32HidBackend.h
        src/control/hid/Esp32HidBackend.cpp
    )
else()
    message(STATUS "[${PROJECT_NAME}] ESP32 HID: disabled by option")
endif()
