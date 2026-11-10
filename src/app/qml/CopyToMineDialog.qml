pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Dialog {
    id: dialog

    property string kind
    property string source
    property string sourceName

    signal confirmed(string kind, string source, string sourceName)

    function ask(copyKind: string, copySource: string, copyName: string) {
        dialog.kind = copyKind
        dialog.source = copySource
        dialog.sourceName = copyName
        dialog.open()
    }

    objectName: "copyToMineDialog"
    title: qsTr("Copy %1 to yours?").arg(dialog.sourceName)
    acceptText: qsTr("Copy")
    onAccepted: dialog.confirmed(dialog.kind, dialog.source, dialog.sourceName)

    Text {
        objectName: "copyToMineWarning"
        width: parent.width
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        text: qsTr("After copying, its code runs in GameScrcpy and can do what your own logic can do.")
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.body.size
    }
}
