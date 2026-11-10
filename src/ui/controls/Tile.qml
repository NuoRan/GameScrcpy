import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons

Rectangle {
    id: tile

    property string iconName
    property string iconPath
    property string tone
    property bool large: false

    implicitWidth: large ? Theme.size.tileLarge : Theme.size.tile
    implicitHeight: implicitWidth
    radius: large ? Theme.radius.lg : Theme.radius.md
    color: Theme.toneFill(tone)

    Icon {
        anchors.centerIn: parent
        name: tile.iconName
        customPath: tile.iconPath
        size: tile.large ? Theme.size.iconLarge : Theme.size.iconSmall
        color: Theme.toneInk(tile.tone)
    }
}
