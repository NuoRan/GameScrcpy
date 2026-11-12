pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Icons

Item {
    id: art

    property string kind: "hero"
    readonly property bool moving: !Theme.reducedMotion && art.visible && art.Window.active
    property real flow: 0.5

    implicitWidth: art.kind === "hero" ? Theme.size.helpHeroArtWidth : 280
    implicitHeight: art.kind === "hero" ? Theme.size.helpHeroArtHeight : Theme.size.helpCardArt

    NumberAnimation {
        target: art
        property: "flow"
        from: 0
        to: 1
        duration: 2 * Theme.motion.pulse
        loops: Animation.Infinite
        running: art.moving && ["hero", "connecting", "scripts", "aoa-driver", "touch-box", "capture-card"].includes(art.kind)
    }

    component Dot: Rectangle {
        width: Theme.size.guideDot
        height: width
        radius: width / 2
        color: Theme.colors.accent

        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 2 * Theme.space.xs
            height: width
            radius: width / 2
            color: Theme.colors.accentHalo
        }
    }

    component KeyDot: Rectangle {
        id: keyDot

        property bool round: false
        property bool chosen: false

        width: 14
        height: 14
        radius: keyDot.round ? width / 2 : Theme.radius.xs
        color: "#A80B0C0F"
        border.width: Theme.size.stroke
        border.color: keyDot.chosen ? Theme.colors.accent : "#4DFFFFFF"

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            visible: keyDot.chosen
            radius: keyDot.radius + 3
            color: "transparent"
            border.width: 3
            border.color: Theme.colors.accentRing
        }
    }

    component Ring: Rectangle {
        radius: width / 2
        color: "#240B0C0F"
        border.width: 1.5
        border.color: "#4DFFFFFF"
    }

    component MiniBar: Rectangle {
        id: miniBar

        property int dots: 5

        width: dotRow.implicitWidth + 2 * Theme.space.sm - Theme.space.xxs
        height: 16
        radius: height / 2
        color: Theme.colors.bgFrosted
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong

        Row {
            id: dotRow

            anchors.centerIn: parent
            spacing: 5

            Repeater {
                model: miniBar.dots

                Rectangle {
                    required property int index

                    width: Theme.size.guideDot
                    height: width
                    radius: width / 2
                    color: index === 0 ? Theme.colors.accent : "#B3FFFFFF"
                }
            }
        }
    }

    component MiniCapsule: Rectangle {
        width: 54
        height: 12
        radius: height / 2
        color: Theme.colors.bgFrosted
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong

        Rectangle {
            x: 5
            anchors.verticalCenter: parent.verticalCenter
            width: 4
            height: 4
            radius: 2
            color: Theme.colors.accent
        }
    }

    component Wasd: Item {
        id: wasd

        property real size: 40

        width: wasd.size
        height: wasd.size

        Ring {
            anchors.fill: parent
        }

        KeyDot { x: (wasd.size - width) / 2; y: -width / 2 }
        KeyDot { x: (wasd.size - width) / 2; y: wasd.size - width / 2 }
        KeyDot { x: -width / 2; y: (wasd.size - width) / 2 }
        KeyDot { x: wasd.size - width / 2; y: (wasd.size - width) / 2 }
    }

    component QuestionBar: Rectangle {
        id: question

        property real share: 0.46
        property bool open: false

        height: question.open ? 46 : 26
        radius: Theme.radius.md
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeDefault

        Rectangle {
            id: mark

            x: Theme.space.sm
            y: 6
            width: 14
            height: 14
            radius: width / 2
            color: Theme.colors.warningSubtle

            Icon {
                anchors.centerIn: parent
                name: "circle-help"
                size: 10
                color: Theme.colors.warningText
            }
        }

        Rectangle {
            x: mark.x + mark.width + Theme.space.sm
            y: mark.y + (mark.height - height) / 2
            width: (question.width - x - Theme.space.sm) * question.share
            height: 5
            radius: height / 2
            color: question.open ? Theme.colors.textSecondary : Theme.colors.strokeStrong
        }

        Rectangle {
            visible: question.open
            x: mark.x + mark.width + Theme.space.sm
            y: mark.y + mark.height + Theme.space.sm
            width: question.width - x - Theme.space.sm
            height: 5
            radius: height / 2
            color: Theme.colors.strokeDefault
        }
    }

    component MiniProfile: Rectangle {
        id: profile

        property bool front: true

        width: 132
        height: 84
        radius: Theme.radius.lg
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeDefault

        Rectangle {
            id: pane

            x: 6
            y: 6
            width: parent.width - 12
            height: 44
            radius: Theme.radius.sm
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Rectangle {
                visible: profile.front
                x: 10
                y: 12
                width: 20
                height: 20
                radius: width / 2
                color: Theme.colors.accentSubtle
                border.width: Theme.size.stroke
                border.color: Theme.colors.accentLine
            }

            Repeater {
                model: profile.front ? [[84, 20], [100, 12]] : []

                Rectangle {
                    required property var modelData

                    x: modelData[0]
                    y: modelData[1]
                    width: 5
                    height: 5
                    radius: 2.5
                    color: Theme.colors.accent
                }
            }
        }

        Rectangle {
            x: 6
            y: pane.y + pane.height + 9
            width: parent.width * 0.6
            height: 5
            radius: height / 2
            color: Theme.colors.strokeStrong
        }

        Rectangle {
            visible: profile.front
            x: 6
            y: pane.y + pane.height + 20
            width: parent.width * 0.4
            height: 4
            radius: height / 2
            color: Theme.colors.strokeDefault
        }
    }

    Item {
        id: hero

        visible: art.kind === "hero"
        anchors.centerIn: parent
        width: Theme.size.helpHeroArtWidth
        height: Theme.size.helpHeroArtHeight

        HelpScreen {
            id: heroPhone

            kind: "phone"
            x: 94
            y: 14
            width: 128
            height: 62
        }

        Rectangle {
            x: heroPhone.x + heroPhone.width / 2 - width / 2
            y: heroPhone.y + heroPhone.height
            width: Theme.size.focusRing
            height: heroWindow.y - y
            color: Theme.colors.strokeStrong
        }

        Dot {
            visible: art.moving
            x: heroPhone.x + heroPhone.width / 2 - width / 2
            y: heroPhone.y + heroPhone.height + (heroWindow.y - heroPhone.y - heroPhone.height - height) * art.flow
        }

        HelpPill {
            x: heroPhone.x + heroPhone.width / 2 - width / 2
            y: (heroPhone.y + heroPhone.height + heroWindow.y) / 2 - height / 2
            iconName: "usb"
        }

        HelpScreen {
            id: heroWindow

            x: 58
            y: 110
            width: 200
            height: 92

            Wasd {
                x: 12
                y: 38
            }

            KeyDot {
                x: 160
                y: 50
                round: true
            }

            MiniBar {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 6
            }

            MiniCapsule {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 6
            }
        }
    }

    Item {
        id: card

        visible: art.kind !== "hero"
        anchors.centerIn: parent
        width: 280
        height: Theme.size.helpCardArt

        Item {
            anchors.fill: parent
            visible: art.kind === "connecting"

            Rectangle {
                id: miniPhone

                x: 58
                y: 17
                width: 54
                height: 104
                radius: Theme.radius.xl - 2
                color: Theme.colors.bgElevated
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeStrong

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 3
                    radius: parent.radius - 3
                    color: Theme.colors.bgBase

                    Column {
                        id: settingRows

                        x: 5
                        y: 18
                        width: parent.width - 10
                        spacing: 4

                        Repeater {
                            model: [false, true, false, false]

                            Rectangle {
                                id: settingRow

                                required property bool modelData

                                width: settingRows.width
                                height: 10
                                radius: 3
                                color: settingRow.modelData ? Theme.colors.accentSubtle : "transparent"
                                border.width: settingRow.modelData ? Theme.size.stroke : 0
                                border.color: Theme.colors.accentLine

                                Rectangle {
                                    visible: !settingRow.modelData
                                    x: 3
                                    y: 3.5
                                    width: parent.width * 0.6
                                    height: 3
                                    radius: 1.5
                                    color: Theme.colors.strokeDefault
                                }

                                Rectangle {
                                    visible: settingRow.modelData
                                    anchors.right: parent.right
                                    anchors.rightMargin: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 10
                                    height: 5
                                    radius: 2.5
                                    color: Theme.colors.accent
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: miniWire

                x: miniPhone.x + miniPhone.width
                y: 68
                width: miniMonitor.x - x
                height: Theme.size.focusRing
                color: Theme.colors.strokeStrong
            }

            Dot {
                visible: art.moving
                x: miniWire.x + (miniWire.width - width) * art.flow
                y: miniWire.y + miniWire.height / 2 - height / 2
            }

            HelpPill {
                x: miniWire.x + miniWire.width / 2 - width / 2
                y: miniWire.y - height - Theme.space.xs
                iconName: "usb"
            }

            HelpScreen {
                id: miniMonitor

                kind: "monitor"
                x: 170
                y: 38
                width: 62
                height: 42
            }
        }

        Item {
            anchors.fill: parent
            visible: art.kind === "mirror-window"

            HelpScreen {
                x: 40
                y: 17
                width: 200
                height: 104

                MiniBar {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 6
                    dots: 7
                }

                Wasd {
                    x: 14
                    y: 48
                }

                MiniCapsule {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                }
            }
        }

        Item {
            anchors.fill: parent
            visible: art.kind === "keymap-editor"

            HelpScreen {
                id: editorWindow

                x: 66
                y: 17
                width: 184
                height: 104
                scene: "moba"
                dim: true

                Ring {
                    x: 14
                    y: 50
                    width: 38
                    height: 38
                }

                KeyDot { x: 120; y: 58; round: true }
                KeyDot { x: 146; y: 38; round: true; chosen: true }
                KeyDot { x: 150; y: 72; round: true }
            }

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Theme.colors.accent
                    strokeWidth: 1.5
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [3, 3]
                    fillColor: "transparent"
                    startX: 66
                    startY: 38

                    PathCubic {
                        x: editorWindow.x + 146 + 6
                        y: editorWindow.y + 38 + 4
                        control1X: 110
                        control1Y: 26
                        control2X: 170
                        control2Y: 24
                    }
                }
            }

            HelpPill {
                x: 20
                y: 26
                iconName: "tap-echo"
                text: qsTr("Tap")
                hot: true
            }
        }

        Item {
            anchors.fill: parent
            visible: art.kind === "profiles"

            MiniProfile {
                x: 30
                y: 34
                opacity: 0.55
                front: false
            }

            MiniProfile {
                x: 42
                y: 26
            }

            Icon {
                x: 184
                y: 58
                name: "link"
                size: Theme.size.iconSmall
                color: Theme.colors.textTertiary
            }

            Rectangle {
                x: 208
                y: 44
                width: 44
                height: 44
                radius: Theme.radius.xl - 2
                border.width: Theme.size.stroke
                border.color: "#1FFFFFFF"
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#F5C542" }
                    GradientStop { position: 1; color: "#E8873A" }
                }

                Icon {
                    anchors.centerIn: parent
                    name: "gamepad-2"
                    size: Theme.size.iconLarge
                    color: "#99000000"
                }
            }
        }

        Item {
            anchors.fill: parent
            visible: art.kind === "scripts"

            Rectangle {
                id: codeCard

                x: 20
                y: 24
                width: 132
                height: 90
                radius: Theme.radius.lg
                color: Theme.colors.bgOverlay
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeDefault

                Column {
                    x: 10
                    y: 12
                    spacing: 6

                    Repeater {
                        model: [[0.7, 0, Theme.colors.accentText], [0.5, 1, Theme.colors.infoText],
                                [0.62, 1, Theme.colors.successText], [0.34, 1, Theme.colors.infoText],
                                [0.22, 0, Theme.colors.textSecondary]]

                        Rectangle {
                            required property var modelData

                            x: modelData[1] * 10
                            width: 112 * modelData[0]
                            height: 5
                            radius: height / 2
                            color: modelData[2]
                        }
                    }
                }
            }

            Icon {
                x: codeCard.x + codeCard.width + Theme.space.sm
                y: codeCard.y + codeCard.height / 2 - height / 2
                name: "chevron-right"
                size: Theme.size.iconSmall
                color: Theme.colors.textTertiary
            }

            HelpScreen {
                id: lobbyPhone

                kind: "phone"
                scene: "lobby"
                x: 184
                y: 44
                width: 76
                height: 44
                radius: Theme.radius.lg

                Dot {
                    x: parent.width * 0.69 - width / 2
                    y: parent.height * 0.8 - height / 2
                    opacity: art.moving ? 1 - art.flow : 1
                }
            }
        }

        Item {
            anchors.fill: parent
            visible: art.kind === "aoa-driver"

            HelpScreen {
                id: aoaPhone

                kind: "phone"
                scene: "lobby"
                x: 24
                y: 47
                width: 76
                height: 44
                radius: Theme.radius.lg

                Dot {
                    x: parent.width * 0.62 - width / 2
                    y: parent.height * 0.62 - height / 2
                    opacity: art.moving ? art.flow : 1
                }
            }

            Rectangle {
                id: aoaWire

                x: aoaPhone.x + aoaPhone.width
                y: aoaPhone.y + aoaPhone.height / 2 - height / 2
                width: aoaMonitor.x - x
                height: Theme.size.focusRing
                color: Theme.colors.strokeStrong
            }

            Dot {
                visible: art.moving
                x: aoaWire.x + aoaWire.width - width - (aoaWire.width - width) * art.flow
                y: aoaWire.y + aoaWire.height / 2 - height / 2
            }

            HelpPill {
                x: aoaWire.x + aoaWire.width / 2 - width / 2
                y: aoaWire.y - height - Theme.space.xs
                iconName: "usb"
                text: "AOA"
                hot: true
            }

            HelpScreen {
                id: aoaMonitor

                kind: "monitor"
                x: 190
                y: 46
                width: 66
                height: 46
            }
        }

        Item {
            anchors.fill: parent
            visible: art.kind === "touch-box"

            HelpScreen {
                id: boxMonitor

                kind: "monitor"
                x: 18
                y: 46
                width: 62
                height: 44
            }

            Rectangle {
                id: boxLeftWire

                x: boxMonitor.x + boxMonitor.width
                y: boxBoard.y + boxBoard.height / 2 - height / 2
                width: boxBoard.x - x
                height: Theme.size.focusRing
                color: Theme.colors.strokeStrong
            }

            Rectangle {
                id: boxBoard

                x: 112
                y: 47
                width: 56
                height: 42
                radius: Theme.radius.sm
                color: Theme.colors.bgElevated
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeStrong

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 4
                    spacing: 4

                    Repeater {
                        model: 7

                        Rectangle {
                            width: 3
                            height: 3
                            radius: 1.5
                            color: Theme.colors.strokeDefault
                        }
                    }
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    spacing: 4

                    Repeater {
                        model: 7

                        Rectangle {
                            width: 3
                            height: 3
                            radius: 1.5
                            color: Theme.colors.strokeDefault
                        }
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 30
                    height: 18
                    radius: Theme.radius.xs
                    color: Theme.colors.bgBase
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.accentLine

                    Text {
                        anchors.centerIn: parent
                        textFormat: Text.PlainText
                        text: "HID"
                        color: Theme.colors.accentText
                        font.pixelSize: Theme.type.micro.size - 2
                        font.weight: Font.DemiBold
                    }
                }
            }

            Rectangle {
                id: boxRightWire

                x: boxBoard.x + boxBoard.width
                y: boxBoard.y + boxBoard.height / 2 - height / 2
                width: boxPhone.x - x
                height: Theme.size.focusRing
                color: Theme.colors.strokeStrong
            }

            Dot {
                visible: art.moving
                x: art.flow < 0.5 ? boxLeftWire.x + (boxLeftWire.width - width) * art.flow * 2
                                  : boxRightWire.x + (boxRightWire.width - width) * (art.flow - 0.5) * 2
                y: boxLeftWire.y + boxLeftWire.height / 2 - height / 2
            }

            HelpScreen {
                id: boxPhone

                kind: "phone"
                scene: "lobby"
                x: 200
                y: 49
                width: 62
                height: 38
                radius: Theme.radius.lg
            }
        }

        Item {
            anchors.fill: parent
            visible: art.kind === "capture-card"

            HelpScreen {
                id: capturePhone

                kind: "phone"
                x: 22
                y: 50
                width: 62
                height: 36
                radius: Theme.radius.lg
            }

            Rectangle {
                id: captureLeftWire

                x: capturePhone.x + capturePhone.width
                y: captureBox.y + captureBox.height / 2 - height / 2
                width: captureBox.x - x
                height: Theme.size.focusRing
                color: Theme.colors.strokeStrong
            }

            HelpPill {
                x: captureLeftWire.x + captureLeftWire.width / 2 - width / 2
                y: captureLeftWire.y - height - Theme.space.xs
                text: "HDMI"
            }

            Rectangle {
                id: captureBox

                x: 122
                y: 55
                width: 40
                height: 26
                radius: Theme.radius.sm
                color: Theme.colors.bgElevated
                border.width: Theme.size.stroke
                border.color: Theme.colors.accentLine

                Icon {
                    anchors.centerIn: parent
                    name: "monitor-play"
                    size: Theme.size.iconSmall
                    color: Theme.colors.accentText
                }
            }

            Rectangle {
                id: captureRightWire

                x: captureBox.x + captureBox.width
                y: captureBox.y + captureBox.height / 2 - height / 2
                width: captureMonitor.x - x
                height: Theme.size.focusRing
                color: Theme.colors.strokeStrong
            }

            HelpPill {
                x: captureRightWire.x + captureRightWire.width / 2 - width / 2
                y: captureRightWire.y - height - Theme.space.xs
                iconName: "usb"
            }

            Dot {
                visible: art.moving
                x: art.flow < 0.5 ? captureLeftWire.x + (captureLeftWire.width - width) * art.flow * 2
                                  : captureRightWire.x + (captureRightWire.width - width) * (art.flow - 0.5) * 2
                y: captureLeftWire.y + captureLeftWire.height / 2 - height / 2
            }

            HelpScreen {
                id: captureMonitor

                kind: "monitor"
                x: 194
                y: 45
                width: 64
                height: 46
            }
        }

        Item {
            anchors.fill: parent
            visible: art.kind === "faq"

            QuestionBar {
                x: 40
                y: 18
                width: 200
                share: 0.46
            }

            QuestionBar {
                x: 40
                y: 52
                width: 200
                share: 0.58
                open: true
            }

            QuestionBar {
                x: 40
                y: 106
                width: 200
                share: 0.38
            }
        }
    }
}
