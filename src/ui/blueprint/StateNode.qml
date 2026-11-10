pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

import "Geometry.js" as Geometry

Item {
    id: card

    required property string key
    property var info: ({})
    property string shape: ""
    property real modelX: 0
    property real modelY: 0
    property real moveX: 0
    property real moveY: 0
    property bool selected: false
    property bool readOnly: false
    property bool target: false
    property real now: 0
    property bool renaming: false
    property real glowLevel: 0
    readonly property bool active: info.active === true
    readonly property bool first: info.start === true
    readonly property bool visited: info.visited === true
    readonly property bool reroute: false
    readonly property real inX: 0
    readonly property real outX: width
    readonly property real pad: Theme.space.md

    signal bodyPressed(int button, int modifiers, point scene)
    signal bodyMoved(point scene)
    signal bodyReleased(point scene)
    signal bodyDoubleClicked(int button)
    signal ringPressed(int button, point scene)
    signal ringMoved(point scene)
    signal ringReleased(point scene)
    signal renamed(string name)
    signal renameFinished()

    function apply(next: var) {
        const copy = Object.assign({}, next)
        delete copy.x
        delete copy.y
        const text = JSON.stringify(copy)
        if (text !== shape) {
            shape = text
            info = next
        }
        modelX = next.x ?? 0
        modelY = next.y ?? 0
    }

    function glow() {
        pulse.restart()
    }

    function anchorY(side: string, pin: string): real {
        return height / 2
    }

    function pinAt(localY: real, side: string): string {
        return ""
    }

    function rename() {
        if (!readOnly)
            renaming = true
    }

    function finishRename(keep: bool, text: string) {
        if (!renaming)
            return
        renaming = false
        const name = text.trim()
        if (keep && name !== "" && name !== (info.title ?? ""))
            renamed(name)
        renameFinished()
    }

    objectName: "bpNode_" + key
    x: modelX + moveX
    y: modelY + moveY
    z: selected ? 1 : 0
    width: Theme.size.bpStateWidth
    height: content.implicitHeight + 2 * pad

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("%1: %2").arg(BlueprintLook.kindName("state")).arg(info.title ?? "")
    Accessible.description: info.subtitle ?? ""

    NumberAnimation {
        id: pulse

        target: card
        property: "glowLevel"
        from: 1
        to: 0
        duration: Theme.motion.pulse
        easing.type: Easing.OutCubic
    }

    Item {
        objectName: "bpRing_" + card.key
        anchors.fill: parent
        anchors.margins: -Theme.size.bpRing

        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.size.bpRing / 2
            visible: card.target || (ringArea.containsMouse && !card.readOnly)
            radius: face.radius + Theme.size.bpRing / 2
            color: "transparent"
            border.width: Theme.size.focusRing
            border.color: card.target ? Theme.colors.accent : Theme.colors.accentLine
        }

        MouseArea {
            id: ringArea

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            cursorShape: card.readOnly ? Qt.ArrowCursor : Qt.CrossCursor
            onPressed: mouse => card.ringPressed(mouse.button, ringArea.mapToItem(null, mouse.x, mouse.y))
            onPositionChanged: mouse => {
                if (ringArea.pressed)
                    card.ringMoved(ringArea.mapToItem(null, mouse.x, mouse.y))
            }
            onReleased: mouse => card.ringReleased(ringArea.mapToItem(null, mouse.x, mouse.y))
        }
    }

    Loader {
        x: -Theme.size.bpEntryLead
        y: card.height / 2 - Theme.size.bpPin / 2
        active: card.first

        sourceComponent: Item {
            width: Theme.size.bpEntryLead
            height: Theme.size.bpPin

            Rectangle {
                width: Theme.size.bpPin
                height: width
                radius: width / 2
                color: Theme.colors.textSecondary
            }

            Shape {
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Theme.colors.textSecondary
                    strokeWidth: Theme.size.bpTransition
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: Theme.size.bpPin
                    startY: Theme.size.bpPin / 2

                    PathLine {
                        x: Theme.size.bpEntryLead - Theme.size.bpArrow
                        y: Theme.size.bpPin / 2
                    }
                }

                ShapePath {
                    strokeWidth: 0
                    strokeColor: "transparent"
                    fillColor: Theme.colors.textSecondary

                    PathSvg {
                        path: Geometry.arrowHead(Theme.size.bpEntryLead, Theme.size.bpPin / 2, 0,
                                                 Theme.size.bpPin / 2, Theme.size.bpArrow)
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: face
        anchors.margins: -(Theme.space.xs + Theme.size.focusRing)
        visible: card.glowLevel > 0
        opacity: card.glowLevel
        radius: face.radius + Theme.space.xs + Theme.size.focusRing
        color: Theme.colors.accentGlow
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accentEdge
    }

    Rectangle {
        anchors.fill: face
        anchors.margins: -Theme.space.xs
        visible: card.active || card.selected
        radius: face.radius + Theme.space.xs
        color: "transparent"
        border.width: Theme.space.xs
        border.color: Theme.colors.accentHalo
    }

    RectangularShadow {
        anchors.fill: face
        radius: face.radius
        offset.y: Theme.shadow.keyOffset
        blur: Theme.shadow.keyBlur
        color: Theme.shadow.key
    }

    Rectangle {
        id: face

        anchors.fill: parent
        radius: Theme.radius.lg + Theme.space.xxs
        color: Theme.colors.bpNode
        border.width: card.visited && !card.active && !card.selected ? Theme.size.focusRing : Theme.size.stroke
        border.color: card.active || card.selected ? Theme.colors.accent
                    : card.visited ? Theme.colors.textTertiary : Theme.colors.bpNodeStroke
    }

    MouseArea {
        id: body

        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        preventStealing: true
        enabled: !card.renaming
        cursorShape: card.readOnly ? Qt.ArrowCursor : Qt.SizeAllCursor
        onPressed: mouse => card.bodyPressed(mouse.button, mouse.modifiers, body.mapToItem(null, mouse.x, mouse.y))
        onPositionChanged: mouse => card.bodyMoved(body.mapToItem(null, mouse.x, mouse.y))
        onReleased: mouse => card.bodyReleased(body.mapToItem(null, mouse.x, mouse.y))
        onDoubleClicked: mouse => card.bodyDoubleClicked(mouse.button)
    }

    Column {
        id: content

        x: card.pad
        y: card.pad
        width: card.width - 2 * card.pad
        spacing: Theme.space.xxs

        Item {
            width: parent.width
            height: Math.max(titleText.implicitHeight, nameLoader.active ? nameLoader.height : 0,
                             clockLoader.active ? clockLoader.height : 0)

            Row {
                id: lead

                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.sm

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: (card.info.icon ?? "") !== ""
                    name: card.info.icon ?? ""
                    size: Theme.size.iconSmall
                    color: card.active ? Theme.colors.accentText : Theme.colors.textSecondary
                }

                Item {
                    width: 0
                    height: Theme.size.stroke
                }
            }

            Text {
                id: titleText

                x: lead.width
                width: Math.max(0, (clockLoader.active ? clockLoader.x - Theme.space.xs : parent.width) - x)
                anchors.verticalCenter: parent.verticalCenter
                visible: !card.renaming
                textFormat: Text.PlainText
                text: card.info.title ?? ""
                elide: Text.ElideRight
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.DemiBold
            }

            Loader {
                id: nameLoader

                x: lead.width
                width: parent.width - x
                anchors.verticalCenter: parent.verticalCenter
                active: card.renaming

                sourceComponent: TextField {
                    id: field

                    objectName: "bpStateName_" + card.key
                    dense: true
                    Accessible.name: qsTr("Name of the state")
                    Component.onCompleted: {
                        field.text = card.info.title ?? ""
                        field.selectAll()
                        field.forceActiveFocus(Qt.MouseFocusReason)
                    }
                    onAccepted: card.finishRename(true, field.text)
                    onActiveFocusChanged: {
                        if (!field.activeFocus)
                            card.finishRename(true, field.text)
                    }
                    Keys.onEscapePressed: card.finishRename(false, "")
                }
            }

            Loader {
                id: clockLoader

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                active: card.active && (card.info.activeSince ?? 0) > 0 && !card.renaming

                sourceComponent: StatusPill {
                    objectName: "bpStateClock_" + card.key
                    tone: "live"
                    text: Geometry.clock(card.now - (card.info.activeSince ?? 0))
                }
            }
        }

        Loader {
            width: parent.width
            active: (card.info.subtitle ?? "") !== ""
            visible: active

            sourceComponent: Text {
                textFormat: Text.PlainText
                text: card.info.subtitle ?? ""
                elide: Text.ElideRight
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }
        }
    }
}
