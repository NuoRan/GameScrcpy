pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Item {
    id: art

    property string kind: "workbench"
    property string stage: "freeze"
    readonly property var code: ({
        keyword: Theme.colors.accentText,
        string: Theme.colors.successText,
        number: Theme.colors.warningText,
        comment: Theme.colors.textTertiary,
        type: Theme.colors.infoText
    })

    implicitWidth: art.kind === "until" ? flowFigure.width : art.kind === "vision" ? vision.width : bench.width
    implicitHeight: art.kind === "until" ? flowFigure.height : art.kind === "vision" ? vision.height : bench.height

    component Bars: Column {
        id: bars

        property var rows: []
        property real full: 100
        property real thickness: 5

        spacing: bars.thickness + Theme.space.xxs

        Repeater {
            model: bars.rows

            Rectangle {
                required property var modelData

                x: (modelData.indent ?? 0) * Theme.space.lg
                width: bars.full * modelData.width
                height: bars.thickness
                radius: height / 2
                color: modelData.color ?? Theme.colors.strokeDefault
            }
        }
    }

    component Panel: Rectangle {
        color: Theme.colors.bgRaised
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle
    }

    component Step: Rectangle {
        id: step

        property string iconName
        property string tone
        property string title
        property string detail
        property var ticks: []

        width: 196
        height: stepColumn.implicitHeight + 2 * Theme.space.md
        radius: Theme.radius.xl
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeDefault

        Column {
            id: stepColumn

            x: Theme.space.md
            y: Theme.space.md
            width: parent.width - 2 * Theme.space.md
            spacing: Theme.space.sm

            Row {
                spacing: Theme.space.sm

                Tile {
                    iconName: step.iconName
                    tone: step.tone
                    scale: 0.86
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: step.title
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                }
            }

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: step.detail
                color: Theme.colors.textSecondary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size
                elide: Text.ElideRight
            }

            Row {
                visible: step.ticks.length > 0
                spacing: Theme.space.xs

                Repeater {
                    model: step.ticks

                    Rectangle {
                        required property bool modelData

                        width: Theme.space.md
                        height: width
                        radius: Theme.radius.xs - 1
                        color: modelData ? Theme.colors.success : Theme.colors.strokeDefault
                    }
                }
            }
        }
    }

    component Arrow: Icon {
        name: "chevron-right"
        size: Theme.size.iconLarge
        color: Theme.colors.textTertiary
    }

    Item {
        id: bench

        visible: art.kind === "workbench"
        width: 680
        height: 268

        Rectangle {
            id: frame

            x: 20
            y: 14
            width: 640
            height: 240
            radius: Theme.radius.lg
            color: Theme.colors.bgBase
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeStrong
            clip: true

            Rectangle {
                id: benchTop

                width: parent.width
                height: 28
                color: Theme.colors.bgRaised

                Bars {
                    x: Theme.space.md
                    anchors.verticalCenter: parent.verticalCenter
                    full: 160
                    rows: [{ width: 0.5, color: Theme.colors.strokeStrong }]
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 16
                    radius: Theme.radius.sm
                    color: Theme.colors.accent

                    Icon {
                        anchors.centerIn: parent
                        name: "play"
                        size: Theme.size.keyHintTagIcon
                        color: Theme.colors.textOnAccent
                    }
                }
            }

            Panel {
                id: tree

                y: benchTop.height
                width: 118
                height: parent.height - benchTop.height

                Bars {
                    x: Theme.space.sm + Theme.space.xxs
                    y: Theme.space.md
                    full: 90
                    rows: [{ width: 0.75 }, { width: 0.55, indent: 0.6 }, { width: 0.6, indent: 0.6, color: Theme.colors.accentLine },
                           { width: 0.5, indent: 0.6 }, { width: 0.65 }, { width: 0.45 }]
                }
            }

            Rectangle {
                id: editor

                x: tree.width
                y: benchTop.height
                width: parent.width - tree.width - side.width
                height: parent.height - benchTop.height - consolePanel.height
                color: Theme.colors.bgEditor

                Bars {
                    x: Theme.space.md
                    y: Theme.space.md
                    full: editor.width - 2 * Theme.space.md
                    rows: [{ width: 0.42, color: art.code.keyword }, { width: 0.6, indent: 1, color: art.code.type },
                           { width: 0.36, indent: 1, color: art.code.string }, { width: 0.1, color: Theme.colors.textSecondary },
                           { width: 0 }, { width: 0.5, color: art.code.keyword }, { width: 0.44, indent: 1, color: art.code.type },
                           { width: 0.3, indent: 2, color: art.code.number }]
                }
            }

            Panel {
                id: consolePanel

                x: tree.width
                y: parent.height - height
                width: editor.width
                height: 62

                Bars {
                    x: Theme.space.md
                    y: Theme.space.md
                    full: consolePanel.width - 2 * Theme.space.md
                    thickness: 4
                    rows: [{ width: 0.5 }, { width: 0.38, color: Theme.colors.dangerLine }, { width: 0.46 }]
                }
            }

            Panel {
                id: side

                x: parent.width - width
                y: benchTop.height
                width: 146
                height: parent.height - benchTop.height

                HelpScreen {
                    id: sidePreview

                    x: Theme.space.sm + Theme.space.xxs
                    y: Theme.space.sm + Theme.space.xxs
                    width: parent.width - 2 * x
                    height: 64
                    radius: Theme.radius.md
                    scene: "lobby"
                }

                Bars {
                    x: sidePreview.x
                    y: sidePreview.y + sidePreview.height + Theme.space.md
                    full: sidePreview.width
                    rows: [{ width: 0.6 }, { width: 0.8 }, { width: 0.4 }]
                }
            }
        }

        HelpMarker {
            number: 1
            x: frame.x - width / 2
            y: frame.y + benchTop.height + Theme.space.lg
        }

        HelpMarker {
            number: 2
            x: frame.x + tree.width + editor.width / 2 - width / 2
            y: frame.y + benchTop.height + Theme.space.lg + 64
        }

        HelpMarker {
            number: 3
            x: frame.x + frame.width - width / 2
            y: frame.y + benchTop.height + Theme.space.lg
        }

        HelpMarker {
            number: 4
            x: frame.x + tree.width + editor.width / 2 - width / 2
            y: frame.y + frame.height - consolePanel.height / 2 - height / 2
        }
    }

    Item {
        id: flowFigure

        visible: art.kind === "until"
        width: 680
        height: flowRow.height + Theme.space.md + branch.height + 2 * Theme.space.md

        Row {
            id: flowRow

            x: Theme.space.md
            y: Theme.space.md
            spacing: Theme.space.sm

            Step {
                id: firstStep

                iconName: "tap-echo"
                tone: "orange"
                title: qsTr("Do the action")
                detail: "touch.tap(0.9, 0.9)"
                height: middleStep.height
            }

            Arrow { anchors.verticalCenter: parent.verticalCenter }

            Step {
                id: middleStep

                iconName: "eye"
                tone: "azure"
                title: qsTr("Check every frame")
                detail: "vision.find(…)"
                ticks: [false, false, false, false, true]
            }

            Arrow { anchors.verticalCenter: parent.verticalCenter }

            Step {
                iconName: "check"
                tone: "lime"
                title: qsTr("True: go on")
                detail: qsTr("returns what it found")
                height: middleStep.height
            }
        }

        Rectangle {
            id: branch

            x: Theme.space.md
            y: flowRow.y + flowRow.height + Theme.space.md
            width: parent.width - 2 * Theme.space.md
            height: branchRow.implicitHeight + 2 * Theme.space.sm + Theme.space.xxs
            radius: Theme.radius.lg
            color: "transparent"
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeStrong

            Row {
                id: branchRow

                x: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * Theme.space.md
                spacing: Theme.space.sm

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "refresh-cw"
                    size: Theme.size.iconSmall
                    color: Theme.colors.textTertiary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - Theme.size.iconSmall - parent.spacing
                    textFormat: Text.PlainText
                    text: qsTr("Not true by timeoutMs: the action is done again, at most retry times. If it never comes true, a TimeoutError names the line.")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size + 0.5
                    lineHeight: 19
                    lineHeightMode: Text.FixedHeight
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Item {
        id: vision

        visible: art.kind === "vision"
        width: 640
        height: 292

        Rectangle {
            id: visionFrame

            x: Theme.space.lg
            y: Theme.space.sm
            width: 608
            height: 276
            radius: Theme.radius.lg
            color: Theme.colors.bgBase
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeStrong
            clip: true

            Rectangle {
                id: visionTop

                width: parent.width
                height: 36
                color: Theme.colors.bgRaised

                Rectangle {
                    id: liveSwitch

                    x: Theme.space.md
                    anchors.verticalCenter: parent.verticalCenter
                    width: liveRow.implicitWidth + 2 * Theme.space.xxs
                    height: Theme.size.controlSmall - Theme.space.xs
                    radius: height / 2
                    color: Theme.colors.bgSunken
                    border.width: Theme.size.stroke
                    border.color: art.stage === "freeze" ? Theme.colors.accentLine : Theme.colors.strokeDefault

                    Row {
                        id: liveRow

                        anchors.centerIn: parent

                        Repeater {
                            model: [qsTr("Live"), qsTr("Frozen")]

                            delegate: Rectangle {
                                id: liveSegment

                                required property string modelData
                                required property int index

                                width: liveText.implicitWidth + 2 * Theme.space.md
                                height: liveSwitch.height - 2 * Theme.space.xxs
                                radius: height / 2
                                color: liveSegment.index === 1 ? Theme.colors.bgElevated : "transparent"
                                border.width: liveSegment.index === 1 ? Theme.size.stroke : 0
                                border.color: Theme.colors.strokeStrong

                                Text {
                                    id: liveText

                                    anchors.centerIn: parent
                                    textFormat: Text.PlainText
                                    text: liveSegment.modelData
                                    color: liveSegment.index === 1 ? Theme.colors.textPrimary : Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.micro.size
                                    font.weight: liveSegment.index === 1 ? Font.DemiBold : Font.Normal
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.md
                    anchors.verticalCenter: parent.verticalCenter
                    width: testRow.implicitWidth + 2 * Theme.space.md
                    height: Theme.size.controlSmall - Theme.space.xs
                    radius: Theme.radius.md
                    color: art.stage === "test" ? Theme.colors.accent : Theme.colors.bgOverlay
                    border.width: art.stage === "test" ? 0 : Theme.size.stroke
                    border.color: Theme.colors.strokeStrong

                    Row {
                        id: testRow

                        anchors.centerIn: parent
                        spacing: Theme.space.xs

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "search"
                            size: Theme.size.keyHintTag - Theme.space.xxs
                            color: art.stage === "test" ? Theme.colors.textOnAccent : Theme.colors.textPrimary
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: qsTr("Test find")
                            color: art.stage === "test" ? Theme.colors.textOnAccent : Theme.colors.textPrimary
                            font.pixelSize: Theme.type.micro.size
                        }
                    }
                }
            }

            HelpScreen {
                id: shot

                x: Theme.space.md
                y: visionTop.height + Theme.space.md
                width: 400
                height: 188
                radius: Theme.radius.md
                scene: "lobby"

                Rectangle {
                    readonly property real startX: shot.width * (1 - 0.09 - 0.22) - Theme.size.stroke
                    readonly property real startY: shot.height * (1 - 0.13 - 0.13) - Theme.size.stroke

                    visible: art.stage === "draw" || art.stage === "name" || art.stage === "test"
                    x: startX - Theme.space.xs
                    y: startY - Theme.space.xs
                    width: shot.width * 0.22 + 2 * Theme.space.xs
                    height: shot.height * 0.13 + 2 * Theme.space.xs
                    radius: Theme.radius.sm
                    color: art.stage === "test" ? "transparent" : Theme.colors.accentGlow
                    border.width: Theme.size.focusRing
                    border.color: art.stage === "test" ? Theme.colors.success : Theme.colors.accent

                    Repeater {
                        model: art.stage === "draw" ? 4 : 0

                        Rectangle {
                            required property int index

                            x: (index % 2 === 0 ? 0 : parent.width) - width / 2
                            y: (index < 2 ? 0 : parent.height) - height / 2
                            width: Theme.size.guideDot + Theme.space.xxs
                            height: width
                            radius: Theme.radius.xs / 2
                            color: Theme.colors.knob
                            border.width: Theme.size.stroke
                            border.color: Theme.colors.accent
                        }
                    }

                    Rectangle {
                        visible: art.stage === "test"
                        anchors.bottom: parent.top
                        anchors.bottomMargin: Theme.space.xs
                        anchors.right: parent.right
                        width: foundText.implicitWidth + 2 * Theme.space.sm
                        height: Theme.size.badge
                        radius: height / 2
                        color: Theme.colors.success

                        Text {
                            id: foundText

                            anchors.centerIn: parent
                            textFormat: Text.PlainText
                            text: qsTr("Found · 0.97 · 12 ms")
                            color: Theme.colors.textOnAccent
                            font.pixelSize: Theme.type.micro.size
                        }
                    }
                }

                Rectangle {
                    visible: art.stage === "freeze"
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: Theme.space.sm
                    width: frozenRow.implicitWidth + 2 * Theme.space.sm
                    height: Theme.size.badge + Theme.space.xxs
                    radius: height / 2
                    color: Theme.colors.bgFrosted
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeStrong

                    Row {
                        id: frozenRow

                        anchors.centerIn: parent
                        spacing: Theme.space.xs

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "pause"
                            size: Theme.size.keyHintTag - Theme.space.xxs
                            color: Theme.colors.textPrimary
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: qsTr("Frozen")
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.micro.size
                        }
                    }
                }
            }

            Rectangle {
                id: tools

                anchors.horizontalCenter: shot.horizontalCenter
                y: shot.y + shot.height + Theme.space.sm
                width: toolRow.implicitWidth + 2 * Theme.space.xs
                height: 28
                radius: height / 2
                color: Theme.colors.bgOverlay
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeDefault

                Row {
                    id: toolRow

                    anchors.centerIn: parent
                    spacing: Theme.space.xxs

                    Repeater {
                        model: ["mouse-pointer-click", "square", "layout-grid", "crosshair", "swipe-path", "palette"]

                        delegate: Rectangle {
                            id: tool

                            required property string modelData
                            readonly property bool on: art.stage === "draw" && tool.modelData === "square"

                            width: 22
                            height: width
                            radius: width / 2
                            color: tool.on ? Theme.colors.accentSubtle : "transparent"

                            Icon {
                                anchors.centerIn: parent
                                name: tool.modelData
                                size: Theme.size.keyHintTag - Theme.space.xxs
                                color: tool.on ? Theme.colors.accentText : Theme.colors.textSecondary
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: inspector

                x: parent.width - width - Theme.space.md
                y: visionTop.height + Theme.space.md
                width: 170
                height: parent.height - visionTop.height - 2 * Theme.space.md
                radius: Theme.radius.lg
                color: Theme.colors.bgRaised
                border.width: art.stage === "name" ? Theme.size.focusRing : Theme.size.stroke
                border.color: art.stage === "name" ? Theme.colors.accentLine : Theme.colors.strokeSubtle

                Column {
                    x: Theme.space.md
                    y: Theme.space.md
                    width: parent.width - 2 * Theme.space.md
                    spacing: Theme.space.sm

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Template")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.micro.size
                    }

                    Rectangle {
                        width: parent.width
                        height: Theme.size.controlSmall
                        radius: Theme.radius.md
                        color: Theme.colors.bgSunken
                        border.width: Theme.size.stroke
                        border.color: art.stage === "name" ? Theme.colors.accent : Theme.colors.strokeDefault

                        Text {
                            x: Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: art.stage === "freeze" || art.stage === "draw" ? "" : qsTr("Start button")
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.caption.size
                        }
                    }

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Similarity")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.micro.size
                    }

                    Item {
                        width: parent.width
                        height: Theme.size.sliderHandle

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: Theme.size.sliderTrack
                            radius: height / 2
                            color: Theme.colors.strokeDefault

                            Rectangle {
                                width: parent.width * 0.86
                                height: parent.height
                                radius: height / 2
                                color: Theme.colors.accent
                            }
                        }

                        Rectangle {
                            x: parent.width * 0.86 - width / 2
                            width: Theme.size.sliderHandle - Theme.space.xs
                            height: width
                            y: (parent.height - height) / 2
                            radius: width / 2
                            color: Theme.colors.knob
                        }
                    }

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Compare")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.micro.size
                    }

                    Row {
                        spacing: Theme.space.xs

                        StatusPill { text: qsTr("Color"); tone: "live" }
                        StatusPill { text: qsTr("Grey") }
                    }
                }
            }
        }
    }
}
