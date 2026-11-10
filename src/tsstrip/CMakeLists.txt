add_library(gs_tsstrip STATIC
    include/gs/tsstrip/Strip.h
    include/gs/tsstrip/Tokens.h
    src/Scanner.cpp
    src/Scanner.h
    src/Strip.cpp
    src/Tokens.cpp
)
add_library(gs::tsstrip ALIAS gs_tsstrip)

target_include_directories(gs_tsstrip PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
gs_apply_compiler_policy(gs_tsstrip)
