import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

Item {
    id: page

    required property string title
    required property string iconName
    required property string summary
    property string actionText

    signal actionChosen()

    Text {
        x: Theme.size.panelPaddingH
        y: Theme.size.panelPaddingV
        textFormat: Text.PlainText
        text: page.title
        color: Theme.colors.textPrimary
        font.pixelSize: Theme.type.display.size
        font.weight: Theme.type.display.weight
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(summaryText.implicitWidth, page.width - 2 * Theme.size.panelPaddingH)
        spacing: Theme.space.lg

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: page.iconName
            size: Theme.size.iconHero
            color: Theme.colors.textTertiary
        }

        Text {
            id: summaryText

            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: page.summary
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }

        Button {
            objectName: "placeholderAction"
            anchors.horizontalCenter: parent.horizontalCenter
            visible: page.actionText !== ""
            text: page.actionText
            onClicked: page.actionChosen()
        }
    }
}
