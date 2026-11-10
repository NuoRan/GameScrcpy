pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Rectangle {
    id: card

    property string vendorId: {
        const lead = DeviceManager.leadDevice.vendor ?? ""
        return lead !== "" ? lead : (SetupGuide.vendors[0]?.id ?? "other")
    }
    property int chosen: -1
    readonly property var steps: setup.steps
    readonly property int keyStep: {
        const input = card.steps.findIndex(entry => entry.screen === "input")
        return input >= 0 ? input : Math.max(0, card.steps.findIndex(entry => entry.screen === "developer"))
    }
    readonly property int shown: stepList.hovered >= 0 ? stepList.hovered : card.chosen >= 0 ? card.chosen : card.keyStep
    readonly property var current: card.steps[Math.min(card.shown, card.steps.length - 1)]

    signal linkActivated(string link)

    implicitHeight: Math.max(left.implicitHeight, art.implicitHeight)
    radius: Theme.radius.xl
    color: Theme.colors.bgOverlay
    border.width: Theme.size.stroke
    border.color: Theme.colors.strokeSubtle
    clip: true

    SetupSteps {
        id: setup

        vendorId: card.vendorId
        rich: true
    }

    Column {
        id: left

        x: Theme.space.xl
        width: card.width - Theme.size.helpStepsArt - 2 * Theme.space.xl
        topPadding: Theme.space.lg + Theme.space.xxs
        bottomPadding: Theme.space.xl
        spacing: Theme.space.xl

        Flow {
            width: parent.width
            spacing: Theme.space.sm - Theme.space.xxs

            Text {
                height: Theme.size.chip
                rightPadding: Theme.space.xs
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                text: qsTr("Your phone")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Repeater {
                model: setup.vendors

                delegate: FilterChip {
                    id: chip

                    required property var modelData

                    objectName: "helpVendor"
                    text: setup.text(chip.modelData.name)
                    selected: chip.modelData.id === card.vendorId
                    onClicked: {
                        card.vendorId = chip.modelData.id
                        card.chosen = -1
                    }
                }
            }
        }

        HelpSteps {
            id: stepList

            width: parent.width
            interactive: true
            current: card.shown
            items: card.steps.map(entry => ({ title: entry.title, html: entry.detail, note: entry.note }))
            onPicked: index => card.chosen = index
            onLinkActivated: link => card.linkActivated(link)
        }
    }

    Rectangle {
        id: art

        readonly property real phoneScale: 0.8

        anchors.right: parent.right
        width: Theme.size.helpStepsArt
        height: card.height
        implicitHeight: phone.height * art.phoneScale + dots.height + 3 * Theme.space.lg + Theme.space.xxs
        color: Theme.colors.bgSunken

        Rectangle {
            width: Theme.size.stroke
            height: parent.height
            color: Theme.colors.strokeSubtle
        }

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            y: (parent.height - height - dots.height - Theme.space.lg) / 2
            width: phone.width * art.phoneScale
            height: phone.height * art.phoneScale

            GuidePhone {
                id: phone

                transformOrigin: Item.TopLeft
                scale: art.phoneScale
                screen: card.current.screen
                title: card.current.phoneTitle
                target: card.current.target
            }
        }

        Row {
            id: dots

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.space.lg + Theme.space.xxs
            spacing: Theme.space.sm - Theme.space.xxs

            Repeater {
                model: card.steps.length

                delegate: Rectangle {
                    id: dot

                    required property int index

                    anchors.verticalCenter: parent.verticalCenter
                    width: dot.index === card.shown ? Theme.size.guideDotActive : Theme.size.guideDot
                    height: Theme.size.guideDot
                    radius: height / 2
                    color: dot.index === card.shown ? Theme.colors.accent : Theme.colors.strokeStrong

                    Behavior on width {
                        NumberAnimation {
                            duration: Theme.motion.base
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: Theme.motion.standard
                        }
                    }

                    TapHandler {
                        onTapped: card.chosen = dot.index
                    }
                }
            }
        }
    }
}
