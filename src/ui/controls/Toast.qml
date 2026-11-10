import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.Popup {
    id: control

    property string text
    property string tone: "info"
    property string actionText
    property string secondActionText
    property string iconName
    property bool capsule: false
    property int duration: Math.min(3 * Theme.motion.toastStay, Math.max(Theme.motion.toastStay, 150 * text.length))
    readonly property real widest: parent ? Math.min(Theme.size.toastMaxWidth, parent.width - 2 * Theme.space.sm)
                                          : Theme.size.toastMaxWidth

    readonly property color toneColor: {
        switch (tone) {
        case "ok":
            return Theme.colors.success
        case "warn":
            return Theme.colors.warning
        case "bad":
            return Theme.colors.danger
        default:
            return Theme.colors.info
        }
    }
    readonly property color toneSubtle: {
        switch (tone) {
        case "ok":
            return Theme.colors.successSubtle
        case "warn":
            return Theme.colors.warningSubtle
        case "bad":
            return Theme.colors.dangerSubtle
        default:
            return Theme.colors.infoSubtle
        }
    }

    signal actionTriggered()
    signal secondActionTriggered()

    function show(message: string, kind: string) {
        text = message
        tone = kind
        open()
    }

    parent: T.Overlay.overlay
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? parent.height - height - Theme.space.xxl : 0
    implicitWidth: Math.min(widest, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.size.toast
    leftPadding: {
        if (!capsule)
            return Theme.space.sm + Theme.size.toastBar + Theme.space.md
        return iconName !== "" ? Theme.space.xs + Theme.space.xxs : Theme.space.lg
    }
    rightPadding: Theme.space.xs + Theme.space.xxs
    verticalPadding: 0
    modal: false
    focus: false
    closePolicy: T.Popup.NoAutoClose

    contentItem: Row {
        spacing: Theme.space.sm

        Accessible.role: Accessible.AlertMessage
        Accessible.name: control.text

        Rectangle {
            id: badge

            objectName: "iconBadge"
            anchors.verticalCenter: parent.verticalCenter
            visible: control.capsule && control.iconName !== ""
            width: visible ? Theme.size.icon + 2 * Theme.space.sm : 0
            height: width
            radius: width / 2
            color: control.toneSubtle

            Icon {
                anchors.centerIn: parent
                name: control.iconName
                size: Theme.size.icon
                color: control.toneColor
            }
        }

        Text {
            objectName: "message"
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, control.widest - control.leftPadding - control.rightPadding
                            - badge.width - action.width - secondAction.width - dismiss.width
                            - ((badge.visible ? 1 : 0) + (action.visible ? 1 : 0) + (secondAction.visible ? 1 : 0)
                               + 1) * parent.spacing)
            textFormat: Text.PlainText
            text: control.text
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            elide: Text.ElideRight
        }

        Button {
            id: action

            objectName: "action"
            anchors.verticalCenter: parent.verticalCenter
            visible: control.actionText !== ""
            width: visible ? implicitWidth : 0
            tone: "ghost"
            small: true
            text: control.actionText
            focusPolicy: Qt.NoFocus
            onClicked: {
                control.actionTriggered()
                control.close()
            }
        }

        Button {
            id: secondAction

            objectName: "secondAction"
            anchors.verticalCenter: parent.verticalCenter
            visible: control.secondActionText !== ""
            width: visible ? implicitWidth : 0
            tone: "ghost"
            small: true
            text: control.secondActionText
            focusPolicy: Qt.NoFocus
            onClicked: {
                control.secondActionTriggered()
                control.close()
            }
        }

        Button {
            id: dismiss

            anchors.verticalCenter: parent.verticalCenter
            tone: "ghost"
            small: true
            iconName: "x"
            description: qsTr("Dismiss")
            focusPolicy: Qt.NoFocus
            onClicked: control.close()
        }
    }

    background: PopupSurface {
        radius: control.capsule ? height / 2 : Theme.radius.lg

        Rectangle {
            visible: !control.capsule
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.sm
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.size.toastBar
            height: parent.height - 2 * Theme.space.md
            radius: width / 2
            color: control.toneColor
        }
    }

    Timer {
        interval: control.duration
        running: control.opened && control.actionText === "" && control.secondActionText === ""
        onTriggered: control.close()
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
            NumberAnimation {
                property: "scale"
                from: Theme.motion.enterScale
                to: 1
                duration: Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
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
