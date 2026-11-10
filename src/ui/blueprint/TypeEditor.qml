pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

T.Popup {
    id: editor

    property var model: null
    property var typeNames: []
    property string typeName: ""
    property string problem: ""
    property var info: ({})

    readonly property string kind: info.kind ?? ""
    readonly property var bases: info.bases ?? []
    readonly property string base: info.base ?? ""
    readonly property var fields: info.fields ?? []
    readonly property var options: info.options ?? []
    readonly property var members: info.members ?? []
    readonly property var params: info.params ?? []

    function showType(name: string) {
        typeName = name
        problem = ""
        refresh()
        open()
    }

    function refresh() {
        info = model !== null && typeName !== "" ? model.typeInfo(typeName) : ({})
    }

    function edit(field: string, value: var) {
        if (model === null || typeName === "")
            return
        problem = model.editType(typeName, field, value)
        refresh()
        if (problem === "" && (field === "delete" || field === "name"))
            close()
    }

    function kindName(): string {
        return kind === "struct" ? qsTr("Structure") : kind === "enum" ? qsTr("Enum") : kind === "class" ? qsTr("Class") : ""
    }

    function typeChoices(): var {
        const plain = ["number", "boolean", "string", "PicturePoint", "Region", "ColorName", "TemplateName", "InputName"]
        const list = []
        for (const name of plain)
            list.push({ value: name, text: name })
        for (const name of editor.typeNames)
            list.push({ value: name, text: name })
        return list
    }

    function baseChoices(): var {
        const list = [{ value: "", text: qsTr("None") }]
        for (const name of editor.bases) {
            if (name !== editor.typeName)
                list.push({ value: name, text: name })
        }
        return list
    }

    objectName: "bpTypeEditor"
    parent: T.Overlay.overlay
    x: parent ? Math.max(Theme.space.md, (parent.width - width) / 2) : 0
    y: parent ? Math.max(Theme.space.md, (parent.height - height) / 2) : 0
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape
    width: Math.min(parent ? parent.width - 2 * Theme.space.xl : Theme.size.workbenchSidebar,
                    Theme.size.workbenchSidebar + Theme.size.bpSection * 6)
    height: Math.min((contentItem ? contentItem.implicitHeight : 0) + 2 * Theme.space.lg,
                     parent ? parent.height - 2 * Theme.space.xl : Theme.size.workbenchCode)
    padding: Theme.space.lg
    background: Rectangle {
        radius: Theme.radius.lg
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeDefault
    }

    contentItem: Flickable {
        id: scroller

        implicitHeight: content.implicitHeight
        contentWidth: content.width
        contentHeight: content.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        interactive: contentHeight > height

        Column {
            id: content

            width: scroller.width
            spacing: Theme.space.md

            Row {
                width: parent.width
                spacing: Theme.space.sm

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: editor.kind === "enum" ? "layers" : "square"
                    size: Theme.size.iconSmall
                    color: Theme.colors.accentText
                }

                Text {
                    id: heading

                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - parent.spacing - Theme.size.iconSmall - closeButton.width
                    textFormat: Text.PlainText
                    text: editor.typeName !== "" ? editor.typeName : qsTr("New type")
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.heading.size
                    font.weight: Theme.type.heading.weight
                }

                Button {
                    id: closeButton

                    objectName: "bpTypeEditorClose"
                    tone: "ghost"
                    small: true
                    iconName: "x"
                    description: qsTr("Close")
                    onClicked: editor.close()
                }
            }

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: editor.kindName()
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Text {
                width: parent.width
                visible: editor.problem !== ""
                textFormat: Text.PlainText
                text: editor.problem
                wrapMode: Text.Wrap
                color: Theme.colors.danger
                font.pixelSize: Theme.type.caption.size
            }

            DetailField {
                width: parent.width
                spec: ({ name: "name", label: qsTr("Name"), kind: "text", value: editor.typeName })
                onEdited: value => editor.edit("name", value)
            }

            DetailField {
                width: parent.width
                visible: editor.kind === "class"
                spec: ({ name: "base", label: qsTr("Parent class"), kind: "choice", value: editor.base,
                         choices: editor.baseChoices() })
                onEdited: value => editor.edit("base", value)
            }

            Column {
                width: parent.width
                spacing: Theme.space.xs
                visible: editor.kind === "struct"

                Repeater {
                    model: editor.fields

                    delegate: Row {
                        id: fieldRow

                        required property var modelData
                        required property int index

                        objectName: "bpTypeField_" + fieldRow.index
                        width: content.width
                        spacing: Theme.space.xs

                        TextField {
                            id: fieldName

                            width: (parent.width - 2 * parent.spacing - removeField.width) * 0.34
                            text: fieldRow.modelData.name ?? ""
                            Accessible.name: qsTr("Field name")
                            onEditingFinished: editor.edit("field." + fieldRow.modelData.name + ".name", fieldName.text)
                        }

                        Select {
                            width: (parent.width - 2 * parent.spacing - removeField.width) * 0.46
                            model: editor.typeChoices()
                            textRole: "text"
                            currentIndex: {
                                const choices = editor.typeChoices()
                                for (let index = 0; index < choices.length; ++index) {
                                    if (choices[index].value === (fieldRow.modelData.type ?? ""))
                                        return index
                                }
                                return -1
                            }
                            Accessible.name: qsTr("Field type")
                            onActivated: index => editor.edit("field." + fieldRow.modelData.name + ".type",
                                                              editor.typeChoices()[index].value)
                        }

                        Switch {
                            width: (parent.width - 2 * parent.spacing - removeField.width) * 0.2
                            checked: fieldRow.modelData.optional === true
                            Accessible.name: qsTr("Optional")
                            onToggled: editor.edit("field." + fieldRow.modelData.name + ".optional", checked)
                        }

                        Button {
                            id: removeField

                            tone: "ghost"
                            small: true
                            iconName: "trash-2"
                            description: qsTr("Remove “%1”").arg(fieldRow.modelData.name ?? "")
                            onClicked: editor.edit("removeField." + fieldRow.modelData.name, "")
                        }
                    }
                }

                Button {
                    objectName: "bpTypeAddField"
                    small: true
                    tone: "ghost"
                    iconName: "plus"
                    text: qsTr("Add a field")
                    onClicked: editor.edit("addField", "")
                }
            }

            Column {
                width: parent.width
                spacing: Theme.space.xs
                visible: editor.kind === "enum"

                Repeater {
                    model: editor.options

                    delegate: Row {
                        id: optionRow

                        required property var modelData
                        required property int index

                        objectName: "bpTypeOption_" + optionRow.index
                        width: content.width
                        spacing: Theme.space.xs

                        TextField {
                            id: optionName

                            width: parent.width - removeOption.width - parent.spacing
                            text: optionRow.modelData ?? ""
                            Accessible.name: qsTr("Option")
                            onEditingFinished: editor.edit("option." + optionRow.modelData, optionName.text)
                        }

                        Button {
                            id: removeOption

                            tone: "ghost"
                            small: true
                            iconName: "trash-2"
                            description: qsTr("Remove the option “%1”").arg(optionRow.modelData ?? "")
                            onClicked: editor.edit("removeOption." + optionRow.modelData, "")
                        }
                    }
                }

                Button {
                    objectName: "bpTypeAddOption"
                    small: true
                    tone: "ghost"
                    iconName: "plus"
                    text: qsTr("Add an option")
                    onClicked: editor.edit("addOption", "")
                }
            }

            Column {
                width: parent.width
                spacing: Theme.space.xs
                visible: editor.kind === "class"

                Repeater {
                    model: editor.members

                    delegate: Row {
                        id: memberRow

                        required property var modelData
                        required property int index

                        readonly property string memberKind: memberRow.modelData.kind ?? ""

                        objectName: "bpTypeMember_" + memberRow.index
                        width: content.width
                        spacing: Theme.space.xs

                        TextField {
                            id: memberName

                            width: (parent.width - privateSwitch.width - removeMember.width - 2 * parent.spacing) * 0.62
                            text: memberRow.modelData.name ?? ""
                            Accessible.name: qsTr("Name")
                            onEditingFinished: editor.edit(memberRow.memberKind + "." + memberRow.modelData.name + ".name",
                                                           memberName.text)
                        }

                        Text {
                            width: (parent.width - privateSwitch.width - removeMember.width - 2 * parent.spacing) * 0.38
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: memberRow.memberKind === "function" ? qsTr("Function")
                                : memberRow.memberKind === "event" ? qsTr("Event") : qsTr("Variable")
                            elide: Text.ElideRight
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                        }

                        Switch {
                            id: privateSwitch

                            checked: memberRow.modelData.private === true
                            Accessible.name: qsTr("Private")
                            onToggled: editor.edit(memberRow.memberKind + "." + memberRow.modelData.name + ".private",
                                                   checked)
                        }

                        Button {
                            id: removeMember

                            tone: "ghost"
                            small: true
                            iconName: "trash-2"
                            description: qsTr("Remove “%1”").arg(memberRow.modelData.name ?? "")
                            onClicked: editor.edit(memberRow.memberKind + "." + memberRow.modelData.name + ".remove", "")
                        }
                    }
                }

                Row {
                    spacing: Theme.space.xs

                    Button {
                        objectName: "bpTypeAddVariable"
                        small: true
                        tone: "ghost"
                        iconName: "plus"
                        text: qsTr("Add a variable")
                        onClicked: editor.edit("addMember", "")
                    }

                    Button {
                        objectName: "bpTypeAddFunction"
                        small: true
                        tone: "ghost"
                        iconName: "plus"
                        text: qsTr("Add a function")
                        onClicked: editor.edit("addFunction", "")
                    }

                    Button {
                        objectName: "bpTypeAddEvent"
                        small: true
                        tone: "ghost"
                        iconName: "plus"
                        text: qsTr("Add a dispatcher")
                        onClicked: editor.edit("addEvent", "")
                    }
                }

                Repeater {
                    model: editor.params

                    delegate: Row {
                        id: paramRow

                        required property var modelData
                        required property int index

                        objectName: "bpTypeParam_" + paramRow.index
                        width: content.width
                        spacing: Theme.space.xs

                        TextField {
                            id: paramName

                            width: parent.width - removeParam.width - parent.spacing
                            text: paramRow.modelData.name ?? ""
                            placeholderText: qsTr("Constructor parameter")
                            Accessible.name: qsTr("Constructor parameter")
                            onEditingFinished: {
                                if (paramName.text !== "" && paramName.text !== (paramRow.modelData.name ?? "")) {
                                    editor.edit("ctor.remove." + paramRow.modelData.name, "")
                                    editor.edit("addCtorParam", ({ name: paramName.text, type: "" }))
                                }
                            }
                        }

                        Button {
                            id: removeParam

                            tone: "ghost"
                            small: true
                            iconName: "trash-2"
                            description: qsTr("Remove “%1”").arg(paramRow.modelData.name ?? "")
                            onClicked: editor.edit("ctor.remove." + paramRow.modelData.name, "")
                        }
                    }
                }

                Button {
                    objectName: "bpTypeAddParam"
                    small: true
                    tone: "ghost"
                    iconName: "plus"
                    text: qsTr("Add a constructor parameter")
                    onClicked: editor.edit("addCtorParam", "")
                }
            }

            Row {
                width: parent.width
                spacing: Theme.space.sm

                Item {
                    width: parent.width - deleteButton.width - parent.spacing
                    height: 1
                }

                Button {
                    id: deleteButton

                    objectName: "bpTypeDelete"
                    small: true
                    tone: "danger"
                    iconName: "trash-2"
                    text: qsTr("Delete")
                    onClicked: editor.edit("delete", "")
                }
            }
        }
    }
}
