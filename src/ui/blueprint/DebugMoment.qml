pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

Item {
    id: moment

    property string title: ""
    property string subtitle: ""
    property url frame: ""
    property var boxes: []
    property string result: ""
    property var variables: []
    property var stack: []

    signal stackChosen(int index)

    objectName: "bpMoment"
    implicitWidth: Theme.size.workbenchSidebar
    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.Pane
    Accessible.name: title

    Flickable {
        id: scroller

        anchors.fill: parent
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column

            width: scroller.width

            Item {
                width: column.width
                height: headRow.implicitHeight + 2 * Theme.space.lg

                Row {
                    id: headRow

                    x: Theme.space.lg
                    y: Theme.space.lg
                    width: parent.width - 2 * x
                    spacing: Theme.space.md

                    Tile {
                        anchors.verticalCenter: parent.verticalCenter
                        large: true
                        iconName: "pause"
                        tone: "amber"
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.size.tileLarge - parent.spacing
                        spacing: Theme.space.xxs

                        Text {
                            objectName: "bpMomentTitle"
                            width: parent.width
                            textFormat: Text.PlainText
                            text: moment.title
                            elide: Text.ElideRight
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.heading.size
                            font.weight: Theme.type.heading.weight
                        }

                        Text {
                            width: parent.width
                            visible: text !== ""
                            textFormat: Text.PlainText
                            text: moment.subtitle
                            elide: Text.ElideRight
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                        }
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
                x: Theme.space.lg
                width: column.width - 2 * x
                topPadding: Theme.space.lg
                bottomPadding: Theme.space.lg
                spacing: Theme.space.md

                Rectangle {
                    id: picture

                    readonly property real aspect: shot.implicitWidth > 0 ? shot.implicitHeight / shot.implicitWidth : 9 / 16

                    objectName: "bpMomentPicture"
                    width: parent.width
                    height: Math.round(width * Math.min(1, Math.max(9 / 16, aspect)))
                    radius: Theme.radius.md
                    color: Theme.colors.bgSunken
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeDefault
                    clip: true

                    Image {
                        id: shot

                        anchors.fill: parent
                        anchors.margins: Theme.size.stroke
                        visible: moment.frame.toString() !== ""
                        source: moment.frame
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        retainWhileLoading: true
                        smooth: true
                    }

                    Icon {
                        anchors.centerIn: parent
                        visible: moment.frame.toString() === ""
                        name: "image"
                        size: Theme.size.iconLarge
                        color: Theme.colors.textDisabled
                    }

                    Item {
                        id: painted

                        x: shot.x + (shot.width - shot.paintedWidth) / 2
                        y: shot.y + (shot.height - shot.paintedHeight) / 2
                        width: shot.paintedWidth
                        height: shot.paintedHeight
                        visible: shot.visible

                        Repeater {
                            model: moment.boxes ?? []

                            delegate: Rectangle {
                                id: box

                                required property var modelData
                                readonly property color tone: BlueprintLook.statusColor(modelData.tone ?? "")

                                x: (modelData.x ?? 0) * painted.width
                                y: (modelData.y ?? 0) * painted.height
                                width: (modelData.w ?? 0) * painted.width
                                height: (modelData.h ?? 0) * painted.height
                                radius: Theme.radius.xs
                                color: "transparent"
                                border.width: Theme.size.focusRing
                                border.color: box.tone

                                Rectangle {
                                    visible: (box.modelData.text ?? "") !== ""
                                    x: -Theme.size.focusRing
                                    y: box.y >= height + Theme.space.xxs ? -height - Theme.space.xxs : box.height + Theme.space.xxs
                                    width: boxText.implicitWidth + 2 * Theme.space.xs
                                    height: Theme.size.keyHintTag
                                    radius: Theme.radius.xs / 2
                                    color: box.tone

                                    Text {
                                        id: boxText

                                        anchors.centerIn: parent
                                        textFormat: Text.PlainText
                                        text: box.modelData.text ?? ""
                                        color: Theme.colors.textOnAccent
                                        font.pixelSize: Theme.type.micro.size
                                        font.weight: Font.Bold
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    objectName: "bpMomentResult"
                    width: parent.width
                    visible: moment.result !== ""
                    textFormat: Text.PlainText
                    text: moment.result
                    wrapMode: Text.Wrap
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }
            }

            Rectangle {
                width: column.width
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }

            Column {
                x: Theme.space.lg
                width: column.width - 2 * x
                topPadding: Theme.space.lg
                bottomPadding: Theme.space.lg
                spacing: Theme.space.xs + Theme.space.xxs
                visible: (moment.variables ?? []).length > 0

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Variables")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Theme.type.label.weight
                }

                Rectangle {
                    width: parent.width
                    height: variableRows.implicitHeight + 2 * Theme.space.xs
                    radius: Theme.radius.md
                    color: Theme.colors.bgSunken
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeDefault

                    Column {
                        id: variableRows

                        y: Theme.space.xs
                        width: parent.width

                        Repeater {
                            model: moment.variables ?? []

                            delegate: Item {
                                id: variable

                                required property var modelData

                                width: variableRows.width
                                height: Theme.size.bpKvRow

                                Text {
                                    x: Theme.space.sm + Theme.space.xxs
                                    width: Theme.size.bpKvKey
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: variable.modelData.name ?? ""
                                    elide: Text.ElideRight
                                    color: Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.caption.size
                                }

                                Text {
                                    x: Theme.space.sm + Theme.space.xxs + Theme.size.bpKvKey + Theme.space.md
                                    width: parent.width - x - Theme.space.sm
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: variable.modelData.value ?? ""
                                    elide: Text.ElideRight
                                    color: Theme.colors.textPrimary
                                    font.family: Theme.type.monoFamily
                                    font.pixelSize: Theme.type.caption.size
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: column.width
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
                visible: (moment.stack ?? []).length > 0
            }

            Column {
                x: Theme.space.lg
                width: column.width - 2 * x
                topPadding: Theme.space.lg
                bottomPadding: Theme.space.lg
                spacing: Theme.space.xs + Theme.space.xxs
                visible: (moment.stack ?? []).length > 0

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Call stack")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Theme.type.label.weight
                }

                Rectangle {
                    width: parent.width
                    height: frameRows.implicitHeight + 2 * Theme.space.xs
                    radius: Theme.radius.md
                    color: Theme.colors.bgSunken
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeDefault
                    clip: true

                    Column {
                        id: frameRows

                        y: Theme.space.xs
                        width: parent.width

                        Repeater {
                            model: moment.stack ?? []

                            delegate: Item {
                                id: entry

                                required property var modelData
                                required property int index

                                objectName: "bpStack_" + entry.index
                                width: frameRows.width
                                height: Theme.size.bpKvRow

                                Accessible.role: Accessible.ListItem
                                Accessible.name: (entry.modelData.title ?? "") + " " + (entry.modelData.place ?? "")
                                Accessible.selected: entry.modelData.current === true

                                Rectangle {
                                    anchors.fill: parent
                                    visible: entry.modelData.current === true || entryArea.containsMouse
                                    color: Theme.colors.stateHover
                                }

                                Rectangle {
                                    visible: entry.modelData.current === true
                                    width: Theme.size.focusRing
                                    height: parent.height
                                    color: Theme.colors.accent
                                }

                                Text {
                                    x: Theme.space.sm + Theme.space.xxs
                                    width: parent.width - x - place.implicitWidth - 2 * Theme.space.sm
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: entry.modelData.title ?? ""
                                    elide: Text.ElideRight
                                    color: Theme.colors.textPrimary
                                    font.pixelSize: Theme.type.caption.size
                                }

                                Text {
                                    id: place

                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.space.sm + Theme.space.xxs
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: entry.modelData.place ?? ""
                                    color: Theme.colors.textTertiary
                                    font.family: Theme.type.monoFamily
                                    font.pixelSize: Theme.type.micro.size
                                }

                                MouseArea {
                                    id: entryArea

                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: moment.stackChosen(entry.index)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
