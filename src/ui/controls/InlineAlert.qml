import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import GameScrcpy.Theme

T.Pane {
    id: control

    property string tone: "info"
    property string title
    property string detail
    property string actionText
    property bool actionEnabled: true

    readonly property color toneColor: {
        switch (tone) {
        case "warn":
            return Theme.colors.warning
        case "bad":
            return Theme.colors.danger
        default:
            return Theme.colors.info
        }
    }
    readonly property color toneText: {
        switch (tone) {
        case "warn":
            return Theme.colors.warningText
        case "bad":
            return Theme.colors.dangerText
        default:
            return Theme.colors.infoText
        }
    }

    signal actionTriggered()

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: Theme.space.lg
    leftPadding: Theme.space.md + Theme.size.toastBar + Theme.space.md

    Accessible.role: Accessible.AlertMessage
    Accessible.name: title
    Accessible.description: detail

    background: Rectangle {
        id: face

        radius: Theme.radius.xl
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle

        Rectangle {
            x: Theme.space.md
            y: Theme.space.md
            width: Theme.size.toastBar
            height: face.height - 2 * Theme.space.md
            radius: width / 2
            color: control.toneColor
        }
    }

    contentItem: RowLayout {
        spacing: Theme.space.lg

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space.xs

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: control.title
                color: control.toneText
                font.pixelSize: Theme.type.body.size
                font.weight: Font.Medium
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                visible: control.detail !== ""
                textFormat: Text.PlainText
                text: control.detail
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }

        Button {
            objectName: "action"
            visible: control.actionText !== ""
            enabled: control.actionEnabled
            text: control.actionText
            onClicked: control.actionTriggered()
        }
    }
}
