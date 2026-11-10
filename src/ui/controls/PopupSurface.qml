import QtQuick
import QtQuick.Effects
import GameScrcpy.Theme

Item {
    id: surface

    property real radius: Theme.radius.lg
    property bool deep: true

    RectangularShadow {
        visible: surface.deep
        anchors.fill: face
        radius: face.radius
        offset.y: Theme.shadow.ambientOffset
        blur: Theme.shadow.ambientBlur
        color: Theme.shadow.ambient
    }

    RectangularShadow {
        anchors.fill: face
        radius: face.radius
        offset.y: Theme.shadow.keyOffset
        blur: Theme.shadow.keyBlur
        color: Theme.shadow.key
    }

    Rectangle {
        id: face

        anchors.fill: parent
        radius: surface.radius
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong
    }
}
