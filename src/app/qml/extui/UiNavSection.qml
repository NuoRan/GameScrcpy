pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Column {
    id: section

    property var pages: []
    property string currentId: ""
    property bool active: false
    property real maximumHeight: Theme.size.navItemHeight

    signal picked(var entry)

    objectName: "extensionNav"
    visible: section.pages.length > 0
    spacing: Theme.space.xs

    Accessible.role: Accessible.Grouping
    Accessible.name: heading.text

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: Theme.size.navItemWidth / 2
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    Text {
        id: heading

        width: Theme.size.navItemWidth
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        text: qsTr("Extensions")
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.micro.size
        font.weight: Theme.type.micro.weight
        elide: Text.ElideRight
    }

    Flickable {
        id: scroller

        width: Theme.size.navItemWidth
        height: Math.min(items.implicitHeight,
                         Math.max(Theme.size.navItemHeight, section.maximumHeight - heading.implicitHeight
                                  - Theme.size.stroke - 2 * section.spacing))
        contentWidth: width
        contentHeight: items.implicitHeight
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: items

            spacing: Theme.space.xs

            Repeater {
                model: section.pages

                delegate: NavItem {
                    id: item

                    required property var modelData

                    objectName: "extensionNavItem"
                    text: metrics.elidedText
                    iconName: item.modelData.icon !== "" || item.modelData.iconPath !== "" ? item.modelData.icon : "puzzle"
                    iconPath: item.modelData.iconPath
                    current: section.active && section.currentId === item.modelData.id
                    Accessible.name: item.modelData.title
                    Accessible.description: item.modelData.extensionName
                    onClicked: section.picked(item.modelData)

                    TextMetrics {
                        id: metrics

                        font: item.font
                        text: item.modelData.title
                        elide: Qt.ElideRight
                        elideWidth: Theme.size.navItemWidth - Theme.space.xs
                    }

                    Tooltip {
                        x: item.width + Theme.space.xs
                        y: (item.height - height) / 2
                        text: qsTr("%1 · %2").arg(item.modelData.title).arg(item.modelData.extensionName)
                        visible: item.hovered && !item.down
                    }
                }
            }
        }
    }
}
