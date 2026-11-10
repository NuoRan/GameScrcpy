pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Dialog {
    id: dialog

    property string profileName
    property string extensionName

    title: qsTr("Restore the original %1?").arg(dialog.profileName)
    acceptText: qsTr("Restore")
    danger: true

    Text {
        width: parent.width
        textFormat: Text.PlainText
        text: qsTr("Your changes to this profile are removed, and it goes back to how %1 provides it. Phones using it switch to the original.")
              .arg(dialog.extensionName)
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.body.size
        wrapMode: Text.Wrap
    }
}
