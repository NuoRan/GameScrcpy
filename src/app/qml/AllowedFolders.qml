pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Column {
    id: allowed

    required property string owner
    readonly property var folders: FileAccess.folders[allowed.owner] ?? []
    readonly property int inset: Theme.space.lg + Theme.space.xxs

    objectName: "allowedFolders"
    width: parent ? parent.width : 0
    visible: allowed.folders.length > 0

    Repeater {
        model: allowed.folders

        delegate: Item {
            id: folderRow

            required property var modelData
            required property int index

            objectName: "allowedFolder"
            width: allowed.width
            implicitHeight: Math.max(Theme.size.settingsRow, line.implicitHeight + 2 * Theme.space.md)

            Rectangle {
                visible: folderRow.index > 0
                x: allowed.inset
                width: folderRow.width - 2 * allowed.inset
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }

            RowLayout {
                id: line

                x: allowed.inset
                width: folderRow.width - 2 * allowed.inset
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.md

                Icon {
                    name: "folder-open"
                    size: Theme.size.iconSmall
                    color: Theme.colors.textSecondary
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.xxs

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: [folderRow.modelData.name ?? "", folderRow.modelData.purpose ?? ""]
                              .filter(part => part !== "").join(" · ")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.body.size
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    Text {
                        objectName: "folderPath"
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: folderRow.modelData.path ?? ""
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        elide: Text.ElideMiddle
                    }
                }

                Button {
                    objectName: "forgetFolder"
                    small: true
                    tone: "ghost"
                    text: qsTr("Forget")
                    Accessible.description: folderRow.modelData.path ?? ""
                    onClicked: FileAccess.forgetFolder(allowed.owner, folderRow.modelData.name ?? "")
                }
            }
        }
    }
}
