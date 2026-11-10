# GameScrcpy.Controls: the components of the design system (DESIGN_SYSTEM.md 3), drawn on Qt Quick
# Templates (ADR-0002) from the theme's tokens alone; tools/check_source_rules.py rejects bare
# colors, durations and sizes here (UI-001).

qt_add_library(gs_ui_controls STATIC)
qt_add_qml_module(gs_ui_controls
    URI GameScrcpy.Controls
    VERSION 1.0
    QML_FILES
        Button.qml
        Card.qml
        Checkbox.qml
        CodeField.qml
        ConnectionStepper.qml
        ContentPanel.qml
        Dialog.qml
        EdgeGlow.qml
        FieldFrame.qml
        FilterChip.qml
        FloatingToolbar.qml
        FocusRing.qml
        InfoButton.qml
        InlineAlert.qml
        Keycap.qml
        KeyCaptureField.qml
        KeyHint.qml
        MappingPreview.qml
        Menu.qml
        MenuItem.qml
        MenuSeparator.qml
        NavItem.qml
        NavRail.qml
        NumberField.qml
        NumberScale.qml
        PageHost.qml
        PopupSurface.qml
        SearchField.qml
        SegmentedControl.qml
        Select.qml
        SettingsGroup.qml
        SettingsRow.qml
        SettingsSheet.qml
        Slider.qml
        StateLayer.qml
        StatusCapsule.qml
        StatusPill.qml
        Switch.qml
        Tabs.qml
        TextField.qml
        Tile.qml
        Toast.qml
        ToolbarButton.qml
        ToolbarRevealArea.qml
        ToolbarSeparator.qml
        Tooltip.qml
        Workbench.qml
)
target_link_libraries(gs_ui_controls PRIVATE Qt6::QuickTemplates2)
gs_apply_compiler_policy(gs_ui_controls)
