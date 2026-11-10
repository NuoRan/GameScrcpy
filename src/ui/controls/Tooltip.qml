import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.ToolTip {
    id: control

    property string shortcut
    property string detail

    x: parent ? (parent.width - width) / 2 : 0
    y: -height - Theme.space.xs
    margins: Theme.space.sm
    delay: Theme.motion.tooltipDelay
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: control.detail === "" ? Theme.size.tooltip : implicitContentHeight + topPadding + bottomPadding
    horizontalPadding: Theme.space.sm + Theme.space.xxs
    verticalPadding: control.detail === "" ? 0 : Theme.space.sm
    font.pixelSize: Theme.type.caption.size

    contentItem: Item {
        implicitWidth: control.detail === ""
                       ? line.implicitWidth
                       : Math.min(Theme.size.tooltipWide - control.leftPadding - control.rightPadding,
                                  Math.max(line.implicitWidth, detailText.implicitWidth))
        implicitHeight: control.detail === "" ? line.implicitHeight
                                              : line.implicitHeight + Theme.space.xxs + detailText.height

        Row {
            id: line

            y: control.detail === "" ? Math.round((parent.height - height) / 2) : 0
            spacing: Theme.space.sm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: control.text
                font.pixelSize: control.font.pixelSize
                font.weight: control.detail === "" ? control.font.weight : Theme.type.label.weight
                color: Theme.colors.textPrimary
            }

            Keycap {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.shortcut !== ""
                text: control.shortcut
            }
        }

        Text {
            id: detailText

            objectName: "tooltipDetail"
            visible: control.detail !== ""
            y: line.height + Theme.space.xxs
            width: parent.width
            textFormat: Text.PlainText
            text: control.detail
            wrapMode: Text.Wrap
            lineHeight: 1.15
            font.pixelSize: control.font.pixelSize
            color: Theme.colors.textSecondary
        }
    }

    background: PopupSurface {
        radius: Theme.radius.sm
        deep: false
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.motion.fast
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.standard
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: Theme.motion.fast
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.exit
        }
    }
}
