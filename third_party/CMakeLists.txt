# Third-party dependencies. Versions, licenses and checksums: third_party/README.md
# Everything is vendored: configuring never touches the network.

add_library(gs_third_party_doctest INTERFACE)
add_library(gs::doctest ALIAS gs_third_party_doctest)
target_include_directories(gs_third_party_doctest SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/doctest"
)
# Forward declarations of std::ostream break string_view stringification on MSVC.
target_compile_definitions(gs_third_party_doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)

add_library(gs_third_party_tl_expected INTERFACE)
add_library(gs::tl_expected ALIAS gs_third_party_tl_expected)
target_include_directories(gs_third_party_tl_expected SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/tl-expected/include"
)

# Profiles, settings and the golden-vector tests (ADR-0040). Link it PRIVATE and include it only
# from .cpp files so its header never reaches public interfaces.
add_library(gs_third_party_nlohmann_json INTERFACE)
add_library(gs::nlohmann_json ALIAS gs_third_party_nlohmann_json)
target_include_directories(gs_third_party_nlohmann_json SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann-json/include"
)

add_library(gs_third_party_miniz STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/miniz/miniz.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/miniz/miniz.h"
)
add_library(gs::miniz ALIAS gs_third_party_miniz)
target_include_directories(gs_third_party_miniz SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/miniz")
target_compile_definitions(gs_third_party_miniz
    PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES
    PRIVATE _CRT_SECURE_NO_WARNINGS
)

add_subdirectory(quickjs-ng)

# Video and audio decoding (ADR-0015, ADR-0046): the minimal LGPL build that
# tools/build_ffmpeg.ps1 makes from pinned sources. Every executable lands in one directory, so
# the DLLs are copied there once.
foreach(gs_ffmpeg_library IN ITEMS avcodec avformat avutil swresample)
    file(GLOB gs_ffmpeg_dll "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/bin/${gs_ffmpeg_library}-*.dll")
    list(LENGTH gs_ffmpeg_dll gs_ffmpeg_dll_count)
    if(NOT gs_ffmpeg_dll_count EQUAL 1)
        message(FATAL_ERROR
            "third_party/ffmpeg holds ${gs_ffmpeg_dll_count} ${gs_ffmpeg_library} DLLs instead of one; "
            "rebuild it with tools/build_ffmpeg.ps1")
    endif()
    add_library(FFmpeg::${gs_ffmpeg_library} SHARED IMPORTED GLOBAL)
    set_target_properties(FFmpeg::${gs_ffmpeg_library} PROPERTIES
        IMPORTED_LOCATION "${gs_ffmpeg_dll}"
        IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/lib/${gs_ffmpeg_library}.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/include"
    )
    get_filename_component(gs_ffmpeg_dll_name "${gs_ffmpeg_dll}" NAME)
    configure_file("${gs_ffmpeg_dll}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${gs_ffmpeg_dll_name}" COPYONLY)
endforeach()
set_property(TARGET FFmpeg::avcodec FFmpeg::swresample APPEND PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::avutil)
set_property(TARGET FFmpeg::avformat APPEND PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::avcodec FFmpeg::avutil)

add_library(SDL3::SDL3 SHARED IMPORTED GLOBAL)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/sdl3/bin/SDL3.dll"
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/sdl3/lib/SDL3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/sdl3/include"
)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/sdl3/bin/SDL3.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/SDL3.dll" COPYONLY)

add_library(libusb::libusb SHARED IMPORTED GLOBAL)
set_target_properties(libusb::libusb PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libusb/bin/libusb-1.0.dll"
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/libusb/lib/libusb-1.0.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/libusb/include"
)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libusb/bin/libusb-1.0.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/libusb-1.0.dll"
               COPYONLY)

if(GS_BUILD_APP)
    # QR codes for wireless pairing (ADR-0059): Nayuki's QR Code generator as released. Its warnings
    # are not ours to fix, so the project's compiler policy stays off it.
    add_library(gs_third_party_qrcodegen STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/qrcodegen/qrcodegen.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/qrcodegen/qrcodegen.hpp"
    )
    add_library(gs::qrcodegen ALIAS gs_third_party_qrcodegen)
    target_include_directories(gs_third_party_qrcodegen SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/qrcodegen")
    target_compile_features(gs_third_party_qrcodegen PRIVATE cxx_std_17)

    # QWindowKit needs the qmsetup host tool at configure time. Build it once into a directory
    # shared by every preset; qm_install_package skips the build when the package already exists.
    set(GS_SHARED_TOOLS_DIR "${PROJECT_SOURCE_DIR}/build/_shared" CACHE PATH
        "Host tools shared by all build trees")
    include("${CMAKE_CURRENT_SOURCE_DIR}/qwindowkit/qmsetup/cmake/modules/private/InstallPackage.cmake")
    qm_install_package(qmsetup
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qwindowkit/qmsetup"
        BUILD_TREE_DIR "${GS_SHARED_TOOLS_DIR}/build"
        INSTALL_DIR "${GS_SHARED_TOOLS_DIR}/qmsetup"
        BUILD_TYPE Release
        RESULT_PATH gs_qmsetup_package_dir
    )
    set(qmsetup_DIR "${gs_qmsetup_package_dir}" CACHE PATH "" FORCE)

    set(QWINDOWKIT_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_WIDGETS OFF CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_QUICK ON CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(QWINDOWKIT_BUILD_DOCUMENTATIONS OFF CACHE BOOL "" FORCE)
    set(QWINDOWKIT_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(qwindowkit EXCLUDE_FROM_ALL SYSTEM)

    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(gs_licenses_dir "${CMAKE_CURRENT_BINARY_DIR}/licenses")
    add_custom_command(
        OUTPUT "${gs_licenses_dir}/licenses.json" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/README.txt"
        COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/gen_licenses.py"
            --third-party "${CMAKE_CURRENT_SOURCE_DIR}"
            --extras "${PROJECT_SOURCE_DIR}/tools/licenses"
            --qt-version "${Qt6_VERSION}"
            --msvc-version "${MSVC_VERSION}"
            --platform-tools "${PROJECT_SOURCE_DIR}/packaging/adb.json"
            --json "${gs_licenses_dir}/licenses.json"
            --folder "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses"
            --depfile "${gs_licenses_dir}/licenses.d"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/gen_licenses.py"
        DEPFILE "${gs_licenses_dir}/licenses.d"
        COMMENT "Collecting the third-party licenses next to GameScrcpy.exe"
        VERBATIM
    )
    add_library(gs_third_party_licenses OBJECT)
    add_library(gs::licenses ALIAS gs_third_party_licenses)
    qt_add_resources(gs_third_party_licenses "third_party_licenses"
        PREFIX "/licenses"
        BASE "${gs_licenses_dir}"
        FILES "${gs_licenses_dir}/licenses.json"
    )
    target_link_libraries(gs_third_party_licenses PRIVATE Qt6::Core)
endif()
