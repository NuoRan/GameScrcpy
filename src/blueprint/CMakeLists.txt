add_library(gs_blueprint STATIC
    include/gs/blueprint/Document.h
    include/gs/blueprint/Environment.h
    include/gs/blueprint/Instrument.h
    include/gs/blueprint/Layout.h
    include/gs/blueprint/Text.h
    src/Api.cpp
    src/Api.h
    src/Builder.cpp
    src/Catalog.cpp
    src/Edits.cpp
    src/Instrument.cpp
    src/Json.cpp
    src/Layout.cpp
    src/LayoutEdits.cpp
    src/Machine.cpp
    src/Model.h
    src/Parser.cpp
    src/Scopes.cpp
    src/Scopes.h
    src/Support.cpp
    src/Syntax.h
    src/Text.cpp
    src/Variables.cpp
    src/Variables.h
    src/Types.cpp
    src/Types.h
)
add_library(gs::blueprint ALIAS gs_blueprint)

target_include_directories(gs_blueprint PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_blueprint PUBLIC gs::nlohmann_json PRIVATE gs::tsstrip gs::base)
gs_apply_compiler_policy(gs_blueprint)
