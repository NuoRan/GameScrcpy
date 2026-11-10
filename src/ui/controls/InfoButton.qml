import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.AbstractButton {
    id: control

    property string title
    property string info
    property bool pinned: false
    property int delay: Theme.motion.infoDelay
    readonly property bool shown: bubble.visible

    function toggle() {
        pinned = !pinned
        if (pinned)
            bubble.open()
        else
            bubble.close()
    }

    implicitWidth: Theme.size.infoButton
    implicitHeight: Theme.size.infoButton
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("About “%1”").arg(title)
    Accessible.description: info

    onClicked: toggle()
    onHoveredChanged: {
        if (hovered) {
            showSoon.restart()
            return
        }
        showSoon.stop()
        if (!pinned)
            bubble.close()
    }
    Keys.onReturnPressed: toggle()
    Keys.onEnterPressed: toggle()
    Keys.onEscapePressed: event => {
        event.accepted = bubble.visible
        bubble.close()
    }

    Timer {
        id: showSoon

        interval: control.delay
        onTriggered: {
            if (control.hovered)
                bubble.open()
        }
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: "circle-help"
            size: Theme.size.infoIcon
            color: {
                if (control.pinned)
                    return Theme.colors.accentText
                return control.hovered ? Theme.colors.textPrimary : Theme.colors.textTertiary
            }
        }
    }

    background: Rectangle {
        id: face

        radius: height / 2
        color: control.pinned ? Theme.colors.accentSubtle : "transparent"

        StateLayer {
            anchors.fill: parent
            radius: face.radius
            visible: !control.pinned
            hovered: control.hovered
            pressed: control.down
        }

        FocusRing {
            visible: control.visualFocus
            baseRadius: face.radius
        }
    }

    T.Popup {
        id: bubble

        objectName: "bubble"
        x: -Theme.space.md
        y: control.height + Theme.space.xs
        width: Theme.size.infoBubble
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        margins: Theme.space.sm
        padding: Theme.space.md
        leftPadding: Theme.space.md + Theme.space.xxs
        rightPadding: leftPadding
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
        onClosed: control.pinned = false

        background: PopupSurface {
            radius: Theme.radius.lg
        }

        contentItem: Column {
            spacing: Theme.space.xs

            Text {
                objectName: "bubbleTitle"
                width: parent.width
                visible: control.title !== ""
                textFormat: Text.PlainText
                text: control.title
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "bubbleText"
                width: parent.width
                textFormat: Text.PlainText
                text: control.info
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
                lineHeight: Theme.type.caption.lineHeight
                lineHeightMode: Text.FixedHeight
                wrapMode: Text.Wrap
            }
        }
    }
}
