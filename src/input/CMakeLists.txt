# gs_input: input sources, profile model v2 and coordinate mapping.
# Pure C++, no Qt, no platform headers.

add_library(gs_input STATIC
    include/gs/input/Controls.h
    include/gs/input/DeviceEvents.h
    include/gs/input/Geometry.h
    include/gs/input/InputSource.h
    include/gs/input/KeyHints.h
    include/gs/input/MappingEngine.h
    include/gs/input/Modules.h
    include/gs/input/PadReports.h
    include/gs/input/PointerAllocator.h
    include/gs/input/Profile.h
    include/gs/input/ProfileDiff.h
    include/gs/input/ProfilePatch.h
    include/gs/input/TouchFrameBuilder.h
    include/gs/input/TriggerState.h
    src/AttachmentChain.cpp
    src/AttachmentChain.h
    src/Controls.cpp
    src/DeviceEvents.cpp
    src/Geometry.cpp
    src/InputSource.cpp
    src/KeyHints.cpp
    src/MappingEngine.cpp
    src/Modules.cpp
    src/PadReports.cpp
    src/PointerAllocator.cpp
    src/ProfileDiff.cpp
    src/ProfileIo.cpp
    src/ProfilePatch.cpp
    src/TouchFrameBuilder.cpp
    src/TriggerState.cpp
)
add_library(gs::input ALIAS gs_input)

target_include_directories(gs_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_input
    PUBLIC gs::base gs::protocol
    PRIVATE gs::nlohmann_json
)
gs_apply_compiler_policy(gs_input)
