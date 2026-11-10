pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

import "Geometry.js" as Geometry

Rectangle {
    id: timeline

    property var span: ({ start: 0, end: 0 })
    property real playhead: 0
    property bool live: true
    property var frames: []
    property var steps: []
    property var lanes: []
    property bool dragging: false
    property real dragAt: 0
    property real pendingSeek: -1
    readonly property real begin: span?.start ?? 0
    readonly property real length: Math.max(1, (span?.end ?? 0) - begin)
    readonly property real shown: dragging ? dragAt : Math.max(begin, Math.min(begin + length, playhead))
    readonly property var rows: (lanes ?? []).length > 1 ? lanes : [{ key: "", title: qsTr("Steps") }]
    readonly property real trackX: Theme.space.md + Theme.size.bpTrackLabel + Theme.space.md - Theme.space.xxs
    readonly property real trackWidth: Math.max(1, width - trackX - Theme.space.md)

    signal seeked(real at)
    signal liveClicked()
    signal saveClicked()

    function xOf(at: real): real {
        return trackX + (at - begin) / length * trackWidth
    }

    function timeAt(x: real): real {
        return begin + Math.max(0, Math.min(1, (x - trackX) / trackWidth)) * length
    }

    function seekTo(x: real) {
        dragAt = timeAt(x)
        pendingSeek = dragAt
        Qt.callLater(timeline.flushSeek)
    }

    function flushSeek() {
        if (pendingSeek < 0)
            return
        const at = pendingSeek
        pendingSeek = -1
        seeked(at)
    }

    objectName: "bpTimeline"
    implicitWidth: Theme.size.workbenchCode
    implicitHeight: tracks.y + tracks.height + Theme.space.sm
    color: Theme.colors.bgRaised

    Accessible.role: Accessible.Slider
    Accessible.name: qsTr("Timeline")
    Accessible.description: Geometry.preciseClock(shown - begin)

    Rectangle {
        width: parent.width
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    Item {
        id: header

        width: parent.width
        height: Theme.size.bpTimelineHeader

        Row {
            x: Theme.space.md
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "history"
                size: Theme.size.iconSmall
                color: Theme.colors.accentText
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("Timeline")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.DemiBold
            }

            Text {
                objectName: "bpTimelineClock"
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("%1 / %2").arg(Geometry.preciseClock(timeline.shown - timeline.begin))
                                     .arg(Geometry.preciseClock(timeline.length))
                color: Theme.colors.textSecondary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space.md
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("Drag to look back at each step: the picture and what was found")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Button {
                objectName: "bpLive"
                anchors.verticalCenter: parent.verticalCenter
                visible: !timeline.live
                small: true
                tone: "ghost"
                iconName: "play"
                text: qsTr("Back to now")
                onClicked: timeline.liveClicked()
            }

            Button {
                objectName: "bpSaveSegment"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                iconName: "pin"
                text: qsTr("Keep this part")
                toolTip: qsTr("Save the last 5 minutes so they are not lost when debugging stops")
                onClicked: timeline.saveClicked()
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }
    }

    Column {
        id: tracks

        y: header.height + Theme.space.sm + Theme.space.xxs
        width: parent.width
        spacing: Theme.space.xs

        Item {
            width: parent.width
            height: Theme.size.bpFrames

            Text {
                x: Theme.space.md
                width: Theme.size.bpTrackLabel
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("Picture")
                elide: Text.ElideRight
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Row {
                id: strip

                readonly property real cell: Math.max(Theme.size.bpThumbWidth, height * 16 / 9)
                readonly property int slots: Math.max(1, Math.floor((timeline.trackWidth + spacing)
                                                                    / (cell + spacing)))
                readonly property var picks: {
                    const list = timeline.frames ?? []
                    const result = []
                    if (list.length === 0)
                        return result
                    for (let slot = 0; slot < slots; ++slot) {
                        const at = timeline.begin + (slot + 0.5) / slots * timeline.length
                        let best = list[0]
                        for (const frame of list) {
                            if (Math.abs(frame.at - at) < Math.abs(best.at - at))
                                best = frame
                        }
                        result.push({ at: best.at, source: best.source ?? "", from: timeline.begin + slot / slots
                                      * timeline.length, to: timeline.begin + (slot + 1) / slots * timeline.length })
                    }
                    return result
                }

                x: timeline.trackX
                width: timeline.trackWidth
                height: parent.height
                spacing: Theme.space.xs

                Repeater {
                    model: strip.picks

                    delegate: Rectangle {
                        id: thumb

                        required property var modelData
                        readonly property bool current: timeline.shown >= modelData.from && timeline.shown < modelData.to
                                                        || (timeline.shown >= timeline.begin + timeline.length
                                                            && modelData.to >= timeline.begin + timeline.length)

                        width: (strip.width - (strip.slots - 1) * strip.spacing) / strip.slots
                        height: strip.height
                        radius: Theme.radius.xs
                        color: Theme.colors.bgSunken
                        opacity: modelData.from > timeline.shown ? Theme.disabledOpacity : 1
                        clip: true

                        Image {
                            anchors.fill: parent
                            visible: thumb.modelData.source !== ""
                            source: thumb.modelData.source
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            retainWhileLoading: true
                            smooth: true
                        }

                        Icon {
                            anchors.centerIn: parent
                            visible: thumb.modelData.source === ""
                            name: "image"
                            size: Theme.size.iconSmall
                            color: Theme.colors.textDisabled
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: "transparent"
                            border.width: thumb.current ? Theme.size.focusRing : Theme.size.stroke
                            border.color: thumb.current ? Theme.colors.accent : Theme.colors.strokeSubtle
                        }
                    }
                }
            }
        }

        Repeater {
            model: timeline.rows

            delegate: Item {
                id: lane

                required property var modelData
                readonly property string key: modelData.key ?? ""

                width: tracks.width
                height: Theme.size.bpSteps

                Text {
                    x: Theme.space.md
                    width: Theme.size.bpTrackLabel
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: lane.modelData.title ?? ""
                    elide: Text.ElideRight
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                Repeater {
                    model: (timeline.steps ?? []).filter(step => timeline.rows.length <= 1 || (step.lane ?? "") === lane.key)

                    delegate: Rectangle {
                        id: stepItem

                        required property var modelData
                        readonly property bool current: timeline.shown >= modelData.start && timeline.shown <= modelData.end

                        x: timeline.xOf(modelData.start)
                        y: (lane.height - height) / 2
                        width: Math.max(Theme.size.stroke * 2, timeline.xOf(modelData.end) - x)
                        height: Theme.size.bpStep
                        radius: Theme.radius.sm
                        color: BlueprintLook.tint(modelData.tone ?? "")
                        border.width: current ? Theme.size.focusRing : 0
                        border.color: Theme.colors.accent
                        clip: true

                        Accessible.role: Accessible.StaticText
                        Accessible.name: modelData.text ?? ""

                        Text {
                            x: Theme.space.xs + Theme.space.xxs
                            width: parent.width - 2 * x
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: stepItem.modelData.text ?? ""
                            elide: Text.ElideRight
                            color: BlueprintLook.ink(stepItem.modelData.tone ?? "")
                            font.pixelSize: Theme.type.micro.size
                        }
                    }
                }
            }
        }
    }

    MouseArea {
        id: scrub

        x: timeline.trackX - Theme.size.bpPlayhead / 2
        y: tracks.y - Theme.size.bpPlayhead
        width: timeline.trackWidth + Theme.size.bpPlayhead
        height: tracks.height + Theme.size.bpPlayhead
        preventStealing: true
        cursorShape: Qt.SizeHorCursor
        onPressed: mouse => {
            timeline.dragging = true
            timeline.seekTo(scrub.mapToItem(timeline, mouse.x, mouse.y).x)
        }
        onPositionChanged: mouse => {
            if (scrub.pressed)
                timeline.seekTo(scrub.mapToItem(timeline, mouse.x, mouse.y).x)
        }
        onReleased: {
            timeline.flushSeek()
            timeline.dragging = false
        }
        onCanceled: timeline.dragging = false
    }

    Item {
        id: playheadItem

        objectName: "bpPlayhead"
        x: timeline.xOf(timeline.shown) - width / 2
        y: tracks.y - Theme.size.bpPlayhead / 2
        width: Theme.size.bpPlayhead
        height: tracks.height + Theme.size.bpPlayhead / 2

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.size.bpPlayhead / 2
            width: Theme.size.focusRing
            height: parent.height - y
            color: Theme.colors.accent
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.size.bpPlayhead + 2 * Theme.size.fieldGlow
            height: width
            y: -Theme.size.fieldGlow
            radius: width / 2
            color: Theme.colors.accentRing

            Rectangle {
                anchors.centerIn: parent
                width: Theme.size.bpPlayhead
                height: width
                radius: width / 2
                color: Theme.colors.accent
            }
        }
    }
}
