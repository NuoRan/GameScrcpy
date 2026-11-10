pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: panel

    property var details: ({})
    property var keyName: null
    readonly property bool empty: (details?.title ?? "") === "" && (details?.groups ?? []).length === 0

    signal fieldEdited(string name, var value)
    signal jumpRequested()
    signal codeDocumentReady(var textDocument)

    objectName: "bpDetails"
    implicitWidth: Theme.size.workbenchSidebar
    implicitHeight: Theme.size.workbenchCode

    Accessible.role: Accessible.Pane
    Accessible.name: details?.title ?? ""

    Text {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.space.xl
        visible: panel.empty
        textFormat: Text.PlainText
        text: qsTr("Select a block to see and change it here.")
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
    }

    Flickable {
        id: scroller

        anchors.fill: parent
        visible: !panel.empty
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column

            width: scroller.width

            Item {
                width: column.width
                height: headerRow.implicitHeight + 2 * Theme.space.lg

                Row {
                    id: headerRow

                    x: Theme.space.lg
                    y: Theme.space.lg
                    width: parent.width - 2 * x
                    spacing: Theme.space.md

                    Tile {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: (panel.details?.icon ?? "") !== ""
                        large: true
                        iconName: panel.details?.icon ?? ""
                        tone: BlueprintLook.tileTone(panel.details?.tone ?? "")
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - (panel.details?.icon ? Theme.size.tileLarge + parent.spacing : 0)
                        spacing: Theme.space.xxs

                        Text {
                            objectName: "bpDetailsTitle"
                            width: parent.width
                            textFormat: Text.PlainText
                            text: panel.details?.title ?? ""
                            elide: Text.ElideRight
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.heading.size
                            font.weight: Theme.type.heading.weight
                        }

                        Text {
                            width: parent.width
                            visible: text !== ""
                            textFormat: Text.PlainText
                            text: panel.details?.subtitle ?? ""
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

            Repeater {
                model: panel.details?.groups ?? []

                delegate: Column {
                    id: group

                    required property var modelData
                    required property int index

                    width: column.width

                    Rectangle {
                        visible: group.index > 0
                        width: parent.width
                        height: Theme.size.stroke
                        color: Theme.colors.strokeSubtle
                    }

                    Item {
                        width: parent.width
                        height: Theme.space.lg
                    }

                    Text {
                        x: Theme.space.lg
                        width: parent.width - 2 * x
                        visible: (group.modelData.title ?? "") !== ""
                        bottomPadding: Theme.space.sm
                        textFormat: Text.PlainText
                        text: group.modelData.title ?? ""
                        elide: Text.ElideRight
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.micro.size
                        font.weight: Theme.type.micro.weight
                    }

                    Column {
                        id: fieldColumn

                        x: Theme.space.lg
                        width: parent.width - 2 * x
                        spacing: Theme.space.md + Theme.space.xxs

                        Repeater {
                            model: group.modelData.fields ?? []

                            delegate: DetailField {
                                id: fieldItem

                                required property var modelData

                                width: fieldColumn.width
                                spec: fieldItem.modelData
                                keyName: panel.keyName
                                onEdited: value => panel.fieldEdited(fieldItem.spec.name ?? "", value)
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: Theme.space.lg
                    }
                }
            }

            Column {
                width: column.width
                visible: (panel.details?.code ?? "") !== ""

                Rectangle {
                    width: parent.width
                    height: Theme.size.stroke
                    color: Theme.colors.strokeSubtle
                }

                Item {
                    width: parent.width
                    height: Theme.space.lg
                }

                Item {
                    x: Theme.space.lg
                    width: parent.width - 2 * x
                    height: jump.height

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("The code")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        font.weight: Theme.type.label.weight
                    }

                    Text {
                        anchors.right: jump.left
                        anchors.rightMargin: Theme.space.xs
                        anchors.verticalCenter: parent.verticalCenter
                        visible: (panel.details?.codeLine ?? 0) > 0
                        textFormat: Text.PlainText
                        text: qsTr("Line %1").arg(panel.details?.codeLine ?? 0)
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }

                    Button {
                        id: jump

                        objectName: "bpJumpToCode"
                        anchors.right: parent.right
                        small: true
                        tone: "ghost"
                        iconName: "code-xml"
                        text: qsTr("Jump to the code")
                        onClicked: panel.jumpRequested()
                    }
                }

                Item {
                    width: parent.width
                    height: Theme.space.xs + Theme.space.xxs
                }

                Rectangle {
                    x: Theme.space.lg
                    width: parent.width - 2 * x
                    height: code.contentHeight + 2 * Theme.space.sm
                    radius: Theme.radius.md
                    color: Theme.colors.bgCode
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeDefault
                    clip: true

                    TextEdit {
                        id: code

                        objectName: "bpDetailsCode"
                        x: Theme.space.sm + Theme.space.xxs
                        y: Theme.space.sm
                        width: parent.width - 2 * x
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        readOnly: true
                        selectByMouse: true
                        text: panel.details?.code ?? ""
                        color: Theme.colors.textPrimary
                        selectionColor: Theme.colors.accentLine
                        selectedTextColor: Theme.colors.textPrimary
                        font.family: Theme.type.monoFamily
                        font.pixelSize: Theme.type.caption.size
                        Accessible.name: qsTr("The code")
                        Component.onCompleted: panel.codeDocumentReady(code.textDocument)
                    }
                }

                Item {
                    width: parent.width
                    height: Theme.space.lg
                }
            }
        }
    }
}
