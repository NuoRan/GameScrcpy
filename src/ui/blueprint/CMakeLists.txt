find_package(Qt6 6.10 REQUIRED COMPONENTS ShaderTools)

set_source_files_properties(BlueprintLook.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(gs_ui_blueprint STATIC)
set_target_properties(gs_ui_blueprint PROPERTIES QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")
qt_add_qml_module(gs_ui_blueprint
    URI GameScrcpy.Blueprint
    VERSION 1.0
    QML_FILES
        BlueprintCanvas.qml
        BlueprintDetails.qml
        BlueprintLook.qml
        BlueprintMinimap.qml
        BlueprintMock.qml
        BlueprintNode.qml
        BlueprintPages.qml
        BlueprintPicker.qml
        BlueprintSidebar.qml
        BlueprintWire.qml
        CommentBox.qml
        DebugBar.qml
        DebugMoment.qml
        DebugTimeline.qml
        DetailField.qml
        ExecPin.qml
        Geometry.js
        GridDots.qml
        NodePin.qml
        PinEditor.qml
        StateNode.qml
        TransitionWire.qml
        VariableEditor.qml
        TypeEditor.qml
        WirePulse.qml
)
qt_add_shaders(gs_ui_blueprint "gs_ui_blueprint_shaders"
    PREFIX "/qt/qml/GameScrcpy/Blueprint"
    FILES
        shaders/griddots.frag
)
target_link_libraries(gs_ui_blueprint PRIVATE Qt6::QuickTemplates2)
gs_apply_compiler_policy(gs_ui_blueprint)
