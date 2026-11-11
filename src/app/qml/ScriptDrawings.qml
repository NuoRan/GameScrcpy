pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme

Item {
    id: layer

    property rect picture
    property var progress: []

    function draw(id: string, shown: var) {
        layer.erase(id)
        drawings.append({
            drawingId: id,
            group: "",
            kind: shown.kind ?? "box",
            fx: shown.x ?? 0,
            fy: shown.y ?? 0,
            fw: shown.width ?? 0,
            fh: shown.height ?? 0,
            label: shown.text ?? "",
            tone: shown.color ?? "accent",
            image: shown.source ?? "",
            extension: shown.extension ?? ""
        })
    }

    function erase(id: string) {
        for (let i = 0; i < drawings.count; ++i) {
            if (drawings.get(i).drawingId === id) {
                drawings.remove(i)
                return
            }
        }
    }

    function clear() {
        for (let i = drawings.count - 1; i >= 0; --i) {
            if (drawings.get(i).group === "")
                drawings.remove(i)
        }
    }

    function setOverlay(group: string, shapes: var) {
        const wanted = new Map()
        for (const shape of shapes)
            wanted.set(group + "/" + shape.id, shape)
        for (let i = drawings.count - 1; i >= 0; --i) {
            const entry = drawings.get(i)
            if (entry.group !== group)
                continue
            const next = wanted.get(entry.drawingId)
            if (next === undefined) {
                drawings.remove(i)
            } else {
                drawings.set(i, layer.rowOf(entry.drawingId, group, next))
                wanted.delete(entry.drawingId)
            }
        }
        for (const [id, shape] of wanted)
            drawings.append(layer.rowOf(id, group, shape))
    }

    function rowOf(id: string, group: string, shown: var): var {
        return {
            drawingId: id,
            group: group,
            kind: shown.kind ?? "box",
            fx: shown.x ?? 0,
            fy: shown.y ?? 0,
            fw: shown.width ?? 0,
            fh: shown.height ?? 0,
            label: shown.text ?? "",
            tone: shown.color ?? "accent",
            image: "",
            extension: shown.extension ?? ""
        }
    }

    function setProgress(source: string, title: string, value: real) {
        const next = layer.progress.filter(card => card.source !== source)
        next.push({ source: source, title: title, value: Math.max(0, Math.min(1, value)) })
        layer.progress = next
    }

    function clearProgress(source: string) {
        layer.progress = layer.progress.filter(card => card.source !== source)
    }

    function colorOf(tone: string): color {
        switch (tone) {
        case "good":
            return Theme.colors.success
        case "warn":
            return Theme.colors.warning
        case "bad":
            return Theme.colors.danger
        case "white":
            return "#FFFFFF"
        default:
            return Theme.colors.accent
        }
    }

    ListModel {
        id: drawings
    }

    Repeater {
        model: drawings

        Item {
            id: shape

            required property string kind
            required property real fx
            required property real fy
            required property real fw
            required property real fh
            required property string label
            required property string tone
            required property string image
            required property string extension
            readonly property string tagText: shape.line ? ""
                                              : shape.extension === "" ? shape.label
                                              : shape.label === "" ? shape.extension
                                              : qsTr("%1 · %2").arg(shape.extension).arg(shape.label)
            readonly property bool mark: shape.kind === "mark"
            readonly property bool area: shape.kind === "box" || shape.kind === "image"
            readonly property bool line: shape.kind === "line"

            x: layer.picture.x + shape.fx * layer.picture.width - (shape.mark ? ring.width / 2 : 0)
            y: layer.picture.y + shape.fy * layer.picture.height - (shape.mark ? ring.height / 2 : 0)
            width: shape.area ? shape.fw * layer.picture.width : shape.mark ? ring.width : tag.width
            height: shape.area ? shape.fh * layer.picture.height : shape.mark ? ring.height : tag.height

            Image {
                anchors.fill: parent
                visible: shape.kind === "image"
                source: shape.kind === "image" ? shape.image : ""
                fillMode: Image.Stretch
                asynchronous: true
                cache: false
                smooth: true
            }

            Rectangle {
                anchors.fill: parent
                visible: shape.area
                color: "transparent"
                border.width: shape.kind === "image" ? Theme.size.stroke : 2
                border.color: layer.colorOf(shape.tone)
            }

            Rectangle {
                id: stroke

                readonly property real dx: shape.fw * layer.picture.width
                readonly property real dy: shape.fh * layer.picture.height

                objectName: "scriptDrawingLine"
                visible: shape.line
                width: Math.sqrt(stroke.dx * stroke.dx + stroke.dy * stroke.dy)
                height: Theme.space.xxs
                y: -height / 2
                transformOrigin: Item.Left
                rotation: Math.atan2(stroke.dy, stroke.dx) * 180 / Math.PI
                antialiasing: true
                color: layer.colorOf(shape.tone)
            }

            Rectangle {
                id: ring

                visible: shape.mark
                width: Theme.space.lg
                height: Theme.space.lg
                radius: width / 2
                color: "transparent"
                border.width: Theme.space.xxs
                border.color: layer.colorOf(shape.tone)

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.space.xs
                    height: Theme.space.xs
                    radius: width / 2
                    color: layer.colorOf(shape.tone)
                }
            }

            Rectangle {
                id: tag

                objectName: "scriptDrawingTag"
                visible: shape.tagText !== ""
                x: shape.mark ? ring.width + Theme.space.xs + Theme.space.xxs : 0
                y: shape.area ? -height : shape.mark ? (ring.height - height) / 2 : 0
                width: caption.implicitWidth + 2 * Theme.space.xs
                height: caption.implicitHeight + 2 * Theme.space.xxs
                radius: Theme.radius.xs
                color: Qt.alpha(Theme.colors.bgElevated, 0.85)

                Text {
                    id: caption

                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: shape.tagText
                    color: layer.colorOf(shape.tone)
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }
    }

    Column {
        objectName: "scriptProgress"
        x: layer.picture.x + (layer.picture.width - width) / 2
        y: layer.picture.y + Theme.space.md
        width: Math.min(layer.picture.width - 2 * Theme.space.lg, Theme.size.infoBubble)
        spacing: Theme.space.xs
        visible: layer.progress.length > 0

        Repeater {
            model: layer.progress

            delegate: Rectangle {
                id: card

                required property var modelData

                objectName: "scriptProgressCard"
                width: parent.width
                height: cardColumn.implicitHeight + 2 * Theme.space.sm
                radius: Theme.radius.md
                color: Qt.alpha(Theme.colors.bgElevated, 0.92)
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeSubtle

                Accessible.role: Accessible.ProgressBar
                Accessible.name: card.modelData.title ?? ""

                Column {
                    id: cardColumn

                    x: Theme.space.md
                    y: Theme.space.sm
                    width: parent.width - 2 * x
                    spacing: Theme.space.xs

                    Item {
                        width: parent.width
                        height: Math.max(title.implicitHeight, percent.implicitHeight)

                        Text {
                            id: title

                            width: parent.width - percent.implicitWidth - Theme.space.sm
                            textFormat: Text.PlainText
                            text: card.modelData.title ?? ""
                            elide: Text.ElideRight
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Theme.type.label.weight
                        }

                        Text {
                            id: percent

                            anchors.right: parent.right
                            textFormat: Text.PlainText
                            text: qsTr("%1%").arg(Math.round((card.modelData.value ?? 0) * 100))
                            color: Theme.colors.textSecondary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.caption.size
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: Theme.space.xs
                        radius: height / 2
                        color: Theme.colors.strokeSubtle

                        Rectangle {
                            width: parent.width * (card.modelData.value ?? 0)
                            height: parent.height
                            radius: height / 2
                            color: Theme.colors.accent
                        }
                    }
                }
            }
        }
    }
}
