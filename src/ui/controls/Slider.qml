pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.Slider {
    id: control

    property bool showValue: true
    property string valueText: Math.round(value).toString()
    property bool automatic: false
    property var ticks: []

    readonly property real valueWidth: showValue ? Theme.space.md + Theme.size.sliderValue : 0
    readonly property real band: ticks.length > 0 ? Theme.size.sliderBand : height

    signal tickActivated(int index)

    implicitWidth: Theme.size.sliderWidth + valueWidth
    implicitHeight: ticks.length > 0 ? Theme.size.sliderTicked : Theme.size.control
    containmentMask: ticks.length > 0 ? trackBand : null
    leftPadding: Theme.size.sliderHandlePressed / 2
    rightPadding: Theme.size.sliderHandlePressed / 2 + valueWidth
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity

    Accessible.description: valueText

    background: Item {
        x: control.leftPadding
        y: (control.band - height) / 2
        width: control.availableWidth
        height: Theme.size.sliderTrack

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.colors.strokeStrong
        }

        Rectangle {
            objectName: "fill"
            width: control.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: control.automatic ? Theme.colors.textDisabled : Theme.colors.accent
        }
    }

    handle: Item {
        x: control.leftPadding + control.visualPosition * control.availableWidth
        y: control.band / 2
        z: 1

        Rectangle {
            id: knob

            objectName: "knob"
            anchors.centerIn: parent
            width: control.pressed ? Theme.size.sliderHandlePressed : Theme.size.sliderHandle
            height: width
            radius: width / 2
            color: control.automatic ? Theme.colors.bgOverlay : Theme.colors.knob
            border.width: control.automatic ? Theme.size.focusRing : Theme.size.stroke
            border.color: control.automatic ? Theme.colors.textSecondary : Theme.colors.strokeStrong

            Behavior on width {
                NumberAnimation {
                    duration: Theme.motion.fast
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.motion.standard
                }
            }

            FocusRing {
                visible: control.visualFocus
                baseRadius: knob.width / 2
            }
        }

        Rectangle {
            objectName: "bubble"
            visible: control.pressed && !control.showValue
            x: -width / 2
            y: -Theme.size.sliderHandlePressed / 2 - Theme.space.sm - height
            width: bubbleText.implicitWidth + 2 * Theme.space.sm
            height: Theme.size.sliderBubble
            radius: Theme.radius.sm
            color: Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Text {
                id: bubbleText

                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: control.valueText
                color: Theme.colors.textPrimary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size
            }
        }
    }

    Item {
        id: trackBand

        width: control.width
        height: control.band
    }

    Repeater {
        model: control.ticks

        delegate: Item {
            id: tick

            required property var modelData
            required property int index
            readonly property bool first: modelData.position <= 0
            readonly property bool last: modelData.position >= 1

            x: control.leftPadding + tick.modelData.position * control.availableWidth
            y: control.band / 2

            Rectangle {
                x: -width / 2
                width: Theme.size.stroke
                height: Theme.size.sliderTick
                color: Theme.colors.strokeStrong
            }

            Text {
                objectName: "tick"
                x: tick.first ? 0 : tick.last ? -width : -width / 2
                y: Theme.size.sliderTick + Theme.space.xxs
                textFormat: Text.PlainText
                text: tick.modelData.text
                color: tick.modelData.current ? Theme.colors.textPrimary : Theme.colors.textTertiary
                font.pixelSize: Theme.type.micro.size
                font.weight: tick.modelData.current ? Font.DemiBold : Font.Normal

                MouseArea {
                    anchors.fill: parent
                    enabled: control.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: control.tickActivated(tick.index)
                }
            }
        }
    }

    Text {
        visible: control.showValue
        x: control.width - width
        width: Theme.size.sliderValue
        height: control.band
        textFormat: Text.PlainText
        text: control.valueText
        color: Theme.colors.textSecondary
        font.family: Theme.type.monoFamily
        font.pixelSize: Theme.type.mono.size
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
    }
}
