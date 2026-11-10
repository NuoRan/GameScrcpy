add_library(gs_ext_esp32 SHARED src/esp32_extension.cpp)
set_target_properties(gs_ext_esp32 PROPERTIES
    OUTPUT_NAME esp32
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package$<0:>"
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols$<0:>"
)
target_include_directories(gs_ext_esp32 PRIVATE "${PROJECT_SOURCE_DIR}/sdk/include")
target_compile_definitions(gs_ext_esp32 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(gs_ext_esp32 PRIVATE gs::esp32 gs::nlohmann_json)
gs_apply_compiler_policy(gs_ext_esp32)
configure_file(manifest.json "${CMAKE_CURRENT_BINARY_DIR}/package/manifest.json" COPYONLY)
