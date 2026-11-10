pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons

Rectangle {
    id: preview

    property var markers: []
    property bool portrait: false

    implicitWidth: portrait ? Theme.size.previewPortrait : Theme.size.previewLong
    implicitHeight: Theme.size.previewShort
    radius: Theme.radius.sm
    color: Theme.colors.bgSunken
    border.width: Theme.size.stroke
    border.color: Theme.colors.strokeDefault
    clip: true

    Accessible.role: Accessible.Graphic
    Accessible.ignored: true

    Repeater {
        model: preview.markers

        delegate: Item {
            id: marker

            required property var modelData

            readonly property bool ring: ["wasd", "rightWalk", "padStick", "skill", "dial"].indexOf(modelData.type) >= 0
            readonly property bool camera: modelData.type === "camera" || modelData.type === "padCamera"

            objectName: "marker"
            x: modelData.x * preview.width
            y: modelData.y * preview.height

            Rectangle {
                visible: marker.ring
                x: -width / 2
                y: -height / 2
                width: Math.max(Theme.size.previewDot, 2 * marker.modelData.rx * preview.width)
                height: Math.max(Theme.size.previewDot, 2 * marker.modelData.ry * preview.height)
                radius: Math.min(width, height) / 2
                color: Theme.colors.accentSubtle
                border.width: Theme.size.stroke
                border.color: Theme.colors.accentLine
            }

            Icon {
                visible: marker.camera
                x: -width / 2
                y: -height / 2
                name: "crosshair"
                size: Theme.size.iconSmall
                color: Theme.colors.accentText
            }

            Rectangle {
                visible: !marker.ring && !marker.camera
                x: -width / 2
                y: -height / 2
                width: Theme.size.previewDot
                height: width
                radius: width / 2
                color: Theme.colors.accent
            }
        }
    }
}
