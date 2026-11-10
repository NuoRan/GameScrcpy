import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.Dialog {
    id: control

    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property string alternativeText
    property bool danger: false

    signal alternativeChosen()

    function activateFocused() {
        if (alternativeButton.activeFocus) {
            control.close()
            control.alternativeChosen()
        } else if (rejectButton.activeFocus) {
            control.reject()
        } else {
            control.accept()
        }
    }

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    implicitWidth: Theme.size.dialogWidth
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape
    horizontalPadding: Theme.space.xxl
    topPadding: Theme.space.md
    bottomPadding: 0

    implicitHeight: implicitHeaderHeight + implicitContentHeight + implicitFooterHeight + topPadding + bottomPadding

    header: Text {
        textFormat: Text.PlainText
        text: control.title
        visible: text !== ""
        topPadding: Theme.space.xxl
        leftPadding: Theme.space.xxl
        rightPadding: Theme.space.xxl
        color: Theme.colors.textPrimary
        font.pixelSize: Theme.type.subtitle.size
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + 2 * Theme.space.xl

        Keys.onReturnPressed: control.activateFocused()
        Keys.onEnterPressed: control.activateFocused()

        Button {
            id: alternativeButton

            objectName: "alternative"
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.xxl
            anchors.verticalCenter: parent.verticalCenter
            visible: control.alternativeText !== ""
            tone: "ghost"
            text: control.alternativeText
            onClicked: {
                control.close()
                control.alternativeChosen()
            }
        }

        Row {
            id: buttons

            anchors.right: parent.right
            anchors.rightMargin: Theme.space.xxl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Button {
                id: rejectButton

                objectName: "reject"
                focus: control.danger
                visible: control.rejectText !== ""
                text: control.rejectText
                onClicked: control.reject()
            }

            Button {
                objectName: "accept"
                focus: !control.danger
                tone: control.danger ? "danger" : "primary"
                text: control.acceptText
                onClicked: control.accept()
            }
        }
    }

    background: PopupSurface {
        radius: Theme.radius.xl
    }

    T.Overlay.modal: Rectangle {
        color: Theme.colors.scrim

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.fast
            }
        }
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
