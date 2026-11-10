pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme

Item {
    id: view

    property var wheel: ({})
    property rect picture
    readonly property var cells: wheel.cells ?? []
    readonly property int pointed: wheel.cell ?? -1
    readonly property real diameter: Math.max(240, Math.min(picture.width, picture.height) * 0.46)
    readonly property real outer: diameter / 2
    readonly property real hub: outer * 0.34

    objectName: "actionWheel"
    visible: wheel.open === true && cells.length >= 2
    x: picture.x + picture.width / 2 - outer
    y: picture.y + picture.height / 2 - outer
    width: diameter
    height: diameter
    opacity: 0.94

    Repeater {
        model: view.cells.length

        delegate: Shape {
            id: slice

            required property int index
            readonly property real step: 2 * Math.PI / view.cells.length
            readonly property real from: -Math.PI / 2 + (index - 0.5) * step
            readonly property real to: from + step
            readonly property bool chosen: index === view.pointed

            objectName: "wheelSlice"
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                fillColor: slice.chosen ? Theme.colors.accent : Theme.colors.bgElevated
                strokeColor: Theme.colors.strokeDefault
                strokeWidth: Theme.size.stroke
                startX: view.outer + view.hub * Math.cos(slice.from)
                startY: view.outer + view.hub * Math.sin(slice.from)

                PathLine {
                    x: view.outer + view.outer * Math.cos(slice.from)
                    y: view.outer + view.outer * Math.sin(slice.from)
                }

                PathArc {
                    x: view.outer + view.outer * Math.cos(slice.to)
                    y: view.outer + view.outer * Math.sin(slice.to)
                    radiusX: view.outer
                    radiusY: view.outer
                }

                PathLine {
                    x: view.outer + view.hub * Math.cos(slice.to)
                    y: view.outer + view.hub * Math.sin(slice.to)
                }

                PathArc {
                    x: view.outer + view.hub * Math.cos(slice.from)
                    y: view.outer + view.hub * Math.sin(slice.from)
                    radiusX: view.hub
                    radiusY: view.hub
                    direction: PathArc.Counterclockwise
                }
            }
        }
    }

    Repeater {
        model: view.cells.length

        delegate: Text {
            id: label

            required property int index
            readonly property var cell: view.cells[index] ?? ({})
            readonly property bool empty: (cell.name ?? "") === "" && (cell.action ?? "none") === "none"
            readonly property real angle: -Math.PI / 2 + index * 2 * Math.PI / view.cells.length
            readonly property real reach: (view.outer + view.hub) / 2

            objectName: "wheelLabel"
            width: (view.outer - view.hub) * 1.1
            x: view.outer + reach * Math.cos(angle) - width / 2
            y: view.outer + reach * Math.sin(angle) - height / 2
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: (cell.name ?? "") !== "" ? cell.name : empty ? qsTr("Empty") : qsTr("Cell %1").arg(index + 1)
            color: index === view.pointed ? Theme.colors.textOnAccent
                                          : empty ? Theme.colors.textTertiary : Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: Theme.type.label.weight
        }
    }

    Rectangle {
        objectName: "wheelHub"
        anchors.centerIn: parent
        width: 2 * view.hub - 2 * Theme.space.xs
        height: width
        radius: width / 2
        color: Theme.colors.bgSunken
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle

        Text {
            anchors.fill: parent
            anchors.margins: Theme.space.xs
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: view.pointed >= 0 ? qsTr("Let go to run") : qsTr("Move to choose")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
        }
    }
}
