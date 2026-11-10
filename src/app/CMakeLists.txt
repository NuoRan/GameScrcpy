qt_add_executable(GameScrcpy WIN32
    GameScrcpy.manifest
    main.cpp
)
set(GS_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/GameScrcpy.ico")
configure_file(GameScrcpy.rc.in "${CMAKE_CURRENT_BINARY_DIR}/GameScrcpy.rc" @ONLY)
target_sources(GameScrcpy PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/GameScrcpy.rc")

qt_add_qml_module(GameScrcpy
    URI GameScrcpy.App
    VERSION 1.0
    QML_FILES
        qml/AboutPage.qml
        qml/AccentSetting.qml
        qml/ActionWheelView.qml
        qml/AiCorner.qml
        qml/AiDisclosureDialog.qml
        qml/AiModelPicker.qml
        qml/AiPanel.qml
        qml/AiSettings.qml
        qml/AiWindow.qml
        qml/AllowedDevices.qml
        qml/AppsPage.qml
        qml/BatchBar.qml
        qml/CaptionButton.qml
        qml/CaptureAssignDialog.qml
        qml/CaptureCardSheet.qml
        qml/CaptureCropEditor.qml
        qml/CommandMenuItem.qml
        qml/BisectBar.qml
        qml/BundleDialog.qml
        qml/CommandPalette.qml
        qml/CompanionQrBlock.qml
        qml/ComponentSettings.qml
        qml/CopyToMineDialog.qml
        qml/CurveBox.qml
        qml/DeclaredParams.qml
        qml/DeviceCard.qml
        qml/DevicesPage.qml
        qml/Esp32SettingsSheet.qml
        qml/ExtensionLook.js
        qml/ExtensionSettingsSheet.qml
        qml/ExtensionsPage.qml
        qml/FilesPage.qml
        qml/FolderSetting.qml
        qml/Gallery.qml
        qml/GridOverlay.qml
        qml/GridHintLayer.qml
        qml/GuideHero.qml
        qml/GuideOverlay.qml
        qml/GuidePhone.qml
        qml/HardwarePicker.qml
        qml/HelpArticle.qml
        qml/HelpArtConnect.qml
        qml/HelpArtDriver.qml
        qml/HelpArtEditor.qml
        qml/HelpArtHome.qml
        qml/HelpArtMirror.qml
        qml/HelpArtProfile.qml
        qml/HelpArtScript.qml
        qml/HelpBlocks.qml
        qml/HelpFaq.qml
        qml/HelpFigure.qml
        qml/HelpHome.qml
        qml/HelpMarker.qml
        qml/HelpPage.qml
        qml/HelpPill.qml
        qml/HelpRich.js
        qml/HelpScene.qml
        qml/HelpScreen.qml
        qml/HelpSetupCard.qml
        qml/HelpSteps.qml
        qml/HelpText.qml
        qml/InstallDialog.qml
        qml/KeyFieldRow.qml
        qml/KeyHintLayer.qml
        qml/KeymapDraftLayer.qml
        qml/KeymapEditor.qml
        qml/KeyProperties.qml
        qml/LatencyTestCard.qml
        qml/LazyPage.qml
        qml/LogicImportDialog.qml
        qml/LogicView.qml
        qml/LogicWindow.qml
        qml/Main.qml
        qml/MaterialsList.qml
        qml/McpPrompts.qml
        qml/McpSettings.qml
        qml/MirrorOverlay.qml
        qml/NewComponentDialog.qml
        qml/NewProfileDialog.qml
        qml/NumberSetting.qml
        qml/PictureSourceSetting.qml
        qml/PlaceholderPage.qml
        qml/ProblemList.qml
        qml/ProfilesPage.qml
        qml/QrCodeView.qml
        qml/RecordingDialog.qml
        qml/RecordingsDialog.qml
        qml/RestoreDialog.qml
        qml/SaveAsDialog.qml
        qml/ScriptDrawings.qml
        qml/ScriptPrompt.qml
        qml/ScriptWorkbenchEditor.qml
        qml/ScriptWorkbenchPanel.qml
        qml/ScriptWorkbenchSidebar.qml
        qml/ScriptWorkbenchTree.qml
        qml/SegmentsSetting.qml
        qml/SelectSetting.qml
        qml/SettingCard.qml
        qml/SettingRow.qml
        qml/SettingsPage.qml
        qml/SetupSteps.qml
        qml/ShortcutRow.qml
        qml/SliderSetting.qml
        qml/SponsorDialog.qml
        qml/StartPanel.qml
        qml/SwitchSetting.qml
        qml/SyncDialog.qml
        qml/TaskExampleDialog.qml
        qml/TaskSettings.qml
        qml/TasksPage.qml
        qml/TextSetting.qml
        qml/ThemeSetting.qml
        qml/TitleBar.qml
        qml/TourOverlay.qml
        qml/TouchMethodSetting.qml
        qml/VisionWorkbenchWindow.qml
        qml/WirelessSection.qml
        qml/ExportPackageDialog.qml
        qml/extui/UiCanvas.qml
        qml/extui/UiChart.qml
        qml/extui/UiContextMenu.qml
        qml/extui/UiList.qml
        qml/extui/UiMarkdown.js
        qml/extui/UiNavSection.qml
        qml/extui/UiNode.qml
        qml/extui/UiPage.qml
        qml/extui/UiPanelView.qml
        qml/extui/UiSidePanel.qml
        qml/extui/UiStatusRow.qml
        qml/extui/UiTable.qml
    SOURCES
        AiAssistant.cpp
        AiAssistant.h
        AiChats.cpp
        AiChats.h
        AiConversation.cpp
        AiConversation.h
        AiDrafts.cpp
        AiDrafts.h
        AiNetwork.cpp
        AiNetwork.h
        AiScripts.cpp
        AiScripts.h
        AiService.cpp
        AiService.h
        AoaSession.cpp
        AoaSession.h
        AoaTouchSink.cpp
        AoaTouchSink.h
        AppActions.cpp
        AppActions.h
        AppCatalog.cpp
        AppCatalog.h
        AppCommandHost.cpp
        AppCommandHost.h
        AppInfo.cpp
        AppInfo.h
        AppSettings.cpp
        AppSettings.h
        CaptureCards.cpp
        CaptureCards.h
        CaptureDemo.cpp
        CaptureDemo.h
        CaptureFeed.cpp
        CaptureFeed.h
        CaptureMatch.cpp
        CaptureMatch.h
        CaptureRow.h
        CommandMatcher.cpp
        CommandMatcher.h
        CommandModel.cpp
        CommandModel.h
        Commands.cpp
        Commands.h
        CommandState.cpp
        CommandState.h
        CommandTable.cpp
        CommandTable.h
        CompanionCipher.cpp
        CompanionCipher.h
        CompanionInput.cpp
        CompanionInput.h
        CompanionLan.cpp
        CompanionLan.h
        CompanionLink.cpp
        CompanionLink.h
        CompanionShots.cpp
        CompanionShots.h
        ComponentDefaults.cpp
        ComponentDefaults.h
        ComponentLibrary.cpp
        ComponentLibrary.h
        ControlChannel.cpp
        ControlChannel.h
        CrashReport.cpp
        CrashReport.h
        Declarations.cpp
        Declarations.h
        DemoFeed.cpp
        DemoFeed.h
        DeviceBatch.cpp
        DeviceBatch.h
        DeviceCommands.cpp
        DeviceCommands.h
        DebugRecord.cpp
        DebugRecord.h
        DeviceIdentifier.cpp
        DeviceIdentifier.h
        DeviceManager.cpp
        DeviceManager.h
        DevicePresentation.cpp
        DevicePresentation.h
        DiagnosticsBundle.cpp
        DiagnosticsBundle.h
        DiagnosticsOverlay.cpp
        DiagnosticsOverlay.h
        DraftDiff.cpp
        DraftDiff.h
        Esp32Boards.cpp
        Esp32Boards.h
        Esp32Panel.cpp
        Esp32Panel.h
        Esp32Session.cpp
        Esp32Session.h
        ExtensionBisect.cpp
        ExtensionBisect.h
        ExtensionHost.cpp
        ExtensionHost.h
        ExtensionLogic.cpp
        ExtensionLogic.h
        ExtensionModules.cpp
        ExtensionModules.h
        ExtensionPanels.cpp
        ExtensionPanels.h
        ExtensionProfile.h
        ExtensionTouchSink.cpp
        ExtensionTouchSink.h
        FailureAdvice.cpp
        FailureAdvice.h
        FileTransfers.cpp
        FileTransfers.h
        FilterBudget.cpp
        FilterBudget.h
        GamepadPassthrough.cpp
        GamepadPassthrough.h
        GridCells.cpp
        GridCells.h
        GridLayout.cpp
        GridLayout.h
        GridWindow.cpp
        GridWindow.h
        HardwareAccess.cpp
        HardwareAccess.h
        HelpLibrary.cpp
        HelpLibrary.h
        HelpMarkdown.cpp
        HelpMarkdown.h
        HidTouchscreen.cpp
        HidTouchscreen.h
        InputController.cpp
        InputController.h
        InputHost.cpp
        InputHost.h
        InputReadGrants.cpp
        InputReadGrants.h
        ItemOrder.cpp
        ItemOrder.h
        JsSequence.cpp
        JsSequence.h
        JsonFile.cpp
        JsonFile.h
        KeyCapture.cpp
        KeyCapture.h
        KeyForwarding.cpp
        KeyForwarding.h
        KeyHintModel.cpp
        KeyHintModel.h
        KeyLabels.cpp
        KeyLabels.h
        KeymapBackgrounds.cpp
        KeymapBackgrounds.h
        LatencyMeter.cpp
        LatencyMeter.h
        LogFile.cpp
        LogFile.h
        LogicReferences.cpp
        LogicReferences.h
        LogicServices.cpp
        LogicServices.h
        McpBlueprint.cpp
        McpBlueprint.h
        McpDevices.cpp
        McpDevices.h
        McpDrafts.cpp
        McpDrafts.h
        McpGuides.cpp
        McpGuides.h
        McpServer.cpp
        McpServer.h
        McpStarters.cpp
        McpStarters.h
        McpTools.cpp
        McpTools.h
        McpTrace.cpp
        McpTrace.h
        MirrorInputItem.cpp
        MirrorInputItem.h
        MirrorWindow.cpp
        MirrorWindow.h
        NativeExtensions.cpp
        NativeExtensions.h
        OfflineNetwork.cpp
        OfflineNetwork.h
        PhoneFiles.cpp
        PhoneFiles.h
        PhoneStatus.cpp
        PhoneStatus.h
        PhoneVendors.cpp
        PhoneVendors.h
        PictureTouch.cpp
        PictureTouch.h
        PictureZoom.cpp
        PictureZoom.h
        ProfileEditor.cpp
        ProfileEditor.h
        ProfileForm.cpp
        ProfileForm.h
        ProfileLibrary.cpp
        ProfileLibrary.h
        QrPicture.cpp
        QrPicture.h
        QtConversions.h
        RecordingConvert.cpp
        RecordingConvert.h
        Recordings.cpp
        Recordings.h
        ScriptHighlighter.cpp
        ScriptHighlighter.h
        ScriptRuntime.cpp
        ScriptRuntime.h
        ScriptWorkbench.cpp
        ScriptWorkbench.h
        ScriptWorkbenchApi.cpp
        ScriptWorkbenchApi.h
        ScriptWorkbenchConsole.cpp
        ScriptWorkbenchConsole.h
        ScriptWorkbenchFiles.cpp
        ScriptWorkbenchFiles.h
        SessionController.cpp
        SessionController.h
        SessionMetrics.cpp
        SessionMetrics.h
        Settings.cpp
        Settings.h
        SettingsCatalog.cpp
        SettingsCatalog.h
        SettingsSearch.cpp
        SettingsSearch.h
        SettingVariant.cpp
        SettingVariant.h
        SetupGuide.cpp
        SetupGuide.h
        SponsorPrompt.h
        SyncGroups.cpp
        SyncGroups.h
        SystemPanelService.cpp
        SystemPanelService.h
        TaskLibrary.cpp
        TaskLibrary.h
        ShortcutDispatcher.cpp
        ShortcutDispatcher.h
        ShortcutModel.cpp
        ShortcutModel.h
        ThemeTokens.h
        ThumbnailView.cpp
        ThumbnailView.h
        TouchMethods.h
        TranslateNoop.h
        TrayNotices.cpp
        TrayNotices.h
        UhidTouchSink.cpp
        UhidTouchSink.h
        UhidMouse.cpp
        UhidMouse.h
        UsbPhones.cpp
        UsbPhones.h
        VisionSearch.cpp
        VisionSearch.h
        VisionWorkbench.cpp
        VisionWorkbench.h
        WindowChrome.cpp
        WindowChrome.h
        WindowFit.cpp
        WindowFit.h
        WirelessController.cpp
        WirelessController.h
        BlueprintLabels.cpp
        BlueprintLabels.h
        BlueprintModel.cpp
        BlueprintModel.h
        AmsiScan.cpp
        AmsiScan.h
        ExtensionContent.h
        ExtensionExport.cpp
        ExtensionExport.h
)

add_library(gs_profile_templates OBJECT)
qt_add_resources(gs_profile_templates "profile_templates"
    PREFIX "/templates"
    BASE templates
    FILES
        templates/action.json
        templates/blank.json
        templates/cards.json
        templates/moba.json
        templates/racing.json
        templates/shooter.json
)
target_link_libraries(gs_profile_templates PRIVATE Qt6::Core)

add_library(gs_help_articles OBJECT)
qt_add_resources(gs_help_articles "help_articles"
    PREFIX "/help"
    BASE help
    FILES
        help/index.json
        help/en/aoa-driver.md
        help/en/capture-card.md
        help/en/connecting.md
        help/en/esp32-touch.md
        help/en/faq.md
        help/en/keymap-editor.md
        help/en/mirror-window.md
        help/en/profiles.md
        help/en/scripts.md
        help/en/tours.md
        help/zh_CN/aoa-driver.md
        help/zh_CN/capture-card.md
        help/zh_CN/connecting.md
        help/zh_CN/esp32-touch.md
        help/zh_CN/faq.md
        help/zh_CN/keymap-editor.md
        help/zh_CN/mirror-window.md
        help/zh_CN/profiles.md
        help/zh_CN/scripts.md
        help/zh_CN/tours.md
)
file(GLOB gs_help_api_pages CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/sdk/docs/*.md")
qt_add_resources(gs_help_articles "help_api"
    PREFIX "/help/api"
    BASE "${PROJECT_SOURCE_DIR}/sdk/docs"
    FILES ${gs_help_api_pages}
)
target_link_libraries(gs_help_articles PRIVATE Qt6::Core)

target_link_libraries(GameScrcpy PRIVATE
    gs::base
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
    gs_ui_themeplugin
    gs_ui_iconsplugin
    gs_ui_controlsplugin
    gs_ui_blueprintplugin
    QWindowKit::Quick
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    gs::blueprint
    gs::esp32
    gs::espflash
    ws2_32
)
target_compile_definitions(GameScrcpy PRIVATE GS_BUILD_CONFIG="$<CONFIG>")
gs_apply_compiler_policy(GameScrcpy)

qt_add_translations(GameScrcpy
    TS_FILES i18n/gamescrcpy_zh_CN.ts
    PLURALS_TS_FILE i18n/gamescrcpy_en.ts
    SOURCES "${PROJECT_SOURCE_DIR}/sdk/i18n/api-texts.js"
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none -tr-function-alias QT_TRANSLATE_NOOP+=GS_TRANSLATE_NOOP
)

option(GS_DEPLOY_QT_ON_BUILD "Copy the Qt runtime next to GameScrcpy.exe after each build" ON)
if(GS_DEPLOY_QT_ON_BUILD)
    find_program(GS_WINDEPLOYQT windeployqt
        HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
        NO_DEFAULT_PATH
        REQUIRED
    )
    add_custom_command(TARGET GameScrcpy POST_BUILD
        COMMAND "${GS_WINDEPLOYQT}"
            --verbose 0
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            --no-compiler-runtime
            --qmldir "${PROJECT_SOURCE_DIR}/src"
            "$<TARGET_FILE:GameScrcpy>"
        COMMENT "Deploying the Qt runtime next to GameScrcpy.exe"
        VERBATIM
    )
endif()

add_custom_target(gs_server_package ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/android/server/build/outputs/apk/release/server-release-unsigned.apk"
        "-DDESTINATION=$<TARGET_FILE_DIR:GameScrcpy>/gs-server.apk"
        -P "${PROJECT_SOURCE_DIR}/cmake/CopyIfExists.cmake"
    COMMENT "Placing the server build next to GameScrcpy.exe"
    VERBATIM
)

add_custom_target(gs_companion_package ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/android/companion/build/outputs/apk/debug/companion-debug.apk"
        "-DDESTINATION=$<TARGET_FILE_DIR:GameScrcpy>/gs-companion.apk"
        -P "${PROJECT_SOURCE_DIR}/cmake/CopyIfExists.cmake"
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/android/companion/build/outputs/apk/debug/output-metadata.json"
        "-DDESTINATION=$<TARGET_FILE_DIR:GameScrcpy>/gs-companion.json"
        -P "${PROJECT_SOURCE_DIR}/cmake/CopyIfExists.cmake"
    COMMENT "Placing the companion app build next to GameScrcpy.exe"
    VERBATIM
)

if(EXISTS "${PROJECT_SOURCE_DIR}/firmware/esp32/release/firmware.json")
    add_custom_target(gs_esp32_firmware ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:GameScrcpy>/esp32-firmware"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/firmware/esp32/release"
            "$<TARGET_FILE_DIR:GameScrcpy>/esp32-firmware"
        COMMENT "Placing the bundled ESP32 firmware next to GameScrcpy.exe"
        VERBATIM
    )
endif()

add_custom_target(gs_builtin_components
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:GameScrcpy>/components"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/examples/components"
        "$<TARGET_FILE_DIR:GameScrcpy>/components"
    COMMENT "Placing the example components next to GameScrcpy.exe"
    VERBATIM
)
add_dependencies(GameScrcpy gs_builtin_components)

add_custom_target(gs_builtin_tasks
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:GameScrcpy>/tasks"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/examples/tasks"
        "$<TARGET_FILE_DIR:GameScrcpy>/tasks"
    COMMENT "Placing the example tasks next to GameScrcpy.exe"
    VERBATIM
)
add_dependencies(GameScrcpy gs_builtin_tasks)

add_dependencies(GameScrcpy gs_exthost_program)
add_dependencies(GameScrcpy gs_scripthost_program)

if(GS_BUILD_TESTS)
    add_test(NAME gs_app_smoke COMMAND GameScrcpy --smoke-test)
    set_tests_properties(gs_app_smoke PROPERTIES TIMEOUT 60
        ENVIRONMENT "GS_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke-data"
    )
    add_test(NAME gs_app_smoke_mirror COMMAND GameScrcpy --smoke-test
        --demo-device "${PROJECT_SOURCE_DIR}/tests/data/video/emulator_h264.media")
    set_tests_properties(gs_app_smoke_mirror PROPERTIES TIMEOUT 60
        ENVIRONMENT "GS_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke-mirror-data"
    )
    if(NOT GS_DEPLOY_QT_ON_BUILD)
        set_tests_properties(gs_app_smoke gs_app_smoke_mirror PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
        )
    endif()
endif()
