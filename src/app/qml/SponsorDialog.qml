pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Dialog {
    id: dialog

    property int launches: 0

    function ask(count: int) {
        dialog.launches = count
        AppSettings.markSponsorAsked(count)
        dialog.open()
    }

    objectName: "sponsorDialog"
    title: dialog.launches >= 200 ? qsTr("%1 launches. We're old friends now").arg(dialog.launches)
           : dialog.launches >= 100 ? qsTr("A full %1 launches. Thank you for staying").arg(dialog.launches)
           : dialog.launches >= 50 ? qsTr("%1 launches! It seems to really help").arg(dialog.launches)
           : qsTr("You've opened GameScrcpy %1 times").arg(dialog.launches)
    acceptText: qsTr("Sponsor")
    rejectText: qsTr("Heartlessly decline")
    onAccepted: Qt.openUrlExternally(AppInfo.sponsorUrl)

    Column {
        width: parent.width
        spacing: Theme.space.md

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: qsTr("GameScrcpy is free and has no ads, and I build it by directing AI. So far that has used up about 10 billion Opus 5.5 tokens, all paid out of my own pocket… and my wallet is starting to feel it.")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: qsTr("If it works well for you, would you consider sponsoring a little? (bows)")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: qsTr("If not, no worries. Thank you for using it!")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }
    }
}
