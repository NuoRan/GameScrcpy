add_executable(gs_cli main.cpp McpRelay.cpp McpRelay.h ScriptProject.cpp ScriptProject.h ScriptRun.cpp ScriptTools.cpp
                      ScriptTools.h)
set_target_properties(gs_cli PROPERTIES OUTPUT_NAME gs)
target_link_libraries(gs_cli PRIVATE gs::base gs::ext gs::input gs::platform gs::record gs::script gs::tsstrip gs::nlohmann_json d3dcompiler)
gs_apply_compiler_policy(gs_cli)

add_custom_target(gs_typescript ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
        "${PROJECT_SOURCE_DIR}/third_party/typescript/lib" "$<TARGET_FILE_DIR:gs_cli>/typescript"
    COMMENT "Placing the TypeScript compiler next to gs.exe"
    VERBATIM
)

if(GS_BUILD_TESTS)
    file(GLOB gs_example_folders LIST_DIRECTORIES true CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/examples/extensions/*")
    foreach(folder IN LISTS gs_example_folders)
        if(EXISTS "${folder}/manifest.json" AND NOT EXISTS "${folder}/CMakeLists.txt")
            get_filename_component(name "${folder}" NAME)
            add_test(NAME gs_validate_${name} COMMAND gs_cli validate --official "${folder}")
        endif()
    endforeach()
    add_test(NAME gs_validate_pc-clock COMMAND gs_cli validate --official "$<TARGET_FILE_DIR:gs_ext_pc_clock>")
    add_test(NAME gs_validate_esp32 COMMAND gs_cli validate --official "$<TARGET_FILE_DIR:gs_ext_esp32>")

    set(gs_cli_sample_package "${CMAKE_CURRENT_BINARY_DIR}/samples/example.pc-clock-test.gsext")
    add_test(NAME gs_pack_pc-clock_sample
             COMMAND "${CMAKE_COMMAND}" "-DGS=$<TARGET_FILE:gs_cli>" "-DSOURCE=$<TARGET_FILE_DIR:gs_ext_pc_clock>"
                     -DID=example.pc-clock "-DOUT=${gs_cli_sample_package}"
                     -P "${PROJECT_SOURCE_DIR}/cmake/PackSample.cmake")
    set_tests_properties(gs_pack_pc-clock_sample PROPERTIES FIXTURES_SETUP gs_cli_packed_sample)
    add_test(NAME gs_validate_pc-clock_sample COMMAND gs_cli validate "${gs_cli_sample_package}")
    set_tests_properties(gs_validate_pc-clock_sample PROPERTIES PASS_REGULAR_EXPRESSION
                                                                "example[.]pc-clock [0-9.]+: (ok|0 error)"
                                                                FIXTURES_REQUIRED gs_cli_packed_sample)

    set(gs_cli_test_data "${PROJECT_SOURCE_DIR}/tests/data/extensions")
    set(gs_cli_test_package "${CMAKE_CURRENT_BINARY_DIR}/crt-scanlines-test.gsext")

    add_test(NAME gs_cli_validate_broken_shader COMMAND gs_cli validate "${gs_cli_test_data}/broken-shader")
    set_tests_properties(gs_cli_validate_broken_shader PROPERTIES PASS_REGULAR_EXPRESSION "filter-compile-failed")
    add_test(NAME gs_cli_validate_broken_shader_json COMMAND gs_cli validate "${gs_cli_test_data}/broken-shader" --json)
    set_tests_properties(gs_cli_validate_broken_shader_json PROPERTIES PASS_REGULAR_EXPRESSION "\"ok\": false")
    add_test(NAME gs_cli_validate_broken_shader_exit COMMAND gs_cli validate "${gs_cli_test_data}/broken-shader")
    set_tests_properties(gs_cli_validate_broken_shader_exit PROPERTIES WILL_FAIL TRUE)

    add_test(NAME gs_cli_validate_missing_license COMMAND gs_cli validate "${gs_cli_test_data}/missing-license")
    set_tests_properties(gs_cli_validate_missing_license PROPERTIES PASS_REGULAR_EXPRESSION
                                                                   "missing-field at [$][.]license")
    add_test(NAME gs_cli_validate_missing_license_exit COMMAND gs_cli validate "${gs_cli_test_data}/missing-license")
    set_tests_properties(gs_cli_validate_missing_license_exit PROPERTIES WILL_FAIL TRUE)

    add_test(NAME gs_cli_usage COMMAND gs_cli)
    set_tests_properties(gs_cli_usage PROPERTIES PASS_REGULAR_EXPRESSION "usage:")
    add_test(NAME gs_cli_usage_exit COMMAND gs_cli)
    set_tests_properties(gs_cli_usage_exit PROPERTIES WILL_FAIL TRUE)

    add_test(NAME gs_cli_version COMMAND gs_cli --version)
    set_tests_properties(gs_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "^gs [0-9]")

    add_test(NAME gs_cli_mcp_usage COMMAND gs_cli mcp extra)
    set_tests_properties(gs_cli_mcp_usage PROPERTIES PASS_REGULAR_EXPRESSION "usage: gs mcp")

    add_test(NAME gs_script_api_current COMMAND gs_cli api check "${PROJECT_SOURCE_DIR}/sdk")

    set(gs_cli_script_project "${CMAKE_CURRENT_BINARY_DIR}/script-project")
    set(gs_cli_broken_script "${PROJECT_SOURCE_DIR}/tests/data/scripts/broken")
    add_test(NAME gs_cli_new_script_clean COMMAND "${CMAKE_COMMAND}" -E rm -rf "${gs_cli_script_project}")
    set_tests_properties(gs_cli_new_script_clean PROPERTIES FIXTURES_SETUP gs_cli_script_clean)
    add_test(NAME gs_cli_new_script COMMAND gs_cli new "${gs_cli_script_project}")
    set_tests_properties(gs_cli_new_script PROPERTIES PASS_REGULAR_EXPRESSION "created .*src/main[.]ts"
                                                      FIXTURES_REQUIRED gs_cli_script_clean
                                                      FIXTURES_SETUP gs_cli_script_project)
    add_test(NAME gs_cli_check_new_script COMMAND gs_cli check "${gs_cli_script_project}")
    set_tests_properties(gs_cli_check_new_script PROPERTIES PASS_REGULAR_EXPRESSION "no problems"
                                                            FIXTURES_REQUIRED gs_cli_script_project)
    add_test(NAME gs_cli_check_broken_script_erase COMMAND gs_cli check "${gs_cli_broken_script}")
    set_tests_properties(gs_cli_check_broken_script_erase PROPERTIES PASS_REGULAR_EXPRESSION
                                                                     "src/main[.]ts:1:1: error GS0001 ")
    add_test(NAME gs_cli_check_broken_script_types COMMAND gs_cli check "${gs_cli_broken_script}")
    set_tests_properties(gs_cli_check_broken_script_types PROPERTIES PASS_REGULAR_EXPRESSION
                                                                     "src/main[.]ts:2:7: error TS2322 ")
    add_test(NAME gs_cli_check_broken_script_exit COMMAND gs_cli check "${gs_cli_broken_script}" --json)
    set_tests_properties(gs_cli_check_broken_script_exit PROPERTIES WILL_FAIL TRUE)
    add_test(NAME gs_cli_check_busy_loop COMMAND gs_cli check "${PROJECT_SOURCE_DIR}/tests/data/scripts/busy")
    set_tests_properties(gs_cli_check_busy_loop PROPERTIES PASS_REGULAR_EXPRESSION
                                                           "src/main[.]ts:3:5: warning GS-CHECK-101 [^\n]*\n1 problem\n")
    add_test(NAME gs_cli_check_busy_loop_exit COMMAND gs_cli check "${PROJECT_SOURCE_DIR}/tests/data/scripts/busy")
    add_test(NAME gs_cli_check_sleep_before_find
             COMMAND gs_cli check "${PROJECT_SOURCE_DIR}/tests/data/scripts/sleep-find")
    set_tests_properties(gs_cli_check_sleep_before_find PROPERTIES PASS_REGULAR_EXPRESSION
                         "src/main[.]ts:3:5: warning GS-CHECK-102 [^\n]*\nsrc/main[.]ts:13:9: warning GS-CHECK-102 [^\n]*\n2 problems\n")
    set(gs_cli_until_bind "${PROJECT_SOURCE_DIR}/tests/data/scripts/until-bind")
    add_test(NAME gs_cli_check_until_bind COMMAND gs_cli check "${gs_cli_until_bind}")
    set_tests_properties(gs_cli_check_until_bind PROPERTIES PASS_REGULAR_EXPRESSION "no problems")
    add_test(NAME gs_cli_test_until_bind COMMAND gs_cli test "${gs_cli_until_bind}")
    set_tests_properties(gs_cli_test_until_bind PROPERTIES PASS_REGULAR_EXPRESSION "1 passed, 0 failed")

    set(gs_cli_profile_demo "${PROJECT_SOURCE_DIR}/tests/data/scripts/profile-demo")
    set(gs_cli_profile_project "${CMAKE_CURRENT_BINARY_DIR}/profile-project")
    add_test(NAME gs_cli_check_profile_folder COMMAND gs_cli check "${gs_cli_profile_demo}")
    set_tests_properties(gs_cli_check_profile_folder PROPERTIES PASS_REGULAR_EXPRESSION "no problems")
    foreach(gs_cli_await IN ITEMS top-level-await top-level-await-old)
        add_test(NAME gs_cli_check_${gs_cli_await}
                 COMMAND gs_cli check "${PROJECT_SOURCE_DIR}/tests/data/scripts/${gs_cli_await}")
        set_tests_properties(gs_cli_check_${gs_cli_await} PROPERTIES PASS_REGULAR_EXPRESSION "no problems")
    endforeach()
    foreach(gs_cli_component IN ITEMS toggle-hold double-tap burst in-turn show-all-hints)
        add_test(NAME gs_cli_check_component_${gs_cli_component}
                 COMMAND gs_cli check "${PROJECT_SOURCE_DIR}/examples/components/${gs_cli_component}")
        set_tests_properties(gs_cli_check_component_${gs_cli_component} PROPERTIES
                             PASS_REGULAR_EXPRESSION "no problems")
    endforeach()
    foreach(gs_cli_component IN ITEMS quick-hold quick-hold-plus jump combo combo-plus volley)
        add_test(NAME gs_cli_check_component_${gs_cli_component}
                 COMMAND gs_cli check "${PROJECT_SOURCE_DIR}/tests/data/components/${gs_cli_component}")
        set_tests_properties(gs_cli_check_component_${gs_cli_component} PROPERTIES
                             PASS_REGULAR_EXPRESSION "no problems")
    endforeach()
    add_test(NAME gs_cli_run_replay
             COMMAND gs_cli run "${gs_cli_profile_demo}" --replay "${gs_cli_profile_demo}/sessions/basic.json")
    set_tests_properties(gs_cli_run_replay PROPERTIES PASS_REGULAR_EXPRESSION "160 control fire up")
    add_test(NAME gs_cli_run_replay_end
             COMMAND gs_cli run "${gs_cli_profile_demo}" --replay "${gs_cli_profile_demo}/sessions/basic.json")
    set_tests_properties(gs_cli_run_replay_end PROPERTIES PASS_REGULAR_EXPRESSION "ended at 700 ms")
    add_test(NAME gs_cli_run_replay_json
             COMMAND gs_cli run "${gs_cli_profile_demo}" --replay "${gs_cli_profile_demo}/sessions/basic.json" --json)
    set_tests_properties(gs_cli_run_replay_json PROPERTIES PASS_REGULAR_EXPRESSION "\"ok\": true")
    add_test(NAME gs_cli_test_profile COMMAND gs_cli test "${gs_cli_profile_demo}")
    set_tests_properties(gs_cli_test_profile PROPERTIES PASS_REGULAR_EXPRESSION "1 passed, 0 failed")
    add_test(NAME gs_cli_new_profile_clean COMMAND "${CMAKE_COMMAND}" -E rm -rf "${gs_cli_profile_project}")
    set_tests_properties(gs_cli_new_profile_clean PROPERTIES FIXTURES_SETUP gs_cli_profile_clean)
    add_test(NAME gs_cli_new_profile
             COMMAND gs_cli new "${gs_cli_profile_project}" --profile "${gs_cli_profile_demo}/profile.json")
    set_tests_properties(gs_cli_new_profile PROPERTIES PASS_REGULAR_EXPRESSION "created .*logic[.]ts"
                                                       FIXTURES_REQUIRED gs_cli_profile_clean
                                                       FIXTURES_SETUP gs_cli_profile_project)
    add_test(NAME gs_cli_check_new_profile COMMAND gs_cli check "${gs_cli_profile_project}")
    set_tests_properties(gs_cli_check_new_profile PROPERTIES PASS_REGULAR_EXPRESSION "no problems"
                                                             FIXTURES_REQUIRED gs_cli_profile_project)
    add_test(NAME gs_cli_types_profile COMMAND gs_cli types "${gs_cli_profile_project}")
    set_tests_properties(gs_cli_types_profile PROPERTIES PASS_REGULAR_EXPRESSION
                                                         "wrote types/gamescrcpy[.]d[.]ts, types/profile[.]d[.]ts"
                                                         FIXTURES_REQUIRED gs_cli_profile_project)

    set(gs_cli_profile_res "${PROJECT_SOURCE_DIR}/tests/data/scripts/profile-res")
    set(gs_cli_profile_res_typo "${PROJECT_SOURCE_DIR}/tests/data/scripts/profile-res-typo")
    add_test(NAME gs_cli_check_resources COMMAND gs_cli check "${gs_cli_profile_res}")
    set_tests_properties(gs_cli_check_resources PROPERTIES PASS_REGULAR_EXPRESSION "no problems")
    add_test(NAME gs_cli_check_resources_typo COMMAND gs_cli check "${gs_cli_profile_res_typo}")
    set_tests_properties(gs_cli_check_resources_typo PROPERTIES PASS_REGULAR_EXPRESSION
                                                                "logic[.]ts:2:[0-9]+: error TS2345 ")
    add_test(NAME gs_cli_check_resources_typo_exit COMMAND gs_cli check "${gs_cli_profile_res_typo}" --json)
    set_tests_properties(gs_cli_check_resources_typo_exit PROPERTIES WILL_FAIL TRUE)
    add_test(NAME gs_cli_run_resources_find
             COMMAND gs_cli run "${gs_cli_profile_res}" --replay "${gs_cli_profile_res}/sessions/basic.json")
    set_tests_properties(gs_cli_run_resources_find PROPERTIES PASS_REGULAR_EXPRESSION "100 vision find startButton")
    add_test(NAME gs_cli_run_resources_color
             COMMAND gs_cli run "${gs_cli_profile_res}" --replay "${gs_cli_profile_res}/sessions/basic.json")
    set_tests_properties(gs_cli_run_resources_color PROPERTIES PASS_REGULAR_EXPRESSION
                                                               "100 vision findColor #F5C542")
    add_test(NAME gs_cli_run_resources_names
             COMMAND gs_cli run "${gs_cli_profile_res}" --replay "${gs_cli_profile_res}/sessions/basic.json")
    set_tests_properties(gs_cli_run_resources_names PROPERTIES PASS_REGULAR_EXPRESSION
                                                               "100 # log info seen true 0 true true 0[.]12 2")

    set(gs_cli_import_api "${PROJECT_SOURCE_DIR}/tests/data/scripts/import-api")
    add_test(NAME gs_cli_check_import_api COMMAND gs_cli check "${gs_cli_import_api}")
    set_tests_properties(gs_cli_check_import_api PROPERTIES PASS_REGULAR_EXPRESSION "no problems")
    add_test(NAME gs_cli_check_import_api_typo
             COMMAND gs_cli check "${PROJECT_SOURCE_DIR}/tests/data/scripts/import-api-typo")
    set_tests_properties(gs_cli_check_import_api_typo PROPERTIES PASS_REGULAR_EXPRESSION
                                                                 "src/main[.]ts:1:10: error TS2305 ")
    add_test(NAME gs_cli_test_import_api COMMAND gs_cli test "${gs_cli_import_api}")
    set_tests_properties(gs_cli_test_import_api PROPERTIES PASS_REGULAR_EXPRESSION "1 passed, 0 failed")
    add_test(NAME gs_cli_run_import_api
             COMMAND gs_cli run "${gs_cli_import_api}" --replay "${gs_cli_import_api}/sessions/basic.json")
    set_tests_properties(gs_cli_run_import_api PROPERTIES PASS_REGULAR_EXPRESSION
                                                          "160 # log info pixel null false false Toggle")

    set(gs_cli_recording_pick "${PROJECT_SOURCE_DIR}/tests/data/scripts/recording-pick")
    set(gs_cli_recording "${CMAKE_CURRENT_BINARY_DIR}/recording-pick.gsrec")
    add_test(NAME gs_cli_recording_copy_session
             COMMAND "${CMAKE_COMMAND}" -E copy_directory "${gs_cli_recording_pick}/demo.gsrec" "${gs_cli_recording}")
    set_tests_properties(gs_cli_recording_copy_session PROPERTIES FIXTURES_SETUP gs_cli_recording)
    add_test(NAME gs_cli_recording_copy_video
             COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/tests/data/video/emulator_h264.media"
                     "${gs_cli_recording}/video.media")
    set_tests_properties(gs_cli_recording_copy_video PROPERTIES FIXTURES_SETUP gs_cli_recording
                                                                DEPENDS gs_cli_recording_copy_session)
    add_test(NAME gs_cli_run_recording COMMAND gs_cli run "${gs_cli_recording_pick}" --replay "${gs_cli_recording}")
    set_tests_properties(gs_cli_run_recording PROPERTIES PASS_REGULAR_EXPRESSION "500 # log info picked a color"
                                                         FIXTURES_REQUIRED gs_cli_recording)
    add_test(NAME gs_cli_run_recording_end COMMAND gs_cli run "${gs_cli_recording_pick}" --replay "${gs_cli_recording}")
    set_tests_properties(gs_cli_run_recording_end PROPERTIES PASS_REGULAR_EXPRESSION "ended at [0-9.]+ ms\n"
                                                             FIXTURES_REQUIRED gs_cli_recording)
    add_test(NAME gs_cli_check_recording_profile COMMAND gs_cli check "${gs_cli_recording_pick}")
    set_tests_properties(gs_cli_check_recording_profile PROPERTIES PASS_REGULAR_EXPRESSION "no problems")

    add_test(NAME gs_cli_pack_official
             COMMAND gs_cli pack --official "${PROJECT_SOURCE_DIR}/examples/extensions/crt-scanlines" -o
                     "${gs_cli_test_package}")
    set_tests_properties(gs_cli_pack_official PROPERTIES PASS_REGULAR_EXPRESSION "packed .*crt-scanlines-test[.]gsext"
                                                         FIXTURES_SETUP gs_cli_packed_crt_scanlines)
    add_test(NAME gs_cli_validate_packed COMMAND gs_cli validate --official "${gs_cli_test_package}")
    set_tests_properties(gs_cli_validate_packed PROPERTIES FIXTURES_REQUIRED gs_cli_packed_crt_scanlines)

    add_test(NAME gs_cli_compile_timeout_invalid
             COMMAND gs_cli validate "${gs_cli_test_data}/broken-shader" --compile-timeout 0)
    set_tests_properties(gs_cli_compile_timeout_invalid PROPERTIES PASS_REGULAR_EXPRESSION "usage:")

    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(gs_cli_test_bundles "${CMAKE_CURRENT_BINARY_DIR}/bundles")
    add_test(NAME gs_cli_make_bundles
             COMMAND Python3::Interpreter "${gs_cli_test_data}/make_bundles.py" "${gs_cli_test_bundles}")
    set_tests_properties(gs_cli_make_bundles PROPERTIES FIXTURES_SETUP gs_cli_bundles)

    add_test(NAME gs_cli_validate_bundle COMMAND gs_cli validate "${gs_cli_test_bundles}/good.gsbundle")
    add_test(NAME gs_cli_validate_bundle_json
             COMMAND gs_cli validate "${gs_cli_test_bundles}/good.gsbundle" --json --compile-timeout 60)
    set_tests_properties(gs_cli_validate_bundle_json PROPERTIES PASS_REGULAR_EXPRESSION
                                                               "\"builtin\": true,[^\"]+\"checked\": false")

    add_test(NAME gs_cli_validate_bundle_missing_index
             COMMAND gs_cli validate "${gs_cli_test_bundles}/missing-index.gsbundle")
    set_tests_properties(gs_cli_validate_bundle_missing_index PROPERTIES PASS_REGULAR_EXPRESSION
                                                                        "bundle-index-missing at [$]: bundle[.]json")
    add_test(NAME gs_cli_validate_bundle_missing_index_exit
             COMMAND gs_cli validate "${gs_cli_test_bundles}/missing-index.gsbundle")
    set_tests_properties(gs_cli_validate_bundle_missing_index_exit PROPERTIES WILL_FAIL TRUE)

    add_test(NAME gs_cli_validate_bundle_mismatch COMMAND gs_cli validate "${gs_cli_test_bundles}/mismatch.gsbundle")
    set_tests_properties(
        gs_cli_validate_bundle_mismatch
        PROPERTIES PASS_REGULAR_EXPRESSION
                   "bundle-package-mismatch at extensions/gstest[.]bundle-tint-1[.]0[.]0[.]gsext: [^ ]+ 1[.]0[.]1")
    add_test(NAME gs_cli_validate_bundle_mismatch_exit
             COMMAND gs_cli validate "${gs_cli_test_bundles}/mismatch.gsbundle")
    set_tests_properties(gs_cli_validate_bundle_mismatch_exit PROPERTIES WILL_FAIL TRUE)

    set_tests_properties(
        gs_cli_validate_bundle gs_cli_validate_bundle_json gs_cli_validate_bundle_missing_index
        gs_cli_validate_bundle_missing_index_exit gs_cli_validate_bundle_mismatch gs_cli_validate_bundle_mismatch_exit
        PROPERTIES FIXTURES_REQUIRED gs_cli_bundles)
endif()
