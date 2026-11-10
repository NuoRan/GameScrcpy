pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    property var labels: ({})
    readonly property var choices: entry ? entry.choices : []
    readonly property ColorTokens darkColors: ColorTokens {
        dark: true
        accent: Theme.colors.accent
    }
    readonly property ColorTokens lightColors: ColorTokens {
        dark: false
        accent: Theme.colors.accent
    }

    globalText: labels[globalValue] ?? String(globalValue)

    component MiniWindow: Item {
        id: mini

        required property ColorTokens colors

        Rectangle {
            width: Math.round(parent.width * 0.22)
            height: parent.height
            topLeftRadius: Theme.radius.md
            bottomLeftRadius: Theme.radius.md
            color: mini.colors.bgBase
        }

        Rectangle {
            x: Math.round(parent.width * 0.22)
            width: parent.width - x
            height: parent.height
            topRightRadius: Theme.radius.md
            bottomRightRadius: Theme.radius.md
            color: mini.colors.bgRaised
        }

        Rectangle {
            x: Math.round(parent.width * 0.34)
            y: Math.round(parent.height * 0.2)
            width: Math.round(parent.width * 0.56)
            height: Math.round(parent.height * 0.24)
            radius: Theme.radius.sm
            color: mini.colors.bgOverlay
            border.width: mini.colors.dark ? 0 : Theme.size.stroke
            border.color: mini.colors.strokeSubtle
        }

        Rectangle {
            x: Math.round(parent.width * 0.34)
            y: Math.round(parent.height * 0.52)
            width: Math.round(parent.width * 0.4)
            height: Math.round(parent.height * 0.24)
            radius: Theme.radius.sm
            color: mini.colors.bgOverlay
            border.width: mini.colors.dark ? 0 : Theme.size.stroke
            border.color: mini.colors.strokeSubtle
        }

        Rectangle {
            x: Math.round(parent.width * 0.76)
            y: Math.round(parent.height * 0.54)
            width: Math.round(parent.width * 0.14)
            height: Math.round(parent.height * 0.2)
            radius: Theme.radius.xs
            color: mini.colors.accent
        }
    }

    below: [
        Row {
            id: cards

            width: parent ? parent.width : implicitWidth
            spacing: Theme.space.md

            Repeater {
                model: row.choices

                delegate: T.AbstractButton {
                    id: card

                    required property string modelData
                    readonly property bool selected: row.value === modelData

                    width: (cards.width - (row.choices.length - 1) * cards.spacing) / Math.max(1, row.choices.length)
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    padding: Theme.space.sm
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: row.labels[modelData] ?? modelData
                    Accessible.checked: selected
                    onClicked: row.commit(modelData)

                    background: Rectangle {
                        id: face

                        radius: Theme.radius.lg
                        color: Theme.colors.bgSunken
                        border.width: Theme.size.stroke
                        border.color: card.selected ? Theme.colors.accent
                                                    : card.hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -Theme.size.fieldGlow
                            visible: card.selected
                            radius: face.radius + Theme.size.fieldGlow
                            color: "transparent"
                            border.width: Theme.size.fieldGlow
                            border.color: Theme.colors.accentSubtle
                        }

                        FocusRing {
                            visible: card.visualFocus
                            baseRadius: face.radius
                        }
                    }

                    contentItem: Column {
                        spacing: Theme.space.sm

                        Item {
                            width: parent.width
                            height: Theme.size.themePreview

                            Item {
                                width: card.modelData === "system" ? parent.width / 2 : parent.width
                                height: parent.height
                                clip: true

                                MiniWindow {
                                    width: card.width - card.leftPadding - card.rightPadding
                                    height: parent.height
                                    colors: card.modelData === "light" ? row.lightColors : row.darkColors
                                }
                            }

                            Item {
                                visible: card.modelData === "system"
                                x: parent.width / 2
                                width: parent.width / 2
                                height: parent.height
                                clip: true

                                MiniWindow {
                                    x: -parent.width
                                    width: parent.width * 2
                                    height: parent.height
                                    colors: row.lightColors
                                }
                            }
                        }

                        Row {
                            leftPadding: Theme.space.xxs
                            spacing: Theme.space.sm

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.size.checkbox
                                height: width
                                radius: width / 2
                                color: Theme.colors.bgSunken
                                border.width: Theme.size.stroke
                                border.color: card.selected ? Theme.colors.accent : Theme.colors.strokeStrong

                                Rectangle {
                                    anchors.centerIn: parent
                                    visible: card.selected
                                    width: Theme.size.pillDot + Theme.space.xxs
                                    height: width
                                    radius: width / 2
                                    color: Theme.colors.accent
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: row.labels[card.modelData] ?? card.modelData
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                            }
                        }
                    }
                }
            }
        }
    ]
}
