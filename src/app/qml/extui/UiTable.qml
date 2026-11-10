pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme

Column {
    id: table

    property var node: null
    readonly property var columns: table.node?.columns ?? []
    readonly property int columnCount: table.columns.length
    readonly property int rowCount: (table.node?.rows ?? []).length
    readonly property real cellWidth: table.columnCount > 0 ? table.width / table.columnCount : table.width

    Accessible.role: Accessible.Table

    Rectangle {
        width: table.width
        height: Theme.size.listItem
        radius: Theme.radius.sm
        color: Theme.colors.stateHover

        Row {
            anchors.fill: parent

            Repeater {
                model: table.columnCount

                delegate: Text {
                    id: header

                    required property int index

                    width: table.cellWidth
                    height: Theme.size.listItem
                    leftPadding: Theme.space.sm
                    rightPadding: Theme.space.sm
                    verticalAlignment: Text.AlignVCenter
                    textFormat: Text.PlainText
                    text: table.columns[header.index] ?? ""
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Accessible.role: Accessible.ColumnHeader
                }
            }
        }
    }

    Repeater {
        model: table.rowCount

        delegate: Item {
            id: line

            required property int index
            readonly property var cells: (table.node?.rows ?? [])[line.index] ?? []

            width: table.width
            height: Theme.size.listItem

            Accessible.role: Accessible.Row

            Row {
                anchors.fill: parent

                Repeater {
                    model: table.columnCount

                    delegate: Text {
                        id: cell

                        required property int index

                        width: table.cellWidth
                        height: Theme.size.listItem
                        leftPadding: Theme.space.sm
                        rightPadding: Theme.space.sm
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: line.cells[cell.index] ?? ""
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        elide: Text.ElideRight
                        Accessible.role: Accessible.Cell
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }
        }
    }
}
