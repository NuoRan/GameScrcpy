pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Dialog {
    id: dialog

    readonly property bool remember: rememberBox.checked

    objectName: "aiDisclosure"
    title: qsTr("What the AI assistant sends")
    acceptText: qsTr("Send")
    rejectText: qsTr("Cancel")
    onAboutToShow: rememberBox.checked = false

    Column {
        width: parent.width
        spacing: Theme.space.lg

        Text {
            width: parent.width
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("The assistant sends something only when you send a message, and only to %1, the address in "
                       + "Settings > AI.").arg(AiAssistant.host)
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }

        Group {
            objectName: "aiDisclosureSends"
            heading: qsTr("It sends")
            iconName: "check"
            tint: Theme.colors.successText
            lines: [
                qsTr("What you write"),
                qsTr("Which window and page you are in, and what you have selected there"),
                qsTr("The files and the logic code of the profile open in that window, and the lines its logic writes "
                     + "to the log"),
                qsTr("In a window without an open profile, the profiles, tasks and help articles it reads to answer "
                     + "you"),
                qsTr("The settings and shortcuts it looks up, with how they are set now; text such as folders and "
                     + "addresses is left out"),
                qsTr("When a phone does not connect or show its screen, the lines of the log about connecting and "
                     + "mirroring"),
                qsTr("A picture of the phone's screen when the assistant needs to see it")
            ]
        }

        Group {
            objectName: "aiDisclosureKeeps"
            heading: qsTr("It never sends")
            iconName: "x"
            tint: Theme.colors.textTertiary
            lines: [
                qsTr("Account details, apart from the API key"),
                qsTr("Anything else in the log"),
                qsTr("Files on the phone")
            ]
        }

        Text {
            objectName: "aiDisclosureSaved"
            width: parent.width
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("Conversations are saved on this computer; Settings > AI deletes them.")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Checkbox {
            id: rememberBox

            objectName: "aiDisclosureRemember"
            text: qsTr("Don't show this again")
        }
    }

    component Group: Column {
        id: group

        property string heading
        property string iconName
        property color tint
        property var lines: []

        width: parent ? parent.width : implicitWidth
        spacing: Theme.space.xs

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: group.heading
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: Font.DemiBold
        }

        Repeater {
            model: group.lines

            delegate: Item {
                id: line

                required property string modelData

                width: group.width
                height: Math.max(mark.height, lineText.implicitHeight)

                Icon {
                    id: mark

                    y: (Theme.type.body.lineHeight - height) / 2
                    name: group.iconName
                    size: Theme.size.iconSmall
                    color: group.tint
                }

                Text {
                    id: lineText

                    anchors.left: mark.right
                    anchors.leftMargin: Theme.space.sm
                    anchors.right: parent.right
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: line.modelData
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                }
            }
        }
    }
}
