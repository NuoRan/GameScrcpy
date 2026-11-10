add_library(gs_ext_pc_clock SHARED src/pc_clock.cpp)
set_target_properties(gs_ext_pc_clock PROPERTIES
    OUTPUT_NAME pc-clock
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package$<0:>"
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols$<0:>"
)
target_include_directories(gs_ext_pc_clock PRIVATE "${PROJECT_SOURCE_DIR}/sdk/include")
gs_apply_compiler_policy(gs_ext_pc_clock)
configure_file(manifest.json "${CMAKE_CURRENT_BINARY_DIR}/package/manifest.json" COPYONLY)
