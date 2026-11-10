pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme

Column {
    id: list

    property var items: []
    property int current: -1
    property bool interactive: false
    property int hovered: -1

    signal picked(int index)
    signal linkActivated(string link)

    Repeater {
        model: list.items

        delegate: Item {
            id: step

            required property var modelData
            required property int index
            readonly property bool last: step.index === list.items.length - 1
            readonly property bool lit: step.index === list.current

            objectName: "helpStep"
            width: list.width
            height: body.implicitHeight + (step.last ? 0 : Theme.space.lg + Theme.space.xxs)

            Accessible.role: Accessible.ListItem
            Accessible.name: (step.index + 1) + ". " + titleText.text

            Rectangle {
                visible: !step.last
                x: (Theme.size.helpStepNumber - width) / 2
                y: Theme.size.helpStepNumber + Theme.space.xs + Theme.space.xxs
                width: Theme.size.stroke
                height: step.height - y - Theme.space.xs - Theme.size.stroke
                color: Theme.colors.strokeDefault
            }

            Rectangle {
                id: number

                width: Theme.size.helpStepNumber
                height: width
                radius: width / 2
                color: step.lit ? Theme.colors.accent : Theme.colors.bgElevated
                border.width: step.lit ? 0 : Theme.size.stroke
                border.color: Theme.colors.strokeDefault

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motion.fast
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    visible: step.lit
                    width: parent.width + 2 * Theme.space.xs
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: Theme.space.xs
                    border.color: Theme.colors.accentSubtle
                }

                Text {
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: step.index + 1
                    color: step.lit ? Theme.colors.textOnAccent : Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.Bold
                    font.features: ({ "tnum": 1 })
                }
            }

            Column {
                id: body

                x: Theme.size.helpStepNumber + Theme.space.md + Theme.space.xxs
                width: step.width - x
                topPadding: Theme.space.xxs
                spacing: Theme.space.xxs

                HelpText {
                    id: titleText

                    visible: (step.modelData.title ?? "") !== ""
                    width: parent.width
                    html: (step.modelData.title ?? "").replace(/;font-weight:\d+/g, "")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.prose.size
                    font.weight: Font.DemiBold
                    lineSize: Theme.type.subtitle.lineHeight
                    onLinkActivated: link => list.linkActivated(link)
                }

                HelpText {
                    visible: (step.modelData.html ?? "") !== ""
                    width: parent.width
                    html: step.modelData.html ?? ""
                    font.pixelSize: Theme.type.label.size
                    lineSize: Theme.type.code.lineHeight
                    onLinkActivated: link => list.linkActivated(link)
                }

                Text {
                    visible: text !== ""
                    width: parent.width
                    topPadding: Theme.space.xxs
                    textFormat: Text.PlainText
                    text: step.modelData.note ?? ""
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    lineHeight: Theme.type.caption.lineHeight + Theme.space.xxs
                    lineHeightMode: Text.FixedHeight
                    wrapMode: Text.Wrap
                }
            }

            HoverHandler {
                enabled: list.interactive
                cursorShape: Qt.PointingHandCursor
                onHoveredChanged: {
                    if (hovered)
                        list.hovered = step.index
                    else if (list.hovered === step.index)
                        list.hovered = -1
                }
            }

            TapHandler {
                enabled: list.interactive
                onTapped: list.picked(step.index)
            }
        }
    }
}
