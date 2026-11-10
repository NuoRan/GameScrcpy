pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Item {
    id: art

    property string kind: "overview"

    readonly property var entries: [
        { icon: "keyboard", mark: 1, selected: true },
        { gap: Theme.space.xxs },
        { icon: "chevron-down", mark: 2, compact: true },
        { separator: true },
        { icon: "arrow-left", mark: 3 },
        { icon: "house", mark: 3 },
        { icon: "square", mark: 3 },
        { separator: true },
        { icon: "rotate-cw", mark: 4 },
        { icon: "power", mark: 5 },
        { icon: "volume-2", mark: 6 },
        { separator: true },
        { icon: "activity", mark: 7 },
        { icon: "maximize-2", mark: 8 },
        { icon: "ellipsis", mark: 9 },
        { separator: true },
        { icon: "pin", mark: 10 }
    ]
    readonly property var layout: {
        const placed = []
        let at = Theme.space.xs
        for (const entry of art.entries) {
            const wide = entry.separator ? Theme.size.stroke + 2 * Theme.space.xs
                       : entry.gap !== undefined ? entry.gap
                       : entry.compact ? Theme.size.toolbarChevron : Theme.size.toolbarButton
            placed.push(Object.assign({ x: at, w: wide }, entry))
            at += wide + Theme.space.xxs
        }
        return placed
    }
    readonly property real barWidth: {
        const last = art.layout[art.layout.length - 1]
        return last.x + last.w + Theme.space.xs
    }
    readonly property var marks: {
        const spans = {}
        for (const entry of art.layout) {
            if (entry.mark === undefined)
                continue
            const span = spans[entry.mark] ?? { from: entry.x, to: entry.x + entry.w, count: 0 }
            span.from = Math.min(span.from, entry.x)
            span.to = Math.max(span.to, entry.x + entry.w)
            span.count += 1
            spans[entry.mark] = span
        }
        return Object.keys(spans).map(key => Object.assign({ number: Number(key) }, spans[key]))
    }

    implicitWidth: art.kind === "toolbar" ? toolbarFigure.width
                 : art.kind === "cameraLock" ? lockFigure.width
                 : art.kind === "windowModes" ? modesFigure.width : overview.width
    implicitHeight: art.kind === "toolbar" ? toolbarFigure.height
                  : art.kind === "cameraLock" ? lockFigure.height
                  : art.kind === "windowModes" ? modesFigure.height : overview.height

    component Bar: Rectangle {
        id: bar

        property var layout: []

        height: Theme.size.toolbar
        radius: height / 2
        color: Theme.reducedTransparency ? Theme.colors.bgElevated : Theme.colors.bgFrosted
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong

        Repeater {
            model: bar.layout

            delegate: Item {
                id: slot

                required property var modelData

                x: slot.modelData.x
                y: (bar.height - height) / 2
                width: slot.modelData.w
                height: Theme.size.toolbarButton

                Rectangle {
                    anchors.centerIn: parent
                    visible: slot.modelData.separator === true
                    width: Theme.size.stroke
                    height: Theme.size.toolbarSeparator
                    color: Theme.colors.strokeDefault
                }

                Rectangle {
                    anchors.fill: parent
                    visible: slot.modelData.selected === true
                    radius: Math.min(width, height) / 2
                    color: Theme.colors.accentSubtle
                }

                Icon {
                    anchors.centerIn: parent
                    visible: (slot.modelData.icon ?? "") !== ""
                    name: slot.modelData.icon ?? ""
                    size: slot.modelData.compact === true ? Theme.size.iconSmall : Theme.size.icon
                    color: slot.modelData.selected === true ? Theme.colors.accentText : Theme.colors.textSecondary
                }
            }
        }
    }

    component Caption: Text {
        textFormat: Text.PlainText
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    component Crosshair: Item {
        width: 26
        height: 26

        Repeater {
            model: 4

            Rectangle {
                required property int index
                readonly property bool across: index < 2

                x: across ? (index === 0 ? 0 : parent.width - width) : (parent.width - width) / 2
                y: across ? (parent.height - height) / 2 : (index === 2 ? 0 : parent.height - height)
                width: across ? 8 : 2
                height: across ? 2 : 8
                radius: 1
                color: "#FFFFFF"
                border.width: Theme.size.stroke
                border.color: "#80000000"
            }
        }
    }

    component Key: Rectangle {
        id: key

        property string label
        property bool hot: false
        property bool faded: false

        width: 40
        height: 38
        radius: Theme.radius.md - 1
        opacity: key.faded ? 0.45 : 1
        color: key.hot ? Theme.colors.accentSubtle : Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: key.hot ? Theme.colors.accentLine : Theme.colors.strokeDefault

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: key.radius
            anchors.rightMargin: key.radius
            height: Theme.size.focusRing
            color: key.hot ? Theme.colors.accentLine : Theme.colors.strokeStrong
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.space.xs
            visible: key.hot
            radius: key.radius + Theme.space.xs
            color: "transparent"
            border.width: Theme.space.xs
            border.color: Theme.colors.accentGlow
        }

        Text {
            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: key.label
            color: key.hot ? Theme.colors.accentText : Theme.colors.textSecondary
            font.pixelSize: key.hot ? Theme.type.title.size : Theme.type.caption.size + 0.5
            font.weight: key.hot ? Font.Bold : Font.Normal
        }
    }

    component Desk: Rectangle {
        radius: Theme.radius.md
        clip: true
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong
        gradient: Gradient {
            GradientStop { position: 0; color: "#1C2A3A" }
            GradientStop { position: 0.6; color: "#0D1219" }
            GradientStop { position: 1; color: "#090C10" }
        }
    }

    Item {
        id: overview

        visible: art.kind === "overview"
        width: 680
        height: 300

        HelpScreen {
            id: overviewWindow

            x: 20
            y: 12
            width: 640
            height: 278

            Bar {
                id: overviewBar

                anchors.horizontalCenter: parent.horizontalCenter
                y: Theme.space.md
                width: art.barWidth
                layout: art.layout
                transformOrigin: Item.Top
                scale: 0.84
            }

            Rectangle {
                id: ring

                x: 46
                y: 150
                width: 86
                height: width
                radius: width / 2
                color: Theme.colors.keyHintRingFill
                border.width: Theme.size.keyHintRing
                border.color: Theme.colors.keyHintRing

                KeyHint { text: "W"; anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: parent.top }
                KeyHint { text: "S"; anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: parent.bottom }
                KeyHint { text: "A"; anchors.horizontalCenter: parent.left; anchors.verticalCenter: parent.verticalCenter }
                KeyHint { text: "D"; anchors.horizontalCenter: parent.right; anchors.verticalCenter: parent.verticalCenter }
            }

            KeyHint { x: 520; y: 150; shape: "tap"; text: "F" }
            KeyHint { x: 466; y: 196; shape: "tap"; text: "R" }

            Crosshair {
                x: (parent.width - width) / 2
                y: 112
            }

            StatusCapsule {
                id: overviewCapsule

                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.space.md
                cameraLocked: true
                framesPerSecond: 120
                latencyMs: 18
                transport: qsTr("USB")
            }
        }

        HelpMarker {
            number: 1
            x: overviewWindow.x + (overviewWindow.width - overviewBar.width * overviewBar.scale) / 2 - width - Theme.space.xs
            y: overviewWindow.y + Theme.space.md + (Theme.size.toolbar * overviewBar.scale - height) / 2
        }

        HelpMarker {
            number: 2
            x: overviewWindow.x + ring.x - width - Theme.space.xs
            y: overviewWindow.y + ring.y - height
        }

        HelpMarker {
            number: 3
            x: overviewWindow.x + overviewWindow.width - Theme.space.md - overviewCapsule.width - width - Theme.space.xs
            y: overviewWindow.y + overviewWindow.height - Theme.space.md - (overviewCapsule.height + height) / 2
        }
    }

    Item {
        id: toolbarFigure

        visible: art.kind === "toolbar"
        width: art.barWidth + 2 * Theme.space.lg
        height: 124

        Bar {
            id: bigBar

            x: Theme.space.lg
            y: Theme.space.md
            width: art.barWidth
            layout: art.layout
        }

        Repeater {
            model: art.marks

            delegate: Item {
                id: mark

                required property var modelData
                readonly property bool group: mark.modelData.count > 1
                readonly property real reach: mark.modelData.number === 2 ? 34 : mark.group ? 12 : 14

                x: bigBar.x + mark.modelData.from
                y: bigBar.y + bigBar.height
                width: mark.modelData.to - mark.modelData.from
                height: mark.reach + marker.height

                Rectangle {
                    visible: mark.group
                    x: Theme.space.xs
                    width: parent.width - 2 * Theme.space.xs
                    height: Theme.space.sm + Theme.space.xxs
                    radius: Theme.radius.sm
                    color: "transparent"
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.accentLine

                    Rectangle {
                        width: parent.width
                        height: parent.height / 2
                        color: Theme.colors.bgSunken
                    }
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: mark.group ? Theme.space.sm + Theme.space.xxs : 0
                    width: Theme.size.stroke
                    height: mark.reach - y
                    color: Theme.colors.accentLine
                }

                HelpMarker {
                    id: marker

                    anchors.horizontalCenter: parent.horizontalCenter
                    y: mark.reach
                    number: mark.modelData.number
                }
            }
        }
    }

    Item {
        id: lockFigure

        visible: art.kind === "cameraLock"
        width: 680
        height: 214

        Rectangle {
            id: keyboard

            x: 8
            y: 14
            width: keys.implicitWidth + 2 * Theme.space.md
            height: keys.implicitHeight + 2 * Theme.space.md
            radius: Theme.radius.xl
            color: Theme.colors.bgOverlay
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Column {
                id: keys

                anchors.centerIn: parent
                spacing: Theme.space.sm - Theme.space.xxs

                Row {
                    spacing: Theme.space.sm - Theme.space.xxs

                    Key { label: "~"; hot: true }
                    Key { label: "1" }
                    Key { label: "2" }
                    Key { label: "3" }
                    Key { label: "4"; faded: true }
                }

                Row {
                    spacing: Theme.space.sm - Theme.space.xxs

                    Key { label: "Tab"; width: 60 }
                    Key { label: "Q" }
                    Key { label: "W" }
                    Key { label: "E"; faded: true }
                }

                Row {
                    spacing: Theme.space.sm - Theme.space.xxs

                    Key { label: "Caps"; width: 70 }
                    Key { label: "A" }
                    Key { label: "S"; faded: true }
                }
            }
        }

        Caption {
            anchors.horizontalCenter: keyboard.horizontalCenter
            anchors.top: keyboard.bottom
            anchors.topMargin: Theme.space.md
            width: keyboard.width + Theme.space.x3l
            text: qsTr("The camera lock key: above Tab, left of 1")
        }

        Icon {
            x: (keyboard.x + keyboard.width + lockWindow.x) / 2 - width / 2
            y: lockWindow.y + (lockWindow.height - height) / 2
            name: "chevron-right"
            size: Theme.size.iconLarge + Theme.space.xs
            color: Theme.colors.textTertiary
        }

        HelpScreen {
            id: lockWindow

            x: 332
            y: 14
            width: 340
            height: 164

            EdgeGlow {
                anchors.fill: parent
                active: true
            }

            Crosshair {
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2
            }

            StatusCapsule {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.space.sm
                transformOrigin: Item.BottomRight
                scale: 0.8
                cameraLocked: true
            }
        }

        Caption {
            anchors.horizontalCenter: lockWindow.horizontalCenter
            anchors.top: lockWindow.bottom
            anchors.topMargin: Theme.space.md
            width: lockWindow.width
            text: qsTr("Locked: the pointer hides and the edges of the picture glow")
        }
    }

    Item {
        id: modesFigure

        visible: art.kind === "windowModes"
        width: 640
        height: 196

        Desk {
            id: fullDesk

            x: 30
            y: 12
            width: 250
            height: 140

            HelpScene {
                anchors.fill: parent
                anchors.margins: Theme.size.stroke
            }

            Keycap {
                anchors.centerIn: parent
                large: true
                text: "F11"
            }
        }

        Caption {
            anchors.horizontalCenter: fullDesk.horizontalCenter
            anchors.top: fullDesk.bottom
            anchors.topMargin: Theme.space.md
            text: qsTr("Full screen")
            color: Theme.colors.textSecondary
            font.weight: Font.DemiBold
        }

        Desk {
            id: miniDesk

            x: 360
            y: 12
            width: 250
            height: 140

            Rectangle {
                x: 16
                y: 14
                width: 150
                height: 86
                radius: Theme.radius.sm
                color: "#171A1F"
                border.width: Theme.size.stroke
                border.color: "#1AFFFFFF"

                Column {
                    x: Theme.space.sm
                    y: Theme.space.md
                    spacing: Theme.space.sm - Theme.space.xxs

                    Repeater {
                        model: [0.7, 0.5, 0.4]

                        Rectangle {
                            required property real modelData

                            width: 134 * modelData
                            height: Theme.space.xs
                            radius: height / 2
                            color: "#1AFFFFFF"
                        }
                    }
                }
            }

            HelpScreen {
                x: 166
                y: 72
                width: 74
                height: 42
                radius: Theme.radius.sm
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.size.stroke
                height: 14
                color: "#E614181E"
            }
        }

        Caption {
            anchors.horizontalCenter: miniDesk.horizontalCenter
            anchors.top: miniDesk.bottom
            anchors.topMargin: Theme.space.md
            text: qsTr("A mini window on top")
            color: Theme.colors.textSecondary
            font.weight: Font.DemiBold
        }
    }
}
