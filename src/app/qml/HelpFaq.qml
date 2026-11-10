pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Column {
    id: faq

    property var categories: []
    property int category: -1
    property var opened: ({})
    property int revision: 0
    property var rows: ({})
    readonly property var tones: ["azure", "orange", "lime", "teal", "rose", "amber"]
    readonly property var questions: {
        const all = []
        faq.categories.forEach((group, index) => {
            for (const question of group.questions ?? [])
                all.push(Object.assign({ group: index, groupTitle: group.title ?? "", icon: group.icon ?? "circle-help",
                                         tone: group.tone || faq.tones[index % faq.tones.length] }, question))
        })
        return all
    }
    readonly property var visibleQuestions: faq.category < 0 ? faq.questions
                                                            : faq.questions.filter(question => question.group === faq.category)

    signal linkActivated(string link)

    function setOpen(anchor: string, open: bool) {
        faq.opened[anchor] = open
        faq.revision += 1
    }

    function target(anchor: string): Item {
        const question = faq.questions.find(entry => entry.anchor === anchor)
        if (question !== undefined) {
            if (faq.category >= 0 && faq.category !== question.group)
                faq.category = -1
            faq.setOpen(anchor, true)
            return faq.rows[anchor] ?? null
        }
        const group = faq.categories.findIndex(entry => entry.anchor === anchor)
        if (group >= 0) {
            faq.category = group
            return chips
        }
        return null
    }

    onCategoriesChanged: {
        faq.category = -1
        faq.opened = {}
        faq.revision += 1
    }

    Flow {
        id: chips

        width: parent.width
        topPadding: Theme.space.xl
        spacing: Theme.space.sm

        FilterChip {
            objectName: "helpFaqAll"
            text: qsTr("All")
            count: faq.questions.length
            selected: faq.category < 0
            onClicked: faq.category = -1
        }

        Repeater {
            model: faq.categories

            delegate: FilterChip {
                id: chip

                required property var modelData
                required property int index

                objectName: "helpFaqCategory"
                text: chip.modelData.title ?? ""
                count: (chip.modelData.questions ?? []).length
                selected: faq.category === chip.index
                onClicked: faq.category = chip.index
            }
        }
    }

    Item {
        width: parent.width
        height: Theme.space.lg
    }

    Rectangle {
        width: Math.min(parent.width, Theme.size.helpArticle)
        height: list.implicitHeight
        radius: Theme.radius.xl
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle
        clip: true

        Column {
            id: list

            width: parent.width

            Repeater {
                model: faq.visibleQuestions

                delegate: Column {
                    id: row

                    required property var modelData
                    required property int index
                    readonly property bool open: faq.revision >= 0 && faq.opened[row.modelData.anchor] === true

                    objectName: "helpQuestion"
                    width: list.width

                    Component.onCompleted: faq.rows[row.modelData.anchor] = row
                    Component.onDestruction: {
                        if (faq.rows[row.modelData.anchor] === row)
                            delete faq.rows[row.modelData.anchor]
                    }

                    Rectangle {
                        visible: row.index > 0
                        width: parent.width
                        height: Theme.size.stroke
                        color: Theme.colors.strokeSubtle
                    }

                    T.AbstractButton {
                        id: header

                        width: parent.width
                        height: Math.max(Theme.size.toolbar + Theme.space.sm + Theme.space.xxs,
                                         questionText.implicitHeight + 2 * Theme.space.md)
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        onClicked: faq.setOpen(row.modelData.anchor, !row.open)

                        Accessible.role: Accessible.Button
                        Accessible.name: questionText.text
                        Accessible.checkable: true
                        Accessible.checked: row.open

                        background: Rectangle {
                            color: row.open ? Theme.colors.stateHover : "transparent"

                            StateLayer {
                                anchors.fill: parent
                                hovered: header.hovered && !row.open
                                pressed: header.down
                            }

                            FocusRing {
                                visible: header.visualFocus
                                baseRadius: 0
                            }
                        }

                        contentItem: Item {
                            Tile {
                                id: questionTile

                                x: Theme.space.lg
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: row.modelData.icon
                                tone: row.modelData.tone
                            }

                            Text {
                                id: questionText

                                x: questionTile.x + questionTile.width + Theme.space.md
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - x - groupPill.width - chevron.width - 3 * Theme.space.md
                                textFormat: Text.PlainText
                                text: row.modelData.title ?? ""
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.prose.size
                                font.weight: row.open ? Font.DemiBold : Font.Medium
                                wrapMode: Text.Wrap
                            }

                            StatusPill {
                                id: groupPill

                                anchors.right: chevron.left
                                anchors.rightMargin: Theme.space.md
                                anchors.verticalCenter: parent.verticalCenter
                                visible: faq.category < 0
                                width: visible ? implicitWidth : 0
                                text: row.modelData.groupTitle
                            }

                            Icon {
                                id: chevron

                                anchors.right: parent.right
                                anchors.rightMargin: Theme.space.lg
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron-down"
                                size: Theme.size.iconSmall
                                color: Theme.colors.textTertiary
                                rotation: row.open ? 180 : 0

                                Behavior on rotation {
                                    NumberAnimation {
                                        duration: Theme.motion.fast
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        visible: row.open
                        width: parent.width
                        height: visible ? answer.implicitHeight + Theme.space.xl : 0

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.colors.stateHover
                        }

                        HelpBlocks {
                            id: answer

                            objectName: "helpAnswer"
                            x: Theme.space.lg + Theme.size.tile + Theme.space.md
                            y: -Theme.space.sm
                            width: parent.width - x - Theme.space.xl
                            model: row.open ? row.modelData.blocks ?? [] : []
                            onLinkActivated: link => faq.linkActivated(link)
                        }
                    }
                }
            }
        }
    }
}
