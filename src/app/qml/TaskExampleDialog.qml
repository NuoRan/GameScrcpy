pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    property string failure
    readonly property var examples: TaskLibrary.examples
    readonly property real bodyLimit: (parent ? parent.height : 0) - 2 * Theme.space.x3l - implicitHeaderHeight
                                      - implicitFooterHeight - topPadding - bottomPadding

    signal copied(string id, string name)

    function start() {
        dialog.failure = ""
        dialog.open()
    }

    function choose(example: var) {
        dialog.failure = ""
        const id = TaskLibrary.copyExample(example.id)
        if (id === "") {
            dialog.failure = qsTr("The example could not be copied.")
            return
        }
        const made = TaskLibrary.tasks.find(task => task.id === id)
        dialog.close()
        dialog.copied(id, made !== undefined ? made.name : example.name)
    }

    objectName: "taskExampleDialog"
    title: qsTr("New task from an example")

    header: Column {
        topPadding: Theme.space.xxl
        leftPadding: Theme.space.xxl
        rightPadding: Theme.space.xxl
        spacing: Theme.space.xs

        Text {
            width: dialog.availableWidth
            textFormat: Text.PlainText
            text: dialog.title
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.subtitle.size
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            width: dialog.availableWidth
            textFormat: Text.PlainText
            text: qsTr("Pick one to copy it to your tasks, where you can change it like any task of yours.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
            wrapMode: Text.Wrap
        }
    }

    footer: Item {
        implicitHeight: cancelButton.implicitHeight + 2 * Theme.space.xl

        Button {
            id: cancelButton

            objectName: "reject"
            anchors.right: parent.right
            anchors.rightMargin: Theme.space.xxl
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        }
    }

    Flickable {
        id: scroller

        width: dialog.availableWidth
        height: implicitHeight
        implicitHeight: dialog.bodyLimit > 0 ? Math.min(body.implicitHeight, dialog.bodyLimit) : body.implicitHeight
        contentWidth: width
        contentHeight: body.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: body

            objectName: "taskExamples"
            width: scroller.width
            spacing: Theme.space.sm

            Repeater {
                model: dialog.examples

                delegate: ExampleCard {}
            }

            Text {
                objectName: "noTaskExamples"
                width: parent.width
                visible: dialog.examples.length === 0
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("No examples were found next to GameScrcpy.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }

            Text {
                objectName: "taskExampleFailure"
                width: parent.width
                visible: dialog.failure !== ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: dialog.failure
                color: Theme.colors.dangerText
                font.pixelSize: Theme.type.caption.size
            }
        }
    }

    component ExampleCard: T.AbstractButton {
        id: card

        required property var modelData

        objectName: "taskExample"
        width: parent ? parent.width : implicitWidth
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        padding: Theme.space.md
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: card.modelData.name
        Accessible.description: card.modelData.description
        onClicked: dialog.choose(card.modelData)

        background: Rectangle {
            radius: Theme.radius.lg
            color: card.down ? Theme.colors.statePress : card.hovered ? Theme.colors.stateSelected
                                                                      : Theme.colors.stateHover
            border.width: Theme.size.stroke
            border.color: card.hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault

            FocusRing {
                visible: card.visualFocus
                baseRadius: Theme.radius.lg
            }
        }

        contentItem: Row {
            spacing: Theme.space.md

            Tile {
                iconName: card.modelData.icon
                tone: "teal"
            }

            Column {
                width: Math.max(0, card.availableWidth - Theme.size.tile - Theme.space.md)
                spacing: Theme.space.xxs

                Text {
                    objectName: "taskExampleName"
                    width: parent.width
                    textFormat: Text.PlainText
                    text: card.modelData.name
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                }

                Text {
                    objectName: "taskExampleDescription"
                    width: parent.width
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: card.modelData.description
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }
    }
}
