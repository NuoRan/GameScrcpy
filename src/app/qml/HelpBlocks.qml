pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQml.Models
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Column {
    id: blocks

    property var model: []
    property url baseUrl
    property var targets: ({})
    property var tabTargets: ({})

    signal linkActivated(string link)

    function register(anchor: string, item: Item) {
        if (anchor !== "")
            blocks.targets[anchor] = item
    }

    function forget(anchor: string, item: Item) {
        if (blocks.targets[anchor] === item)
            delete blocks.targets[anchor]
    }

    function registerTabs(tabs: var, item: Item) {
        for (const tab of tabs) {
            blocks.register(tab.anchor, item)
            blocks.tabTargets[tab.anchor] = item
        }
    }

    function target(anchor: string): Item {
        const tabs = blocks.tabTargets[anchor]
        if (tabs !== undefined)
            tabs.select(anchor)
        return blocks.targets[anchor] ?? null
    }

    function find(anchor: string): Item {
        return blocks.targets[anchor] ?? null
    }

    function copy(text: string) {
        clipboard.text = text
        clipboard.selectAll()
        clipboard.copy()
        clipboard.deselect()
    }

    TextEdit {
        id: clipboard

        visible: false
        textFormat: TextEdit.PlainText
    }

    component Heading: Item {
        id: heading

        required property var modelData
        required property int index
        readonly property bool major: (heading.modelData.level ?? 2) === 2
        readonly property int gap: heading.index === 0 ? Theme.space.lg
                                 : heading.major ? Theme.space.x5l - Theme.space.xxs : Theme.space.xxl + Theme.space.xs

        signal linkActivated(string link)

        objectName: "helpHeading"
        implicitHeight: heading.gap + headingRow.implicitHeight + Theme.space.xxs

        Accessible.role: Accessible.Heading
        Accessible.name: heading.modelData.text ?? ""

        Row {
            id: headingRow

            y: heading.gap
            width: parent.width
            spacing: Theme.space.sm + Theme.space.xxs

            HelpText {
                id: headingText

                width: Math.min(implicitWidth, headingRow.width - (note.visible ? note.implicitWidth + headingRow.spacing : 0))
                html: heading.modelData.html ?? ""
                color: Theme.colors.textPrimary
                font.pixelSize: heading.major ? Theme.type.title.size + 1 : Theme.type.subtitle.size
                font.weight: Font.DemiBold
                lineSize: heading.major ? Theme.type.title.lineHeight + Theme.space.xxs : Theme.type.subtitle.lineHeight
                onLinkActivated: link => heading.linkActivated(link)
            }

            Text {
                id: note

                anchors.baseline: headingText.baseline
                visible: text !== ""
                textFormat: Text.PlainText
                text: heading.modelData.note ?? ""
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                font.weight: Font.Medium
            }
        }
    }

    component Paragraph: HelpText {
        id: paragraph

        required property var modelData

        topPadding: Theme.space.sm + Theme.space.xxs
        html: paragraph.modelData.html ?? ""
    }

    component LinkCard: Item {
        id: linkCard

        required property var modelData

        signal linkActivated(string link)

        implicitHeight: Theme.space.md + Theme.space.xxs + linkButton.height

        T.AbstractButton {
            id: linkButton

            objectName: "helpLinkCard"
            y: linkCard.implicitHeight - height
            width: Math.min(linkCard.width, Theme.size.helpArticle)
            height: linkRow.implicitHeight + 2 * Theme.space.md
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            onClicked: linkCard.linkActivated(linkCard.modelData.target)

            Accessible.role: Accessible.Link
            Accessible.name: linkCard.modelData.label ?? ""

            background: Rectangle {
                id: linkFace

                radius: Theme.radius.xl - Theme.space.xxs
                color: "transparent"
                border.width: Theme.size.stroke
                border.color: linkButton.hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault

                StateLayer {
                    anchors.fill: parent
                    radius: linkFace.radius
                    hovered: linkButton.hovered
                    pressed: linkButton.down
                }

                FocusRing {
                    visible: linkButton.visualFocus
                    baseRadius: linkFace.radius
                }
            }

            contentItem: Item {
                Row {
                    id: linkRow

                    x: Theme.space.md + Theme.space.xxs
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 2 * x
                    spacing: Theme.space.md

                    Tile {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: linkCard.modelData.icon ?? "circle-help"
                        tone: linkCard.modelData.tile ?? "amber"
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.size.tile - Theme.size.iconSmall - 2 * parent.spacing
                        spacing: Theme.space.xxs

                        Text {
                            width: parent.width
                            visible: text !== ""
                            textFormat: Text.PlainText
                            text: linkCard.modelData.lead ?? ""
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: linkCard.modelData.label ?? ""
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size + 0.5
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                    }

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-right"
                        size: Theme.size.iconSmall
                        color: Theme.colors.textTertiary
                    }
                }
            }
        }
    }

    component Steps: Item {
        id: stepsBlock

        required property var modelData
        readonly property var items: stepsBlock.modelData.items ?? []
        readonly property var art: stepsBlock.modelData.figure ?? ({})
        readonly property var names: stepsBlock.art.names ?? []
        readonly property bool framed: stepsBlock.names.length > 0 && stepFigure.known
        readonly property real artRoom: Theme.size.helpStepsArt - 2 * Theme.space.lg
        readonly property bool wide: stepsBlock.framed && stepFigure.implicitWidth > stepsBlock.artRoom
        property int chosen: -1
        readonly property int keyStep: Math.min(Math.max(0, stepsBlock.art.focus ?? 0), stepsBlock.items.length - 1)
        readonly property int shown: !stepsBlock.framed ? -1
                                   : stepList.hovered >= 0 ? stepList.hovered
                                   : stepsBlock.chosen >= 0 ? stepsBlock.chosen : stepsBlock.keyStep

        readonly property real listRoom: stepList.implicitHeight + 2 * Theme.space.lg + Theme.space.xxs
        readonly property real sideBySide: Math.max(stepsBlock.listRoom, artPanel.implicitHeight)

        signal linkActivated(string link)

        implicitHeight: Theme.space.lg + stepsCard.height

        Rectangle {
            id: stepsCard

            y: Theme.space.lg
            width: stepsBlock.framed ? Math.min(stepsBlock.width, Theme.size.helpArticle) : stepsBlock.width
            height: !stepsBlock.framed ? stepList.implicitHeight
                  : stepsBlock.wide ? artPanel.implicitHeight + stepsBlock.listRoom
                  : stepsBlock.sideBySide
            radius: Theme.radius.xl
            color: stepsBlock.framed ? Theme.colors.bgOverlay : "transparent"
            border.width: stepsBlock.framed ? Theme.size.stroke : 0
            border.color: Theme.colors.strokeSubtle
            clip: stepsBlock.framed

            Rectangle {
                id: artPanel

                visible: stepsBlock.framed
                x: stepsBlock.wide ? 0 : stepsCard.width - width
                width: stepsBlock.wide ? stepsCard.width : Theme.size.helpStepsArt
                height: stepsBlock.wide ? implicitHeight : stepsBlock.sideBySide
                implicitHeight: figureBox.height + stepDots.height + 3 * Theme.space.lg
                color: Theme.colors.bgSunken

                Rectangle {
                    y: stepsBlock.wide ? parent.height - height : 0
                    width: stepsBlock.wide ? parent.width : Theme.size.stroke
                    height: stepsBlock.wide ? Theme.size.stroke : parent.height
                    color: Theme.colors.strokeSubtle
                }

                Item {
                    id: figureBox

                    readonly property real fit: Math.min(1, (artPanel.width - 2 * Theme.space.lg) / Math.max(1, stepFigure.implicitWidth))

                    anchors.horizontalCenter: parent.horizontalCenter
                    y: (parent.height - height - stepDots.height - Theme.space.lg) / 2
                    width: stepFigure.implicitWidth * figureBox.fit
                    height: stepFigure.implicitHeight * figureBox.fit

                    HelpFigure {
                        id: stepFigure

                        width: implicitWidth
                        height: implicitHeight
                        transformOrigin: Item.TopLeft
                        scale: figureBox.fit
                        name: stepsBlock.names.length > 0
                              ? stepsBlock.names[Math.min(Math.max(stepsBlock.shown, 0), stepsBlock.names.length - 1)] : ""
                        Accessible.name: stepsBlock.art.caption ?? ""
                    }
                }

                Row {
                    id: stepDots

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.space.lg
                    spacing: Theme.space.sm - Theme.space.xxs

                    Repeater {
                        model: stepsBlock.items.length

                        delegate: Rectangle {
                            id: stepDot

                            required property int index

                            y: (stepDots.height - height) / 2
                            width: stepDot.index === stepsBlock.shown ? Theme.size.guideDotActive : Theme.size.guideDot
                            height: Theme.size.guideDot
                            radius: height / 2
                            color: stepDot.index === stepsBlock.shown ? Theme.colors.accent : Theme.colors.strokeStrong

                            Behavior on width {
                                NumberAnimation {
                                    duration: Theme.motion.base
                                    easing.type: Easing.BezierSpline
                                    easing.bezierCurve: Theme.motion.standard
                                }
                            }

                            TapHandler {
                                onTapped: stepsBlock.chosen = stepDot.index
                            }
                        }
                    }
                }
            }

            HelpSteps {
                id: stepList

                x: stepsBlock.framed ? Theme.space.xl : 0
                y: !stepsBlock.framed ? 0
                 : stepsBlock.wide ? artPanel.height + Theme.space.lg + Theme.space.xxs : Theme.space.lg + Theme.space.xxs
                width: !stepsBlock.framed ? Math.min(stepsCard.width, Theme.size.helpProse)
                     : stepsBlock.wide ? stepsCard.width - 2 * Theme.space.xl
                     : stepsCard.width - Theme.size.helpStepsArt - 2 * Theme.space.xl
                items: stepsBlock.items
                interactive: stepsBlock.framed
                current: stepsBlock.shown
                onPicked: index => stepsBlock.chosen = index
                onLinkActivated: link => stepsBlock.linkActivated(link)
            }
        }
    }

    component Bullets: Column {
        id: bullets

        required property var modelData

        signal linkActivated(string link)

        topPadding: Theme.space.sm + Theme.space.xxs
        spacing: Theme.space.xs

        Repeater {
            model: bullets.modelData.items ?? []

            delegate: Item {
                id: bullet

                required property string modelData

                width: bullets.width
                height: bulletText.implicitHeight

                Rectangle {
                    x: Theme.space.xs
                    y: (Theme.type.prose.lineHeight - height) / 2
                    width: Theme.size.pillDot - 1
                    height: width
                    radius: width / 2
                    color: Theme.colors.textTertiary
                }

                HelpText {
                    id: bulletText

                    x: Theme.space.lg + Theme.space.xxs
                    width: parent.width - x
                    html: bullet.modelData
                    onLinkActivated: link => bullets.linkActivated(link)
                }
            }
        }
    }

    component Choices: Item {
        id: choices

        required property var modelData
        readonly property var items: choices.modelData.items ?? []
        readonly property var tones: ["azure", "teal", "amber", "lime", "rose", "orange"]

        signal linkActivated(string link)

        implicitHeight: Theme.space.lg + choiceGrid.implicitHeight

        GridLayout {
            id: choiceGrid

            y: Theme.space.lg
            width: Math.min(choices.width, Theme.size.helpArticle)
            columns: choices.items.length === 2 || choices.items.length === 4 ? 2 : 3
            columnSpacing: Theme.space.md
            rowSpacing: Theme.space.md
            uniformCellWidths: true
            uniformCellHeights: true

            Repeater {
                model: choices.items

                delegate: Rectangle {
                    id: choice

                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    implicitHeight: choiceColumn.implicitHeight + 2 * Theme.space.lg
                    radius: Theme.radius.xl
                    color: Theme.colors.bgOverlay
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeSubtle

                    Column {
                        id: choiceColumn

                        x: Theme.space.lg
                        y: Theme.space.lg
                        width: parent.width - 2 * Theme.space.lg
                        spacing: Theme.space.xs

                        Tile {
                            iconName: choice.modelData.icon ?? ""
                            tone: choices.tones[choice.index % choices.tones.length]
                        }

                        HelpText {
                            width: parent.width
                            topPadding: Theme.space.sm
                            html: choice.modelData.title ?? ""
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.prose.size
                            font.weight: Font.DemiBold
                            lineSize: Theme.type.title.lineHeight - Theme.space.sm + Theme.space.xxs
                            onLinkActivated: link => choices.linkActivated(link)
                        }

                        HelpText {
                            width: parent.width
                            html: choice.modelData.html ?? ""
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size + 0.5
                            lineSize: Theme.type.caption.lineHeight + 3
                            onLinkActivated: link => choices.linkActivated(link)
                        }
                    }
                }
            }
        }
    }

    component Callout: Item {
        id: callout

        required property var modelData
        readonly property string tone: callout.modelData.tone ?? "note"
        readonly property color ink: callout.tone === "warning" ? Theme.colors.warningText
                                   : callout.tone === "tip" ? Theme.colors.infoText : Theme.colors.textSecondary
        readonly property var action: callout.modelData.action ?? ({})

        signal linkActivated(string link)

        implicitHeight: Theme.space.lg + calloutBox.height

        Accessible.role: Accessible.Note
        Accessible.name: calloutTitle.text

        Rectangle {
            id: calloutBox

            y: Theme.space.lg
            width: Math.min(callout.width, Theme.size.helpArticle)
            height: calloutRow.implicitHeight + 2 * (Theme.space.md + Theme.space.xxs)
            radius: Theme.radius.xl - Theme.space.xxs
            color: callout.tone === "warning" ? Theme.colors.warningSubtle
                 : callout.tone === "tip" ? Theme.colors.infoSubtle : Theme.colors.statePress
            border.width: Theme.size.stroke
            border.color: callout.tone === "warning" ? Theme.colors.warningLine : Theme.colors.strokeSubtle

            Row {
                id: calloutRow

                x: Theme.space.lg
                y: Theme.space.md + Theme.space.xxs
                width: parent.width - 2 * Theme.space.lg
                spacing: Theme.space.md

                Icon {
                    y: Theme.space.xxs
                    name: callout.tone === "warning" ? "triangle-alert" : callout.tone === "tip" ? "sparkles" : "info"
                    size: Theme.size.icon
                    color: callout.ink
                }

                Column {
                    width: parent.width - Theme.size.icon - parent.spacing
                           - (calloutAction.visible ? calloutAction.width + parent.spacing : 0)
                    spacing: Theme.space.xxs

                    HelpText {
                        id: calloutTitle

                        visible: (callout.modelData.title ?? "") !== ""
                        width: parent.width
                        html: callout.modelData.title ?? ""
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size + 0.5
                        font.weight: Font.DemiBold
                        lineSize: Theme.type.body.lineHeight
                        onLinkActivated: link => callout.linkActivated(link)
                    }

                    HelpText {
                        visible: (callout.modelData.html ?? "") !== ""
                        width: parent.width
                        html: callout.modelData.html ?? ""
                        font.pixelSize: Theme.type.label.size
                        lineSize: Theme.type.code.lineHeight
                        onLinkActivated: link => callout.linkActivated(link)
                    }
                }

                Button {
                    id: calloutAction

                    objectName: "helpCalloutAction"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: (callout.action.target ?? "") !== ""
                    small: true
                    text: callout.action.label ?? ""
                    onClicked: callout.linkActivated(callout.action.target)
                }
            }
        }
    }

    component Table: Item {
        id: table

        required property var modelData
        readonly property var header: table.modelData.header ?? []
        readonly property var rows: table.modelData.rows ?? []
        readonly property var shares: {
            const count = Math.max(1, table.header.length)
            if (count === 2)
                return [0.3, 0.7]
            if (count === 3)
                return [0.2, 0.24, 0.56]
            return Array(count).fill(1 / count)
        }

        signal linkActivated(string link)

        implicitHeight: Theme.space.md + Theme.space.xxs + tableFrame.height

        Rectangle {
            id: tableFrame

            y: Theme.space.md + Theme.space.xxs
            width: Math.min(table.width, Theme.size.helpArticle)
            height: tableColumn.implicitHeight
            radius: Theme.radius.lg
            color: "transparent"
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault
            clip: true

            Column {
                id: tableColumn

                width: parent.width

                Rectangle {
                    width: parent.width
                    height: headerRow.implicitHeight
                    color: Theme.colors.stateHover

                    Row {
                        id: headerRow

                        Repeater {
                            model: table.header

                            delegate: Item {
                                id: headerCell

                                required property string modelData
                                required property int index

                                width: tableColumn.width * table.shares[headerCell.index]
                                height: headerText.implicitHeight + 2 * Theme.space.sm + Theme.space.xxs

                                HelpText {
                                    id: headerText

                                    x: Theme.space.md + Theme.space.xxs
                                    y: Theme.space.sm + Theme.space.xxs
                                    width: parent.width - 2 * x
                                    html: headerCell.modelData
                                    color: Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.caption.size
                                    font.weight: Font.DemiBold
                                    lineSize: Theme.type.caption.lineHeight
                                }
                            }
                        }
                    }
                }

                Repeater {
                    model: table.rows

                    delegate: Item {
                        id: tableRow

                        required property var modelData

                        width: tableColumn.width
                        height: cells.implicitHeight

                        Rectangle {
                            width: parent.width
                            height: Theme.size.stroke
                            color: Theme.colors.strokeSubtle
                        }

                        Row {
                            id: cells

                            Repeater {
                                model: tableRow.modelData

                                delegate: Item {
                                    id: cell

                                    required property string modelData
                                    required property int index

                                    width: tableColumn.width * (table.shares[cell.index] ?? 0)
                                    height: cellText.implicitHeight + 2 * Theme.space.sm + Theme.space.xxs

                                    HelpText {
                                        id: cellText

                                        x: Theme.space.md + Theme.space.xxs
                                        y: Theme.space.sm + Theme.space.xxs
                                        width: parent.width - 2 * x
                                        html: cell.modelData
                                        color: cell.index === 0 ? Theme.colors.textPrimary : Theme.colors.textSecondary
                                        font.pixelSize: Theme.type.label.size
                                        font.weight: cell.index === 0 ? Font.Medium : Font.Normal
                                        lineSize: Theme.type.body.lineHeight
                                        onLinkActivated: link => table.linkActivated(link)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    component Keys: Item {
        id: keys

        required property var modelData

        signal linkActivated(string link)

        implicitHeight: Theme.space.md + Theme.space.xxs + keysFrame.height

        Rectangle {
            id: keysFrame

            y: Theme.space.md + Theme.space.xxs
            width: Math.min(keys.width, Theme.size.helpProse)
            height: keysColumn.implicitHeight
            radius: Theme.radius.lg
            color: "transparent"
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Column {
                id: keysColumn

                width: parent.width

                Repeater {
                    model: keys.modelData.items ?? []

                    delegate: Item {
                        id: keyRow

                        required property var modelData
                        required property int index

                        width: keysColumn.width
                        height: Math.max(Theme.size.listItem + Theme.space.sm, keyText.implicitHeight + 2 * Theme.space.sm)

                        Rectangle {
                            visible: keyRow.index > 0
                            width: parent.width
                            height: Theme.size.stroke
                            color: Theme.colors.strokeSubtle
                        }

                        Row {
                            x: Theme.space.md + Theme.space.xxs
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.space.xs

                            Repeater {
                                model: keyRow.modelData.keys ?? []

                                Keycap {
                                    required property string modelData

                                    large: true
                                    text: modelData
                                }
                            }
                        }

                        HelpText {
                            id: keyText

                            x: Theme.size.scopeName - Theme.space.xl
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - x - Theme.space.md
                            html: keyRow.modelData.html ?? ""
                            font.pixelSize: Theme.type.label.size
                            lineSize: Theme.type.body.lineHeight
                            onLinkActivated: link => keys.linkActivated(link)
                        }
                    }
                }
            }
        }
    }

    component Code: Item {
        id: code

        required property var modelData
        property bool copied: false

        signal copyRequested(string text)

        implicitHeight: Theme.space.md + Theme.space.xxs + codeBox.height

        Rectangle {
            id: codeBox

            y: Theme.space.md + Theme.space.xxs
            width: Math.min(code.width, Theme.size.helpArticle)
            height: codeHead.height + codeBody.height
            radius: Theme.radius.xl - Theme.space.xxs
            color: Theme.colors.bgCode
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault
            clip: true

            Item {
                id: codeHead

                width: parent.width
                height: Theme.size.listItem + Theme.space.xxs

                Row {
                    x: Theme.space.md + Theme.space.xxs
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.sm

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "code-xml"
                        size: Theme.size.infoIcon
                        color: Theme.colors.textTertiary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: (code.modelData.title ?? "") !== "" ? code.modelData.title
                            : (code.modelData.language ?? "") === "ts" ? "TypeScript"
                            : (code.modelData.language ?? "") === "js" ? "JavaScript"
                            : (code.modelData.language ?? "") !== "" ? code.modelData.language : qsTr("Command")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }

                Button {
                    objectName: "helpCopy"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.xs
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    tone: "ghost"
                    iconName: code.copied ? "check" : "copy"
                    text: code.copied ? qsTr("Copied") : qsTr("Copy")
                    onClicked: {
                        code.copyRequested(code.modelData.text ?? "")
                        code.copied = true
                    }
                    onHoveredChanged: {
                        if (!hovered)
                            code.copied = false
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.size.stroke
                    color: Theme.colors.strokeSubtle
                }
            }

            Flickable {
                id: codeBody

                y: codeHead.height
                width: parent.width
                height: codeText.implicitHeight + 2 * Theme.space.md
                contentWidth: codeText.implicitWidth + 2 * Theme.space.lg
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentWidth > width
                clip: true

                HelpText {
                    id: codeText

                    x: Theme.space.lg
                    y: Theme.space.md
                    wrapMode: Text.NoWrap
                    html: code.modelData.html ?? ""
                    color: Theme.colors.textPrimary
                    font.family: Theme.type.monoFamily
                    font.pixelSize: Theme.type.code.size
                    lineSize: Theme.type.code.lineHeight
                }
            }
        }
    }

    component Figure: Item {
        id: figureBlock

        required property var modelData
        readonly property var names: figureBlock.modelData.names ?? []
        readonly property var legend: figureBlock.modelData.legend ?? []
        readonly property string caption: figureBlock.modelData.caption ?? ""

        implicitHeight: Theme.space.lg + figurePanel.height
                        + (figureCaption.visible ? figureCaption.implicitHeight + Theme.space.sm : 0)
                        + (legendGrid.visible ? legendGrid.implicitHeight + Theme.space.sm : 0)

        Rectangle {
            id: figurePanel

            readonly property real fit: Math.min(1, (width - 2 * Theme.space.lg) / Math.max(1, figureArt.implicitWidth))

            y: Theme.space.lg
            width: Math.min(figureBlock.width, Theme.size.helpArticle)
            height: figureArt.implicitHeight * figurePanel.fit + 2 * Theme.space.lg
            radius: Theme.radius.xl
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle
            clip: true

            HelpFigure {
                id: figureArt

                x: (figurePanel.width - implicitWidth * figurePanel.fit) / 2
                y: Theme.space.lg
                width: implicitWidth
                height: implicitHeight
                transformOrigin: Item.TopLeft
                scale: figurePanel.fit
                name: figureBlock.names[Math.min(figureBlock.modelData.focus ?? 0, figureBlock.names.length - 1)] ?? ""
                Accessible.name: figureBlock.caption
            }
        }

        Text {
            id: figureCaption

            anchors.top: figurePanel.bottom
            anchors.topMargin: Theme.space.sm
            width: Math.min(figureBlock.width, Theme.size.helpProse)
            visible: figureBlock.legend.length === 0 && /[。.！？!?]$/.test(text)
            textFormat: Text.PlainText
            text: figureBlock.caption
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size + 0.5
            lineHeight: Theme.type.caption.lineHeight + 3
            lineHeightMode: Text.FixedHeight
            wrapMode: Text.Wrap
        }

        Grid {
            id: legendGrid

            readonly property int across: figureBlock.legend.length === 3 ? 3 : 2

            anchors.top: figurePanel.bottom
            anchors.topMargin: Theme.space.sm
            visible: figureBlock.legend.length > 0
            width: figurePanel.width
            columns: legendGrid.across
            rows: Math.ceil(figureBlock.legend.length / legendGrid.across)
            flow: legendGrid.across === 2 ? Grid.TopToBottom : Grid.LeftToRight
            columnSpacing: Theme.space.xxl + Theme.space.xs

            Repeater {
                model: figureBlock.legend

                delegate: Item {
                    id: legendItem

                    required property var modelData
                    required property int index

                    width: (legendGrid.width - (legendGrid.across - 1) * legendGrid.columnSpacing) / legendGrid.across
                    height: legendText.implicitHeight + 2 * Theme.space.md

                    Rectangle {
                        visible: legendGrid.across === 2
                        width: parent.width
                        height: Theme.size.stroke
                        color: Theme.colors.strokeSubtle
                    }

                    Row {
                        y: Theme.space.md
                        width: parent.width
                        spacing: Theme.space.md

                        Rectangle {
                            width: Theme.size.helpMarker
                            height: width
                            radius: width / 2
                            color: Theme.colors.accent

                            Text {
                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: legendItem.index + 1
                                color: Theme.colors.textOnAccent
                                font.pixelSize: Theme.type.caption.size
                                font.weight: Font.Bold
                                font.features: ({ "tnum": 1 })
                            }
                        }

                        Rectangle {
                            id: legendIcon

                            visible: (legendItem.modelData.icon ?? "") !== ""
                            width: Theme.size.tile + Theme.space.xxs
                            height: width
                            radius: Theme.radius.md
                            color: Theme.colors.bgElevated
                            border.width: Theme.size.stroke
                            border.color: Theme.colors.strokeDefault

                            Icon {
                                anchors.centerIn: parent
                                name: legendItem.modelData.icon ?? ""
                                size: Theme.size.iconSmall
                                color: Theme.colors.textSecondary
                            }
                        }

                        Column {
                            id: legendText

                            width: parent.width - Theme.size.helpMarker - parent.spacing
                                   - (legendIcon.visible ? legendIcon.width + parent.spacing : 0)
                            spacing: Theme.space.xxs

                            HelpText {
                                width: parent.width
                                html: legendItem.modelData.title ?? ""
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size + 0.5
                                font.weight: Font.DemiBold
                                lineSize: Theme.type.body.lineHeight
                            }

                            HelpText {
                                width: parent.width
                                visible: (legendItem.modelData.html ?? "") !== ""
                                html: legendItem.modelData.html ?? ""
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size + 0.5
                                lineSize: Theme.type.caption.lineHeight + 3
                            }
                        }
                    }
                }
            }
        }
    }

    component Setup: Item {
        id: setupBlock

        required property var modelData

        signal linkActivated(string link)

        implicitHeight: Theme.space.lg + setupCard.height

        HelpSetupCard {
            id: setupCard

            y: Theme.space.lg
            width: Math.min(setupBlock.width, Theme.size.helpArticle)
            height: implicitHeight
            onLinkActivated: link => setupBlock.linkActivated(link)
        }
    }

    component Picture: Item {
        id: picture

        required property var modelData
        property url baseUrl

        implicitHeight: Theme.space.lg + image.height

        Image {
            id: image

            y: Theme.space.lg
            width: Math.min(picture.width, implicitWidth)
            height: implicitWidth > 0 ? width * implicitHeight / implicitWidth : 0
            fillMode: Image.PreserveAspectFit
            source: (picture.modelData.source ?? "") !== "" ? Qt.url(picture.baseUrl.toString() + picture.modelData.source) : Qt.url("")
            Accessible.role: Accessible.Graphic
            Accessible.name: picture.modelData.caption ?? ""
        }
    }

    component Tabs: Item {
        id: tabsBlock

        required property var modelData
        readonly property var tabs: tabsBlock.modelData.tabs ?? []
        property int current: 0
        readonly property var shown: tabsBlock.tabs[Math.min(tabsBlock.current, tabsBlock.tabs.length - 1)] ?? ({})

        signal linkActivated(string link)
        signal copyRequested(string text)

        function select(anchor: string) {
            const at = tabsBlock.tabs.findIndex(tab => tab.anchor === anchor)
            if (at >= 0)
                tabsBlock.current = at
        }

        implicitHeight: Theme.space.xl + switcher.height + tabBody.implicitHeight

        Row {
            id: switcher

            y: Theme.space.xl
            spacing: Theme.space.sm - Theme.space.xxs

            Repeater {
                model: tabsBlock.tabs

                delegate: FilterChip {
                    id: tabChip

                    required property var modelData
                    required property int index

                    objectName: "helpTab"
                    iconName: tabChip.modelData.icon ?? ""
                    text: tabChip.modelData.title ?? ""
                    selected: tabChip.index === tabsBlock.current
                    onClicked: tabsBlock.current = tabChip.index
                }
            }
        }

        Column {
            id: tabBody

            y: switcher.y + switcher.height
            width: tabsBlock.width

            Repeater {
                model: tabsBlock.shown.blocks ?? []

                delegate: DelegateChooser {
                    role: "kind"

                    DelegateChoice {
                        roleValue: "paragraph"

                        Paragraph {
                            width: Math.min(tabBody.width, Theme.size.helpProse)
                            onLinkActivated: link => tabsBlock.linkActivated(link)
                        }
                    }
                    DelegateChoice {
                        roleValue: "link"

                        LinkCard {
                            width: tabBody.width
                            onLinkActivated: link => tabsBlock.linkActivated(link)
                        }
                    }
                    DelegateChoice {
                        roleValue: "steps"

                        Steps {
                            width: tabBody.width
                            onLinkActivated: link => tabsBlock.linkActivated(link)
                        }
                    }
                    DelegateChoice {
                        roleValue: "bullets"

                        Bullets {
                            width: Math.min(tabBody.width, Theme.size.helpProse)
                            onLinkActivated: link => tabsBlock.linkActivated(link)
                        }
                    }
                    DelegateChoice {
                        roleValue: "callout"

                        Callout {
                            width: tabBody.width
                            onLinkActivated: link => tabsBlock.linkActivated(link)
                        }
                    }
                    DelegateChoice {
                        roleValue: "figure"

                        Figure {
                            width: tabBody.width
                        }
                    }
                    DelegateChoice {
                        roleValue: "code"

                        Code {
                            width: tabBody.width
                            onCopyRequested: text => tabsBlock.copyRequested(text)
                        }
                    }
                    DelegateChoice {
                        roleValue: "table"

                        Table {
                            width: tabBody.width
                            onLinkActivated: link => tabsBlock.linkActivated(link)
                        }
                    }
                }
            }
        }
    }

    component FunctionCard: Item {
        id: fn

        required property var modelData
        property bool open: true
        readonly property var params: fn.modelData.params ?? []
        readonly property var failures: fn.modelData.failures ?? []
        readonly property string stability: {
            switch (fn.modelData.stability ?? "") {
            case "stable":
                return qsTr("Stable")
            case "experimental":
                return qsTr("Experimental")
            case "deprecated":
                return qsTr("Deprecated")
            default:
                return fn.modelData.stability ?? ""
            }
        }

        signal linkActivated(string link)
        signal copyRequested(string text)

        objectName: "helpFunction"
        implicitHeight: Theme.space.lg + fnCard.height

        Rectangle {
            id: fnCard

            y: Theme.space.lg
            width: Math.min(fn.width, Theme.size.helpArticle)
            height: fnColumn.implicitHeight
            radius: Theme.radius.xl
            color: Theme.colors.bgOverlay
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle
            clip: true

            Column {
                id: fnColumn

                width: parent.width
                bottomPadding: fn.open ? Theme.space.lg + Theme.space.xxs : 0

                T.AbstractButton {
                    id: fnHead

                    width: parent.width
                    height: fnTitle.implicitHeight + 2 * Theme.space.lg
                    hoverEnabled: true
                    onClicked: fn.open = !fn.open

                    Accessible.role: Accessible.Button
                    Accessible.name: fn.modelData.title ?? ""

                    background: Item {
                        FocusRing {
                            visible: fnHead.visualFocus
                            baseRadius: Theme.radius.xl
                        }
                    }

                    contentItem: Item {
                        Text {
                            id: fnTitle

                            x: Theme.space.lg + Theme.space.xxs
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - x - fnBadge.width - fnChevron.width - 3 * Theme.space.md
                            textFormat: Text.PlainText
                            text: fn.modelData.title ?? ""
                            color: Theme.colors.textPrimary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.subtitle.size
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }

                        StatusPill {
                            id: fnBadge

                            anchors.right: fnChevron.left
                            anchors.rightMargin: Theme.space.md
                            anchors.verticalCenter: parent.verticalCenter
                            visible: fn.stability !== ""
                            tone: (fn.modelData.stability ?? "") === "stable" ? "ok" : "warn"
                            text: (fn.modelData.since ?? "") !== "" ? qsTr("%1 · API %2").arg(fn.stability).arg(fn.modelData.since)
                                                                     : fn.stability
                        }

                        Icon {
                            id: fnChevron

                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space.lg
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron-down"
                            size: Theme.size.iconSmall
                            color: Theme.colors.textTertiary
                            rotation: fn.open ? 180 : 0

                            Behavior on rotation {
                                NumberAnimation {
                                    duration: Theme.motion.fast
                                }
                            }
                        }
                    }
                }

                Column {
                    x: Theme.space.lg + Theme.space.xxs
                    width: parent.width - 2 * x
                    visible: fn.open
                    spacing: Theme.space.md

                    HelpText {
                        width: parent.width
                        visible: (fn.modelData.html ?? "") !== ""
                        html: fn.modelData.html ?? ""
                        font.pixelSize: Theme.type.label.size + 0.5
                        lineSize: Theme.type.subtitle.lineHeight
                        onLinkActivated: link => fn.linkActivated(link)
                    }

                    Rectangle {
                        width: parent.width
                        visible: (fn.modelData.signatureHtml ?? "") !== ""
                        height: signature.implicitHeight + 2 * Theme.space.sm + Theme.space.xxs
                        radius: Theme.radius.lg
                        color: Theme.colors.bgCode
                        border.width: Theme.size.stroke
                        border.color: Theme.colors.strokeDefault
                        clip: true

                        HelpText {
                            id: signature

                            x: Theme.space.md + Theme.space.xxs
                            y: Theme.space.sm + Theme.space.xxs
                            width: parent.width - 2 * x
                            wrapMode: Text.Wrap
                            html: (fn.modelData.signatureHtml ?? "").replace(/,&nbsp;/g, ", ")
                            color: Theme.colors.textPrimary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.caption.size + 0.5
                            lineSize: Theme.type.body.lineHeight
                        }
                    }

                    Column {
                        id: paramColumn

                        width: parent.width
                        visible: fn.params.length > 0

                        Text {
                            bottomPadding: Theme.space.sm
                            textFormat: Text.PlainText
                            text: qsTr("Parameters")
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.micro.size
                            font.weight: Theme.type.micro.weight
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.4
                        }

                        Repeater {
                            model: fn.params

                            delegate: Item {
                                id: param

                                required property var modelData

                                width: paramColumn.width
                                height: Math.max(paramName.implicitHeight, paramText.implicitHeight) + 2 * Theme.space.sm + Theme.space.xxs

                                Rectangle {
                                    width: parent.width
                                    height: Theme.size.stroke
                                    color: Theme.colors.strokeSubtle
                                }

                                Column {
                                    id: paramName

                                    y: Theme.space.sm + Theme.space.xxs
                                    width: Theme.size.numberFieldWide + Theme.space.sm

                                    Text {
                                        width: parent.width
                                        textFormat: Text.PlainText
                                        text: param.modelData.name ?? ""
                                        color: Theme.colors.textPrimary
                                        font.family: Theme.type.monoFamily
                                        font.pixelSize: Theme.type.caption.size + 0.5
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        visible: param.modelData.optional === true
                                        textFormat: Text.PlainText
                                        text: qsTr("optional")
                                        color: Theme.colors.textTertiary
                                        font.pixelSize: Theme.type.micro.size
                                        font.weight: Font.Normal
                                    }
                                }

                                Text {
                                    id: paramType

                                    x: paramName.width + Theme.space.md
                                    y: Theme.space.sm + Theme.space.xxs
                                    width: Theme.size.numberField - Theme.space.lg
                                    textFormat: Text.PlainText
                                    text: param.modelData.type ?? ""
                                    color: Theme.colors.infoText
                                    font.family: Theme.type.monoFamily
                                    font.pixelSize: Theme.type.caption.size
                                    wrapMode: Text.Wrap
                                }

                                HelpText {
                                    id: paramText

                                    x: paramType.x + paramType.width + Theme.space.md
                                    y: Theme.space.sm + Theme.space.xxs
                                    width: parent.width - x
                                    html: param.modelData.html ?? ""
                                    font.pixelSize: Theme.type.caption.size + 0.5
                                    lineSize: Theme.type.caption.lineHeight + 3
                                    onLinkActivated: link => fn.linkActivated(link)
                                }
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        visible: (fn.modelData.returns ?? "") !== ""
                        spacing: Theme.space.md

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: qsTr("Returns")
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.micro.size
                            font.weight: Theme.type.micro.weight
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.4
                        }

                        HelpText {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - x
                            html: fn.modelData.returns ?? ""
                            font.pixelSize: Theme.type.caption.size + 0.5
                            lineSize: Theme.type.caption.lineHeight + 3
                        }
                    }

                    HelpText {
                        width: parent.width
                        visible: (fn.modelData.extra ?? "") !== ""
                        html: fn.modelData.extra ?? ""
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size + 0.5
                        lineSize: Theme.type.caption.lineHeight + 3
                    }

                    Column {
                        id: failureColumn

                        width: parent.width
                        visible: fn.failures.length > 0

                        Text {
                            bottomPadding: Theme.space.sm
                            textFormat: Text.PlainText
                            text: qsTr("Errors")
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.micro.size
                            font.weight: Theme.type.micro.weight
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.4
                        }

                        Repeater {
                            model: fn.failures

                            delegate: Item {
                                id: failure

                                required property var modelData

                                width: failureColumn.width
                                height: failureText.implicitHeight + 2 * Theme.space.sm

                                Rectangle {
                                    width: parent.width
                                    height: Theme.size.stroke
                                    color: Theme.colors.strokeSubtle
                                }

                                StatusPill {
                                    id: failureCode

                                    y: Theme.space.sm
                                    visible: (failure.modelData.code ?? "") !== ""
                                    tone: "bad"
                                    text: failure.modelData.code ?? ""
                                }

                                Column {
                                    id: failureText

                                    x: Theme.size.numberFieldWide
                                    y: Theme.space.sm
                                    width: parent.width - x

                                    HelpText {
                                        width: parent.width
                                        html: failure.modelData.html ?? ""
                                        font.pixelSize: Theme.type.caption.size + 0.5
                                        lineSize: Theme.type.caption.lineHeight + 3
                                    }

                                    HelpText {
                                        width: parent.width
                                        visible: (failure.modelData.fix ?? "") !== ""
                                        html: failure.modelData.fix ?? ""
                                        color: Theme.colors.textTertiary
                                        font.pixelSize: Theme.type.caption.size + 0.5
                                        lineSize: Theme.type.caption.lineHeight + 3
                                    }
                                }
                            }
                        }
                    }

                    Code {
                        width: parent.width
                        visible: (fn.modelData.example ?? "") !== ""
                        modelData: ({ kind: "code", language: "ts", title: qsTr("Example"), text: fn.modelData.example ?? "",
                                      html: fn.modelData.exampleHtml ?? "" })
                        onCopyRequested: text => fn.copyRequested(text)
                    }
                }
            }
        }
    }

    Repeater {
        model: blocks.model

        delegate: DelegateChooser {
            role: "kind"

            DelegateChoice {
                roleValue: "heading"

                Heading {
                    id: headingBlock

                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                    Component.onCompleted: blocks.register(headingBlock.modelData.anchor ?? "", headingBlock)
                    Component.onDestruction: blocks.forget(headingBlock.modelData.anchor ?? "", headingBlock)
                }
            }
            DelegateChoice {
                roleValue: "paragraph"

                Paragraph {
                    width: Math.min(blocks.width, Theme.size.helpProse)
                    onLinkActivated: link => blocks.linkActivated(link)
                }
            }
            DelegateChoice {
                roleValue: "link"

                LinkCard {
                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                }
            }
            DelegateChoice {
                roleValue: "steps"

                Steps {
                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                }
            }
            DelegateChoice {
                roleValue: "bullets"

                Bullets {
                    width: Math.min(blocks.width, Theme.size.helpProse)
                    onLinkActivated: link => blocks.linkActivated(link)
                }
            }
            DelegateChoice {
                roleValue: "choices"

                Choices {
                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                }
            }
            DelegateChoice {
                roleValue: "callout"

                Callout {
                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                }
            }
            DelegateChoice {
                roleValue: "table"

                Table {
                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                }
            }
            DelegateChoice {
                roleValue: "keys"

                Keys {
                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                }
            }
            DelegateChoice {
                roleValue: "code"

                Code {
                    width: blocks.width
                    onCopyRequested: text => blocks.copy(text)
                }
            }
            DelegateChoice {
                roleValue: "figure"

                Figure {
                    width: blocks.width
                }
            }
            DelegateChoice {
                roleValue: "setup"

                Setup {
                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                }
            }
            DelegateChoice {
                roleValue: "image"

                Picture {
                    width: blocks.width
                    baseUrl: blocks.baseUrl
                }
            }
            DelegateChoice {
                roleValue: "tabs"

                Tabs {
                    id: tabsBlock

                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                    onCopyRequested: text => blocks.copy(text)
                    Component.onCompleted: blocks.registerTabs(tabsBlock.tabs, tabsBlock)
                }
            }
            DelegateChoice {
                roleValue: "function"

                FunctionCard {
                    id: functionBlock

                    width: blocks.width
                    onLinkActivated: link => blocks.linkActivated(link)
                    onCopyRequested: text => blocks.copy(text)
                    Component.onCompleted: blocks.register(functionBlock.modelData.anchor ?? "", functionBlock)
                    Component.onDestruction: blocks.forget(functionBlock.modelData.anchor ?? "", functionBlock)
                }
            }
        }
    }
}
