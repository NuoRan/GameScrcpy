pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

SettingRow {
    id: row

    property var labels: ({})
    readonly property var choices: entry ? entry.choices : []
    readonly property int currentIndex: choices.indexOf(value)

    globalText: labels[globalValue] ?? String(globalValue)

    function select(index: int) {
        const choice = Math.max(0, Math.min(index, choices.length - 1))
        if (choice !== currentIndex)
            commit(choices[choice])
    }

    T.Control {
        id: swatches

        focusPolicy: Qt.StrongFocus
        implicitWidth: implicitContentWidth
        implicitHeight: implicitContentHeight
        Accessible.role: Accessible.Grouping
        Accessible.name: row.title
        Keys.onLeftPressed: row.select(row.currentIndex - 1)
        Keys.onRightPressed: row.select(row.currentIndex + 1)

        contentItem: Row {
            spacing: Theme.space.xs

            Repeater {
                model: row.choices

                delegate: T.AbstractButton {
                    id: swatch

                    required property string modelData
                    required property int index
                    readonly property bool selected: index === row.currentIndex

                    implicitWidth: Theme.size.swatch + 2 * Theme.size.focusRing
                    implicitHeight: implicitWidth
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: true
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: row.labels[modelData] ?? modelData
                    Accessible.checked: selected
                    onClicked: row.select(index)

                    background: Rectangle {
                        radius: width / 2
                        color: "transparent"
                        border.width: Theme.size.focusRing
                        border.color: swatch.hovered && !swatch.selected ? Theme.colors.strokeStrong : "transparent"

                        Rectangle {
                            anchors.centerIn: parent
                            width: Theme.size.swatch
                            height: width
                            radius: width / 2
                            color: Theme.accentChoices[swatch.modelData]

                            Icon {
                                anchors.centerIn: parent
                                visible: swatch.selected
                                name: "check"
                                size: Theme.size.iconSmall
                                color: Theme.colors.textOnAccent
                            }
                        }

                        FocusRing {
                            visible: swatches.visualFocus && swatch.selected
                            anchors.margins: 0
                            baseRadius: width / 2
                        }
                    }

                    Tooltip {
                        text: swatch.Accessible.name
                        visible: swatch.hovered
                    }
                }
            }
        }
    }
}
