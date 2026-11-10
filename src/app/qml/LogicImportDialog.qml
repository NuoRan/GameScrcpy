pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Dialog {
    id: dialog

    property var urls: []
    property var files: []

    signal confirmed(var urls)

    function ask(found: var) {
        dialog.urls = found.map(item => item.url)
        dialog.files = found.map(item => item.file)
        dialog.open()
    }

    title: qsTr("Import a profile with logic?")
    acceptText: qsTr("Import")
    onAccepted: dialog.confirmed(dialog.urls)

    Text {
        width: parent.width
        textFormat: Text.PlainText
        text: qsTr("%1 contains logic code. While you use it, it taps, swipes and presses keys on the phone by itself, can read the keys you hold and where the pointer is in the mirror window, and can read and write the phone's clipboard. Only import profiles you trust.")
              .arg(dialog.files.join(qsTr(", ", "list separator")))
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.body.size
        wrapMode: Text.Wrap
    }
}
