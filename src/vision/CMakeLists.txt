add_library(gs_vision STATIC
    include/gs/vision/Color.h
    include/gs/vision/Glance.h
    include/gs/vision/Image.h
    include/gs/vision/IntegralImage.h
    include/gs/vision/Matcher.h
    include/gs/vision/Simd.h
    src/Bands.cpp
    src/Bands.h
    src/Color.cpp
    src/Glance.cpp
    src/Image.cpp
    src/IntegralImage.cpp
    src/Kernels.h
    src/KernelsAvx2.cpp
    src/KernelsScalar.cpp
    src/Matcher.cpp
    src/Simd.cpp
    src/Template.cpp
)
add_library(gs::vision ALIAS gs_vision)

target_include_directories(gs_vision PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gs_vision PUBLIC gs::base)
gs_apply_compiler_policy(gs_vision)
set_source_files_properties(src/KernelsAvx2.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX2)
