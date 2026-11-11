# gs_ext: extension manifests, versions, dependencies and permissions (ADR-0063, ADR-0066).
# Pure C++, no Qt; shared by the app, the gs command line tool and catalog checks.

add_library(gs_ext STATIC
    include/gs/ext/Archive.h
    include/gs/ext/Bundle.h
    include/gs/ext/Diagnostics.h
    include/gs/ext/FilterSource.h
    include/gs/ext/Manifest.h
    include/gs/ext/Package.h
    include/gs/ext/Resolution.h
    include/gs/ext/Schema.h
    include/gs/ext/Version.h
    src/Archive.cpp
    src/Bundle.cpp
    src/Diagnostics.cpp
    src/FilterSource.cpp
    src/Manifest.cpp
    src/Package.cpp
    src/Resolution.cpp
    src/Schema.cpp
    src/Version.cpp
    include/gs/ext/Minisign.h
    include/gs/ext/NativeCheck.h
    src/Ed25519.cpp
    src/Minisign.cpp
    src/NativeCheck.cpp
)
add_library(gs::ext ALIAS gs_ext)

target_include_directories(gs_ext PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_ext PRIVATE gs::miniz gs::nlohmann_json gs::platform)
gs_apply_compiler_policy(gs_ext)
