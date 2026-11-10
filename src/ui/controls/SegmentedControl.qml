pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.Control {
    id: control

    property var model: []
    property int currentIndex: 0

    signal activated(int index)

    function select(index: int) {
        const choice = Math.max(0, Math.min(index, segments.count - 1))
        if (choice === currentIndex)
            return
        currentIndex = choice
        activated(choice)
    }

    function usable(index: int): bool {
        const entry = Array.isArray(control.model) ? control.model[index] : undefined
        return typeof entry === "string" || entry?.enabled !== false
    }

    function step(direction: int) {
        for (let index = currentIndex + direction; index >= 0 && index < segments.count; index += direction) {
            if (control.usable(index)) {
                control.select(index)
                return
            }
        }
    }

    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: Theme.space.xxs
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    opacity: enabled ? 1 : Theme.disabledOpacity
    font.pixelSize: Theme.type.label.size
    font.weight: Font.Medium

    Accessible.role: Accessible.PageTabList

    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)

    background: Rectangle {
        id: well

        radius: Theme.radius.sm + control.padding
        color: Theme.colors.bgSunken
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle

        FocusRing {
            visible: control.visualFocus
            baseRadius: well.radius
        }
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Rectangle {
            id: thumb

            readonly property Item target: segments.generation >= 0 && segments.count > 0
                                           ? segments.itemAt(control.currentIndex) : null

            visible: target !== null
            x: target ? target.x : 0
            width: target ? target.width : 0
            height: row.height
            radius: Theme.radius.sm
            color: Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

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
        }

        Row {
            id: row

            spacing: Theme.space.xxs

            Repeater {
                id: segments

                property int generation: 0

                model: control.model
                onItemAdded: generation++
                onItemRemoved: generation++

                delegate: T.AbstractButton {
                    id: segment

                    required property var modelData
                    required property int index
                    readonly property bool selected: index === control.currentIndex
                    readonly property bool plain: typeof modelData === "string"
                    readonly property string iconName: plain ? "" : modelData.icon ?? ""
                    readonly property color foreground: !enabled ? Theme.colors.textDisabled
                                                        : selected || hovered ? Theme.colors.textPrimary
                                                                              : Theme.colors.textSecondary

                    enabled: plain || modelData.enabled !== false
                    text: plain ? modelData : modelData.text ?? ""
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: true
                    implicitHeight: Theme.size.segment
                    implicitWidth: text === "" ? implicitHeight : implicitContentWidth + leftPadding + rightPadding
                    horizontalPadding: text === "" ? 0 : Theme.space.md
                    verticalPadding: 0
                    spacing: Theme.space.xs + Theme.space.xxs
                    font: control.font

                    Accessible.role: Accessible.PageTab
                    Accessible.name: text !== "" ? text : plain ? "" : modelData.description ?? ""
                    Accessible.selected: selected

                    onClicked: control.select(index)

                    contentItem: Item {
                        implicitWidth: label.implicitWidth
                        implicitHeight: label.implicitHeight

                        Row {
                            id: label

                            anchors.centerIn: parent
                            spacing: segment.spacing

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: segment.iconName !== ""
                                name: segment.iconName
                                size: Theme.size.iconSmall
                                color: segment.foreground
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: segment.text !== ""
                                textFormat: Text.PlainText
                                text: segment.text
                                font: segment.font
                                color: segment.foreground
                            }
                        }
                    }

                    background: StateLayer {
                        radius: Theme.radius.sm
                        hovered: segment.hovered && !segment.selected
                        pressed: segment.down && !segment.selected
                    }
                }
            }
        }
    }
}
