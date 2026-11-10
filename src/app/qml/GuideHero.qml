pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Item {
    id: hero

    readonly property bool moving: !Theme.reducedMotion && hero.visible && hero.Window.active
    property real flow: 0.5

    implicitWidth: Theme.size.guideWindowWidth
    implicitHeight: handset.height + Theme.size.guideCable + window.height

    NumberAnimation {
        target: hero
        property: "flow"
        from: 0
        to: 1
        duration: Theme.motion.pulse
        loops: Animation.Infinite
        running: hero.moving
    }

    component Scene: Rectangle {
        id: scene

        property real unit: height / 10

        color: Theme.colors.bgBase
        clip: true

        Rectangle {
            x: 0
            y: scene.height * 0.58
            width: scene.width
            height: Theme.size.stroke
            color: Theme.colors.strokeDefault
        }

        Rectangle {
            x: scene.width * 0.08
            y: scene.unit
            width: scene.unit * 2.2
            height: width
            radius: width / 2
            color: "transparent"
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeStrong
        }

        Rectangle {
            x: scene.width * 0.42
            y: scene.height * 0.58 - height
            width: scene.unit * 1.4
            height: scene.unit * 2.6
            radius: Theme.radius.xs
            color: Theme.colors.bgElevated
        }

        Rectangle {
            x: scene.width * 0.56
            y: scene.height * 0.58 - height
            width: scene.unit * 2.4
            height: scene.unit * 1.6
            radius: Theme.radius.xs
            color: Theme.colors.bgOverlay
        }
    }

    Rectangle {
        id: handset

        anchors.horizontalCenter: parent.horizontalCenter
        width: Theme.size.guideHandsetWidth
        height: Theme.size.guideHandsetHeight
        radius: Theme.radius.xl
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong

        Scene {
            anchors.fill: parent
            anchors.margins: Theme.space.xs
            radius: Theme.radius.lg
        }
    }

    Item {
        id: cable

        anchors.top: handset.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Theme.size.guideRow
        height: Theme.size.guideCable

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.size.focusRing
            height: parent.height
            color: Theme.colors.strokeStrong
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: (cable.height - height) * hero.flow
            visible: hero.moving
            width: Theme.size.guideDot
            height: width
            radius: width / 2
            color: Theme.colors.accent
        }

        Rectangle {
            anchors.centerIn: parent
            width: Theme.size.guideTile
            height: Theme.size.pill
            radius: height / 2
            color: Theme.colors.bgOverlay
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Icon {
                anchors.centerIn: parent
                name: "usb"
                size: Theme.size.keyHintTag
                color: Theme.colors.textSecondary
            }
        }
    }

    Rectangle {
        id: window

        anchors.top: cable.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Theme.size.guideWindowWidth
        height: Theme.size.guideWindowHeight
        radius: Theme.radius.lg
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong
        clip: true

        Scene {
            anchors.fill: parent
            anchors.margins: Theme.size.stroke
            radius: Theme.radius.lg

            Rectangle {
                x: parent.width * 0.06
                y: parent.height * 0.42
                width: parent.height * 0.42
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
                x: parent.width * 0.78
                y: parent.height * 0.5
                small: true
                text: "J"
                tagIcon: "plus"
            }

            KeyHint {
                x: parent.width * 0.62
                y: parent.height * 0.18
                small: true
                iconName: "crosshair"
            }
        }

        StatusCapsule {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space.xs
            transformOrigin: Item.BottomRight
            scale: 0.72
            framesPerSecond: 60
            latencyMs: 14
            transport: qsTr("USB")
            cameraLocked: true
        }
    }
}
