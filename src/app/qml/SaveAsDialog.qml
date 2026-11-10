pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    property string serial
    property string profileName

    function start(name: string) {
        dialog.profileName = name
        nameField.errorText = ""
        nameField.text = ProfileLibrary.copyName(name)
        dialog.open()
        nameField.selectAll()
        nameField.forceActiveFocus()
    }

    function save() {
        if (nameField.text.trim() === "")
            return
        if (DeviceManager.saveEditingAs(dialog.serial, nameField.text))
            dialog.close()
        else
            nameField.errorText = qsTr("The profile could not be saved.")
    }

    title: qsTr("Save as a new profile")

    footer: Item {
        implicitHeight: buttons.implicitHeight + 2 * Theme.space.xl

        Row {
            id: buttons

            anchors.right: parent.right
            anchors.rightMargin: Theme.space.xxl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Button {
                objectName: "reject"
                text: qsTr("Cancel")
                onClicked: dialog.reject()
            }

            Button {
                objectName: "accept"
                tone: "primary"
                text: qsTr("Save")
                enabled: nameField.text.trim() !== ""
                onClicked: dialog.save()
            }
        }
    }

    Column {
        width: dialog.availableWidth
        spacing: Theme.space.md

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: qsTr("The new profile keeps the linked game, and you go on editing it. %1 stays as it was last saved.")
                  .arg(dialog.profileName)
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }

        TextField {
            id: nameField

            objectName: "saveAsName"
            width: parent.width
            maximumLength: 64
            selectByMouse: true
            Accessible.name: qsTr("Name")
            onTextEdited: errorText = ""
            onAccepted: dialog.save()
        }
    }
}
