import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Card {
    id: card

    property string title
    property string note
    property string iconName
    property string tone
    default property alias rows: list.data
    readonly property int inset: Theme.space.lg + Theme.space.xxs
    readonly property var settingRows: {
        const found = []
        for (let i = 0; i < list.children.length; ++i) {
            const row = list.children[i] as SettingRow
            if (row !== null)
                found.push(row)
        }
        return found
    }
    readonly property bool matched: settingRows.some(row => row.matched)
    readonly property bool restorable: !SettingsCatalog.searching && settingRows.some(row => row.matched && row.changed)

    function restoreAll() {
        for (const row of settingRows) {
            if (row.matched && row.changed)
                row.restore()
        }
    }

    width: parent ? parent.width : implicitWidth
    visible: matched
    padding: 0
    horizontalPadding: 0

    Column {
        width: parent.width

        Item {
            visible: card.title !== ""
            width: parent.width
            height: Theme.size.tile + Theme.space.md + Theme.space.xxs + Theme.space.md

            Tile {
                id: tile

                x: card.inset
                y: Theme.space.md + Theme.space.xxs
                visible: card.iconName !== ""
                iconName: card.iconName
                tone: card.tone
            }

            Text {
                id: heading

                objectName: "heading"
                anchors.left: card.iconName !== "" ? tile.right : parent.left
                anchors.leftMargin: card.iconName !== "" ? Theme.space.md : card.inset
                anchors.verticalCenter: tile.verticalCenter
                textFormat: Text.PlainText
                text: card.title
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
                font.weight: Font.DemiBold
            }

            Text {
                objectName: "note"
                anchors.left: heading.right
                anchors.leftMargin: Theme.space.lg
                anchors.right: card.restorable ? restore.left : parent.right
                anchors.rightMargin: card.restorable ? Theme.space.md : card.inset
                anchors.verticalCenter: tile.verticalCenter
                visible: card.note !== ""
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: card.note
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Button {
                id: restore

                objectName: "restoreGroup"
                anchors.right: parent.right
                anchors.rightMargin: card.inset - Theme.space.sm
                anchors.verticalCenter: tile.verticalCenter
                visible: card.restorable
                tone: "ghost"
                small: true
                iconName: "rotate-ccw"
                text: SettingsCatalog.device === "" ? qsTr("Restore defaults") : qsTr("Back to global defaults")
                Accessible.description: card.title
                onClicked: card.restoreAll()
            }

            Rectangle {
                x: card.inset
                anchors.bottom: parent.bottom
                width: parent.width - 2 * card.inset
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }
        }

        Column {
            id: list

            width: parent.width
        }
    }
}
