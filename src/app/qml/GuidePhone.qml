pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons

Item {
    id: phone

    property string screen: "about"
    property string title
    property string target
    property string caption
    readonly property bool moving: !Theme.reducedMotion && phone.visible && phone.Window.active
    readonly property bool listed: phone.screen === "about" || phone.screen === "developer" || phone.screen === "input"
                                   || phone.screen === "pairing"
    readonly property bool switchedOn: phone.moving ? phone.flipped : true
    property bool flipped: true
    property real tapScale: 0
    property real tapOpacity: 0
    property real toastOpacity: 1
    property real pulseScale: 1
    property real pulseOpacity: 1

    readonly property var rows: {
        switch (phone.screen) {
        case "about":
            return [{ kind: "bar" }, { kind: "bar" }, { kind: "label", text: phone.target, hot: true }, { kind: "bar" }]
        case "developer":
            return [{ kind: "bar", toggle: true, on: true },
                    { kind: "label", text: qsTr("USB debugging"), toggle: true, hot: true },
                    { kind: "bar", toggle: true, on: false },
                    { kind: "bar" }]
        case "input":
            return [{ kind: "label", text: qsTr("USB debugging"), toggle: true, on: true },
                    { kind: "bar" },
                    { kind: "label", text: phone.target, toggle: true, hot: true },
                    { kind: "bar" }]
        default:
            return [{ kind: "bar" }, { kind: "bar" }, { kind: "bar" }, { kind: "bar" }]
        }
    }

    implicitWidth: Theme.size.guidePhoneWidth
    implicitHeight: Theme.size.guidePhoneHeight

    SequentialAnimation {
        running: phone.moving && phone.screen === "about"
        loops: Animation.Infinite

        PropertyAction { target: phone; property: "toastOpacity"; value: 0 }
        PauseAnimation { duration: Theme.motion.slow }
        SequentialAnimation {
            loops: 7

            ParallelAnimation {
                NumberAnimation {
                    target: phone
                    property: "tapScale"
                    from: 0.3
                    to: 1.15
                    duration: Theme.motion.base
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    target: phone
                    property: "tapOpacity"
                    from: 0.5
                    to: 0
                    duration: Theme.motion.base
                }
            }
        }
        NumberAnimation {
            target: phone
            property: "toastOpacity"
            to: 1
            duration: Theme.motion.base
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.emphasized
        }
        PauseAnimation { duration: Theme.motion.guideHold }
        NumberAnimation {
            target: phone
            property: "toastOpacity"
            to: 0
            duration: Theme.motion.fast
        }
    }

    SequentialAnimation {
        running: phone.moving && (phone.screen === "developer" || phone.screen === "input" || phone.screen === "allow")
        loops: Animation.Infinite

        PropertyAction { target: phone; property: "flipped"; value: false }
        PauseAnimation { duration: Theme.motion.guideHold }
        PropertyAction { target: phone; property: "flipped"; value: true }
        PauseAnimation { duration: 2 * Theme.motion.guideHold }
    }

    SequentialAnimation {
        running: phone.moving && phone.screen === "waiting"
        loops: Animation.Infinite

        ParallelAnimation {
            NumberAnimation {
                target: phone
                property: "pulseScale"
                from: 0.72
                to: 1.12
                duration: Theme.motion.pulse
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: phone
                property: "pulseOpacity"
                from: 1
                to: 0
                duration: Theme.motion.pulse
            }
        }
        PauseAnimation { duration: Theme.motion.slow }
    }

    Rectangle {
        id: body

        anchors.fill: parent
        radius: Theme.radius.xxl + Theme.space.xs
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong

        Rectangle {
            id: display

            anchors.fill: parent
            anchors.margins: Theme.size.guidePhoneBezel
            radius: Theme.radius.xxl
            color: Theme.colors.bgBase
            clip: true

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Theme.space.sm
                width: Theme.size.guideDot
                height: width
                radius: width / 2
                color: Theme.colors.bgElevated
            }

            Text {
                id: header

                x: Theme.space.md
                y: Theme.space.x3l
                width: parent.width - 2 * Theme.space.md
                visible: phone.listed
                textFormat: Text.PlainText
                text: phone.title
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.subtitle.size
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Column {
                id: list

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.leftMargin: Theme.space.sm
                anchors.rightMargin: Theme.space.sm
                anchors.topMargin: Theme.space.md
                spacing: Theme.space.xxs
                visible: phone.listed || phone.screen === "allow" || phone.screen === "usb"

                Repeater {
                    model: phone.rows

                    delegate: Rectangle {
                        id: row

                        required property var modelData
                        readonly property bool hot: modelData.hot === true
                        readonly property bool on: hot ? phone.switchedOn : modelData.on === true

                        width: list.width
                        height: Theme.size.guideRow
                        radius: Theme.radius.md
                        color: row.hot ? Theme.colors.accentSubtle : "transparent"
                        border.width: row.hot ? Theme.size.stroke : 0
                        border.color: Theme.colors.accentLine

                        Rectangle {
                            x: Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.modelData.kind === "bar"
                            width: row.width * 0.5
                            height: Theme.size.guideBar
                            radius: height / 2
                            color: Theme.colors.strokeDefault
                        }

                        Text {
                            x: Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            width: row.width - 2 * Theme.space.sm
                                   - (row.modelData.toggle === true ? Theme.size.guideToggleWidth + Theme.space.xs : 0)
                            visible: row.modelData.kind === "label"
                            textFormat: Text.PlainText
                            text: row.modelData.text ?? ""
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.caption.size
                            font.weight: row.hot ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            id: track

                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.modelData.toggle === true
                            width: Theme.size.guideToggleWidth
                            height: Theme.size.guideToggleHeight
                            radius: height / 2
                            color: row.on ? Theme.colors.accent : Theme.colors.bgSunken
                            border.width: row.on ? 0 : Theme.size.stroke
                            border.color: Theme.colors.strokeStrong

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.motion.fast
                                }
                            }

                            Rectangle {
                                readonly property real inset: (track.height - height) / 2

                                x: row.on ? track.width - width - inset : inset
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.size.guideToggleKnob
                                height: width
                                radius: width / 2
                                color: row.on ? Theme.colors.textOnAccent : Theme.colors.textSecondary

                                Behavior on x {
                                    NumberAnimation {
                                        duration: Theme.motion.base
                                        easing.type: Easing.BezierSpline
                                        easing.bezierCurve: Theme.motion.emphasized
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            x: row.width * 0.62 - width / 2
                            visible: row.hot && phone.screen === "about" && phone.moving
                            width: Theme.size.guideRipple
                            height: width
                            radius: width / 2
                            color: Theme.colors.accent
                            scale: phone.tapScale
                            opacity: phone.tapOpacity
                        }
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space.xl
                visible: phone.screen === "about"
                width: Math.min(toastText.implicitWidth + 2 * Theme.space.md, parent.width - 2 * Theme.space.md)
                height: toastText.implicitHeight + 2 * Theme.space.sm
                radius: height / 2
                color: Theme.colors.bgElevated
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeDefault
                opacity: phone.moving ? phone.toastOpacity : 1

                Text {
                    id: toastText

                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, display.width - 4 * Theme.space.md)
                    textFormat: Text.PlainText
                    text: qsTr("You are now a developer!")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.caption.size
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }

            Item {
                anchors.fill: parent
                visible: phone.screen === "usb" || phone.screen === "pairing"

                Rectangle {
                    anchors.fill: parent
                    color: Theme.colors.scrim
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.space.md
                    height: sheet.implicitHeight + 2 * Theme.space.md
                    radius: Theme.radius.lg
                    color: Theme.colors.bgElevated
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeStrong

                    Column {
                        id: sheet

                        x: Theme.space.md
                        y: Theme.space.md
                        width: parent.width - 2 * Theme.space.md
                        spacing: Theme.space.sm

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: phone.screen === "usb" ? qsTr("Use USB for") : qsTr("Pair with device")
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }

                        Repeater {
                            model: phone.screen === "usb" ? [qsTr("File transfer"), qsTr("Charge only"), qsTr("Transfer photos")] : []

                            delegate: Row {
                                id: choice

                                required property string modelData
                                required property int index
                                readonly property bool chosen: index === 0

                                width: sheet.width
                                spacing: Theme.space.xs

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.size.iconSmall
                                    height: width
                                    radius: width / 2
                                    color: "transparent"
                                    border.width: Theme.size.stroke
                                    border.color: choice.chosen ? Theme.colors.accent : Theme.colors.strokeStrong

                                    Rectangle {
                                        anchors.centerIn: parent
                                        visible: choice.chosen
                                        width: parent.width / 2
                                        height: width
                                        radius: width / 2
                                        color: Theme.colors.accent
                                    }
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - Theme.size.iconSmall - parent.spacing
                                    textFormat: Text.PlainText
                                    text: choice.modelData
                                    color: choice.chosen ? Theme.colors.textPrimary : Theme.colors.textSecondary
                                    font.pixelSize: Theme.type.caption.size
                                    font.weight: choice.chosen ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        Text {
                            visible: phone.screen === "pairing"
                            width: parent.width
                            textFormat: Text.PlainText
                            text: qsTr("Wi-Fi pairing code")
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.micro.size
                        }

                        Text {
                            visible: phone.screen === "pairing"
                            width: parent.width
                            textFormat: Text.PlainText
                            text: "482917"
                            color: Theme.colors.textPrimary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.title.size
                            font.weight: Font.DemiBold
                            font.letterSpacing: Theme.space.xxs
                        }

                        Text {
                            visible: phone.screen === "pairing"
                            width: parent.width
                            textFormat: Text.PlainText
                            text: qsTr("IP address & Port")
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.micro.size
                        }

                        Text {
                            visible: phone.screen === "pairing"
                            width: parent.width
                            textFormat: Text.PlainText
                            text: "192.168.1.23:41235"
                            color: Theme.colors.textSecondary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.micro.size
                            elide: Text.ElideRight
                        }

                        Text {
                            visible: phone.screen === "pairing"
                            anchors.right: parent.right
                            textFormat: Text.PlainText
                            text: qsTr("Cancel")
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                        }
                    }
                }
            }

            Item {
                anchors.fill: parent
                visible: phone.screen === "scan"

                Item {
                    id: viewfinder

                    anchors.centerIn: parent
                    width: parent.width * 0.62
                    height: width

                    Repeater {
                        model: 4

                        delegate: Item {
                            id: corner

                            required property int index

                            x: corner.index % 2 === 0 ? 0 : viewfinder.width - width
                            y: corner.index < 2 ? 0 : viewfinder.height - height
                            width: Theme.size.guideRow / 2
                            height: width

                            Rectangle {
                                y: corner.index < 2 ? 0 : parent.height - height
                                width: parent.width
                                height: Theme.size.focusRing
                                radius: height / 2
                                color: Theme.colors.accent
                            }

                            Rectangle {
                                x: corner.index % 2 === 0 ? 0 : parent.width - width
                                width: Theme.size.focusRing
                                height: parent.height
                                radius: width / 2
                                color: Theme.colors.accent
                            }
                        }
                    }

                    Icon {
                        anchors.centerIn: parent
                        name: "qr-code"
                        size: viewfinder.width * 0.62
                        color: Theme.colors.textPrimary
                    }
                }

                Text {
                    anchors.top: viewfinder.bottom
                    anchors.topMargin: Theme.space.lg
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 2 * Theme.space.md
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: qsTr("Scan the QR code on the computer")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }

            Item {
                anchors.fill: parent
                visible: phone.screen === "allow"

                Rectangle {
                    anchors.fill: parent
                    color: Theme.colors.scrim
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.space.md
                    height: dialog.implicitHeight + 2 * Theme.space.md
                    radius: Theme.radius.lg
                    color: Theme.colors.bgElevated
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeStrong

                    Column {
                        id: dialog

                        x: Theme.space.md
                        y: Theme.space.md
                        width: parent.width - 2 * Theme.space.md
                        spacing: Theme.space.sm

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: qsTr("Allow USB debugging?")
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: "RSA 3A:9F:12:C4:7E:05:B1"
                            color: Theme.colors.textTertiary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.micro.size
                            elide: Text.ElideRight
                        }

                        Row {
                            width: parent.width
                            spacing: Theme.space.xs

                            Rectangle {
                                id: tick

                                width: Theme.size.iconSmall
                                height: width
                                radius: Theme.radius.xs
                                color: phone.switchedOn ? Theme.colors.accent : "transparent"
                                border.width: phone.switchedOn ? 0 : Theme.size.stroke
                                border.color: Theme.colors.strokeStrong

                                Behavior on color {
                                    ColorAnimation {
                                        duration: Theme.motion.fast
                                    }
                                }

                                Icon {
                                    anchors.centerIn: parent
                                    visible: phone.switchedOn
                                    name: "check"
                                    size: Theme.size.keyHintTagIcon
                                    color: Theme.colors.textOnAccent
                                }
                            }

                            Text {
                                width: parent.width - tick.width - parent.spacing
                                textFormat: Text.PlainText
                                text: qsTr("Always allow from this computer")
                                color: Theme.colors.textSecondary
                                font.pixelSize: Theme.type.caption.size
                                wrapMode: Text.Wrap
                            }
                        }

                        Row {
                            anchors.right: parent.right
                            spacing: Theme.space.md

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: qsTr("Cancel")
                                color: Theme.colors.textSecondary
                                font.pixelSize: Theme.type.caption.size
                            }

                            Rectangle {
                                width: allowText.implicitWidth + 2 * Theme.space.sm
                                height: allowText.implicitHeight + Theme.space.sm
                                radius: height / 2
                                color: Theme.colors.accentSubtle
                                border.width: Theme.size.stroke
                                border.color: Theme.colors.accentLine

                                Text {
                                    id: allowText

                                    anchors.centerIn: parent
                                    textFormat: Text.PlainText
                                    text: qsTr("Allow")
                                    color: Theme.colors.accentText
                                    font.pixelSize: Theme.type.caption.size
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space.md
                visible: phone.screen === "waiting" || phone.screen === "ready" || phone.screen === "failed"
                spacing: Theme.space.md

                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.size.guidePulse
                    height: width

                    Rectangle {
                        anchors.centerIn: parent
                        visible: phone.screen === "waiting"
                        width: parent.width
                        height: width
                        radius: width / 2
                        color: "transparent"
                        border.width: Theme.size.focusRing
                        border.color: Theme.colors.accentLine
                        scale: phone.moving ? phone.pulseScale : 1
                        opacity: phone.moving ? phone.pulseOpacity : 1
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * 0.6
                        height: width
                        radius: width / 2
                        color: phone.screen === "ready" ? Theme.colors.successSubtle
                             : phone.screen === "failed" ? Theme.colors.dangerSubtle : Theme.colors.bgOverlay
                        border.width: Theme.size.stroke
                        border.color: phone.screen === "ready" ? Theme.colors.successText
                                    : phone.screen === "failed" ? Theme.colors.dangerText : Theme.colors.strokeDefault

                        Icon {
                            anchors.centerIn: parent
                            name: phone.screen === "ready" ? "check" : phone.screen === "failed" ? "x" : "usb"
                            size: Theme.size.iconLarge
                            color: phone.screen === "ready" ? Theme.colors.successText
                                 : phone.screen === "failed" ? Theme.colors.dangerText : Theme.colors.textSecondary
                        }
                    }
                }

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: phone.caption
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
