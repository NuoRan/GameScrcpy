pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Item {
    id: art

    property string stage: "overview"
    readonly property bool trying: art.stage === "try"
    readonly property real barHeight: 38
    readonly property real panelWidth: 156
    readonly property point drop: Qt.point(318, 132)

    implicitWidth: 640
    implicitHeight: 306

    component Glyph: Item {
        id: glyph

        property string iconName
        property bool on: false

        width: 24
        height: 24

        Rectangle {
            anchors.fill: parent
            visible: glyph.on
            radius: Theme.radius.sm
            color: Theme.colors.accentSubtle
        }

        Icon {
            anchors.centerIn: parent
            name: glyph.iconName
            size: Theme.size.iconSmall - Theme.space.xxs
            color: glyph.on ? Theme.colors.accentText : Theme.colors.textSecondary
        }
    }

    component Part: Row {
        id: part

        property string iconName
        property string label
        property bool hot: false

        spacing: Theme.space.sm

        Rectangle {
            width: 22
            height: 22
            radius: Theme.radius.sm
            color: part.hot ? Theme.colors.accentSubtle : Theme.colors.bgOverlay
            border.width: Theme.size.stroke
            border.color: part.hot ? Theme.colors.accentLine : Theme.colors.strokeDefault

            Icon {
                anchors.centerIn: parent
                name: part.iconName
                size: Theme.size.keyHintTag - Theme.space.xxs
                color: part.hot ? Theme.colors.accentText : Theme.colors.textSecondary
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: part.label
            color: part.hot ? Theme.colors.textPrimary : Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
            font.weight: part.hot ? Font.DemiBold : Font.Normal
        }
    }

    HelpScreen {
        id: window

        x: Theme.space.lg
        y: Theme.space.sm
        width: 608
        height: 290
        scene: ""

        Item {
            id: picture

            y: art.barHeight
            width: art.trying ? parent.width : parent.width - art.panelWidth
            height: parent.height - art.barHeight
            clip: true

            HelpScene {
                anchors.fill: parent
                kind: "moba"
            }

            Rectangle {
                anchors.fill: parent
                visible: !art.trying
                color: Theme.colors.canvasDim
            }

            Rectangle {
                id: ring

                x: 28
                y: 132
                width: 82
                height: width
                radius: width / 2
                color: Theme.colors.keyHintRingFill
                border.width: Theme.size.keyHintRing
                border.color: Theme.colors.keyHintRing

                KeyHint { small: true; text: "W"; anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: parent.top }
                KeyHint { small: true; text: "S"; anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: parent.bottom }
                KeyHint { small: true; text: "A"; anchors.horizontalCenter: parent.left; anchors.verticalCenter: parent.verticalCenter }
                KeyHint { small: true; text: "D"; anchors.horizontalCenter: parent.right; anchors.verticalCenter: parent.verticalCenter }
            }

            KeyHint {
                id: skillQ

                x: 330
                y: 178
                shape: "aim"
                text: "Q"
            }

            KeyHint {
                x: 384
                y: 150
                shape: "aim"
                text: "E"
            }

            KeyHint {
                id: skillR

                x: 392
                y: 202
                shape: "tap"
                text: art.stage === "conflict" ? "Q" : "R"
            }

            Repeater {
                model: art.stage === "conflict" ? [skillQ, skillR] : []

                delegate: Rectangle {
                    required property Item modelData

                    x: modelData.x - Theme.space.xs
                    y: modelData.y - Theme.space.xs
                    width: modelData.width + 2 * Theme.space.xs
                    height: modelData.height + 2 * Theme.space.xs
                    radius: height / 2
                    color: "transparent"
                    border.width: Theme.size.focusRing
                    border.color: Theme.colors.danger
                }
            }

            Rectangle {
                x: art.drop.x - width / 2
                y: art.drop.y - height / 2
                visible: art.stage === "drag"
                width: Theme.size.keyHintRound + Theme.space.sm
                height: width
                radius: width / 2
                color: Theme.colors.accentGlow
                border.width: Theme.size.stroke
                border.color: Theme.colors.accentLine
            }

            KeyHint {
                id: placed

                x: art.drop.x - width / 2
                y: art.drop.y - height / 2
                visible: art.stage === "capture" || art.stage === "overview" || art.stage === "try"
                shape: "tap"
                text: art.stage === "capture" ? "" : "J"
                iconName: art.stage === "capture" ? "keyboard" : ""
                border.color: art.stage === "capture" ? Theme.colors.accent : Theme.colors.keyHintStroke
                border.width: art.stage === "capture" ? Theme.size.focusRing : Theme.size.stroke
            }

            Rectangle {
                id: capture

                visible: art.stage === "capture"
                x: art.drop.x - width / 2
                y: placed.y + placed.height + Theme.space.sm
                width: captureColumn.implicitWidth + 2 * Theme.space.md
                height: captureColumn.implicitHeight + 2 * Theme.space.sm + Theme.space.xxs
                radius: Theme.radius.lg
                color: Theme.colors.bgElevated
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeStrong

                Column {
                    id: captureColumn

                    anchors.centerIn: parent
                    spacing: Theme.space.xxs

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        textFormat: Text.PlainText
                        text: qsTr("Press a key")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: Font.DemiBold
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        textFormat: Text.PlainText
                        text: qsTr("or click a mouse button")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.micro.size
                        font.weight: Font.Normal
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        textFormat: Text.PlainText
                        text: qsTr("Skip")
                        color: Theme.colors.accentText
                        font.pixelSize: Theme.type.micro.size
                    }
                }
            }
        }

        Rectangle {
            id: bar

            width: parent.width
            height: art.barHeight
            color: Theme.colors.bgRaised

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }

            Row {
                x: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.sm

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: qsTr("My MOBA keys")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: art.stage === "conflict"
                    width: conflictRow.implicitWidth + 2 * Theme.space.sm
                    height: Theme.size.badge + Theme.space.xxs
                    radius: height / 2
                    color: Theme.colors.dangerSubtle
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.dangerLine

                    Row {
                        id: conflictRow

                        anchors.centerIn: parent
                        spacing: Theme.space.xs

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "triangle-alert"
                            size: Theme.size.keyHintTag - Theme.space.xxs
                            color: Theme.colors.dangerText
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: qsTr("1 conflict")
                            color: Theme.colors.dangerText
                            font.pixelSize: Theme.type.micro.size
                        }
                    }
                }
            }

            Rectangle {
                id: modes

                anchors.centerIn: parent
                width: modeRow.implicitWidth + 2 * Theme.space.xxs
                height: Theme.size.controlSmall - Theme.space.xs
                radius: height / 2
                color: Theme.colors.bgSunken
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeDefault

                Row {
                    id: modeRow

                    anchors.centerIn: parent

                    Repeater {
                        model: [qsTr("Picture"), qsTr("Logic")]

                        delegate: Rectangle {
                            id: segment

                            required property string modelData
                            required property int index
                            readonly property bool chosen: segment.index === 0

                            width: segmentText.implicitWidth + 2 * Theme.space.md
                            height: modes.height - 2 * Theme.space.xxs
                            radius: height / 2
                            color: segment.chosen ? Theme.colors.bgElevated : "transparent"
                            border.width: segment.chosen ? Theme.size.stroke : 0
                            border.color: Theme.colors.strokeStrong

                            Text {
                                id: segmentText

                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: segment.modelData
                                color: segment.chosen ? Theme.colors.textPrimary : Theme.colors.textTertiary
                                font.pixelSize: Theme.type.micro.size
                                font.weight: segment.chosen ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xxs

                Glyph { iconName: "undo-2" }
                Glyph { iconName: "redo-2" }
                Glyph { iconName: "flask-conical"; on: art.trying }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: doneText.implicitWidth + 2 * Theme.space.md
                    height: Theme.size.controlSmall - Theme.space.xs
                    radius: Theme.radius.md
                    color: Theme.colors.accent

                    Text {
                        id: doneText

                        anchors.centerIn: parent
                        textFormat: Text.PlainText
                        text: qsTr("Done")
                        color: Theme.colors.textOnAccent
                        font.pixelSize: Theme.type.micro.size
                    }
                }
            }
        }

        Rectangle {
            id: panel

            visible: !art.trying
            x: parent.width - width
            y: art.barHeight
            width: art.panelWidth
            height: parent.height - art.barHeight
            color: Theme.colors.bgRaised

            Rectangle {
                width: Theme.size.stroke
                height: parent.height
                color: Theme.colors.strokeSubtle
            }

            Row {
                id: tabs

                x: Theme.space.md
                y: Theme.space.sm
                spacing: Theme.space.md

                Repeater {
                    model: [qsTr("Library"), qsTr("Placed")]

                    delegate: Item {
                        id: tab

                        required property string modelData
                        required property int index

                        width: tabText.implicitWidth
                        height: tabText.implicitHeight + Theme.space.sm

                        Text {
                            id: tabText

                            textFormat: Text.PlainText
                            text: tab.modelData
                            color: tab.index === 0 ? Theme.colors.textPrimary : Theme.colors.textTertiary
                            font.pixelSize: Theme.type.micro.size
                            font.weight: tab.index === 0 ? Font.DemiBold : Font.Normal
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            visible: tab.index === 0
                            width: parent.width
                            height: Theme.size.tabIndicator
                            radius: height / 2
                            color: Theme.colors.accent
                        }
                    }
                }
            }

            Column {
                x: Theme.space.md
                y: tabs.y + tabs.height + Theme.space.md
                spacing: Theme.space.sm

                Part { iconName: "tap-echo"; label: qsTr("Tap"); hot: art.stage === "components" || art.stage === "drag" }
                Part { iconName: "timer"; label: qsTr("Long press") }
                Part { iconName: "zap"; label: qsTr("Turbo") }
                Part { iconName: "swipe-path"; label: qsTr("Swipe") }
                Part { iconName: "joystick-ring"; label: qsTr("Joystick") }
                Part { iconName: "crosshair"; label: qsTr("View") }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: Theme.space.xs
                visible: art.stage === "components"
                radius: Theme.radius.lg
                color: "transparent"
                border.width: Theme.size.focusRing
                border.color: Theme.colors.accentLine
            }
        }
    }

    Shape {
        anchors.fill: parent
        visible: art.stage === "drag"
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.colors.accent
            strokeWidth: 1.5
            strokeStyle: ShapePath.DashLine
            dashPattern: [3, 3]
            fillColor: "transparent"
            startX: window.x + window.width - art.panelWidth + Theme.space.md + 11
            startY: window.y + art.barHeight + 58

            PathCubic {
                x: window.x + art.drop.x + Theme.space.lg
                y: window.y + art.barHeight + art.drop.y
                control1X: window.x + window.width - art.panelWidth - 40
                control1Y: window.y + art.barHeight + 20
                control2X: window.x + art.drop.x + 90
                control2Y: window.y + art.barHeight + art.drop.y - 70
            }
        }
    }

    Rectangle {
        visible: art.stage === "drag"
        x: window.x + art.drop.x + Theme.space.lg
        y: window.y + art.barHeight + art.drop.y - height / 2
        width: ghost.implicitWidth + 2 * Theme.space.sm
        height: ghost.implicitHeight + Theme.space.sm
        radius: Theme.radius.md
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: Theme.colors.accentLine

        Part {
            id: ghost

            anchors.centerIn: parent
            iconName: "tap-echo"
            label: qsTr("Tap")
            hot: true
        }

        Icon {
            x: -width / 2
            y: parent.height - height / 2
            name: "mouse-pointer-click"
            size: Theme.size.iconLarge
            color: Theme.colors.textPrimary
        }
    }

    HelpMarker {
        visible: art.stage === "overview"
        number: 1
        x: window.x + window.width * 0.32 - width / 2
        y: window.y + (art.barHeight - height) / 2
    }

    HelpMarker {
        visible: art.stage === "overview"
        number: 2
        x: window.x + ring.x + ring.width
        y: window.y + art.barHeight + ring.y - height
    }

    HelpMarker {
        visible: art.stage === "overview"
        number: 3
        x: window.x + window.width - width / 2
        y: window.y + art.barHeight + (window.height - art.barHeight - height) / 2
    }
}
