pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Controls

import "Geometry.js" as Geometry

Shape {
    id: arrow

    required property string key
    property var info: ({})
    property string shape: ""
    property rect fromRect: Qt.rect(0, 0, 0, 0)
    property rect toRect: Qt.rect(0, 0, 0, 0)
    property int lane: 0
    property bool selected: false
    property bool readOnly: false
    property bool renaming: false
    readonly property bool active: info.active === true
    readonly property bool loop: (info.from ?? "") !== "" && info.from === info.to
    readonly property color tone: active || selected ? Theme.colors.accent : Theme.colors.bpTransition
    readonly property var route: {
        const length = Theme.size.bpArrow
        if (loop) {
            const cx = fromRect.x + fromRect.width / 2
            const top = fromRect.y
            const spread = Theme.size.bpLoop / 2
            const lift = Theme.size.bpLoop
            const tipX = cx + spread
            const tipY = top
            const fromX = cx + lift
            const fromY = top - lift
            const dx = tipX - fromX
            const dy = tipY - fromY
            const size = Math.sqrt(dx * dx + dy * dy) || 1
            const curve = { x0: cx - spread, y0: top, c0x: cx - lift, c0y: top - lift, c1x: fromX, c1y: fromY,
                            x1: tipX - dx / size * length, y1: tipY - dy / size * length }
            return { curve: curve, head: Geometry.arrowHead(tipX, tipY, fromX, fromY, length),
                     labelX: cx, labelY: Geometry.cubicAt(0.5, curve.y0, curve.c0y, curve.c1y, curve.y1)
                                     - Theme.size.pill / 2 - Theme.space.xxs }
        }
        const ax = fromRect.x + fromRect.width / 2
        const ay = fromRect.y + fromRect.height / 2
        const bx = toRect.x + toRect.width / 2
        const by = toRect.y + toRect.height / 2
        const dx = bx - ax
        const dy = by - ay
        const size = Math.sqrt(dx * dx + dy * dy) || 1
        const bendX = (ax + bx) / 2 - dy / size * lane * Theme.size.bpCurve
        const bendY = (ay + by) / 2 + dx / size * lane * Theme.size.bpCurve
        const start = Geometry.borderPoint({ x: fromRect.x, y: fromRect.y, width: fromRect.width,
                                             height: fromRect.height }, bendX, bendY)
        const end = Geometry.borderPoint({ x: toRect.x, y: toRect.y, width: toRect.width, height: toRect.height },
                                         bendX, bendY)
        const qx = lane === 0 ? (start.x + end.x) / 2 : 2 * bendX - (start.x + end.x) / 2
        const qy = lane === 0 ? (start.y + end.y) / 2 : 2 * bendY - (start.y + end.y) / 2
        const ex = end.x - qx
        const ey = end.y - qy
        const run = Math.sqrt(ex * ex + ey * ey) || 1
        const stopX = end.x - ex / run * length
        const stopY = end.y - ey / run * length
        const curve = { x0: start.x, y0: start.y, c0x: start.x + 2 / 3 * (qx - start.x),
                        c0y: start.y + 2 / 3 * (qy - start.y), c1x: stopX + 2 / 3 * (qx - stopX),
                        c1y: stopY + 2 / 3 * (qy - stopY), x1: stopX, y1: stopY }
        return { curve: curve, head: Geometry.arrowHead(end.x, end.y, qx, qy, length),
                 labelX: Geometry.quadAt(0.5, start.x, qx, end.x), labelY: Geometry.quadAt(0.5, start.y, qy, end.y) }
    }
    readonly property var curve: route.curve

    signal labelPressed(int button, int modifiers, point scene)
    signal labelMoved(point scene)
    signal labelReleased(point scene)
    signal renamed(string text)
    signal renameFinished()

    function apply(next: var) {
        const text = JSON.stringify(next)
        if (text !== shape) {
            shape = text
            info = next
        }
    }

    function rename() {
        if (!readOnly)
            renaming = true
    }

    function finishRename(keep: bool, text: string) {
        if (!renaming)
            return
        renaming = false
        if (keep && text !== (info.label ?? ""))
            renamed(text)
        renameFinished()
    }

    objectName: "bpWire_" + key
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeColor: arrow.tone
        strokeWidth: arrow.active || arrow.selected ? Theme.size.bpTransitionActive : Theme.size.bpTransition
        strokeStyle: arrow.info.dashed === true ? ShapePath.DashLine : ShapePath.SolidLine
        dashPattern: [Theme.space.xxs, Theme.space.xxs]
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        startX: arrow.route.curve.x0
        startY: arrow.route.curve.y0

        PathCubic {
            control1X: arrow.route.curve.c0x
            control1Y: arrow.route.curve.c0y
            control2X: arrow.route.curve.c1x
            control2Y: arrow.route.curve.c1y
            x: arrow.route.curve.x1
            y: arrow.route.curve.y1
        }
    }

    ShapePath {
        strokeColor: "transparent"
        strokeWidth: 0
        fillColor: arrow.tone

        PathSvg {
            path: arrow.route.head
        }
    }

    Loader {
        x: arrow.route.labelX - width / 2
        y: arrow.route.labelY - height / 2
        active: (arrow.info.label ?? "") !== "" || arrow.renaming

        sourceComponent: Rectangle {
            id: chip

            objectName: "bpTransitionLabel_" + arrow.key
            width: arrow.renaming ? nameLoader.width : labelText.implicitWidth + 2 * (Theme.space.xs + Theme.space.xxs)
            height: arrow.renaming ? nameLoader.height : Theme.size.pill
            radius: height / 2
            color: Theme.colors.bpLabel
            border.width: Theme.size.stroke
            border.color: arrow.selected ? Theme.colors.accent
                        : arrow.active ? Theme.colors.accentLine : Theme.colors.bpBubbleStroke

            Accessible.role: Accessible.Button
            Accessible.name: arrow.info.label ?? ""

            Text {
                id: labelText

                anchors.centerIn: parent
                visible: !arrow.renaming
                textFormat: Text.PlainText
                text: arrow.info.label ?? ""
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.micro.size
            }

            MouseArea {
                id: chipArea

                anchors.fill: parent
                enabled: !arrow.renaming
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onPressed: mouse => arrow.labelPressed(mouse.button, mouse.modifiers,
                                                       chipArea.mapToItem(null, mouse.x, mouse.y))
                onPositionChanged: mouse => arrow.labelMoved(chipArea.mapToItem(null, mouse.x, mouse.y))
                onReleased: mouse => arrow.labelReleased(chipArea.mapToItem(null, mouse.x, mouse.y))
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton)
                        arrow.rename()
                }
            }

            Loader {
                id: nameLoader

                active: arrow.renaming

                sourceComponent: TextField {
                    id: field

                    objectName: "bpTransitionName_" + arrow.key
                    width: Theme.size.fieldWidth
                    dense: true
                    Accessible.name: qsTr("Reason of the transition")
                    Component.onCompleted: {
                        field.text = arrow.info.label ?? ""
                        field.selectAll()
                        field.forceActiveFocus(Qt.MouseFocusReason)
                    }
                    onAccepted: arrow.finishRename(true, field.text)
                    onActiveFocusChanged: {
                        if (!field.activeFocus)
                            arrow.finishRename(true, field.text)
                    }
                    Keys.onEscapePressed: arrow.finishRename(false, "")
                }
            }
        }
    }
}
