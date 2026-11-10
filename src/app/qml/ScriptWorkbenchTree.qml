pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: tree

    required property ScriptWorkbench bench
    property bool adding: false

    signal opened(string path)

    function iconOf(entry: var): string {
        switch (entry.kind) {
        case "folder":
            return "folder-open"
        case "code":
            return "code-xml"
        case "image":
            return "camera"
        case "profile":
            return "keyboard"
        default:
            return "type"
        }
    }

    function startAdding() {
        nameField.text = ""
        nameField.errorText = ""
        tree.adding = true
        nameField.forceActiveFocus()
    }

    function finishAdding() {
        const problem = tree.bench.newFile(nameField.text)
        if (problem !== "") {
            nameField.errorText = problem
            return
        }
        tree.adding = false
    }

    Item {
        id: heading

        width: parent.width
        height: Theme.size.tab + Theme.space.xs

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.lg
            anchors.right: addButton.left
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: qsTr("Files")
            elide: Text.ElideRight
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.micro.size
            font.weight: Theme.type.micro.weight
        }

        ToolbarButton {
            id: addButton

            objectName: "newFile"
            anchors.right: parent.right
            anchors.rightMargin: Theme.space.xs
            anchors.verticalCenter: parent.verticalCenter
            iconName: "plus"
            text: qsTr("New file")
            onClicked: tree.startAdding()
        }
    }

    TextField {
        id: nameField

        objectName: "newFileName"
        visible: tree.adding
        x: Theme.space.sm
        y: heading.height
        width: parent.width - 2 * Theme.space.sm
        dense: true
        mono: true
        placeholderText: qsTr("File name, such as helper.ts")
        Keys.onReturnPressed: tree.finishAdding()
        Keys.onEnterPressed: tree.finishAdding()
        Keys.onEscapePressed: tree.adding = false
        onActiveFocusChanged: {
            if (!nameField.activeFocus && nameField.text.trim() === "")
                tree.adding = false
        }
        onTextEdited: nameField.errorText = ""
    }

    ListView {
        id: files

        objectName: "fileTree"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tree.adding ? nameField.bottom : heading.bottom
        anchors.topMargin: tree.adding ? Theme.space.xs : 0
        anchors.bottom: footer.top
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: tree.bench.files

        delegate: Rectangle {
            id: row

            required property var modelData
            readonly property bool current: !modelData.folder && modelData.path === tree.bench.currentPath

            objectName: "fileRow"
            width: ListView.view.width
            height: Theme.size.listItem
            color: row.current ? Theme.colors.stateSelected : rowHover.hovered ? Theme.colors.stateHover : "transparent"

            Accessible.role: row.modelData.folder ? Accessible.TreeItem : Accessible.ListItem
            Accessible.name: row.modelData.name

            HoverHandler {
                id: rowHover
            }

            TapHandler {
                onTapped: {
                    if (row.modelData.folder)
                        tree.bench.toggleFolder(row.modelData.path)
                    else
                        tree.opened(row.modelData.path)
                }
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.space.sm + row.modelData.depth * Theme.space.lg
                width: Math.max(0, parent.width - x - Theme.space.sm)
                spacing: Theme.space.xs

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.modelData.folder ? (row.modelData.open ? "chevron-down" : "chevron-right") : ""
                    size: Theme.size.iconSmall
                    color: Theme.colors.textTertiary
                }

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: tree.iconOf(row.modelData)
                    size: Theme.size.iconSmall
                    color: row.modelData.folder ? Theme.colors.accentText : Theme.colors.textSecondary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, parent.width - 2 * (Theme.size.iconSmall + parent.spacing))
                    textFormat: Text.PlainText
                    text: row.modelData.name
                    elide: Text.ElideRight
                    color: row.current ? Theme.colors.textPrimary : Theme.colors.textSecondary
                    font.pixelSize: Theme.type.label.size
                }
            }
        }
    }

    Item {
        id: footer

        anchors.bottom: parent.bottom
        width: parent.width
        height: externalButton.height + 2 * Theme.space.sm

        Rectangle {
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }

        Button {
            id: externalButton

            objectName: "openExternalFooter"
            x: Theme.space.sm
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 2 * Theme.space.sm
            small: true
            tone: "ghost"
            iconName: "share"
            text: qsTr("Open in external editor")
            onClicked: tree.bench.openExternal()
        }
    }
}
