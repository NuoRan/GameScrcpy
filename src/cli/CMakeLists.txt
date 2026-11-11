add_library(gs_mockhost STATIC MockHost.cpp MockHost.h)
target_include_directories(gs_mockhost PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gs_mockhost PUBLIC gs::ext gs::nlohmann_json PRIVATE gs::base gs::exthost gs::platform gs::script)
gs_apply_compiler_policy(gs_mockhost)

add_executable(gs_cli main.cpp ExtCommands.cpp ExtCommands.h ExtTemplates.cpp ExtTemplates.h McpRelay.cpp McpRelay.h
                      ScriptProject.cpp ScriptProject.h ScriptRun.cpp ScriptTools.cpp ScriptTools.h KeyTools.cpp
                      KeyTools.h)
set_target_properties(gs_cli PROPERTIES OUTPUT_NAME gs)
target_link_libraries(gs_cli PRIVATE gs_mockhost gs::base gs::ext gs::input gs::platform gs::record gs::script gs::tsstrip gs::nlohmann_json d3dcompiler)
gs_apply_compiler_policy(gs_cli)
add_dependencies(gs_cli gs_exthost_program)

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

    foreach(gs_cli_example IN ITEMS deck-bridge voice-commands event-log tour-and-help)
        set(gs_cli_example_folder "${PROJECT_SOURCE_DIR}/examples/extensions/${gs_cli_example}")
        set(gs_cli_example_package "${CMAKE_CURRENT_BINARY_DIR}/example-${gs_cli_example}.gsext")
        add_test(NAME gs_cli_check_example_${gs_cli_example} COMMAND gs_cli check "${gs_cli_example_folder}")
        set_tests_properties(gs_cli_check_example_${gs_cli_example} PROPERTIES PASS_REGULAR_EXPRESSION "no problems")
        add_test(NAME gs_cli_pack_example_${gs_cli_example}
                 COMMAND gs_cli pack "${gs_cli_example_folder}" -o "${gs_cli_example_package}")
        set_tests_properties(gs_cli_pack_example_${gs_cli_example} PROPERTIES
                             PASS_REGULAR_EXPRESSION "packed .*example-${gs_cli_example}[.]gsext"
                             FIXTURES_SETUP gs_cli_packed_example_${gs_cli_example})
        add_test(NAME gs_cli_validate_example_${gs_cli_example} COMMAND gs_cli validate "${gs_cli_example_package}")
        set_tests_properties(gs_cli_validate_example_${gs_cli_example} PROPERTIES
                             FIXTURES_REQUIRED gs_cli_packed_example_${gs_cli_example})
    endforeach()

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

    set(gs_cli_keys "${CMAKE_CURRENT_BINARY_DIR}/keys")
    add_test(NAME gs_cli_keygen_clean COMMAND "${CMAKE_COMMAND}" -E rm -rf "${gs_cli_keys}")
    set_tests_properties(gs_cli_keygen_clean PROPERTIES FIXTURES_SETUP gs_cli_keys_clean)
    add_test(NAME gs_cli_keygen COMMAND gs_cli ext keygen "${gs_cli_keys}/author.key")
    set_tests_properties(gs_cli_keygen PROPERTIES PASS_REGULAR_EXPRESSION
                                                  "\"authorKey\": \"RW[A-Za-z0-9+/]+\""
                                                  FIXTURES_REQUIRED gs_cli_keys_clean
                                                  FIXTURES_SETUP gs_cli_keys)
    add_test(NAME gs_cli_keygen_kept COMMAND gs_cli ext keygen "${gs_cli_keys}/author.key")
    set_tests_properties(gs_cli_keygen_kept PROPERTIES WILL_FAIL TRUE FIXTURES_REQUIRED gs_cli_keys)
    add_test(NAME gs_cli_keygen_usage COMMAND gs_cli ext keygen)
    set_tests_properties(gs_cli_keygen_usage PROPERTIES PASS_REGULAR_EXPRESSION "usage: gs ext keygen")
    add_test(NAME gs_cli_sign_package
             COMMAND "${CMAKE_COMMAND}" "-DGS=$<TARGET_FILE:gs_cli>" "-DSOURCE=${gs_cli_test_data}/signed-theme"
                     "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/signed-pack" -P "${PROJECT_SOURCE_DIR}/cmake/SignedPackCheck.cmake")
    add_test(NAME gs_cli_validate_architectures
             COMMAND "${CMAKE_COMMAND}" "-DGS=$<TARGET_FILE:gs_cli>" "-DDLL=$<TARGET_FILE:gs_ext_pc_clock>"
                     "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/architectures"
                     -P "${PROJECT_SOURCE_DIR}/cmake/NativeArchitecturesCheck.cmake")

    set(gs_cli_ext_data "${PROJECT_SOURCE_DIR}/tests/data/ext-test")
    set(gs_cli_ext_root "${CMAKE_CURRENT_BINARY_DIR}/ext-test")
    set(gs_cli_ext_mock "${gs_cli_ext_root}/native")
    set(gs_cli_ext_old "${gs_cli_ext_root}/old")
    set(gs_cli_ext_expect "${PROJECT_SOURCE_DIR}/cmake/ExpectExit.cmake")
    add_test(NAME gs_cli_ext_clean COMMAND "${CMAKE_COMMAND}" -E rm -rf "${gs_cli_ext_root}")
    set_tests_properties(gs_cli_ext_clean PROPERTIES FIXTURES_SETUP gs_cli_ext_clean)
    add_test(NAME gs_cli_ext_copy COMMAND "${CMAKE_COMMAND}" -E copy_directory "${gs_cli_ext_data}/native" "${gs_cli_ext_mock}")
    add_test(NAME gs_cli_ext_copy_old COMMAND "${CMAKE_COMMAND}" -E copy_directory "${gs_cli_ext_data}/native" "${gs_cli_ext_old}")
    set_tests_properties(gs_cli_ext_copy gs_cli_ext_copy_old PROPERTIES FIXTURES_REQUIRED gs_cli_ext_clean
                                                                         FIXTURES_SETUP gs_cli_ext_fixture)
    add_test(NAME gs_cli_ext_copy_dll
             COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:gs_test_native>" "${gs_cli_ext_mock}/native.dll")
    set_tests_properties(gs_cli_ext_copy_dll PROPERTIES FIXTURES_SETUP gs_cli_ext_fixture DEPENDS gs_cli_ext_copy)
    add_test(NAME gs_cli_ext_copy_old_dll
             COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:gs_test_native_old>" "${gs_cli_ext_old}/native.dll")
    set_tests_properties(gs_cli_ext_copy_old_dll PROPERTIES FIXTURES_SETUP gs_cli_ext_fixture DEPENDS gs_cli_ext_copy_old)

    add_test(NAME gs_cli_ext_test COMMAND gs_cli ext test "${gs_cli_ext_mock}")
    set_tests_properties(gs_cli_ext_test PROPERTIES PASS_REGULAR_EXPRESSION "\n6 passed, 0 failed\n")
    add_test(NAME gs_cli_ext_test_exit
             COMMAND "${CMAKE_COMMAND}" "-DGS=$<TARGET_FILE:gs_cli>" -DEXPECTED=0 -DARG1=ext -DARG2=test
                     "-DARG3=${gs_cli_ext_mock}" -P "${gs_cli_ext_expect}")
    add_test(NAME gs_cli_ext_test_hosted COMMAND gs_cli ext test "${gs_cli_ext_mock}" --hosted)
    set_tests_properties(gs_cli_ext_test_hosted PROPERTIES PASS_REGULAR_EXPRESSION "\n6 passed, 0 failed\n")
    add_test(NAME gs_cli_ext_test_hosted_json COMMAND gs_cli ext test "${gs_cli_ext_mock}" --hosted --json)
    set_tests_properties(gs_cli_ext_test_hosted_json PROPERTIES PASS_REGULAR_EXPRESSION
                                                                "\"where\": \"native[.]dll in gs-exthost[.]exe\"")
    add_test(NAME gs_cli_ext_test_json COMMAND gs_cli ext test "${gs_cli_ext_mock}" --json)
    set_tests_properties(gs_cli_ext_test_json PROPERTIES PASS_REGULAR_EXPRESSION "^{\n  \"ok\": true,")
    add_test(NAME gs_cli_ext_test_failing
             COMMAND gs_cli ext test "${gs_cli_ext_mock}" --test "${gs_cli_ext_data}/failing.json")
    set_tests_properties(gs_cli_ext_test_failing PROPERTIES PASS_REGULAR_EXPRESSION
                                                            "FAIL failing [(][^\n]*\n[^\n]*\n  FAIL  #2 ")
    add_test(NAME gs_cli_ext_test_failing_exit
             COMMAND "${CMAKE_COMMAND}" "-DGS=$<TARGET_FILE:gs_cli>" -DEXPECTED=1 -DARG1=ext -DARG2=test
                     "-DARG3=${gs_cli_ext_mock}" -DARG4=--test "-DARG5=${gs_cli_ext_data}/failing.json"
                     -P "${gs_cli_ext_expect}")
    add_test(NAME gs_cli_ext_test_unloadable COMMAND gs_cli ext test "${gs_cli_ext_old}")
    set_tests_properties(gs_cli_ext_test_unloadable PROPERTIES PASS_REGULAR_EXPRESSION
                                                               "cannot be loaded: native[.]dll cannot be loaded: the DLL was made for an older GameScrcpy")
    add_test(NAME gs_cli_ext_test_unloadable_exit
             COMMAND "${CMAKE_COMMAND}" "-DGS=$<TARGET_FILE:gs_cli>" -DEXPECTED=2 -DARG1=ext -DARG2=test
                     "-DARG3=${gs_cli_ext_old}" -P "${gs_cli_ext_expect}")
    add_test(NAME gs_cli_ext_test_missing COMMAND gs_cli ext test "${gs_cli_ext_root}/nowhere")
    set_tests_properties(gs_cli_ext_test_missing PROPERTIES PASS_REGULAR_EXPRESSION "does not exist")
    add_test(NAME gs_cli_ext_command COMMAND gs_cli ext "${gs_cli_ext_mock}" count --steps 3)
    set_tests_properties(gs_cli_ext_command PROPERTIES PASS_REGULAR_EXPRESSION "\"counted\": 3")
    add_test(NAME gs_cli_ext_command_json COMMAND gs_cli ext "${gs_cli_ext_mock}" count --steps=2 --json)
    set_tests_properties(gs_cli_ext_command_json PROPERTIES PASS_REGULAR_EXPRESSION
                                                            "\"result\": {\n    \"counted\": 2\n  }")
    add_test(NAME gs_cli_ext_command_hosted COMMAND gs_cli ext "${gs_cli_ext_mock}" count --steps 2 --hosted --json)
    set_tests_properties(gs_cli_ext_command_hosted PROPERTIES PASS_REGULAR_EXPRESSION "\"counted\": 2")
    add_test(NAME gs_cli_ext_command_help COMMAND gs_cli ext "${gs_cli_ext_mock}" --help)
    set_tests_properties(gs_cli_ext_command_help PROPERTIES PASS_REGULAR_EXPRESSION
                                                            "count +Counts to a number[.]\n +--steps <number>")
    add_test(NAME gs_cli_ext_command_unknown_option COMMAND gs_cli ext "${gs_cli_ext_mock}" count --steps 1 --speed 2)
    set_tests_properties(gs_cli_ext_command_unknown_option PROPERTIES PASS_REGULAR_EXPRESSION "count takes no --speed")
    add_test(NAME gs_cli_ext_command_needs_option COMMAND gs_cli ext "${gs_cli_ext_mock}" count)
    set_tests_properties(gs_cli_ext_command_needs_option PROPERTIES PASS_REGULAR_EXPRESSION "count needs --steps")
    add_test(NAME gs_cli_ext_command_failing COMMAND gs_cli ext "${gs_cli_ext_mock}" count --steps 1 --fail 1)
    set_tests_properties(gs_cli_ext_command_failing PROPERTIES PASS_REGULAR_EXPRESSION "GS-API-065 asked to fail")
    add_test(NAME gs_cli_ext_command_failing_exit
             COMMAND "${CMAKE_COMMAND}" "-DGS=$<TARGET_FILE:gs_cli>" -DEXPECTED=1 -DARG1=ext "-DARG2=${gs_cli_ext_mock}"
                     -DARG3=count -DARG4=--steps -DARG5=1 -DARG6=--fail -DARG7=1 -P "${gs_cli_ext_expect}")
    set_tests_properties(
        gs_cli_ext_test gs_cli_ext_test_exit gs_cli_ext_test_hosted gs_cli_ext_test_hosted_json gs_cli_ext_test_json
        gs_cli_ext_test_failing gs_cli_ext_test_failing_exit gs_cli_ext_test_unloadable gs_cli_ext_test_unloadable_exit
        gs_cli_ext_command gs_cli_ext_command_json gs_cli_ext_command_hosted gs_cli_ext_command_help
        gs_cli_ext_command_unknown_option gs_cli_ext_command_needs_option gs_cli_ext_command_failing
        gs_cli_ext_command_failing_exit
        PROPERTIES FIXTURES_REQUIRED gs_cli_ext_fixture)

    set(gs_cli_ext_init "${CMAKE_CURRENT_BINARY_DIR}/ext-init")
    add_test(NAME gs_cli_ext_init_clean COMMAND "${CMAKE_COMMAND}" -E rm -rf "${gs_cli_ext_init}")
    set_tests_properties(gs_cli_ext_init_clean PROPERTIES FIXTURES_SETUP gs_cli_ext_init_clean)
    foreach(kind IN ITEMS native script theme)
        add_test(NAME gs_cli_ext_init_${kind}
                 COMMAND gs_cli ext init ${kind} "${gs_cli_ext_init}/${kind}" --id gstest.init-${kind})
        set_tests_properties(gs_cli_ext_init_${kind} PROPERTIES PASS_REGULAR_EXPRESSION "created "
                                                                FIXTURES_REQUIRED gs_cli_ext_init_clean
                                                                FIXTURES_SETUP gs_cli_ext_init_${kind})
    endforeach()
    foreach(kind IN ITEMS script theme)
        add_test(NAME gs_cli_ext_init_${kind}_validate COMMAND gs_cli validate "${gs_cli_ext_init}/${kind}")
        set_tests_properties(gs_cli_ext_init_${kind}_validate PROPERTIES
                             PASS_REGULAR_EXPRESSION "gstest[.]init-${kind} 0[.]1[.]0: (ok|0 error)"
                             FIXTURES_REQUIRED gs_cli_ext_init_${kind})
        add_test(NAME gs_cli_ext_init_${kind}_test COMMAND gs_cli ext test "${gs_cli_ext_init}/${kind}")
        set_tests_properties(gs_cli_ext_init_${kind}_test PROPERTIES PASS_REGULAR_EXPRESSION "\n1 passed, 0 failed\n"
                                                                     FIXTURES_REQUIRED gs_cli_ext_init_${kind})
    endforeach()
    add_test(NAME gs_cli_ext_init_script_check COMMAND gs_cli check "${gs_cli_ext_init}/script")
    set_tests_properties(gs_cli_ext_init_script_check PROPERTIES PASS_REGULAR_EXPRESSION "no problems"
                                                                 FIXTURES_REQUIRED gs_cli_ext_init_script)
    add_test(NAME gs_cli_ext_init_taken COMMAND gs_cli ext init theme "${gs_cli_ext_init}/theme")
    set_tests_properties(gs_cli_ext_init_taken PROPERTIES PASS_REGULAR_EXPRESSION "is not empty"
                                                          FIXTURES_REQUIRED gs_cli_ext_init_theme)
    set(gs_cli_ext_init_options -G "${CMAKE_GENERATOR}" -DCMAKE_BUILD_TYPE=Release
                                "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    if(CMAKE_MAKE_PROGRAM)
        list(APPEND gs_cli_ext_init_options "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND gs_cli_ext_init_options -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    add_test(NAME gs_cli_ext_init_native_configure
             COMMAND "${CMAKE_COMMAND}" -S "${gs_cli_ext_init}/native" -B "${gs_cli_ext_init}/native/.build"
                     ${gs_cli_ext_init_options})
    set_tests_properties(gs_cli_ext_init_native_configure PROPERTIES FIXTURES_REQUIRED gs_cli_ext_init_native
                                                                     FIXTURES_SETUP gs_cli_ext_init_native_configured)
    add_test(NAME gs_cli_ext_init_native_build
             COMMAND "${CMAKE_COMMAND}" --build "${gs_cli_ext_init}/native/.build" --config Release)
    set_tests_properties(gs_cli_ext_init_native_build PROPERTIES FIXTURES_REQUIRED gs_cli_ext_init_native_configured
                                                                 FIXTURES_SETUP gs_cli_ext_init_native_built)
    add_test(NAME gs_cli_ext_init_native_validate COMMAND gs_cli validate "${gs_cli_ext_init}/native")
    set_tests_properties(gs_cli_ext_init_native_validate PROPERTIES
                         PASS_REGULAR_EXPRESSION "gstest[.]init-native 0[.]1[.]0: (ok|0 error)"
                         FIXTURES_REQUIRED gs_cli_ext_init_native_built)
    add_test(NAME gs_cli_ext_init_native_test COMMAND gs_cli ext test "${gs_cli_ext_init}/native")
    set_tests_properties(gs_cli_ext_init_native_test PROPERTIES PASS_REGULAR_EXPRESSION "\n1 passed, 0 failed\n"
                                                                FIXTURES_REQUIRED gs_cli_ext_init_native_built)
    add_test(NAME gs_cli_ext_init_native_command COMMAND gs_cli ext "${gs_cli_ext_init}/native" countdown --from 4)
    set_tests_properties(gs_cli_ext_init_native_command PROPERTIES PASS_REGULAR_EXPRESSION "\"counted\": 4"
                                                                   FIXTURES_REQUIRED gs_cli_ext_init_native_built)
endif()
