pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.Control {
    id: control

    property var model: []
    property int currentIndex: 0

    signal activated(int index)

    function select(index: int) {
        const choice = Math.max(0, Math.min(index, tabs.count - 1))
        if (choice === currentIndex)
            return
        currentIndex = choice
        activated(choice)
    }

    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Theme.size.tab
    opacity: enabled ? 1 : Theme.disabledOpacity
    font.pixelSize: Theme.type.label.size

    Accessible.role: Accessible.PageTabList

    Keys.onLeftPressed: select(currentIndex - 1)
    Keys.onRightPressed: select(currentIndex + 1)

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: Theme.size.tab

        Row {
            id: row

            height: parent.height
            spacing: Theme.space.xl

            Repeater {
                id: tabs

                property int generation: 0

                model: control.model
                onItemAdded: generation++
                onItemRemoved: generation++

                delegate: T.AbstractButton {
                    id: tab

                    required property var modelData
                    required property int index
                    readonly property bool selected: index === control.currentIndex
                    readonly property bool plain: typeof modelData === "string"
                    readonly property var count: plain ? undefined : modelData.count
                    readonly property color foreground: selected || hovered ? Theme.colors.textPrimary
                                                                            : Theme.colors.textSecondary

                    text: plain ? modelData : modelData.text ?? ""
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: true
                    height: row.height
                    implicitWidth: implicitContentWidth
                    padding: 0
                    font.pixelSize: control.font.pixelSize
                    font.weight: selected ? Font.DemiBold : Font.Normal

                    Accessible.role: Accessible.PageTab
                    Accessible.name: text
                    Accessible.selected: selected

                    onClicked: control.select(index)

                    contentItem: Row {
                        spacing: Theme.space.xs + Theme.space.xxs

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: tab.text
                            font: tab.font
                            color: tab.foreground
                        }

                        Rectangle {
                            objectName: "tabCount"
                            anchors.verticalCenter: parent.verticalCenter
                            visible: tab.count !== undefined
                            height: Theme.size.badge
                            width: Math.max(height, countText.implicitWidth + 2 * Theme.space.xs)
                            radius: height / 2
                            color: Theme.colors.statePress

                            Text {
                                id: countText

                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: tab.count !== undefined ? String(tab.count) : ""
                                font.pixelSize: Theme.type.micro.size
                                font.weight: Theme.type.micro.weight
                                color: Theme.colors.textSecondary
                            }
                        }
                    }

                    background: Item {}
                }
            }
        }

        Item {
            id: current

            readonly property Item target: tabs.generation >= 0 && tabs.count > 0
                                            ? tabs.itemAt(control.currentIndex) : null

            visible: target !== null
            x: target ? target.x : 0
            width: target ? target.width : 0
            height: parent.height

            Behavior on x {
                NumberAnimation {
                    duration: Theme.motion.base
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.motion.emphasized
                }
            }
            Behavior on width {
                NumberAnimation {
                    duration: Theme.motion.base
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.motion.emphasized
                }
            }

            Rectangle {
                objectName: "tabIndicator"
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.size.tabIndicator
                radius: height / 2
                color: Theme.colors.accent
            }

            FocusRing {
                visible: control.visualFocus
                baseRadius: Theme.radius.xs
            }
        }
    }
}
