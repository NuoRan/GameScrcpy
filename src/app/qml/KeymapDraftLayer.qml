pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Item {
    id: layer

    property ProfileEditor editor
    property rect stage
    property bool active: true

    readonly property var drafts: editor !== null && stage.width > 0 && editor.revision >= 0
                                  && editor.suggestionRevision >= 0
                                  ? editor.suggestionMarkers(stage.width, stage.height) : []

    objectName: "keymapDraftLayer"
    visible: active && drafts.length > 0

    Repeater {
        model: layer.drafts

        delegate: Item {
            id: draft

            required property var modelData
            property point pressAt
            property point moved
            property bool dragging: false

            readonly property real ring: Math.max(Theme.size.keyHintRound, 2 * Math.max(modelData.rx, modelData.ry))

            objectName: "keymapDraft"
            x: layer.stage.x + modelData.x + moved.x - ring / 2
            y: layer.stage.y + modelData.y + moved.y - ring / 2
            width: ring
            height: ring

            Shape {
                anchors.fill: parent

                ShapePath {
                    strokeColor: Theme.colors.accent
                    strokeWidth: Theme.size.focusRing
                    strokeStyle: ShapePath.DashLine
                    fillColor: Theme.colors.keyHintRingFill

                    PathAngleArc {
                        centerX: draft.ring / 2
                        centerY: draft.ring / 2
                        radiusX: draft.ring / 2 - Theme.size.focusRing
                        radiusY: draft.ring / 2 - Theme.size.focusRing
                        startAngle: 0
                        sweepAngle: 360
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space.xs
                textFormat: Text.PlainText
                text: draft.modelData.keyText !== "" ? draft.modelData.keyText : "?"
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                color: Theme.colors.keyHintText
                font.pixelSize: Theme.type.caption.size
                font.weight: Font.DemiBold
            }

            MouseArea {
                objectName: "keymapDraftHandle"
                anchors.fill: parent
                enabled: layer.active
                preventStealing: true
                cursorShape: draft.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                onPressed: mouse => {
                    draft.pressAt = mapToItem(layer, mouse.x, mouse.y)
                    draft.dragging = false
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    const at = mapToItem(layer, mouse.x, mouse.y)
                    draft.dragging = true
                    draft.moved = Qt.point(at.x - draft.pressAt.x, at.y - draft.pressAt.y)
                }
                onReleased: {
                    if (draft.dragging && layer.stage.width > 0 && layer.stage.height > 0)
                        layer.editor.placeSuggestion(draft.modelData.id,
                                                     (draft.modelData.x + draft.moved.x) / layer.stage.width,
                                                     (draft.modelData.y + draft.moved.y) / layer.stage.height)
                    draft.dragging = false
                    draft.moved = Qt.point(0, 0)
                }
                onCanceled: {
                    draft.dragging = false
                    draft.moved = Qt.point(0, 0)
                }
            }

            Rectangle {
                objectName: "keymapDraftCard"
                anchors.top: parent.bottom
                anchors.topMargin: Theme.space.xs
                anchors.horizontalCenter: parent.horizontalCenter
                width: card.implicitWidth + 2 * Theme.space.sm
                height: card.implicitHeight + 2 * Theme.space.xs
                radius: Theme.radius.md
                color: Theme.colors.bgOverlay
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeDefault
                visible: !draft.dragging

                Column {
                    id: card

                    anchors.centerIn: parent
                    spacing: Theme.space.xs

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        textFormat: Text.PlainText
                        text: draft.modelData.name !== "" ? draft.modelData.name : draft.modelData.type
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.caption.size
                    }

                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.space.xs

                        Button {
                            objectName: "keymapDraftAccept"
                            small: true
                            tone: "primary"
                            iconName: "check"
                            text: qsTr("Accept")
                            onClicked: layer.editor.acceptSuggestion(draft.modelData.id)
                        }

                        Button {
                            objectName: "keymapDraftReject"
                            small: true
                            iconName: "x"
                            text: qsTr("Reject")
                            onClicked: layer.editor.rejectSuggestion(draft.modelData.id)
                        }
                    }
                }
            }
        }
    }
}
