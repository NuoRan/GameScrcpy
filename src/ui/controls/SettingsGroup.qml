import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme

ColumnLayout {
    id: group

    property string title
    default property alias rows: list.data

    Layout.fillWidth: true
    spacing: Theme.space.xs

    Text {
        objectName: "groupTitle"
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.xxs
        visible: group.title !== ""
        textFormat: Text.PlainText
        text: group.title
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: list.implicitHeight
        radius: Theme.radius.lg
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle

        Column {
            id: list

            width: parent.width
        }
    }
}
