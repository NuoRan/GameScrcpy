pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    property var bases: []
    readonly property var choices: [{ value: "", text: qsTr("Start from scratch") }]
        .concat(dialog.bases.map(item => ({ value: item.kind, text: item.text })))
        .concat(ComponentLibrary.mine.map(entry => ({ value: entry.ref, text: entry.name })))
    readonly property var chosen: dialog.choices[Math.max(0, baseSelect.currentIndex)] ?? dialog.choices[0]

    signal created(string ref)

    function start() {
        nameField.errorText = ""
        nameField.text = dialog.freeName(qsTr("New component"))
        baseSelect.currentIndex = 0
        dialog.open()
        nameField.selectAll()
        nameField.forceActiveFocus()
    }

    function freeName(base: string): string {
        const taken = ComponentLibrary.mine.concat(ComponentLibrary.examples).map(entry => entry.name)
        if (!taken.includes(base))
            return base
        for (let number = 2; ; ++number) {
            const candidate = base + " " + number
            if (!taken.includes(candidate))
                return candidate
        }
    }

    function create() {
        if (nameField.text.trim() === "")
            return
        const ref = ComponentLibrary.add(nameField.text, dialog.chosen.value)
        if (ref === "") {
            nameField.errorText = qsTr("The component could not be created.")
            return
        }
        dialog.close()
        dialog.created(ref)
    }

    title: qsTr("New component")

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
                text: qsTr("Create and open")
                enabled: nameField.text.trim() !== ""
                onClicked: dialog.create()
            }
        }
    }

    Column {
        width: dialog.availableWidth
        spacing: Theme.space.md

        Text {
            objectName: "newComponentLead"
            width: parent.width
            textFormat: Text.PlainText
            text: dialog.chosen.value === ""
                  ? qsTr("It starts with one key that taps where you place it. Its logic opens next, so you can make it do more.")
                  : qsTr("It works like “%1”, and its logic has a place for what else to do. Its logic opens next, so you can fill it in.")
                        .arg(dialog.chosen.text)
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }

        TextField {
            id: nameField

            objectName: "newComponentName"
            width: parent.width
            maximumLength: 64
            selectByMouse: true
            Accessible.name: qsTr("Name")
            onTextEdited: errorText = ""
            onAccepted: dialog.create()
        }

        Column {
            width: parent.width
            spacing: Theme.space.xs + Theme.space.xxs

            Text {
                textFormat: Text.PlainText
                text: qsTr("Based on")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Theme.type.label.weight
            }

            Select {
                id: baseSelect

                objectName: "newComponentBase"
                width: parent.width
                model: dialog.choices.map(choice => choice.text)
                Accessible.name: qsTr("Based on")
            }
        }
    }
}
