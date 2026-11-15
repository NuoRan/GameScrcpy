pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    property string profileId
    property string current
    property string problem
    readonly property bool ready: nameField.text.trim() !== "" && nameField.text.trim() !== dialog.current

    function start(wanted: string, name: string) {
        dialog.profileId = wanted
        dialog.current = name
        dialog.problem = ""
        nameField.text = name
        dialog.open()
        nameField.selectAll()
        nameField.forceActiveFocus()
    }

    function submit() {
        if (!dialog.ready)
            return
        if (ProfileLibrary.rename(dialog.profileId, nameField.text)) {
            dialog.close()
            return
        }
        dialog.problem = ProfileLibrary.lastWriteBusy() ? qsTr("Not saved: another program is using the file")
                                                        : qsTr("Could not rename %1.").arg(dialog.current)
    }

    title: qsTr("Rename profile")

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
                text: qsTr("Rename")
                enabled: dialog.ready
                onClicked: dialog.submit()
            }
        }
    }

    Column {
        width: dialog.availableWidth
        spacing: Theme.space.md

        TextField {
            id: nameField

            objectName: "renameProfileName"
            width: parent.width
            maximumLength: 64
            selectByMouse: true
            errorText: dialog.problem
            Accessible.name: qsTr("Name")
            onTextEdited: dialog.problem = ""
            onAccepted: dialog.submit()
        }
    }
}
