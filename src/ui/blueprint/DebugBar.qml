pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

Item {
    id: bar

    property string pausedTitle: ""
    property int othersPaused: 0
    property bool shortcutsEnabled: true
    readonly property bool paused: state === "paused"

    signal continueClicked()
    signal stepOverClicked()
    signal stepIntoClicked()
    signal stepOutClicked()
    signal stopClicked()

    objectName: "bpDebugBar"
    implicitWidth: row.implicitWidth + 2 * Theme.space.xs
    implicitHeight: Theme.size.controlSmall + 2 * Theme.space.xs
    states: [
        State {
            name: "running"
        },
        State {
            name: "paused"
        }
    ]

    Accessible.role: Accessible.ToolBar
    Accessible.name: pill.text

    component StepButton: Button {
        id: step

        property string glyph
        property real turn: 0

        anchors.verticalCenter: parent.verticalCenter
        small: true
        focusPolicy: Qt.TabFocus

        contentItem: Item {
            implicitWidth: line.implicitWidth
            implicitHeight: line.implicitHeight

            Row {
                id: line

                anchors.centerIn: parent
                spacing: step.spacing

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: step.glyph !== ""
                    name: step.glyph
                    size: Theme.size.iconSmall
                    rotation: step.turn
                    color: step.foreground
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: step.text
                    font: step.font
                    color: step.foreground
                }
            }
        }
    }

    RectangularShadow {
        anchors.fill: face
        visible: face.visible
        radius: face.radius
        offset.y: Theme.shadow.keyOffset
        blur: Theme.shadow.keyBlur
        color: Theme.shadow.key
    }

    Rectangle {
        id: face

        anchors.fill: parent
        visible: bar.state !== ""
        radius: height / 2
        color: Theme.reducedTransparency ? Theme.colors.bgElevated : Theme.colors.bgFrosted
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong
    }

    Row {
        id: row

        anchors.centerIn: parent
        height: Theme.size.controlSmall
        spacing: Theme.space.sm
        visible: bar.state !== ""

        StatusPill {
            id: pill

            objectName: "bpDebugState"
            anchors.verticalCenter: parent.verticalCenter
            tone: bar.paused ? "warn" : "live"
            text: bar.paused ? (bar.pausedTitle !== "" ? qsTr("Paused · %1").arg(bar.pausedTitle) : qsTr("Paused"))
                             : qsTr("Debugging")
        }

        StepButton {
            objectName: "bpContinue"
            visible: bar.paused
            tone: "primary"
            glyph: "play"
            text: qsTr("Continue")
            toolTip: qsTr("Continue")
            shortcut: "F5"
            onClicked: bar.continueClicked()
        }

        StepButton {
            objectName: "bpStepOver"
            visible: bar.paused
            glyph: "redo-2"
            text: qsTr("Step")
            toolTip: qsTr("Run this block and stop at the next one")
            shortcut: "F10"
            onClicked: bar.stepOverClicked()
        }

        StepButton {
            objectName: "bpStepInto"
            visible: bar.paused
            tone: "ghost"
            glyph: "arrow-left"
            turn: -90
            text: qsTr("Step into")
            toolTip: qsTr("Go into the function this block calls")
            shortcut: "F11"
            onClicked: bar.stepIntoClicked()
        }

        StepButton {
            objectName: "bpStepOut"
            visible: bar.paused
            tone: "ghost"
            glyph: "arrow-left"
            turn: 90
            text: qsTr("Step out")
            toolTip: qsTr("Run to the end of this function")
            shortcut: "Shift+F11"
            onClicked: bar.stepOutClicked()
        }

        StepButton {
            objectName: "bpStopTask"
            visible: bar.paused
            tone: "ghost"
            glyph: "square"
            text: qsTr("Stop")
            toolTip: qsTr("Stop this task")
            onClicked: bar.stopClicked()
        }

        Rectangle {
            objectName: "bpOthersPaused"
            anchors.verticalCenter: parent.verticalCenter
            visible: bar.othersPaused > 0
            width: othersRow.implicitWidth + 2 * Theme.space.sm
            height: Theme.size.badge + Theme.space.xxs
            radius: height / 2
            color: Theme.colors.statePress

            Row {
                id: othersRow

                anchors.centerIn: parent
                spacing: Theme.space.xs

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "pause"
                    size: Theme.size.keyHintTagIcon
                    color: Theme.colors.textSecondary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: qsTr("%n other task(s) paused", "", bar.othersPaused)
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }
    }

    Shortcut {
        sequence: "F5"
        enabled: bar.shortcutsEnabled && bar.paused
        onActivated: bar.continueClicked()
    }

    Shortcut {
        sequence: "F10"
        enabled: bar.shortcutsEnabled && bar.paused
        onActivated: bar.stepOverClicked()
    }

    Shortcut {
        sequence: "F11"
        enabled: bar.shortcutsEnabled && bar.paused
        onActivated: bar.stepIntoClicked()
    }

    Shortcut {
        sequence: "Shift+F11"
        enabled: bar.shortcutsEnabled && bar.paused
        onActivated: bar.stepOutClicked()
    }
}
