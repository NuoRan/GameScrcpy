pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

Item {
    id: strip

    property var pages: []
    property var crumbs: []
    property string current: ""
    readonly property bool nested: (crumbs ?? []).length > 1

    signal pageChosen(string key)
    signal crumbChosen(string key)

    objectName: "bpPages"
    implicitWidth: Theme.size.workbenchCode
    implicitHeight: Theme.size.tab + (nested ? Theme.size.bpCrumb : 0)

    Accessible.role: Accessible.PageTabList

    ListView {
        id: tabs

        width: parent.width
        height: Theme.size.tab
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        model: strip.pages ?? []

        delegate: T.AbstractButton {
            id: tab

            required property var modelData
            readonly property bool selected: (modelData.key ?? "") === strip.current

            objectName: "bpPage_" + (modelData.key ?? "")
            height: tabs.height
            implicitWidth: label.implicitWidth + 2 * (Theme.space.md + Theme.space.xxs)
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            Accessible.role: Accessible.PageTab
            Accessible.name: modelData.title ?? ""
            Accessible.selected: selected
            onClicked: {
                if (!tab.selected)
                    strip.pageChosen(tab.modelData.key ?? "")
            }

            background: Rectangle {
                color: tab.selected ? Theme.colors.stateHover : "transparent"

                StateLayer {
                    anchors.fill: parent
                    visible: !tab.selected
                    hovered: tab.hovered
                    pressed: tab.down
                }

                Rectangle {
                    visible: tab.selected
                    width: parent.width
                    height: Theme.size.tabIndicator
                    color: Theme.colors.accent
                }

                Rectangle {
                    anchors.right: parent.right
                    width: Theme.size.stroke
                    height: parent.height
                    color: Theme.colors.strokeSubtle
                }

                FocusRing {
                    visible: tab.visualFocus
                    anchors.margins: Theme.size.focusRing
                }
            }

            contentItem: Item {
                Row {
                    id: label

                    anchors.centerIn: parent
                    spacing: Theme.space.xs + Theme.space.xxs

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: (tab.modelData.icon ?? "") !== ""
                        name: tab.modelData.icon ?? ""
                        size: Theme.size.iconSmall - Theme.space.xxs
                        color: tab.selected ? Theme.colors.accentText : Theme.colors.textTertiary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: tab.modelData.title ?? ""
                        color: tab.selected || tab.hovered ? Theme.colors.textPrimary : Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: tab.selected ? Font.Medium : Font.Normal
                    }
                }
            }
        }
    }

    Rectangle {
        y: Theme.size.tab - height
        width: parent.width
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    Item {
        objectName: "bpCrumbs"
        y: Theme.size.tab
        width: parent.width
        height: Theme.size.bpCrumb
        visible: strip.nested

        Row {
            x: Theme.space.md
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.xs

            Repeater {
                model: strip.nested ? strip.crumbs : []

                delegate: Row {
                    id: crumb

                    required property var modelData
                    required property int index
                    readonly property bool last: index === strip.crumbs.length - 1

                    spacing: Theme.space.xs

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: crumb.index > 0
                        name: "chevron-right"
                        size: Theme.size.iconSmall - Theme.space.xs
                        color: Theme.colors.textTertiary
                    }

                    Text {
                        objectName: "bpCrumb_" + crumb.index
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: crumb.modelData.title ?? ""
                        color: crumb.last || crumbArea.containsMouse ? Theme.colors.textPrimary
                                                                     : Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: crumb.last ? Font.DemiBold : Font.Normal

                        Accessible.role: crumb.last ? Accessible.StaticText : Accessible.Link
                        Accessible.name: text

                        MouseArea {
                            id: crumbArea

                            anchors.fill: parent
                            enabled: !crumb.last
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: strip.crumbChosen(crumb.modelData.key ?? "")
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }
    }
}
