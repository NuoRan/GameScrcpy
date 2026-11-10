import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.MenuSeparator {
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    horizontalPadding: Theme.space.xs
    verticalPadding: Theme.space.xs

    contentItem: Rectangle {
        implicitHeight: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }
}
