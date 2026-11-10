pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Column {
    id: list

    property var node: null
    property string current: ""
    readonly property bool selectable: list.node?.selectable !== false
    readonly property int count: (list.node?.items ?? []).length

    signal picked(string item)

    function sync() {
        list.current = list.node?.selected ?? ""
    }

    spacing: Theme.space.xxs
    onNodeChanged: list.sync()
    Component.onCompleted: list.sync()

    Accessible.role: Accessible.List

    Repeater {
        model: list.count

        delegate: T.AbstractButton {
            id: row

            required property int index
            readonly property var item: (list.node?.items ?? [])[row.index] ?? ({})
            readonly property bool chosen: list.current !== "" && list.current === (row.item.id ?? "")
            readonly property string glyph: ExtensionPanels.iconOf(row.item.icon ?? "")
            readonly property string subtitle: row.item.subtitle ?? ""

            width: list.width
            implicitHeight: Math.max(Theme.size.listItem, body.implicitHeight + 2 * Theme.space.xs)
            hoverEnabled: list.selectable
            focusPolicy: list.selectable ? Qt.TabFocus : Qt.NoFocus
            text: row.item.title ?? ""

            Accessible.role: Accessible.ListItem
            Accessible.name: row.text
            Accessible.description: row.subtitle
            Accessible.selected: row.chosen

            onClicked: {
                if (!list.selectable)
                    return
                list.current = row.item.id ?? ""
                list.picked(list.current)
            }

            background: Rectangle {
                id: face

                radius: Theme.radius.md
                color: row.chosen ? Theme.colors.stateSelected : "transparent"

                StateLayer {
                    anchors.fill: parent
                    radius: face.radius
                    hovered: row.hovered
                    pressed: row.down && list.selectable
                }

                FocusRing {
                    visible: row.visualFocus
                    baseRadius: face.radius
                }
            }

            contentItem: Item {
                Row {
                    id: body

                    x: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 2 * Theme.space.sm
                    spacing: Theme.space.sm

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.glyph !== ""
                        name: row.glyph
                        size: Theme.size.iconSmall
                        color: row.chosen ? Theme.colors.accentText : Theme.colors.textSecondary
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: body.width - (row.glyph !== "" ? Theme.size.iconSmall + body.spacing : 0)

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: row.text
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: row.chosen ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            visible: row.subtitle !== ""
                            textFormat: Text.PlainText
                            text: row.subtitle
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
