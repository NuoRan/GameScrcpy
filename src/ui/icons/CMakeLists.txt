# GameScrcpy.Icons: line icons stroked from SVG path data (ADR-0057). IconPaths.js is generated
# by tools/gen_icons.py from third_party/lucide/icons and custom/; CTest gs_icons_generated checks
# that it is up to date.

qt_add_library(gs_ui_icons STATIC)
qt_add_qml_module(gs_ui_icons
    URI GameScrcpy.Icons
    VERSION 1.0
    QML_FILES
        Icon.qml
        IconPaths.js
)
gs_apply_compiler_policy(gs_ui_icons)
