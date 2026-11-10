add_library(gs_script STATIC
    include/gs/script/ApiFiles.h
    include/gs/script/LogicHost.h
    include/gs/script/Permissions.h
    include/gs/script/Replay.h
    include/gs/script/Report.h
    include/gs/script/Resources.h
    include/gs/script/ScriptHost.h
    include/gs/script/Services.h
    include/gs/script/Session.h
    include/gs/script/TestRun.h
    include/gs/script/TextSearch.h
    src/Api.cpp
    src/Api.h
    src/ApiFiles.cpp
    src/Engine.cpp
    src/Engine.h
    src/LoadMeter.cpp
    src/LoadMeter.h
    src/ModulePath.cpp
    src/ModulePath.h
    src/NativeAbi.cpp
    src/NativeAbi.h
    src/Nodes.cpp
    src/Permissions.cpp
    src/Prelude.h
    src/Replay.cpp
    src/Report.cpp
    src/Resources.cpp
    src/ScriptHost.cpp
    src/Session.cpp
    src/StackTrace.cpp
    src/StackTrace.h
    src/TestRun.cpp
    src/TextFold.cpp
    src/TextSearch.cpp
)
add_library(gs::script ALIAS gs_script)

target_include_directories(gs_script PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_script PUBLIC gs::base PRIVATE gs::platform gs::quickjs gs::tsstrip gs::nlohmann_json)
gs_apply_compiler_policy(gs_script)
