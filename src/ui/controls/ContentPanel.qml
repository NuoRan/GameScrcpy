import QtQuick
import GameScrcpy.Theme

Rectangle {
    id: panel

    default property alias content: inner.data

    color: Theme.colors.bgRaised
    topLeftRadius: Theme.radius.xl

    Rectangle {
        anchors.fill: parent
        anchors.rightMargin: -Theme.size.stroke
        anchors.bottomMargin: -Theme.size.stroke
        topLeftRadius: panel.topLeftRadius
        color: "transparent"
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle
    }

    Rectangle {
        x: panel.topLeftRadius
        y: Theme.size.stroke
        width: panel.width - x
        height: Theme.size.stroke
        color: Theme.colors.highlight
    }

    Item {
        id: inner

        anchors.fill: parent
    }
}
