# GameScrcpy.Theme: design tokens of the Obsidian design system (ADR-0011).
# Components must read every color, size and duration from here instead of hard-coding values.

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(gs_ui_theme STATIC)
qt_add_qml_module(gs_ui_theme
    URI GameScrcpy.Theme
    VERSION 1.0
    QML_FILES
        Theme.qml
        ColorTokens.qml
        Motion.qml
        Radius.qml
        Shadow.qml
        Size.qml
        Spacing.qml
        TextStyle.qml
        Typography.qml
)
gs_apply_compiler_policy(gs_ui_theme)
