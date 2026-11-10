pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons

Column {
    id: list

    property var problems: []

    visible: list.problems.length > 0
    spacing: Theme.space.md

    Text {
        width: parent.width
        textFormat: Text.PlainText
        text: qsTr("Problems")
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        font.weight: Font.DemiBold
    }

    Repeater {
        model: list.problems

        delegate: Row {
            id: problem

            required property var modelData

            readonly property string severity: modelData.severity ?? "error"
            readonly property string code: modelData.code ?? ""
            readonly property string detail: modelData.detail ?? ""
            readonly property string hint: modelData.hint ?? ""
            readonly property string path: modelData.path ?? ""

            width: parent ? parent.width : implicitWidth
            spacing: Theme.space.sm

            Icon {
                y: Theme.space.xxs
                name: problem.severity === "error" ? "circle-x"
                                                   : problem.severity === "warning" ? "triangle-alert" : "info"
                size: Theme.size.iconSmall
                color: problem.severity === "error" ? Theme.colors.dangerText
                                                    : problem.severity === "warning" ? Theme.colors.warningText
                                                                                     : Theme.colors.infoText
            }

            Column {
                width: problem.width - Theme.size.iconSmall - problem.spacing
                spacing: Theme.space.xxs

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: problem.detail !== "" ? problem.detail : problem.code
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: problem.hint
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: [problem.detail !== "" ? problem.code : "", problem.path].filter(part => part !== "").join(" · ")
                    color: Theme.colors.textTertiary
                    font.family: Theme.type.monoFamily
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
