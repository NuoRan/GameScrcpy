pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: editor

    property var spec: ({})
    property string problem: ""

    property string name: ""
    property string kind: "integer"
    property string container: "single"
    property string keyKind: "text"
    property string value: ""
    property string category: ""
    property string description: ""
    property bool keep: false
    property bool editable: false
    property bool constant: false

    readonly property bool creating: (spec.key ?? "") === ""
    readonly property var types: spec.types ?? []
    readonly property var containers: spec.containers ?? []
    readonly property var keyKinds: spec.keyKinds ?? []
    readonly property bool many: container === "list" || container === "dict" || container === "set"
    readonly property bool byName: container === "dict"
    readonly property bool canBeEditable: spec.canBeEditable === true
    readonly property bool kindEditable: kind === "toggle" || kind === "integer" || kind === "decimal"
                                         || kind === "duration" || kind === "text" || kind === "point"
                                         || kind === "key" || kind === "component"

    signal saved(var spec)
    signal removed()
    signal cancelled()

    objectName: "bpVariableEditor"
    implicitWidth: Theme.size.dialogWidth
    implicitHeight: column.implicitHeight + 2 * Theme.space.lg

    function indexOf(list: var, wanted: string): int {
        for (let index = 0; index < list.length; ++index) {
            if ((list[index].value ?? "") === wanted)
                return index
        }
        return 0
    }

    function textOf(list: var, wanted: string): string {
        for (let index = 0; index < list.length; ++index) {
            if ((list[index].value ?? "") === wanted)
                return list[index].text ?? wanted
        }
        return wanted
    }

    function load(next: var) {
        const source = next ?? ({})
        name = source.name ?? ""
        kind = (source.kind ?? "") !== "" ? source.kind : "integer"
        container = (source.container ?? "") !== "" ? source.container : "single"
        keyKind = (source.keyKind ?? "") !== "" ? source.keyKind : "text"
        value = source.value ?? ""
        category = source.category ?? ""
        description = source.description ?? ""
        keep = source.keep === true
        editable = source.editable === true
        constant = source.constant === true
    }

    function payload(): var {
        return Object.assign({}, spec, {
            name: name,
            kind: kind,
            container: container === "single" ? "" : container,
            keyKind: keyKind,
            value: value,
            category: category,
            description: description,
            keep: keep,
            editable: editable && canBeEditable && kindEditable,
            constant: constant
        })
    }

    onSpecChanged: load(spec)

    Column {
        id: column

        x: Theme.space.lg
        y: Theme.space.lg
        width: parent.width - 2 * Theme.space.lg
        spacing: Theme.space.md

        Text {
            objectName: "bpVarTitle"
            width: parent.width
            textFormat: Text.PlainText
            text: editor.creating ? qsTr("New variable") : qsTr("Variable")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.heading.size
            font.weight: Theme.type.heading.weight
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            visible: (editor.spec.pageTitle ?? "") !== ""
            textFormat: Text.PlainText
            text: editor.spec.pageTitle ?? ""
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
            elide: Text.ElideRight
        }

        Column {
            width: parent.width

            spacing: Theme.space.xs

            Text {
                textFormat: Text.PlainText
                text: qsTr("Name")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            TextField {
                id: nameField

                objectName: "bpVarName"
                width: parent.width
                mono: true
                text: editor.name
                placeholderText: qsTr("health")
                onTextEdited: editor.name = nameField.text
            }
        }

        Row {
            width: parent.width
            spacing: Theme.space.md

            Column {
                width: (parent.width - Theme.space.md) / 2
                spacing: Theme.space.xs

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Type")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }

                Select {
                    id: kindSelect

                    objectName: "bpVarType"
                    width: parent.width
                    model: editor.types
                    textRole: "text"
                    currentIndex: editor.indexOf(editor.types, editor.kind)
                    onActivated: index => {
                        editor.kind = editor.types[index].value
                        kindSelect.currentIndex = Qt.binding(() => editor.indexOf(editor.types, editor.kind))
                    }
                }
            }

            Column {
                width: (parent.width - Theme.space.md) / 2
                spacing: Theme.space.xs

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("One or many")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }

                Select {
                    id: containerSelect

                    objectName: "bpVarContainer"
                    width: parent.width
                    model: editor.containers
                    textRole: "text"
                    currentIndex: editor.indexOf(editor.containers, editor.container)
                    onActivated: index => {
                        editor.container = editor.containers[index].value
                        containerSelect.currentIndex = Qt.binding(() => editor.indexOf(editor.containers, editor.container))
                    }
                }
            }
        }

        Column {
            width: parent.width
            visible: editor.byName
            spacing: Theme.space.xs

            Text {
                textFormat: Text.PlainText
                text: qsTr("Keys are")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            Select {
                id: keySelect

                objectName: "bpVarKeyType"
                width: parent.width
                model: editor.keyKinds
                textRole: "text"
                currentIndex: editor.indexOf(editor.keyKinds, editor.keyKind)
                onActivated: index => {
                    editor.keyKind = editor.keyKinds[index].value
                    keySelect.currentIndex = Qt.binding(() => editor.indexOf(editor.keyKinds, editor.keyKind))
                }
            }
        }

        Column {
            width: parent.width

            spacing: Theme.space.xs

            Text {
                textFormat: Text.PlainText
                text: qsTr("Starting value")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            TextField {
                id: valueField

                objectName: "bpVarValue"
                width: parent.width
                mono: true
                text: editor.value
                placeholderText: editor.many ? "[]" : ""
                onTextEdited: editor.value = valueField.text
            }

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("Written as code, such as 0, \"text\", [] or { x: 0.5, y: 0.5 }")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.micro.size
                wrapMode: Text.Wrap
            }
        }

        Column {
            width: parent.width

            spacing: Theme.space.xs

            Text {
                textFormat: Text.PlainText
                text: qsTr("Category")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            TextField {
                id: categoryField

                objectName: "bpVarCategory"
                width: parent.width
                text: editor.category
                placeholderText: qsTr("Battle")
                onTextEdited: editor.category = categoryField.text
            }
        }

        Column {
            width: parent.width

            spacing: Theme.space.xs

            Text {
                textFormat: Text.PlainText
                text: qsTr("Description")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            TextField {
                id: descriptionField

                objectName: "bpVarDescription"
                width: parent.width
                text: editor.description
                placeholderText: qsTr("Health of the hero")
                onTextEdited: editor.description = descriptionField.text
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space.xs

            Checkbox {
                id: keepBox

                objectName: "bpVarKeep"
                text: qsTr("Keep it for next time")
                checked: editor.keep
                onToggled: editor.keep = keepBox.checked
            }

            Text {
                x: Theme.size.checkbox + Theme.space.sm
                width: parent.width - x
                textFormat: Text.PlainText
                text: qsTr("Saved with the program, so it starts where it ended last time")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.micro.size
                wrapMode: Text.Wrap
            }
        }

        Column {
            width: parent.width
            visible: editor.canBeEditable
            spacing: Theme.space.xs

            Checkbox {
                id: editableBox

                objectName: "bpVarEditable"
                text: qsTr("Change it in the property panel")
                enabled: editor.kindEditable
                checked: editor.editable && editor.kindEditable
                onToggled: editor.editable = editableBox.checked
            }

            Text {
                x: Theme.size.checkbox + Theme.space.sm
                width: parent.width - x
                textFormat: Text.PlainText
                text: editor.kindEditable ? qsTr("Placed components of this class can change it")
                                          : qsTr("This type cannot be changed in the property panel")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.micro.size
                wrapMode: Text.Wrap
            }
        }

        Text {
            objectName: "bpVarProblem"
            width: parent.width
            visible: editor.problem !== ""
            textFormat: Text.PlainText
            text: editor.problem
            color: Theme.colors.dangerText
            font.pixelSize: Theme.type.caption.size
            wrapMode: Text.Wrap
        }

        Item {
            width: parent.width
            height: Theme.space.xs
        }

        Row {
            width: parent.width
            spacing: Theme.space.sm
            layoutDirection: Qt.RightToLeft

            Button {
                objectName: "bpVarSave"
                tone: "primary"
                text: qsTr("Save")
                onClicked: editor.saved(editor.payload())
            }

            Button {
                objectName: "bpVarCancel"
                tone: "ghost"
                text: qsTr("Cancel")
                onClicked: editor.cancelled()
            }

            Button {
                objectName: "bpVarDelete"
                tone: "ghost"
                visible: !editor.creating
                text: qsTr("Delete")
                onClicked: editor.removed()
            }
        }
    }
}
