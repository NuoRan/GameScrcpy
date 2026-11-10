pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls
import GameScrcpy.App

T.AbstractButton {
    id: picker

    required property AiConversation conversation
    property real widest: Theme.size.aiModelPicker
    readonly property var chosen: picker.conversation.choice
    readonly property bool following: (picker.chosen.model ?? "") === ""
                                      || ((picker.chosen.service ?? "") !== ""
                                          && picker.chosen.service !== AiAssistant.service)
    readonly property var current: picker.following ? AiAssistant.defaultChoice : picker.chosen
    readonly property string model: picker.current.model ?? ""
    readonly property var traits: AiAssistant.service !== "" ? AiAssistant.traits(picker.model) : ({})
    readonly property var efforts: picker.traits.thinkingEffort === true && picker.current.think !== true
                                   ? [] : picker.traits.efforts ?? []
    readonly property bool thinking: picker.traits.alwaysThinks === true
                                     || (picker.traits.think === true && picker.current.think === true)
    readonly property string effort: picker.efforts.indexOf(picker.current.effort ?? "") >= 0
                                     ? picker.current.effort : ""
    readonly property bool longContext: picker.traits.longContext === true && picker.current.longContext === true
    readonly property bool hasOptions: picker.traits.think === true || picker.traits.alwaysThinks === true
                                       || picker.efforts.length > 0 || picker.traits.longContext === true
    readonly property bool differs: !picker.following && JSON.stringify(picker.plain(picker.chosen))
                                                         !== JSON.stringify(picker.plain(AiAssistant.defaultChoice))
    readonly property var labels: ({ auto: qsTr("Automatic"), minimal: qsTr("Minimal"), low: qsTr("Low"),
                                     medium: qsTr("Medium"), high: qsTr("High") })
    readonly property string summary: [picker.traits.think === true && picker.current.think === true
                                       ? qsTr("Thinking") : "",
                                       picker.effort !== "" ? picker.labels[picker.effort] : "",
                                       picker.longContext ? qsTr("1M") : ""].filter(part => part !== "").join(" · ")
    readonly property var models: {
        const listed = []
        const add = name => {
            if (name !== "" && listed.indexOf(name) < 0)
                listed.push(name)
        }
        add(AiAssistant.defaultChoice.model ?? "")
        add(picker.model)
        for (const name of AiAssistant.models)
            add(name)
        return listed
    }

    function plain(choice: var): var {
        return { model: choice.model ?? "", think: choice.think === true, effort: choice.effort ?? "",
                 longContext: choice.longContext === true }
    }

    function choose(change: var) {
        const next = picker.plain(picker.current)
        for (const key in change)
            next[key] = change[key]
        next.service = AiAssistant.service
        picker.conversation.choice = next
    }

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Theme.size.segment
    horizontalPadding: Theme.space.sm
    verticalPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    visible: picker.model !== ""
    Accessible.name: qsTr("Model: %1").arg(picker.model)
    Accessible.description: picker.summary
    onClicked: choices.visible ? choices.close() : choices.open()

    contentItem: Row {
        spacing: Theme.space.xs

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: picker.thinking ? "brain" : "sparkles"
            size: Theme.size.iconSmall
            color: picker.thinking ? Theme.colors.accentText : Theme.colors.textTertiary
        }

        Text {
            objectName: "aiModelName"
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(implicitWidth, Theme.size.aiModelChip,
                                        picker.widest - picker.leftPadding - picker.rightPadding
                                        - 2 * Theme.size.iconSmall
                                        - (summaryText.visible ? 3 * Theme.space.xs + summaryText.implicitWidth
                                                               : 2 * Theme.space.xs)))
            textFormat: Text.PlainText
            text: picker.model
            elide: Text.ElideMiddle
            color: picker.hovered || choices.visible ? Theme.colors.textPrimary : Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
            font.weight: Font.Medium
        }

        Text {
            id: summaryText

            objectName: "aiModelSummary"
            anchors.verticalCenter: parent.verticalCenter
            visible: picker.summary !== ""
            textFormat: Text.PlainText
            text: "· " + picker.summary
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-down"
            size: Theme.size.iconSmall
            color: Theme.colors.textTertiary
            rotation: choices.visible ? 180 : 0

            Behavior on rotation {
                NumberAnimation {
                    duration: Theme.motion.fast
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.motion.standard
                }
            }
        }
    }

    background: StateLayer {
        radius: Theme.radius.sm
        hovered: picker.hovered || choices.visible
        pressed: picker.down

        FocusRing {
            visible: picker.visualFocus
            baseRadius: Theme.radius.sm
        }
    }

    T.Popup {
        id: choices

        objectName: "aiModelChoices"
        y: -height - Theme.space.xs
        width: Theme.size.aiModelPicker
        implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                                 implicitContentHeight + topPadding + bottomPadding)
        padding: Theme.space.xs + Theme.space.xxs
        margins: Theme.space.sm
        focus: true
        transformOrigin: Item.Bottom

        background: PopupSurface {}

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.fade
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
            NumberAnimation {
                property: "scale"
                from: Theme.motion.enterScale
                to: 1
                duration: Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: Theme.motion.fast
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.exit
            }
        }

        contentItem: Column {
            spacing: Theme.space.xxs

            Text {
                leftPadding: Theme.space.sm
                topPadding: Theme.space.xxs
                bottomPadding: Theme.space.xxs
                textFormat: Text.PlainText
                text: qsTr("Model for this conversation")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                font.weight: Font.Medium
            }

            ListView {
                id: modelList

                objectName: "aiModelList"
                width: parent.width
                height: Math.min(contentHeight, Theme.size.aiModelList)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: picker.models

                delegate: T.ItemDelegate {
                    id: modelRow

                    required property string modelData
                    readonly property bool current: modelData === picker.model
                    readonly property bool usual: modelData === (AiAssistant.defaultChoice.model ?? "")

                    width: ListView.view.width
                    implicitHeight: Theme.size.menuItem
                    horizontalPadding: Theme.space.sm
                    hoverEnabled: true
                    text: modelData
                    font.pixelSize: Theme.type.label.size
                    Accessible.role: Accessible.RadioButton
                    Accessible.checked: modelRow.current
                    onClicked: picker.choose({ model: modelRow.modelData })

                    contentItem: Item {
                        implicitHeight: modelName.implicitHeight

                        Text {
                            id: modelName

                            anchors.left: parent.left
                            anchors.right: usualTag.visible ? usualTag.left : check.left
                            anchors.rightMargin: Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: modelRow.text
                            elide: Text.ElideMiddle
                            font: modelRow.font
                            color: Theme.colors.textPrimary
                        }

                        Text {
                            id: usualTag

                            anchors.right: check.left
                            anchors.rightMargin: Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            visible: modelRow.usual
                            textFormat: Text.PlainText
                            text: qsTr("Default")
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                        }

                        Icon {
                            id: check

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            name: modelRow.current ? "check" : ""
                            size: Theme.size.iconSmall
                            color: Theme.colors.accentText
                        }
                    }

                    background: Rectangle {
                        radius: Theme.radius.sm
                        color: modelRow.down ? Theme.colors.statePress
                             : modelRow.hovered ? Theme.colors.stateHover : "transparent"
                    }
                }
            }

            Button {
                objectName: "aiModelFetch"
                visible: AiAssistant.models.length === 0
                width: parent.width
                small: true
                tone: "ghost"
                iconName: "refresh-cw"
                text: AiAssistant.modelsState === "running" ? qsTr("Asking the service…")
                                                            : qsTr("Get the models of this service")
                enabled: AiAssistant.modelsState !== "running"
                onClicked: AiAssistant.fetchModels()
            }

            Text {
                visible: AiAssistant.modelsProblem !== ""
                width: parent.width
                leftPadding: Theme.space.sm
                rightPadding: Theme.space.sm
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: AiAssistant.modelsProblem
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Divider {}

            OptionRow {
                objectName: "aiPickThinking"
                visible: picker.traits.think === true
                title: qsTr("Deep thinking")

                Switch {
                    checked: picker.current.think === true
                    Accessible.name: qsTr("Deep thinking")
                    onToggled: picker.choose({ think: checked })
                }
            }

            OptionRow {
                objectName: "aiPickAlwaysThinks"
                visible: picker.traits.alwaysThinks === true
                title: qsTr("Thinks before every answer")
            }

            OptionRow {
                objectName: "aiPickEffort"
                visible: picker.efforts.length > 0
                title: qsTr("Effort")

                SegmentedControl {
                    id: effortSegments

                    readonly property var options: [""].concat(picker.efforts)

                    model: effortSegments.options.map(option => picker.labels[option === "" ? "auto" : option])
                    currentIndex: effortSegments.options.indexOf(picker.effort)
                    Accessible.name: qsTr("Effort")
                    onActivated: index => {
                        picker.choose({ effort: effortSegments.options[index] })
                        effortSegments.currentIndex = Qt.binding(() => effortSegments.options.indexOf(picker.effort))
                    }
                }
            }

            OptionRow {
                objectName: "aiPickLongContext"
                visible: picker.traits.longContext === true
                title: qsTr("Long context")
                detail: qsTr("1M tokens, costs more")

                Switch {
                    checked: picker.current.longContext === true
                    Accessible.name: qsTr("Long context")
                    onToggled: picker.choose({ longContext: checked })
                }
            }

            Text {
                objectName: "aiPickNoOptions"
                visible: !picker.hasOptions
                width: parent.width
                leftPadding: Theme.space.sm
                rightPadding: Theme.space.sm
                topPadding: Theme.space.xxs
                bottomPadding: Theme.space.xxs
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("This model has no thinking options.")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Divider {}

            Text {
                width: parent.width
                leftPadding: Theme.space.sm
                rightPadding: Theme.space.sm
                topPadding: Theme.space.xxs
                bottomPadding: picker.following ? Theme.space.xxs : 0
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: picker.following ? qsTr("Follows the default in Settings > AI.")
                                       : qsTr("Only for this conversation; new ones start with the default.")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Row {
                visible: !picker.following
                leftPadding: Theme.space.xxs
                spacing: Theme.space.xs

                Button {
                    objectName: "aiModelMakeDefault"
                    small: true
                    tone: "ghost"
                    iconName: "check"
                    text: qsTr("Set as default")
                    enabled: picker.differs
                    onClicked: {
                        AiAssistant.makeDefault(picker.plain(picker.chosen))
                        picker.conversation.choice = ({})
                    }
                }

                Button {
                    objectName: "aiModelFollow"
                    small: true
                    tone: "ghost"
                    iconName: "rotate-ccw"
                    text: qsTr("Use the default")
                    onClicked: picker.conversation.choice = ({})
                }
            }
        }
    }

    component Divider: Rectangle {
        width: parent ? parent.width : 0
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    component OptionRow: Item {
        id: option

        property string title
        property string detail
        default property alias control: trailing.data

        width: parent ? parent.width : 0
        implicitHeight: Math.max(Theme.size.menuItem, labels.implicitHeight, trailing.implicitHeight)

        Column {
            id: labels

            anchors.left: parent.left
            anchors.leftMargin: Theme.space.sm
            anchors.right: trailing.left
            anchors.rightMargin: Theme.space.sm
            anchors.verticalCenter: parent.verticalCenter

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: option.title
                elide: Text.ElideRight
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
            }

            Text {
                width: parent.width
                visible: option.detail !== ""
                textFormat: Text.PlainText
                text: option.detail
                elide: Text.ElideRight
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }
        }

        Row {
            id: trailing

            anchors.right: parent.right
            anchors.rightMargin: Theme.space.sm
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
