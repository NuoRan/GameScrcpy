pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Item {
    id: art

    property string kind: "hero"
    readonly property bool moving: !Theme.reducedMotion && art.visible && art.Window.active
    property real flow: 0

    implicitWidth: art.kind === "stepper" ? stepper.width : art.kind === "qr" ? qr.width : hero.width
    implicitHeight: art.kind === "stepper" ? stepper.height : art.kind === "qr" ? qr.height : hero.height

    NumberAnimation {
        target: art
        property: "flow"
        from: 0
        to: 1
        duration: 2 * Theme.motion.pulse
        loops: Animation.Infinite
        running: art.moving && art.kind === "hero"
    }

    component Caption: Text {
        textFormat: Text.PlainText
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        horizontalAlignment: Text.AlignHCenter
    }

    Item {
        id: hero

        readonly property real cable: phone.y + phone.height / 2

        visible: art.kind === "hero"
        width: 680
        height: 200

        HelpScreen {
            id: phone

            kind: "phone"
            x: 0
            y: 54
            width: 190
            height: 92
        }

        Caption {
            anchors.horizontalCenter: phone.horizontalCenter
            anchors.top: phone.bottom
            anchors.topMargin: Theme.space.md
            text: qsTr("Your phone")
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Theme.colors.strokeStrong
                strokeWidth: Theme.size.focusRing
                fillColor: "transparent"
                startX: phone.x + phone.width + Theme.space.sm
                startY: hero.cable

                PathLine { x: mirror.x - Theme.space.sm; y: hero.cable }
            }

            ShapePath {
                strokeColor: Theme.colors.strokeStrong
                strokeWidth: 1.5
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 3]
                fillColor: "transparent"
                startX: phone.x + phone.width + Theme.space.md
                startY: hero.cable - Theme.space.sm

                PathCubic {
                    x: mirror.x - Theme.space.md
                    y: hero.cable - Theme.space.sm
                    control1X: phone.x + phone.width + 70
                    control1Y: hero.cable - 84
                    control2X: mirror.x - 70
                    control2Y: hero.cable - 84
                }
            }
        }

        Rectangle {
            readonly property real from: phone.x + phone.width + Theme.space.sm
            readonly property real to: mirror.x - Theme.space.sm - width

            visible: art.moving
            x: from + (to - from) * art.flow
            y: hero.cable - height / 2
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

        HelpPill {
            x: (phone.x + phone.width + mirror.x) / 2 - width / 2
            y: hero.cable - height / 2
            iconName: "usb"
            text: qsTr("USB cable")
        }

        HelpPill {
            x: (phone.x + phone.width + mirror.x) / 2 - width / 2
            y: hero.cable - 74 - height / 2
            iconName: "wifi"
            text: qsTr("Wi-Fi · Android 11 or later")
        }

        HelpScreen {
            id: mirror

            kind: "window"
            x: 430
            y: 20
            width: 250
            height: 148

            Rectangle {
                x: 18
                y: 66
                width: 56
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
                x: 186
                y: 62
                small: true
                shape: "tap"
                text: "F"
            }

            StatusCapsule {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.space.xs
                transformOrigin: Item.BottomRight
                scale: 0.72
                framesPerSecond: 120
                latencyMs: 16
                transport: qsTr("USB")
            }
        }

        Caption {
            anchors.horizontalCenter: mirror.horizontalCenter
            anchors.top: mirror.bottom
            anchors.topMargin: Theme.space.md
            text: qsTr("The mirror window on this computer")
        }
    }

    Item {
        id: stepper

        visible: art.kind === "stepper"
        width: thumb.width + Theme.space.lg + steps.implicitWidth
        height: thumb.height

        Rectangle {
            id: thumb

            width: Theme.size.phoneWidth
            height: Theme.size.phoneHeight
            radius: Theme.radius.lg
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Icon {
                anchors.centerIn: parent
                name: "smartphone"
                size: Theme.size.iconLarge
                color: Theme.colors.textTertiary
            }
        }

        Row {
            x: thumb.width + Theme.space.lg
            y: Theme.space.xs
            spacing: Theme.space.sm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: "Pixel 8 Pro"
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
                font.weight: Font.DemiBold
            }

            StatusPill {
                anchors.verticalCenter: parent.verticalCenter
                tone: "warn"
                text: qsTr("Connecting")
            }
        }

        ConnectionStepper {
            id: steps

            x: thumb.width + Theme.space.lg
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.space.xs
            current: 2
        }
    }

    Rectangle {
        id: qr

        visible: art.kind === "qr"
        width: 216
        height: qrColumn.implicitHeight + 2 * Theme.space.lg
        radius: Theme.radius.xl
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeDefault

        Column {
            id: qrColumn

            x: Theme.space.lg
            y: Theme.space.lg
            width: parent.width - 2 * Theme.space.lg
            spacing: Theme.space.md

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("Pair with a QR code")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 128
                height: width
                radius: Theme.radius.md
                color: "#FFFFFF"

                Icon {
                    anchors.centerIn: parent
                    name: "qr-code"
                    size: 108
                    color: "#000000"
                }
            }

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("Scan it with the phone")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
}
