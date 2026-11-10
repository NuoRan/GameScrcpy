import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme

import "IconPaths.js" as IconPaths

Item {
    id: icon

    required property string name
    property real size: Theme.size.icon
    property color color: Theme.colors.textSecondary

    readonly property real grid: 24
    readonly property real stroke: 1.5
    readonly property string path: {
        const found = IconPaths.paths[name]
        if (found === undefined && name !== "")
            console.warn("no icon named", name)
        return found ?? ""
    }

    implicitWidth: size
    implicitHeight: size

    Shape {
        width: icon.grid
        height: icon.grid
        scale: icon.size / icon.grid
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.stroke * icon.grid / icon.size
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: icon.path
            }
        }
    }
}
