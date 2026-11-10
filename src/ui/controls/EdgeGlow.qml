import QtQuick
import GameScrcpy.Theme

Item {
    id: glow

    property bool active: false

    opacity: active ? 1 : 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.motion.base
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.standard
        }
    }

    component Band: Rectangle {
        id: band

        property bool horizontal: false
        property bool reversed: false

        gradient: Gradient {
            orientation: band.horizontal ? Gradient.Horizontal : Gradient.Vertical

            GradientStop {
                position: 0
                color: band.reversed ? Theme.colors.accentClear : Theme.colors.accentHalo
            }
            GradientStop {
                position: 1
                color: band.reversed ? Theme.colors.accentHalo : Theme.colors.accentClear
            }
        }
    }

    Band {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.size.edgeHalo
    }

    Band {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.size.edgeHalo
        reversed: true
    }

    Band {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.size.edgeHalo
        horizontal: true
    }

    Band {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.size.edgeHalo
        horizontal: true
        reversed: true
    }

    Rectangle {
        objectName: "edge"
        anchors.fill: parent
        color: "transparent"
        border.width: Theme.size.edgeLine
        border.color: Theme.colors.accentEdge
    }
}
