import QtQuick
import GameScrcpy.Theme

Item {
    id: titleBar

    required property Window window

    readonly property Item minimizeButton: minimizeButton
    readonly property Item maximizeButton: maximizeButton
    readonly property Item closeButton: closeButton

    implicitHeight: 40

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.space.lg
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: titleBar.window.title
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        font.weight: Font.Medium
    }

    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height

        CaptionButton {
            id: minimizeButton
            glyph: "\uE921"
            onClicked: titleBar.window.showMinimized()
        }

        CaptionButton {
            id: maximizeButton
            glyph: titleBar.window.visibility === Window.Maximized ? "\uE923" : "\uE922"
            onClicked: {
                if (titleBar.window.visibility === Window.Maximized)
                    titleBar.window.showNormal()
                else
                    titleBar.window.showMaximized()
            }
        }

        CaptionButton {
            id: closeButton
            glyph: "\uE8BB"
            destructive: true
            onClicked: titleBar.window.close()
        }
    }
}
