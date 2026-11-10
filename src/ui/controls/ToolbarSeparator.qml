import QtQuick
import GameScrcpy.Theme

Item {
    implicitWidth: Theme.size.stroke + 2 * Theme.space.xs
    implicitHeight: Theme.size.toolbarButton

    Accessible.role: Accessible.Separator

    Rectangle {
        anchors.centerIn: parent
        width: Theme.size.stroke
        height: Theme.size.toolbarSeparator
        color: Theme.colors.strokeDefault
    }
}
