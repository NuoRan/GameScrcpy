pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons

Item {
    id: stepper

    property list<string> steps: [
        qsTr("Prepare device"),
        qsTr("Start server"),
        qsTr("Connect"),
        qsTr("Verify"),
        qsTr("Start mirroring")
    ]
    property int current: 0
    property bool failed: false

    function phaseOf(index: int): string {
        if (index < current)
            return "done"
        if (index > current)
            return "pending"
        return failed ? "failed" : "current"
    }

    implicitWidth: steps.length * Theme.size.stepWidth
    implicitHeight: Theme.size.stepDot + Theme.space.sm + Theme.type.caption.lineHeight

    Accessible.role: Accessible.ProgressBar
    Accessible.name: steps[current] ?? ""

    Repeater {
        model: stepper.steps

        delegate: Item {
            id: step

            required property string modelData
            required property int index
            readonly property string phase: stepper.phaseOf(index)

            x: index * width
            width: stepper.width / stepper.steps.length
            height: stepper.height

            Rectangle {
                visible: step.index < stepper.steps.length - 1
                x: (step.width + Theme.size.stepDot) / 2 + Theme.space.xs
                y: (Theme.size.stepDot - height) / 2
                width: step.width - Theme.size.stepDot - 2 * Theme.space.xs
                height: Theme.size.stepLine
                radius: height / 2
                color: step.index < stepper.current ? Theme.colors.success : Theme.colors.strokeDefault
            }

            Rectangle {
                id: dot

                x: (step.width - width) / 2
                width: Theme.size.stepDot
                height: width
                radius: width / 2
                color: {
                    switch (step.phase) {
                    case "done":
                        return Theme.colors.success
                    case "failed":
                        return Theme.colors.danger
                    default:
                        return "transparent"
                    }
                }
                border.width: step.phase === "current" || step.phase === "pending" ? Theme.size.stepLine : 0
                border.color: step.phase === "current" ? Theme.colors.accent : Theme.colors.strokeStrong

                Rectangle {
                    visible: step.phase === "current"
                    anchors.fill: parent
                    anchors.margins: -Theme.size.fieldGlow
                    radius: width / 2
                    color: "transparent"
                    border.width: Theme.size.fieldGlow
                    border.color: Theme.colors.accentSubtle
                }

                Rectangle {
                    visible: step.phase === "current"
                    anchors.centerIn: parent
                    width: Theme.size.pillDot
                    height: width
                    radius: width / 2
                    color: Theme.colors.accent
                }

                Icon {
                    anchors.centerIn: parent
                    visible: step.phase === "done" || step.phase === "failed"
                    name: step.phase === "failed" ? "x" : "check"
                    size: Theme.size.stepDot - Theme.space.xs
                    color: Theme.colors.textOnAccent
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Theme.size.stepDot + Theme.space.sm
                width: step.width - Theme.space.xs
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.PlainText
                text: step.modelData
                color: {
                    switch (step.phase) {
                    case "failed":
                        return Theme.colors.dangerText
                    case "current":
                        return Theme.colors.textPrimary
                    case "done":
                        return Theme.colors.textSecondary
                    default:
                        return Theme.colors.textTertiary
                    }
                }
                font.pixelSize: Theme.type.caption.size
                font.weight: step.phase === "current" || step.phase === "failed" ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
        }
    }
}
