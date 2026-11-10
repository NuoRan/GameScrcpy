import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme

Rectangle {
    id: view

    required property int modules
    required property string path
    property int moduleSize: 4

    readonly property int quietZone: 4

    implicitWidth: (modules + 2 * quietZone) * moduleSize
    implicitHeight: implicitWidth
    radius: Theme.radius.md
    color: "#FFFFFF"

    Shape {
        x: view.quietZone * view.moduleSize
        y: x
        width: view.modules
        height: view.modules
        scale: view.moduleSize
        transformOrigin: Item.TopLeft
        visible: view.modules > 0

        ShapePath {
            fillColor: "#000000"
            strokeWidth: -1

            PathSvg {
                path: view.path
            }
        }
    }
}
