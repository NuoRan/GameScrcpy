# Source rules that review alone does not hold (tools/check_source_rules.py: ADR-0037 R5, R9).
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_test(NAME gs_source_rules_selftest
    COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_source_rules.py" --self-test)
add_test(NAME gs_source_rules
    COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_source_rules.py" --root "${PROJECT_SOURCE_DIR}")
add_test(NAME gs_icons_generated
    COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/gen_icons.py" --check)
add_test(NAME gs_example_images_current
    COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/make_example_images.py" --check)
find_program(GS_POWERSHELL NAMES powershell pwsh)
if(GS_POWERSHELL)
    add_test(NAME gs_libusb_build_selftest
        COMMAND "${GS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${PROJECT_SOURCE_DIR}/tools/build_libusb.ps1" -SelfTest)
    set_tests_properties(gs_libusb_build_selftest PROPERTIES ENVIRONMENT_MODIFICATION "PSModulePath=unset:")
endif()

function(gs_add_unit_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
    add_executable(${name} unit/main.cpp ${arg_SOURCES})
    target_link_libraries(${name} PRIVATE gs::doctest ${arg_LIBRARIES})
    gs_apply_compiler_policy(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

gs_add_unit_test(gs_base_tests
    SOURCES
        unit/base/BuildInfoTests.cpp
        unit/base/ByteOrderTests.cpp
        unit/base/ClockTests.cpp
        unit/base/ExpectedTests.cpp
        unit/base/InplaceVectorTests.cpp
        unit/base/LogTests.cpp
        unit/base/SpscQueueTests.cpp
        unit/base/ThreadCheckerTests.cpp
    LIBRARIES
        gs::base
)
target_compile_definitions(gs_base_tests PRIVATE GS_EXPECTED_VERSION="${PROJECT_VERSION}")

add_executable(gs_atomic_write_child unit/platform/AtomicWriteChild.cpp)
target_link_libraries(gs_atomic_write_child PRIVATE gs::platform)
gs_apply_compiler_policy(gs_atomic_write_child)

gs_add_unit_test(gs_platform_tests
    SOURCES
        unit/platform/AtomicFileTests.cpp
        unit/platform/CredentialsTests.cpp
        unit/platform/CryptoTests.cpp
        unit/platform/CursorTests.cpp
        unit/platform/InputCaptureTests.cpp
        unit/platform/KeyboardLayoutTests.cpp
        unit/platform/RunningMarkerTests.cpp
        unit/platform/SpeechTests.cpp
        unit/platform/TextReaderTests.cpp
        unit/platform/UdpSocketTests.cpp
    LIBRARIES
        gs::platform
        gdi32
        user32
)
target_compile_definitions(gs_platform_tests PRIVATE
    GS_ATOMIC_WRITE_CHILD="$<TARGET_FILE:gs_atomic_write_child>"
)
target_include_directories(gs_platform_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(gs_platform_tests gs_atomic_write_child)

add_executable(gs_crash_child unit/platform/CrashChild.cpp)
target_link_libraries(gs_crash_child PRIVATE gs::platform)
gs_apply_compiler_policy(gs_crash_child)
target_sources(gs_platform_tests PRIVATE unit/platform/AcceptancePlatformTests.cpp)
target_compile_definitions(gs_platform_tests PRIVATE GS_CRASH_CHILD="$<TARGET_FILE:gs_crash_child>")
add_dependencies(gs_platform_tests gs_crash_child)

gs_add_unit_test(gs_protocol_tests
    SOURCES
        unit/protocol/DatagramTests.cpp
        unit/protocol/DirectMediaTests.cpp
        unit/protocol/GoldenVectorTests.cpp
        unit/protocol/JsonVisitors.h
        unit/protocol/ReedSolomonTests.cpp
        unit/protocol/ReliableTests.cpp
        unit/protocol/RobustnessTests.cpp
    LIBRARIES
        gs::protocol
        gs::nlohmann_json
)
target_compile_definitions(gs_protocol_tests PRIVATE
    GS_PROTOCOL_VECTOR_DIR="${PROJECT_SOURCE_DIR}/protocol/vectors"
)
target_include_directories(gs_protocol_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

gs_add_unit_test(gs_input_tests
    SOURCES
        unit/input/ControlsTests.cpp
        unit/input/DeviceEventsTests.cpp
        unit/input/EngineTests.cpp
        unit/input/ExtensionInputTests.cpp
        unit/input/GeometryTests.cpp
        unit/input/InputSourceTests.cpp
        unit/input/KeyHintsTests.cpp
        unit/input/ModuleTests.cpp
        unit/input/PadProfileTests.cpp
        unit/input/PadReportsTests.cpp
        unit/input/PointerAllocatorTests.cpp
        unit/input/ProfileDiffTests.cpp
        unit/input/ProfilePatchTests.cpp
        unit/input/ProfileTests.cpp
        unit/input/TouchFrameBuilderTests.cpp
        unit/input/TriggerStateTests.cpp
    LIBRARIES
        gs::input
)
target_sources(gs_input_tests PRIVATE unit/input/AcceptanceTests.cpp)

gs_add_unit_test(gs_ext_tests
    SOURCES
        unit/ext/ArchiveTests.cpp
        unit/ext/BundleTests.cpp
        unit/ext/FilterSourceTests.cpp
        unit/ext/ManifestTests.cpp
        unit/ext/PackageTests.cpp
        unit/ext/ResolutionTests.cpp
        unit/ext/TestFiles.h
        unit/ext/VersionTests.cpp
        unit/ext/MinisignTests.cpp
        unit/ext/NativeCheckTests.cpp
        unit/ext/PeImage.h
        unit/ext/SchemaTests.cpp
        unit/ext/SigningTests.cpp
    LIBRARIES
        gs::ext
        gs::miniz
        gs::nlohmann_json
)
target_compile_definitions(gs_ext_tests PRIVATE
    GS_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
    GS_TEST_NATIVE_INJECT="$<TARGET_FILE:gs_test_native_inject>"
)
add_dependencies(gs_ext_tests gs_test_native_inject)

add_library(gs_test_native SHARED fakes/native/TestNative.cpp)
target_include_directories(gs_test_native PRIVATE "${PROJECT_SOURCE_DIR}/sdk/include")
gs_apply_compiler_policy(gs_test_native)

add_library(gs_test_native_inject SHARED fakes/native/TestNativeInject.cpp)
target_include_directories(gs_test_native_inject PRIVATE "${PROJECT_SOURCE_DIR}/sdk/include")
target_link_libraries(gs_test_native_inject PRIVATE user32)
gs_apply_compiler_policy(gs_test_native_inject)

add_library(gs_test_native_sizes SHARED fakes/native/TestNativeSizes.cpp)
target_include_directories(gs_test_native_sizes PRIVATE "${PROJECT_SOURCE_DIR}/sdk/include")
gs_apply_compiler_policy(gs_test_native_sizes)

add_library(gs_test_native_old SHARED fakes/native/TestNativeOld.cpp)
gs_apply_compiler_policy(gs_test_native_old)

gs_add_unit_test(gs_hardware_tests
    SOURCES
        unit/hardware/HardwareTests.cpp
    LIBRARIES
        gs::hardware
)

gs_add_unit_test(gs_script_tests
    SOURCES
        unit/script/AiAskTests.cpp
        unit/script/ExtensionNodeTests.cpp
        unit/script/ExtensionServiceTests.cpp
        unit/script/HardwareApiTests.cpp
        unit/script/InputReadTests.cpp
        unit/script/LoadMeterTests.cpp
        unit/script/LogicHostTests.cpp
        unit/script/MediaApiTests.cpp
        unit/script/ModulePathTests.cpp
        unit/script/NativeHeaderTests.cpp
        unit/script/NetFilesApiTests.cpp
        unit/script/ReportTests.cpp
        unit/script/ResourcesTests.cpp
        unit/script/ScriptApiTests.cpp
        unit/script/ScriptFixture.h
        unit/script/ScriptHostTests.cpp
        unit/script/ScriptTimingTests.cpp
        unit/script/SessionTests.cpp
        unit/script/StackTraceTests.cpp
        unit/script/SystemPanelApiTests.cpp
        unit/script/TestRunTests.cpp
        unit/script/TextSearchTests.cpp
        unit/script/UiPanelTests.cpp
        unit/script/VisionEngineTests.cpp
        unit/script/WorldModelTests.cpp
    LIBRARIES
        gs::script
        gs::platform
        gs::nlohmann_json
)
target_include_directories(gs_script_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/script/src")
target_compile_definitions(gs_script_tests PRIVATE GS_SDK_DIR="${PROJECT_SOURCE_DIR}/sdk")
target_compile_definitions(gs_script_tests PRIVATE GS_EXAMPLE_COMPONENTS_DIR="${PROJECT_SOURCE_DIR}/examples/components")

gs_add_unit_test(gs_scripthost_tests
    SOURCES
        unit/script/ScriptFixture.h
        unit/scripthost/RemoteLogicHostTests.cpp
    LIBRARIES
        gs::scripthost
        gs::script
        ws2_32
)
target_include_directories(gs_scripthost_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(gs_scripthost_tests PRIVATE
    GS_SCRIPTHOST_PROGRAM="$<TARGET_FILE:gs_scripthost_program>"
)
add_dependencies(gs_scripthost_tests gs_scripthost_program)

gs_add_unit_test(gs_mockhost_tests
    SOURCES
        unit/cli/MockHostTests.cpp
    LIBRARIES
        gs_mockhost
        gs::ext
        gs::nlohmann_json
)
target_compile_definitions(gs_mockhost_tests PRIVATE
    GS_EXT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/ext-test"
    GS_TEST_NATIVE_DLL="$<TARGET_FILE:gs_test_native>"
    GS_TEST_NATIVE_OLD_DLL="$<TARGET_FILE:gs_test_native_old>"
    GS_EXTHOST_PROGRAM="$<TARGET_FILE:gs_exthost_program>"
)
add_dependencies(gs_mockhost_tests gs_test_native gs_test_native_old gs_exthost_program)

add_test(NAME gs_cli_check_renamed_control
    COMMAND gs_cli check "${CMAKE_CURRENT_SOURCE_DIR}/data/scripts/profile-renamed")
set_tests_properties(gs_cli_check_renamed_control PROPERTIES PASS_REGULAR_EXPRESSION
    "logic[.]ts:1:[0-9]+: error TS(2339|2551) [^\n]*\nlogic[.]ts:3:[0-9]+: error TS(2339|2551) [^\n]*\n2 problems\n")
add_test(NAME gs_cli_check_renamed_control_exit
    COMMAND gs_cli check "${CMAKE_CURRENT_SOURCE_DIR}/data/scripts/profile-renamed" --json)
set_tests_properties(gs_cli_check_renamed_control_exit PROPERTIES WILL_FAIL TRUE)

gs_add_unit_test(gs_tsstrip_tests
    SOURCES
        unit/tsstrip/StripTests.cpp
        unit/tsstrip/TokensTests.cpp
    LIBRARIES
        gs::tsstrip
)
target_include_directories(gs_tsstrip_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/tsstrip/src")

gs_add_unit_test(gs_adb_tests
    SOURCES
        unit/adb/AdbClientTests.cpp
        unit/adb/DeviceIdentityTests.cpp
        unit/adb/DeviceListTests.cpp
        unit/adb/DeviceTrackerTests.cpp
        unit/adb/FakeAdbServer.h
        unit/adb/ForwardListTests.cpp
        unit/adb/ServerLauncherTests.cpp
        unit/adb/ShellProtocolTests.cpp
        unit/adb/WirelessServicesTests.cpp
    LIBRARIES
        gs::adb
        ws2_32
)
target_sources(gs_adb_tests PRIVATE unit/adb/AcceptanceAdbTests.cpp)

gs_add_unit_test(gs_session_tests
    SOURCES
        fakes/FakeDeviceServer.h
        fakes/FakeServerHost.h
        fakes/RecordingListener.h
        unit/adb/FakeAdbServer.h
        unit/session/AdbServerHostTests.cpp
        unit/session/DatagramVectors.h
        unit/session/DirectHandshakeTests.cpp
        unit/session/DirectTransportTests.cpp
        unit/session/HandshakeTests.cpp
        unit/session/KeyFrameGateTests.cpp
        unit/session/ReceiverStatsTests.cpp
        unit/session/ReliableStreamTests.cpp
        unit/session/SessionTests.cpp
        unit/session/StreamTransportTests.cpp
    LIBRARIES
        gs::session
        gs::nlohmann_json
        ws2_32
)
target_include_directories(gs_session_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src/session/src"
)
target_compile_definitions(gs_session_tests PRIVATE
    GS_PROTOCOL_VECTOR_DIR="${PROJECT_SOURCE_DIR}/protocol/vectors"
)
target_sources(gs_session_tests PRIVATE unit/session/AcceptanceSessionTests.cpp)
target_compile_definitions(gs_session_tests PRIVATE
    GS_SERVER_PACKAGE_FILE="${PROJECT_SOURCE_DIR}/android/server/build/outputs/apk/release/server-release-unsigned.apk"
)

gs_add_unit_test(gs_media_tests
    SOURCES
        unit/media/AacRecordingTests.cpp
        unit/media/AudioPlayerTests.cpp
        unit/media/AudioRegulatorTests.cpp
        unit/media/ColorMatrixTests.cpp
        unit/media/FfmpegBuildTests.cpp
        unit/media/FfmpegLogTests.cpp
        unit/media/FrameSnapshotTests.cpp
        unit/media/FrameUploaderTests.cpp
        unit/media/LumaSamplerTests.cpp
        unit/media/RecordingTests.cpp
        unit/media/ThumbnailerTests.cpp
        unit/media/VideoDecoderTests.cpp
    LIBRARIES
        gs::media
        gs::record
        gs::script
        gs::vision
        FFmpeg::avcodec
        FFmpeg::swresample
        d3d11
)
target_compile_definitions(gs_media_tests PRIVATE
    GS_VIDEO_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/video"
    GS_FFMPEG_BUILDINFO="${PROJECT_SOURCE_DIR}/third_party/ffmpeg/BUILDINFO.md"
    GS_RECORDER_CHILD="$<TARGET_FILE:gs_recorder_child>"
)
target_include_directories(gs_media_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(gs_recorder_child unit/media/RecorderChild.cpp)
target_link_libraries(gs_recorder_child PRIVATE gs::record)
gs_apply_compiler_policy(gs_recorder_child)
add_dependencies(gs_media_tests gs_recorder_child)

gs_add_unit_test(gs_capture_tests
    SOURCES
        unit/capture/AudioPairingTests.cpp
        unit/capture/BgraFramesTests.cpp
        unit/capture/CaptureFormatTests.cpp
        unit/capture/CropDetectorTests.cpp
        unit/capture/EncoderTests.cpp
        unit/capture/FrameBuilderTests.cpp
        unit/capture/TouchMappingTests.cpp
        "${PROJECT_SOURCE_DIR}/src/app/HidTouchscreen.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/HidTouchscreen.h"
    LIBRARIES
        gs::capture
        gs::media
        gs::protocol
        d3d11
        ole32
)
target_include_directories(gs_capture_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
target_compile_definitions(gs_capture_tests PRIVATE
    GS_CAPTURE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/capture"
)

gs_add_unit_test(gs_vision_tests
    SOURCES
        unit/vision/BenchmarkTests.cpp
        unit/vision/ColorTests.cpp
        unit/vision/FixtureTests.cpp
        unit/vision/ImageTests.cpp
        unit/vision/MatcherTests.cpp
        unit/vision/TestImages.h
    LIBRARIES
        gs::vision
)
target_compile_definitions(gs_vision_tests PRIVATE
    GS_VISION_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/vision"
)

add_executable(gs_vision_fixtures "${PROJECT_SOURCE_DIR}/tools/vision_fixtures.cpp")
target_link_libraries(gs_vision_fixtures PRIVATE gs::media gs::protocol gs::vision)
gs_apply_compiler_policy(gs_vision_fixtures)
add_test(NAME gs_vision_fixtures_current
    COMMAND gs_vision_fixtures check "${PROJECT_SOURCE_DIR}/tests/data/video" "${PROJECT_SOURCE_DIR}/tests/data/vision")

if(GS_BUILD_APP)
    gs_add_unit_test(gs_app_tests
        SOURCES
            unit/adb/FakeAdbServer.h
            unit/app/AiAssistantTests.cpp
            unit/app/AiChatsTests.cpp
            unit/app/AiConversationTests.cpp
            unit/app/AiDraftsTests.cpp
            unit/app/AiNetworkTests.cpp
            unit/app/AiProvidersTests.cpp
            unit/app/AiScriptsTests.cpp
            unit/app/AiServiceTests.cpp
            unit/app/AoaTouchSinkTests.cpp
            unit/app/AppInfoTests.cpp
            unit/app/AppActionsTests.cpp
            unit/app/AppCatalogTests.cpp
            unit/app/AppEventsTests.cpp
            unit/app/AppSettingsPaletteTests.cpp
            unit/app/AppSettingsTests.cpp
            unit/app/BlueprintModelTests.cpp
            unit/app/CaptureCardsTests.cpp
            unit/app/CaptureFeedTests.cpp
            unit/app/CaptureMatchTests.cpp
            unit/app/CommandPaletteTests.cpp
            unit/app/CommandTests.cpp
            unit/app/CompanionInputTests.cpp
            unit/app/CompanionLanTests.cpp
            unit/app/CompanionLinkTests.cpp
            unit/app/CompanionShotsTests.cpp
            unit/app/CompanionSyncTests.cpp
            unit/app/ComponentLibraryTests.cpp
            unit/app/CrashReportTests.cpp
            unit/app/DebugRecordTests.cpp
            unit/app/DemoFeedTests.cpp
            unit/app/DeviceBatchTests.cpp
            unit/app/DeviceIdentifierTests.cpp
            unit/app/DevicePresentationTests.cpp
            unit/app/DiagnosticsBundleTests.cpp
            unit/app/DraftDiffTests.cpp
            unit/app/ExtensionBisectTests.cpp
            unit/app/ExtensionHostInstallTests.cpp
            unit/app/ExtensionHostListTests.cpp
            unit/app/ExtensionHostTests.cpp
            unit/app/ExtensionIconsTests.cpp
            unit/app/ExtensionLogicTests.cpp
            unit/app/ExtensionPanelsTests.cpp
            unit/app/ExtensionServicesTests.cpp
            unit/app/FailureAdviceTests.cpp
            unit/app/FileAccessTests.cpp
            unit/app/FilterBudgetTests.cpp
            unit/app/GamepadPassthroughTests.cpp
            unit/app/GridTests.cpp
            unit/app/ExtensionHealthTests.cpp
            unit/app/HardwareAccessTests.cpp
            unit/app/HardwareClaimsTests.cpp
            unit/app/HelpLibraryTests.cpp
            unit/app/HelpMarkdownTests.cpp
            unit/app/HidReports.h
            unit/app/InputControllerTests.cpp
            unit/app/InputHostTests.cpp
            unit/app/InputReadGrantsTests.cpp
            unit/app/JsSequenceTests.cpp
            unit/app/JsonFileTests.cpp
            unit/app/KeyHintModelTests.cpp
            unit/app/KeymapBackgroundsTests.cpp
            unit/app/KeymapPlaytestTests.cpp
            unit/app/LanguagePacksTests.cpp
            unit/app/LatencyMeterTests.cpp
            unit/app/LibusbBuildTests.cpp
            unit/app/LocalServersTests.cpp
            unit/app/LogFileTests.cpp
            unit/app/LogicReferencesTests.cpp
            unit/app/McpRelayTests.cpp
            unit/app/McpServerTests.cpp
            unit/app/McpTestSupport.h
            unit/app/McpToolsTests.cpp
            unit/app/MediaFramesTests.cpp
            unit/app/NativeExtensionTests.cpp
            unit/app/NativeMediaTests.cpp
            unit/app/NetAccessTests.cpp
            unit/app/OfflineNetworkTests.cpp
            unit/app/PhoneFilesTests.cpp
            unit/app/PhoneStatusTests.cpp
            unit/app/ProfileEditorTests.cpp
            unit/app/ProfileFormTests.cpp
            unit/app/ProfileLibraryTests.cpp
            unit/app/ProfileTemplatesTests.cpp
            unit/app/QrPictureTests.cpp
            unit/app/RecordingConvertTests.cpp
            unit/app/RecordingsListTests.cpp
            unit/app/RecordingSourceTests.cpp
            unit/app/ScriptRuntimeTests.cpp
            unit/app/ScriptWorkbenchTests.cpp
            unit/app/SessionMetricsTests.cpp
            unit/app/SettingsCatalogExtensionTests.cpp
            unit/app/SettingsSearchTests.cpp
            unit/app/SettingsTests.cpp
            unit/app/SettingVariantTests.cpp
            unit/app/SpeechEnginesTests.cpp
            unit/app/StatusProblemTests.cpp
            unit/app/SyncGroupsTests.cpp
            unit/app/SystemPanelServiceTests.cpp
            unit/app/TaskExamplesTests.cpp
            unit/app/TaskLibraryTests.cpp
            unit/app/TouchBoxSessionTests.cpp
            unit/app/TouchSinkContractTests.cpp
            unit/app/TouchSinkFakes.h
            unit/app/TrayNoticesTests.cpp
            unit/app/UhidTouchSinkTests.cpp
            unit/app/UsbPhonesTests.cpp
            unit/app/VisionEnginesTests.cpp
            unit/app/VisionWorkbenchTests.cpp
            unit/app/WindowFitTests.cpp
            unit/app/ExtensionExportTests.cpp
            unit/app/ExtensionSecurityTests.cpp
            unit/app/ExtensionLinksTests.cpp
            unit/app/ConvertersTests.cpp
            unit/ext/PeImage.h
            "${PROJECT_SOURCE_DIR}/src/app/AiAssistant.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AiAssistant.h"
            "${PROJECT_SOURCE_DIR}/src/app/AiChats.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AiChats.h"
            "${PROJECT_SOURCE_DIR}/src/app/AiConversation.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AiConversation.h"
            "${PROJECT_SOURCE_DIR}/src/app/AiDrafts.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AiDrafts.h"
            "${PROJECT_SOURCE_DIR}/src/app/AiNetwork.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AiNetwork.h"
            "${PROJECT_SOURCE_DIR}/src/app/AiProviders.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AiProviders.h"
            "${PROJECT_SOURCE_DIR}/src/app/AiScripts.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AiScripts.h"
            "${PROJECT_SOURCE_DIR}/src/app/AiService.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AiService.h"
            "${PROJECT_SOURCE_DIR}/src/app/AmsiScan.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AmsiScan.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionContent.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionExport.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionExport.h"
            "${PROJECT_SOURCE_DIR}/src/app/AoaTouchSink.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AoaTouchSink.h"
            "${PROJECT_SOURCE_DIR}/src/app/AppInfo.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AppInfo.h"
            "${PROJECT_SOURCE_DIR}/src/app/AppActions.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AppActions.h"
            "${PROJECT_SOURCE_DIR}/src/app/AppCatalog.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AppCatalog.h"
            "${PROJECT_SOURCE_DIR}/src/app/AppEvents.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AppEvents.h"
            "${PROJECT_SOURCE_DIR}/src/app/AppSettings.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/AppSettings.h"
            "${PROJECT_SOURCE_DIR}/src/app/BlueprintLabels.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/BlueprintLabels.h"
            "${PROJECT_SOURCE_DIR}/src/app/BlueprintModel.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/BlueprintModel.h"
            "${PROJECT_SOURCE_DIR}/src/app/CaptureCards.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CaptureCards.h"
            "${PROJECT_SOURCE_DIR}/src/app/CaptureFeed.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CaptureFeed.h"
            "${PROJECT_SOURCE_DIR}/src/app/CaptureMatch.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CaptureMatch.h"
            "${PROJECT_SOURCE_DIR}/src/app/CommandMatcher.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CommandMatcher.h"
            "${PROJECT_SOURCE_DIR}/src/app/CommandModel.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CommandModel.h"
            "${PROJECT_SOURCE_DIR}/src/app/Commands.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/Commands.h"
            "${PROJECT_SOURCE_DIR}/src/app/CommandTable.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CommandTable.h"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionCipher.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionCipher.h"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionInput.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionInput.h"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionLan.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionLan.h"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionLink.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionLink.h"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionShots.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionShots.h"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionSync.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CompanionSync.h"
            "${PROJECT_SOURCE_DIR}/src/app/ComponentDefaults.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ComponentLibrary.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ComponentDefaults.h"
            "${PROJECT_SOURCE_DIR}/src/app/ControlChannel.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ControlChannel.h"
            "${PROJECT_SOURCE_DIR}/src/app/CrashReport.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/Declarations.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/CrashReport.h"
            "${PROJECT_SOURCE_DIR}/src/app/DemoFeed.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/DemoFeed.h"
            "${PROJECT_SOURCE_DIR}/src/app/DebugRecord.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/DebugRecord.h"
            "${PROJECT_SOURCE_DIR}/src/app/DeviceBatch.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/DeviceBatch.h"
            "${PROJECT_SOURCE_DIR}/src/app/DeviceCommands.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/DeviceCommands.h"
            "${PROJECT_SOURCE_DIR}/src/app/DeviceIdentifier.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/DeviceIdentifier.h"
            "${PROJECT_SOURCE_DIR}/src/app/DevicePresentation.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/DevicePresentation.h"
            "${PROJECT_SOURCE_DIR}/src/app/DiagnosticsBundle.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/DiagnosticsBundle.h"
            "${PROJECT_SOURCE_DIR}/src/app/DraftDiff.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/DraftDiff.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionBisect.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionBisect.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionHost.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionHost.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionLinks.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionLinks.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionLogic.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionLogic.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionMedia.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionMedia.h"
            "${PROJECT_SOURCE_DIR}/src/app/MediaOffers.h"
            "${PROJECT_SOURCE_DIR}/src/app/MediaSinks.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/MediaSinks.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionModules.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionModules.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionPanels.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionPanels.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionServices.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionServices.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionTouchSink.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionTouchSink.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionHealth.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionHealth.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionHelp.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionHelp.h"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionIcons.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ExtensionIcons.h"
            "${PROJECT_SOURCE_DIR}/src/app/NativeEvents.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/NativeEvents.h"
            "${PROJECT_SOURCE_DIR}/src/app/NativeExtensions.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/NativeExtensions.h"
            "${PROJECT_SOURCE_DIR}/src/app/NativeJobs.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/NativeJobs.h"
            "${PROJECT_SOURCE_DIR}/src/app/NetAccess.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/NetAccess.h"
            "${PROJECT_SOURCE_DIR}/src/app/TouchMethods.h"
            "${PROJECT_SOURCE_DIR}/src/app/FailureAdvice.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/FailureAdvice.h"
            "${PROJECT_SOURCE_DIR}/src/app/FileAccess.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/FileAccess.h"
            "${PROJECT_SOURCE_DIR}/src/app/FileTransfers.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/FileTransfers.h"
            "${PROJECT_SOURCE_DIR}/src/app/FilterBudget.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/FilterBudget.h"
            "${PROJECT_SOURCE_DIR}/src/app/GamepadPassthrough.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/GamepadPassthrough.h"
            "${PROJECT_SOURCE_DIR}/src/app/GridCells.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/GridCells.h"
            "${PROJECT_SOURCE_DIR}/src/app/GridLayout.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/GridLayout.h"
            "${PROJECT_SOURCE_DIR}/src/app/HardwareAccess.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/HardwareAccess.h"
            "${PROJECT_SOURCE_DIR}/src/app/HardwareClaims.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/HardwareClaims.h"
            "${PROJECT_SOURCE_DIR}/src/app/HelpLibrary.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/HelpLibrary.h"
            "${PROJECT_SOURCE_DIR}/src/app/HelpMarkdown.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/HelpMarkdown.h"
            "${PROJECT_SOURCE_DIR}/src/app/HidTouchscreen.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/HidTouchscreen.h"
            "${PROJECT_SOURCE_DIR}/src/app/InputController.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/InputController.h"
            "${PROJECT_SOURCE_DIR}/src/app/InputHost.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/InputHost.h"
            "${PROJECT_SOURCE_DIR}/src/app/InputReadGrants.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/InputReadGrants.h"
            "${PROJECT_SOURCE_DIR}/src/app/JsSequence.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/JsSequence.h"
            "${PROJECT_SOURCE_DIR}/src/app/JsonFile.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/JsonFile.h"
            "${PROJECT_SOURCE_DIR}/src/app/KeyHintModel.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/KeyHintModel.h"
            "${PROJECT_SOURCE_DIR}/src/app/KeyLabels.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/KeyLabels.h"
            "${PROJECT_SOURCE_DIR}/src/app/KeymapBackgrounds.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/KeymapBackgrounds.h"
            "${PROJECT_SOURCE_DIR}/src/app/LanguagePacks.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/LanguagePacks.h"
            "${PROJECT_SOURCE_DIR}/src/app/LatencyMeter.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/LatencyMeter.h"
            "${PROJECT_SOURCE_DIR}/src/app/LocalServers.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/LocalServers.h"
            "${PROJECT_SOURCE_DIR}/src/app/LogFile.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/LogFile.h"
            "${PROJECT_SOURCE_DIR}/src/app/LogicReferences.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/LogicReferences.h"
            "${PROJECT_SOURCE_DIR}/src/app/LogicServices.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/LogicServices.h"
            "${PROJECT_SOURCE_DIR}/src/app/LogicUploads.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/LogicUploads.h"
            "${PROJECT_SOURCE_DIR}/src/app/McpBlueprint.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/McpBlueprint.h"
            "${PROJECT_SOURCE_DIR}/src/app/McpDrafts.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/McpDrafts.h"
            "${PROJECT_SOURCE_DIR}/src/app/McpGuides.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/McpGuides.h"
            "${PROJECT_SOURCE_DIR}/src/app/McpServer.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/McpServer.h"
            "${PROJECT_SOURCE_DIR}/src/app/McpStarters.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/McpStarters.h"
            "${PROJECT_SOURCE_DIR}/src/app/McpTools.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/McpTools.h"
            "${PROJECT_SOURCE_DIR}/src/app/McpTrace.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/McpTrace.h"
            "${PROJECT_SOURCE_DIR}/src/app/MediaFrames.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/MediaFrames.h"
            "${PROJECT_SOURCE_DIR}/src/app/OfflineNetwork.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/OfflineNetwork.h"
            "${PROJECT_SOURCE_DIR}/src/app/PhoneFiles.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/PhoneFiles.h"
            "${PROJECT_SOURCE_DIR}/src/app/PhoneStatus.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/PhoneStatus.h"
            "${PROJECT_SOURCE_DIR}/src/app/PhoneVendors.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/PhoneVendors.h"
            "${PROJECT_SOURCE_DIR}/src/app/PictureZoom.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/PictureZoom.h"
            "${PROJECT_SOURCE_DIR}/src/app/ProfileEditor.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ProfileEditor.h"
            "${PROJECT_SOURCE_DIR}/src/app/ProfileForm.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ProfileForm.h"
            "${PROJECT_SOURCE_DIR}/src/app/ProfileLibrary.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ProfileLibrary.h"
            "${PROJECT_SOURCE_DIR}/src/app/QrPicture.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/QrPicture.h"
            "${PROJECT_SOURCE_DIR}/src/app/QtConversions.h"
            "${PROJECT_SOURCE_DIR}/src/app/RecordingConvert.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/RecordingConvert.h"
            "${PROJECT_SOURCE_DIR}/src/app/Recordings.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/Recordings.h"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptHighlighter.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptHighlighter.h"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptRuntime.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptRuntime.h"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptWorkbenchApi.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptWorkbenchApi.h"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptWorkbenchConsole.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptWorkbenchConsole.h"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptWorkbenchFiles.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ScriptWorkbenchFiles.h"
            "${PROJECT_SOURCE_DIR}/src/app/SessionMetrics.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/SessionMetrics.h"
            "${PROJECT_SOURCE_DIR}/src/app/Settings.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/Settings.h"
            "${PROJECT_SOURCE_DIR}/src/app/SettingsCatalog.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/SettingsCatalog.h"
            "${PROJECT_SOURCE_DIR}/src/app/SettingsSearch.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/SettingsSearch.h"
            "${PROJECT_SOURCE_DIR}/src/app/SettingVariant.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/SettingVariant.h"
            "${PROJECT_SOURCE_DIR}/src/app/ShortcutModel.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/ShortcutModel.h"
            "${PROJECT_SOURCE_DIR}/src/app/SpeechEngines.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/SpeechEngines.h"
            "${PROJECT_SOURCE_DIR}/src/app/SyncGroups.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/SyncGroups.h"
            "${PROJECT_SOURCE_DIR}/src/app/SystemPanelService.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/SystemPanelService.h"
            "${PROJECT_SOURCE_DIR}/src/app/TaskLibrary.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/TaskLibrary.h"
            "${PROJECT_SOURCE_DIR}/src/app/ThemeTokens.h"
            "${PROJECT_SOURCE_DIR}/src/app/TouchBoxSession.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/TouchBoxSession.h"
            "${PROJECT_SOURCE_DIR}/src/app/TransportPhones.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/TransportPhones.h"
            "${PROJECT_SOURCE_DIR}/src/app/TrayNotices.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/TrayNotices.h"
            "${PROJECT_SOURCE_DIR}/src/app/UhidTouchSink.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/UhidTouchSink.h"
            "${PROJECT_SOURCE_DIR}/src/app/UsbPhones.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/UsbPhones.h"
            "${PROJECT_SOURCE_DIR}/src/app/VisionEngines.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/VisionEngines.h"
            "${PROJECT_SOURCE_DIR}/src/app/VisionSearch.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/VisionSearch.h"
            "${PROJECT_SOURCE_DIR}/src/app/VisionWorkbench.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/VisionWorkbench.h"
            "${PROJECT_SOURCE_DIR}/src/app/WindowFit.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/WindowFit.h"
            "${PROJECT_SOURCE_DIR}/src/app/Converters.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/Converters.h"
            "${PROJECT_SOURCE_DIR}/src/app/WebSocketClient.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/WebSocketClient.h"
        LIBRARIES
            gs::adb
            gs::blueprint
            gs::capture
            gs::ext
            gs::exthost
            gs::hardware
            gs::input
            gs::licenses
            gs::miniz
            gs::nlohmann_json
            gs::platform
            gs::qrcodegen
            gs::record
            gs::render
            gs::script
            gs::scripthost
            gs::session
            gs::tsstrip
            gs::vision
            gs_help_articles
            gs_profile_templates
            Qt6::Core
            Qt6::Gui
            Qt6::Network
            Qt6::Qml
            ws2_32
            gdi32
            ole32
            user32
            userenv
    )
    target_include_directories(gs_app_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/app" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(gs_app_tests PRIVATE
        GS_APP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/unit/app"
        GS_BUILD_CONFIG="$<CONFIG>"
        GS_CAPTURE_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/capture"
        GS_COLOR_TOKENS_FILE="${PROJECT_SOURCE_DIR}/src/ui/theme/ColorTokens.qml"
        GS_COMPONENT_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/components"
        GS_EXAMPLE_COMPONENTS_DIR="${PROJECT_SOURCE_DIR}/examples/components"
        GS_EXAMPLE_TASKS_DIR="${PROJECT_SOURCE_DIR}/examples/tasks"
        GS_LICENSES_FOLDER="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses"
        GS_THIRD_PARTY_DIR="${PROJECT_SOURCE_DIR}/third_party"
        GS_VIDEO_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/video"
        GS_CLI_PROGRAM="$<TARGET_FILE:gs_cli>"
        GS_SCRIPT_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/scripts"
        GS_VISION_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/vision"
        GS_EXTHOST_PROGRAM="$<TARGET_FILE:gs_exthost_program>"
        GS_TEST_NATIVE_DLL="$<TARGET_FILE:gs_test_native>"
        GS_TEST_NATIVE_SIZES_DLL="$<TARGET_FILE:gs_test_native_sizes>"
        GS_TEST_NATIVE_OLD_DLL="$<TARGET_FILE:gs_test_native_old>"
        GS_PC_CLOCK_DIR="$<TARGET_FILE_DIR:gs_ext_pc_clock>"
        GS_PROTOCOL_VECTOR_DIR="${PROJECT_SOURCE_DIR}/protocol/vectors"
    )
    add_dependencies(gs_app_tests gs_cli gs_exthost_program gs_test_native gs_test_native_sizes gs_test_native_old
                     gs_ext_pc_clock)
    add_dependencies(gs_app_tests gs_scripthost_program)
    if(MSVC)
        target_compile_options(gs_app_tests PRIVATE /bigobj)
    endif()
    target_sources(gs_app_tests PRIVATE
        unit/app/AcceptanceEditorTests.cpp
        unit/app/AcceptanceInputTests.cpp
        unit/app/AcceptanceSettingsTests.cpp
        unit/app/AcceptanceWirelessTests.cpp
        "${PROJECT_SOURCE_DIR}/src/app/WirelessController.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/WirelessController.h"
    )
    target_sources(gs_app_tests PRIVATE unit/app/ExtensionLookTests.cpp)
    target_compile_definitions(gs_app_tests PRIVATE
        GS_EXTENSION_LOOK_FILE="${PROJECT_SOURCE_DIR}/src/app/qml/ExtensionLook.js"
    )
    target_sources(gs_app_tests PRIVATE
        fakes/FakeDeviceServer.h
        stability/FakePhone.h
        unit/app/PictureTouchTests.cpp
        unit/app/SessionControllerTests.cpp
        unit/app/UhidMouseTests.cpp
        "${PROJECT_SOURCE_DIR}/src/app/PictureTouch.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/PictureTouch.h"
        "${PROJECT_SOURCE_DIR}/src/app/SessionController.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/SessionController.h"
        "${PROJECT_SOURCE_DIR}/src/app/UhidMouse.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/UhidMouse.h"
    )

    find_package(Qt6 6.10 REQUIRED COMPONENTS QuickTest)
    file(TO_CMAKE_PATH "$ENV{SystemRoot}/Fonts" GS_SYSTEM_FONTS)
    set(GS_QT_TEST_ENVIRONMENT
        "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
        "QT_PLUGIN_PATH=set:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}"
        "QT_QPA_FONTDIR=set:${GS_SYSTEM_FONTS}"
        "QT_FORCE_STDERR_LOGGING=set:1"
    )
    add_executable(gs_controls_tests ui/controls/ControlsTests.cpp ui/controls/tst_controls.qml)
    target_link_libraries(gs_controls_tests PRIVATE
        Qt6::QuickTest
        gs_ui_themeplugin
        gs_ui_iconsplugin
        gs_ui_controlsplugin
    )
    target_compile_definitions(gs_controls_tests PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ui/controls"
    )
    gs_apply_compiler_policy(gs_controls_tests)
    add_test(NAME gs_controls_tests
        COMMAND gs_controls_tests -platform offscreen -import "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
    set_tests_properties(gs_controls_tests PROPERTIES ENVIRONMENT_MODIFICATION "${GS_QT_TEST_ENVIRONMENT}")

    add_executable(gs_tour_tests ui/tours/ToursTests.cpp ui/tours/tst_tour_overlay.qml)
    target_link_libraries(gs_tour_tests PRIVATE
        Qt6::QuickTest
        gs_ui_themeplugin
        gs_ui_iconsplugin
        gs_ui_controlsplugin
    )
    target_compile_definitions(gs_tour_tests PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ui/tours"
    )
    gs_apply_compiler_policy(gs_tour_tests)
    add_test(NAME gs_tour_tests
        COMMAND gs_tour_tests -platform offscreen -import "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
    set_tests_properties(gs_tour_tests PROPERTIES ENVIRONMENT_MODIFICATION "${GS_QT_TEST_ENVIRONMENT}")

    find_package(Qt6 6.10 REQUIRED COMPONENTS Test)
    add_executable(gs_shortcut_tests
        unit/app/ShortcutDispatcherTests.cpp
        "${PROJECT_SOURCE_DIR}/src/app/Commands.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/Commands.h"
        "${PROJECT_SOURCE_DIR}/src/app/CommandTable.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/CommandTable.h"
        "${PROJECT_SOURCE_DIR}/src/app/DeviceCommands.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/DeviceCommands.h"
        "${PROJECT_SOURCE_DIR}/src/app/ShortcutDispatcher.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/ShortcutDispatcher.h"
    )
    target_include_directories(gs_shortcut_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
    target_link_libraries(gs_shortcut_tests PRIVATE gs::doctest gs::protocol Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
    gs_apply_compiler_policy(gs_shortcut_tests)
    add_test(NAME gs_shortcut_tests COMMAND gs_shortcut_tests)
    set_tests_properties(gs_shortcut_tests PROPERTIES ENVIRONMENT_MODIFICATION "${GS_QT_TEST_ENVIRONMENT}")

    add_executable(gs_mirror_input_tests
        unit/app/GridWindowTests.cpp
        unit/app/ItemOrderTests.cpp
        unit/app/MirrorInputItemTests.cpp
        unit/app/VideoFreezeTests.cpp
        "${PROJECT_SOURCE_DIR}/src/app/DemoFeed.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/DemoFeed.h"
        "${PROJECT_SOURCE_DIR}/src/app/GridCells.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/GridCells.h"
        "${PROJECT_SOURCE_DIR}/src/app/GridLayout.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/GridLayout.h"
        "${PROJECT_SOURCE_DIR}/src/app/GridWindow.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/GridWindow.h"
        "${PROJECT_SOURCE_DIR}/src/app/ItemOrder.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/ItemOrder.h"
        "${PROJECT_SOURCE_DIR}/src/app/KeyForwarding.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/KeyForwarding.h"
        "${PROJECT_SOURCE_DIR}/src/app/MirrorInputItem.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/MirrorInputItem.h"
    )
    target_include_directories(gs_mirror_input_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
    target_link_libraries(gs_mirror_input_tests PRIVATE
        gs::doctest gs::input gs::platform gs::record gs::render Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
    target_compile_definitions(gs_mirror_input_tests PRIVATE
        GS_VIDEO_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/video"
    )
    gs_apply_compiler_policy(gs_mirror_input_tests)
    add_test(NAME gs_mirror_input_tests COMMAND gs_mirror_input_tests)
    set_tests_properties(gs_mirror_input_tests PROPERTIES ENVIRONMENT_MODIFICATION "${GS_QT_TEST_ENVIRONMENT}")
    target_sources(gs_mirror_input_tests PRIVATE unit/app/AcceptanceMirrorTests.cpp)

    add_executable(gs_video_render_probe integration/render/VideoRenderProbe.cpp)
    target_link_libraries(gs_video_render_probe PRIVATE gs::render gs::media gs::protocol Qt6::Gui Qt6::Quick)
    gs_apply_compiler_policy(gs_video_render_probe)
    add_test(NAME gs_video_render_probe
        COMMAND gs_video_render_probe "${PROJECT_SOURCE_DIR}/tests/data/video/emulator_h264.media" --seconds 4)
    add_test(NAME gs_video_render_sharpness
        COMMAND gs_video_render_probe "${PROJECT_SOURCE_DIR}/tests/data/video/emulator_h264.media" --seconds 2
                --check-sharpness)
    add_test(NAME gs_video_render_filters
        COMMAND gs_video_render_probe "${PROJECT_SOURCE_DIR}/tests/data/video/emulator_h264.media" --seconds 2
                --check-filter
                --filter "${PROJECT_SOURCE_DIR}/examples/extensions/crt-scanlines/shaders/scanlines.hlsl")
    add_test(NAME gs_video_render_shared
        COMMAND gs_video_render_probe "${PROJECT_SOURCE_DIR}/tests/data/video/emulator_h264.media" --seconds 3
                --second-window)

    add_executable(gs_persist_writer
        stability/PersistedFiles.cpp
        stability/PersistedFiles.h
        stability/PersistWriter.cpp
        "${PROJECT_SOURCE_DIR}/src/app/ComponentLibrary.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/ComponentLibrary.h"
        "${PROJECT_SOURCE_DIR}/src/app/ControlChannel.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/ControlChannel.h"
        "${PROJECT_SOURCE_DIR}/src/app/Declarations.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/Declarations.h"
        "${PROJECT_SOURCE_DIR}/src/app/ExtensionProfile.h"
        "${PROJECT_SOURCE_DIR}/src/app/ExtensionContent.h"
        "${PROJECT_SOURCE_DIR}/src/app/ExtensionExport.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/ExtensionExport.h"
        "${PROJECT_SOURCE_DIR}/src/app/GamepadPassthrough.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/GamepadPassthrough.h"
        "${PROJECT_SOURCE_DIR}/src/app/InputHost.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/InputHost.h"
        "${PROJECT_SOURCE_DIR}/src/app/JsonFile.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/JsonFile.h"
        "${PROJECT_SOURCE_DIR}/src/app/ProfileLibrary.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/ProfileLibrary.h"
        "${PROJECT_SOURCE_DIR}/src/app/Settings.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/Settings.h"
    )
    target_include_directories(gs_persist_writer PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
    target_link_libraries(gs_persist_writer PRIVATE
        gs::ext gs::input gs::nlohmann_json gs::platform gs::script gs::session Qt6::Core Qt6::Gui Qt6::Qml)
    target_compile_definitions(gs_persist_writer PRIVATE
        GS_PROFILE_TEMPLATE="${PROJECT_SOURCE_DIR}/src/app/templates/shooter.json"
    )
    gs_apply_compiler_policy(gs_persist_writer)

    gs_add_unit_test(gs_stability_tests
        SOURCES
            stability/ChildProcess.h
            stability/KillDuringWriteTests.cpp
            stability/PersistedFiles.cpp
            stability/PersistedFiles.h
            stability/Trend.cpp
            stability/Trend.h
            stability/TrendTests.cpp
            "${PROJECT_SOURCE_DIR}/src/app/Settings.cpp"
            "${PROJECT_SOURCE_DIR}/src/app/Settings.h"
        LIBRARIES
            gs::input
            gs::nlohmann_json
    )
    target_include_directories(gs_stability_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/app" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(gs_stability_tests PRIVATE
        GS_PERSIST_WRITER="$<TARGET_FILE:gs_persist_writer>"
        GS_PROFILE_TEMPLATE="${PROJECT_SOURCE_DIR}/src/app/templates/shooter.json"
    )
    add_dependencies(gs_stability_tests gs_persist_writer)
    set_tests_properties(gs_stability_tests PROPERTIES ENVIRONMENT_MODIFICATION "${GS_QT_TEST_ENVIRONMENT}")

    add_executable(gs_endurance
        fakes/FakeDeviceServer.h
        stability/Endurance.cpp
        stability/FakePhone.h
        stability/ProcessMetrics.cpp
        stability/ProcessMetrics.h
        stability/Trend.cpp
        stability/Trend.h
        unit/adb/FakeAdbServer.h
        "${PROJECT_SOURCE_DIR}/src/app/ControlChannel.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/ControlChannel.h"
        "${PROJECT_SOURCE_DIR}/src/app/DemoFeed.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/DemoFeed.h"
        "${PROJECT_SOURCE_DIR}/src/app/LatencyMeter.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/LatencyMeter.h"
        "${PROJECT_SOURCE_DIR}/src/app/SessionController.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/SessionController.h"
        "${PROJECT_SOURCE_DIR}/src/app/SessionMetrics.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/SessionMetrics.h"
        "${PROJECT_SOURCE_DIR}/src/app/Settings.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/Settings.h"
        "${PROJECT_SOURCE_DIR}/src/app/AoaTouchSink.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/AoaTouchSink.h"
        "${PROJECT_SOURCE_DIR}/src/app/HidTouchscreen.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/HidTouchscreen.h"
        "${PROJECT_SOURCE_DIR}/src/app/UhidTouchSink.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/UhidTouchSink.h"
        "${PROJECT_SOURCE_DIR}/src/app/UhidMouse.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/UhidMouse.h"
    )
    target_include_directories(gs_endurance PRIVATE "${PROJECT_SOURCE_DIR}/src/app" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(gs_endurance PRIVATE
        gs::adb gs::input gs::media gs::nlohmann_json gs::platform gs::record gs::render gs::session Qt6::Core Qt6::Gui psapi ws2_32)
    target_compile_definitions(gs_endurance PRIVATE
        GS_VIDEO_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/video"
        GS_QT_PLUGIN_DIR="${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}"
    )
    gs_apply_compiler_policy(gs_endurance)
    add_test(NAME gs_endurance_smoke
        COMMAND gs_endurance --smoke --out "${CMAKE_CURRENT_BINARY_DIR}/endurance-smoke")
    set_tests_properties(gs_endurance_smoke PROPERTIES
        LABELS endurance
        TIMEOUT 900
        ENVIRONMENT_MODIFICATION "${GS_QT_TEST_ENVIRONMENT}"
    )
endif()

add_executable(gs_session_device_tests integration/session/SessionDeviceTests.cpp)
target_link_libraries(gs_session_device_tests PRIVATE gs::session gs::doctest gs::miniz)
target_include_directories(gs_session_device_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(gs_session_device_tests PRIVATE
    GS_DEFAULT_SERVER_APK="${PROJECT_SOURCE_DIR}/android/server/build/outputs/apk/release/server-release-unsigned.apk"
)
gs_apply_compiler_policy(gs_session_device_tests)
add_test(NAME gs_session_device_tests COMMAND gs_session_device_tests)
set_tests_properties(gs_session_device_tests PROPERTIES SKIP_RETURN_CODE 77 LABELS device)

add_executable(gs_direct_interop_tests integration/session/DirectInteropTests.cpp fakes/LossyRelay.h)
target_link_libraries(gs_direct_interop_tests PRIVATE gs::session gs::doctest)
target_include_directories(gs_direct_interop_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src/session/src"
)
target_compile_definitions(gs_direct_interop_tests PRIVATE
    GS_INTEROP_ARGUMENTS="${PROJECT_SOURCE_DIR}/android/server/build/interop/server.args"
)
gs_apply_compiler_policy(gs_direct_interop_tests)
add_test(NAME gs_direct_interop_tests COMMAND gs_direct_interop_tests)
set_tests_properties(gs_direct_interop_tests PROPERTIES SKIP_RETURN_CODE 77 LABELS interop)

add_executable(gs_adb_device_tests integration/adb/AdbDeviceTests.cpp)
target_link_libraries(gs_adb_device_tests PRIVATE gs::adb gs::platform gs::doctest ws2_32 iphlpapi)
gs_apply_compiler_policy(gs_adb_device_tests)
add_test(NAME gs_adb_device_tests COMMAND gs_adb_device_tests)
set_tests_properties(gs_adb_device_tests PROPERTIES SKIP_RETURN_CODE 77 LABELS device)

gs_add_unit_test(gs_blueprint_tests
    SOURCES
        unit/blueprint/EditTests.cpp
        unit/blueprint/ExtensionCatalogTests.cpp
        unit/blueprint/Fixture.h
        unit/blueprint/GraphTests.cpp
        unit/blueprint/InstrumentTests.cpp
        unit/blueprint/MachineTests.cpp
        unit/blueprint/TypeTests.cpp
    LIBRARIES
        gs::blueprint
        gs::nlohmann_json
        gs::quickjs
        gs::tsstrip
)
target_include_directories(gs_blueprint_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/blueprint/src")
target_compile_definitions(gs_blueprint_tests PRIVATE
    GS_BLUEPRINT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/blueprint"
    GS_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
    GS_SCRIPTS_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/scripts"
    GS_SDK_DIR="${PROJECT_SOURCE_DIR}/sdk"
)

if(GS_BUILD_APP)
    add_executable(gs_blueprint_ui_tests
        ui/blueprint/BlueprintTests.cpp
        ui/blueprint/tst_canvas.qml
        ui/blueprint/tst_debug.qml
        ui/blueprint/tst_machine.qml
        ui/blueprint/tst_panels.qml
        ui/blueprint/tst_performance.qml
    )
    target_link_libraries(gs_blueprint_ui_tests PRIVATE
        Qt6::QuickTest
        gs_ui_themeplugin
        gs_ui_iconsplugin
        gs_ui_controlsplugin
        gs_ui_blueprintplugin
    )
    target_compile_definitions(gs_blueprint_ui_tests PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ui/blueprint"
    )
    gs_apply_compiler_policy(gs_blueprint_ui_tests)
    add_test(NAME gs_blueprint_ui_tests
        COMMAND gs_blueprint_ui_tests -platform offscreen -import "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
    set_tests_properties(gs_blueprint_ui_tests PROPERTIES ENVIRONMENT_MODIFICATION "${GS_QT_TEST_ENVIRONMENT}")
endif()
