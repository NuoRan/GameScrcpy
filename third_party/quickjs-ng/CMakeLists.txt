add_library(gs_third_party_quickjs STATIC
    builtin-array-fromasync.h
    builtin-iterator-zip-keyed.h
    builtin-iterator-zip.h
    cutils.h
    dtoa.c
    dtoa.h
    libregexp-opcode.h
    libregexp.c
    libregexp.h
    libunicode-table.h
    libunicode.c
    libunicode.h
    list.h
    quickjs-atom.h
    quickjs-c-atomics.h
    quickjs-opcode.h
    quickjs.c
    quickjs.h
)
add_library(gs::quickjs ALIAS gs_third_party_quickjs)
set_target_properties(gs_third_party_quickjs PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
)
target_include_directories(gs_third_party_quickjs SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(gs_third_party_quickjs PRIVATE
    QUICKJS_NG_BUILD
    _GNU_SOURCE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601
    _CRT_SECURE_NO_WARNINGS
)
if(MSVC)
    target_compile_options(gs_third_party_quickjs PRIVATE /experimental:c11atomics /wd4098)
endif()
