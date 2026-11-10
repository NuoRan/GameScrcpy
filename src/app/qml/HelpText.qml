import QtQuick
import GameScrcpy.Theme
import "HelpRich.js" as HelpRich

Text {
    id: rich

    property string html
    property int lineSize: Theme.type.prose.lineHeight
    readonly property color codeFill: Qt.tint(Theme.colors.bgRaised, Theme.colors.statePress)

    textFormat: Text.RichText
    text: HelpRich.html(rich.html, Theme.colors, Theme.type.monoFamily, rich.codeFill)
    wrapMode: Text.Wrap
    color: Theme.colors.textSecondary
    linkColor: Theme.colors.accentText
    font.pixelSize: Theme.type.prose.size
    lineHeight: rich.lineSize
    lineHeightMode: Text.FixedHeight

    HoverHandler {
        cursorShape: rich.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
