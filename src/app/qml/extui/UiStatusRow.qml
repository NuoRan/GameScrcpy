pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.App

Row {
    id: row

    property var widgets: []
    property bool flat: false

    function toneColor(tone: string): color {
        switch (tone) {
        case "muted":
            return Theme.colors.textTertiary
        case "accent":
            return Theme.colors.accentText
        case "good":
            return Theme.colors.successText
        case "warn":
            return Theme.colors.warningText
        case "bad":
            return Theme.colors.dangerText
        default:
            return Theme.colors.textPrimary
        }
    }

    spacing: Theme.space.xs

    Repeater {
        model: row.widgets

        delegate: Rectangle {
            id: widget

            required property var modelData
            property var tree: null
            readonly property var parts: widget.tree === null ? []
                                         : widget.tree.type === "row" ? widget.tree.children ?? [] : [widget.tree]

            function refresh() {
                widget.tree = ExtensionPanels.tree(widget.modelData.key) ?? null
            }

            objectName: "statusWidget"
            implicitWidth: partsRow.implicitWidth + 2 * (row.flat ? Theme.space.sm : Theme.space.md)
            implicitHeight: Theme.size.capsule
            visible: widget.parts.length > 0
            radius: height / 2
            color: row.flat ? "transparent" : Theme.reducedTransparency ? Theme.colors.bgElevated : Theme.colors.bgFrosted
            border.width: row.flat ? 0 : Theme.size.stroke
            border.color: Theme.colors.strokeStrong

            Accessible.role: Accessible.StaticText
            Accessible.name: [widget.modelData.title].concat(widget.parts.filter(part => part.type === "text")
                                                                  .map(part => part.text ?? "")).join(" · ")

            Component.onCompleted: widget.refresh()

            Connections {
                target: ExtensionPanels

                function onTreeChanged(key: string) {
                    if (key === widget.modelData.key)
                        widget.refresh()
                }
            }

            Row {
                id: partsRow

                anchors.centerIn: parent
                spacing: Theme.space.xs

                Repeater {
                    model: widget.parts

                    delegate: Item {
                        id: part

                        required property var modelData
                        readonly property string kind: part.modelData.type ?? ""
                        readonly property real amount: part.modelData.value ?? -1

                        implicitWidth: part.kind === "text" ? label.implicitWidth
                                     : part.kind === "icon" ? Theme.size.iconSmall
                                     : part.kind === "progress" ? Theme.size.extensionStatusBar : 0
                        implicitHeight: Theme.size.capsule

                        Text {
                            id: label

                            anchors.verticalCenter: parent.verticalCenter
                            visible: part.kind === "text"
                            textFormat: Text.PlainText
                            text: part.kind === "text" ? part.modelData.text ?? "" : ""
                            color: row.toneColor(part.modelData.tone ?? "normal")
                            font.pixelSize: Theme.type.label.size
                            font.weight: Theme.type.label.weight
                            font.features: ({ "tnum": 1 })
                        }

                        Icon {
                            anchors.centerIn: parent
                            visible: part.kind === "icon"
                            name: part.kind === "icon" ? ExtensionPanels.iconOf(part.modelData.name ?? "") : ""
                            size: Theme.size.iconSmall
                            color: row.toneColor(part.modelData.tone ?? "normal")
                        }

                        Rectangle {
                            id: track

                            anchors.verticalCenter: parent.verticalCenter
                            visible: part.kind === "progress"
                            width: parent.width
                            height: Theme.space.xs
                            radius: height / 2
                            color: Theme.colors.strokeStrong
                            clip: true

                            Accessible.role: Accessible.ProgressBar
                            Accessible.name: part.modelData.title ?? ""

                            Rectangle {
                                id: fill

                                property real sweep: 0

                                x: part.amount >= 0 || Theme.reducedMotion ? 0
                                   : fill.sweep * (track.width + fill.width) - fill.width
                                width: part.amount >= 0 ? track.width * Math.min(1, part.amount) : track.width / 3
                                height: track.height
                                radius: height / 2
                                color: Theme.colors.accent

                                NumberAnimation on sweep {
                                    running: part.kind === "progress" && part.amount < 0 && !Theme.reducedMotion
                                    from: 0
                                    to: 1
                                    duration: Theme.motion.pulse
                                    loops: Animation.Infinite
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
