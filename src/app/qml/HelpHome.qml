pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App
import "HelpRich.js" as HelpRich

Column {
    id: home

    property var homeData: ({})
    readonly property var guides: home.homeData.guides ?? []
    readonly property var popular: home.homeData.popular ?? []
    readonly property var reference: (home.homeData.reference ?? []).filter(entry => entry.slug !== "api")
    readonly property bool searching: HelpLibrary.query.trim() !== ""
    readonly property var results: HelpLibrary.contents.filter(entry => entry.kind !== "group")
    readonly property int across: home.width >= 720 ? 3 : 2

    signal linkActivated(string link)

    function capsOf(state: CommandState): list<string> {
        return state.shortcut !== "" ? Commands.shortcutCaps(state.command) : []
    }

    function focusSearch() {
        bigSearch.forceActiveFocus(Qt.ShortcutFocusReason)
    }

    spacing: 0

    CommandState {
        id: helpCommand

        command: "app.help"
    }

    CommandState {
        id: paletteCommand

        command: "app.commandPalette"
    }

    CommandState {
        id: fullScreenCommand

        command: "device.fullscreen"
    }

    CommandState {
        id: saveCommand

        command: "editor.save"
    }

    component CardHeader: Row {
        id: cardHeader

        property string iconName
        property string tone
        property string title
        property string linkText

        signal linkClicked()

        width: parent ? parent.width : 0
        spacing: Theme.space.sm + Theme.space.xxs
        bottomPadding: Theme.space.sm

        Tile {
            anchors.verticalCenter: parent.verticalCenter
            iconName: cardHeader.iconName
            tone: cardHeader.tone
            scale: 0.86
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - Theme.size.tile - cardLink.width - 2 * parent.spacing
            textFormat: Text.PlainText
            text: cardHeader.title
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.prose.size
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        T.AbstractButton {
            id: cardLink

            anchors.verticalCenter: parent.verticalCenter
            visible: cardHeader.linkText !== ""
            width: visible ? implicitWidth : 0
            implicitWidth: cardLinkRow.implicitWidth
            implicitHeight: Theme.size.controlSmall
            hoverEnabled: true
            onClicked: cardHeader.linkClicked()

            Accessible.role: Accessible.Link
            Accessible.name: cardHeader.linkText

            contentItem: Row {
                id: cardLinkRow

                spacing: Theme.space.xxs

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: cardHeader.linkText
                    color: Theme.colors.accentText
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.Medium
                    font.underline: cardLink.hovered
                }

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-right"
                    size: Theme.size.infoIcon
                    color: Theme.colors.accentText
                }
            }

            background: FocusRing {
                visible: cardLink.visualFocus
                baseRadius: Theme.radius.sm
            }
        }
    }

    component ListRow: T.AbstractButton {
        id: listRow

        property string label

        implicitHeight: Theme.size.listItem + Theme.space.xs
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus

        Accessible.role: Accessible.Link
        Accessible.name: listRow.label

        background: Item {
            Rectangle {
                width: parent.width
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }

            FocusRing {
                visible: listRow.visualFocus
                baseRadius: Theme.radius.sm
            }
        }

        contentItem: Item {
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - Theme.size.infoIcon - Theme.space.sm
                textFormat: Text.PlainText
                text: listRow.label
                color: listRow.hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
                font.pixelSize: Theme.type.label.size
                elide: Text.ElideRight
            }

            Icon {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-right"
                size: Theme.size.infoIcon
                color: Theme.colors.textTertiary
            }
        }
    }

    component Panel: Rectangle {
        radius: Theme.radius.xl
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle
    }

    Panel {
        id: hero

        width: parent.width
        height: Math.max(heroText.implicitHeight, heroArt.visible ? heroArt.height : 0) + 2 * (Theme.space.xxl + Theme.space.xxs)
        radius: Theme.radius.xxl - Theme.space.xs

        Column {
            id: heroText

            x: Theme.space.x3l
            y: Theme.space.xxl + Theme.space.xxs
            width: parent.width - x - Theme.space.xxl - (heroArt.visible ? heroArt.width + Theme.space.xxl : 0)
            spacing: Theme.space.sm

            Text {
                topPadding: Theme.space.xs
                textFormat: Text.PlainText
                text: qsTr("Help")
                color: Theme.colors.accentText
                font.pixelSize: Theme.type.caption.size
                font.weight: Font.DemiBold
                font.letterSpacing: 0.3
            }

            Text {
                objectName: "helpHomeTitle"
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("What can we help you with?")
                wrapMode: Text.Wrap
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.display.size + Theme.space.xxs
                font.weight: Theme.type.display.weight
                font.letterSpacing: -0.3

                Accessible.role: Accessible.Heading
            }

            HelpText {
                width: parent.width
                html: {
                    const key = home.capsOf(helpCommand)
                    const keyHtml = key.length > 0
                        ? "<span style=\"background-color:%KEYBG%;color:%T1%;font-weight:600\">&nbsp;"
                          + HelpRich.escaped(key.join("+")) + "&nbsp;</span>"
                        : ""
                    return keyHtml !== ""
                        ? HelpRich.escaped(qsTr("Start with a guide below, or search. Press %1 at any time to come back here.")).replace("%1", keyHtml)
                        : HelpRich.escaped(qsTr("Start with a guide below, or search."))
                }
                lineSize: Theme.type.subtitle.lineHeight
            }

            Item {
                width: parent.width
                height: Theme.space.md
            }

            SearchField {
                id: bigSearch

                objectName: "helpHomeSearch"
                width: parent.width
                implicitHeight: Theme.size.helpSearch
                font.pixelSize: Theme.type.prose.size
                placeholderText: qsTr("Search help, for example “no sound” or “camera lock”")
                text: HelpLibrary.query
                onTextEdited: HelpLibrary.query = text
                Keys.onEscapePressed: HelpLibrary.query = ""
                onAccepted: {
                    const first = home.results.find(entry => entry.slug !== undefined || entry.of !== undefined)
                    if (first !== undefined)
                        home.linkActivated("help:" + (first.of ?? first.slug) + (first.anchor !== undefined ? "#" + first.anchor : ""))
                }
            }

            Flow {
                width: parent.width
                topPadding: Theme.space.xs
                spacing: Theme.space.xs + Theme.space.xxs

                Text {
                    height: Theme.size.chip
                    verticalAlignment: Text.AlignVCenter
                    rightPadding: Theme.space.xxs
                    textFormat: Text.PlainText
                    text: qsTr("Popular:")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                Repeater {
                    model: [qsTr("USB debugging"), qsTr("Wireless pairing"), qsTr("Camera lock"), qsTr("Share a profile"),
                            qsTr("Find a picture")]

                    delegate: FilterChip {
                        required property string modelData

                        objectName: "helpSuggestion"
                        text: modelData
                        onClicked: HelpLibrary.query = modelData
                    }
                }
            }
        }

        Rectangle {
            id: heroArt

            anchors.right: parent.right
            anchors.rightMargin: Theme.space.xxl + Theme.space.xxs
            anchors.verticalCenter: parent.verticalCenter
            visible: home.width >= 760
            width: Theme.size.helpHeroArtWidth
            height: Theme.size.helpHeroArtHeight
            radius: Theme.radius.xl - Theme.space.xxs
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle
            clip: true

            HelpArtHome {
                anchors.centerIn: parent
                kind: "hero"
            }
        }
    }

    Column {
        width: parent.width
        visible: home.searching
        topPadding: Theme.space.x3l
        spacing: Theme.space.md

        Text {
            textFormat: Text.PlainText
            text: home.results.length > 0 ? qsTr("Results") : qsTr("No article matches “%1”.").arg(HelpLibrary.query.trim())
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.title.size - 2
            font.weight: Font.DemiBold
        }

        Panel {
            width: Math.min(parent.width, Theme.size.helpArticle)
            visible: home.results.length > 0
            height: resultColumn.implicitHeight + Theme.space.sm

            Column {
                id: resultColumn

                x: Theme.space.lg
                y: Theme.space.xs
                width: parent.width - 2 * Theme.space.lg

                Repeater {
                    model: home.results

                    delegate: T.AbstractButton {
                        id: result

                        required property var modelData
                        required property int index
                        readonly property bool section: result.modelData.kind === "section"

                        objectName: "helpResult"
                        width: resultColumn.width
                        implicitHeight: result.section ? Theme.size.listItem : Theme.size.listItem + Theme.space.md
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        onClicked: home.linkActivated("help:" + (result.section ? result.modelData.of + "#" + result.modelData.anchor
                                                                              : result.modelData.slug))

                        Accessible.role: Accessible.Link
                        Accessible.name: result.modelData.title ?? ""

                        background: Item {
                            Rectangle {
                                visible: result.index > 0 && !result.section
                                width: parent.width
                                height: Theme.size.stroke
                                color: Theme.colors.strokeSubtle
                            }

                            FocusRing {
                                visible: result.visualFocus
                                baseRadius: Theme.radius.sm
                            }
                        }

                        contentItem: Row {
                            spacing: Theme.space.md
                            leftPadding: result.section ? Theme.size.tile + Theme.space.md : 0

                            Tile {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !result.section
                                iconName: result.modelData.icon ?? ""
                                tone: result.modelData.tile ?? ""
                            }

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: result.section
                                name: "chevron-right"
                                size: Theme.size.infoIcon
                                color: Theme.colors.textTertiary
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: result.modelData.title ?? ""
                                color: result.hovered ? Theme.colors.accentText
                                     : result.section ? Theme.colors.textSecondary : Theme.colors.textPrimary
                                font.pixelSize: result.section ? Theme.type.label.size : Theme.type.prose.size
                                font.weight: result.section ? Font.Normal : Font.DemiBold
                                font.family: result.modelData.kind === "reference"
                                             && /^[A-Za-z_$][\w$]*$/.test(result.modelData.title ?? "")
                                             ? Theme.type.monoFamily : Application.font.family
                            }
                        }
                    }
                }
            }
        }
    }

    Row {
        visible: !home.searching
        topPadding: Theme.space.x3l
        bottomPadding: Theme.space.md + Theme.space.xxs
        spacing: Theme.space.sm + Theme.space.xxs

        Text {
            anchors.baseline: guideCount.baseline
            textFormat: Text.PlainText
            text: qsTr("Guides")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.title.size - 2
            font.weight: Font.DemiBold

            Accessible.role: Accessible.Heading
        }

        Text {
            id: guideCount

            textFormat: Text.PlainText
            text: qsTr("%n guide(s)", "", home.guides.length)
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }
    }

    GridLayout {
        visible: !home.searching
        width: parent.width
        columns: home.across
        columnSpacing: Theme.space.md + Theme.space.xxs
        rowSpacing: Theme.space.md + Theme.space.xxs
        uniformCellWidths: true
        uniformCellHeights: true

        Repeater {
            model: home.guides

            delegate: T.AbstractButton {
                id: guide

                required property var modelData
                readonly property bool faq: guide.modelData.layout === "faq"

                objectName: "helpGuideCard"
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitHeight: Theme.size.helpCardArt + guideBody.implicitHeight
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                onClicked: home.linkActivated("help:" + guide.modelData.slug)

                Accessible.role: Accessible.Link
                Accessible.name: guide.modelData.title ?? ""
                Accessible.description: guide.modelData.summary ?? ""

                background: Rectangle {
                    id: guideFace

                    radius: Theme.radius.xl
                    color: Theme.colors.bgOverlay
                    border.width: Theme.size.stroke
                    border.color: guide.hovered ? Theme.colors.strokeDefault : Theme.colors.strokeSubtle
                    clip: true

                    Rectangle {
                        width: parent.width
                        height: Theme.size.helpCardArt
                        color: Theme.colors.bgSunken

                        HelpArtHome {
                            anchors.centerIn: parent
                            kind: guide.modelData.slug
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: Theme.size.stroke
                            color: Theme.colors.strokeSubtle
                        }
                    }

                    StateLayer {
                        anchors.fill: parent
                        radius: guideFace.radius
                        hovered: guide.hovered
                        pressed: guide.down
                    }

                    FocusRing {
                        visible: guide.visualFocus
                        baseRadius: guideFace.radius
                    }
                }

                contentItem: Item {
                    Column {
                        id: guideBody

                        x: Theme.space.lg
                        y: Theme.size.helpCardArt + Theme.space.md + Theme.space.xxs
                        width: parent.width - 2 * Theme.space.lg
                        bottomPadding: Theme.space.md + Theme.space.xxs
                        spacing: Theme.space.xs + Theme.space.xxs

                        Row {
                            width: parent.width
                            spacing: Theme.space.sm + Theme.space.xxs

                            Tile {
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: guide.modelData.icon ?? ""
                                tone: guide.modelData.tile ?? ""
                                scale: 0.86
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - Theme.size.tile - parent.spacing
                                textFormat: Text.PlainText
                                text: guide.modelData.title ?? ""
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.heading.size
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: guide.modelData.summary ?? ""
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size + 0.5
                            lineHeight: Theme.type.caption.lineHeight + 3
                            lineHeightMode: Text.FixedHeight
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }

                        Item {
                            width: parent.width
                            height: guideMeta.implicitHeight + Theme.space.xs

                            Text {
                                id: guideMeta

                                y: Theme.space.xs
                                textFormat: Text.PlainText
                                text: guide.faq
                                      ? qsTr("%n question(s)", "", guide.modelData.count ?? 0) + "  ·  "
                                        + qsTr("%n group(s)", "", guide.modelData.groups ?? 0)
                                      : qsTr("%n section(s)", "", guide.modelData.count ?? 0) + "  ·  "
                                        + qsTr("About %n min", "", guide.modelData.minutes ?? 1)
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }

                            Icon {
                                anchors.right: parent.right
                                y: Theme.space.xs
                                name: "chevron-right"
                                size: Theme.size.infoIcon
                                color: guide.hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        visible: !home.searching
        width: parent.width
        height: Theme.space.md + Theme.space.xxs
    }

    GridLayout {
        visible: !home.searching
        width: parent.width
        columns: home.across
        columnSpacing: Theme.space.md + Theme.space.xxs
        rowSpacing: Theme.space.md + Theme.space.xxs

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Layout.horizontalStretchFactor: 5
            implicitHeight: popularColumn.implicitHeight + Theme.space.sm

            Column {
                id: popularColumn

                x: Theme.space.lg
                y: Theme.space.md + Theme.space.xxs
                width: parent.width - 2 * Theme.space.lg

                CardHeader {
                    iconName: "circle-help"
                    tone: "amber"
                    title: qsTr("Common questions")
                    linkText: qsTr("All")
                    onLinkClicked: home.linkActivated("help:faq")
                }

                Repeater {
                    model: home.popular

                    delegate: ListRow {
                        required property var modelData

                        objectName: "helpPopular"
                        width: popularColumn.width
                        label: modelData.title ?? ""
                        onClicked: home.linkActivated("help:" + modelData.slug + "#" + modelData.anchor)
                    }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Layout.horizontalStretchFactor: 4
            implicitHeight: shortcutColumn.implicitHeight + Theme.space.sm

            Column {
                id: shortcutColumn

                x: Theme.space.lg
                y: Theme.space.md + Theme.space.xxs
                width: parent.width - 2 * Theme.space.lg

                CardHeader {
                    iconName: "keyboard"
                    tone: "gray"
                    title: qsTr("Keyboard shortcuts")
                }

                Repeater {
                    model: [helpCommand, paletteCommand, fullScreenCommand, saveCommand]

                    delegate: Item {
                        id: shortcutRow

                        required property CommandState modelData

                        visible: shortcutRow.modelData.shortcut !== ""
                        width: shortcutColumn.width
                        height: visible ? Theme.size.listItem + Theme.space.xs : 0

                        Rectangle {
                            width: parent.width
                            height: Theme.size.stroke
                            color: Theme.colors.strokeSubtle
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - keycaps.width - Theme.space.sm
                            textFormat: Text.PlainText
                            text: shortcutRow.modelData.title
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.label.size
                            elide: Text.ElideRight
                        }

                        Row {
                            id: keycaps

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.space.xs

                            Repeater {
                                model: home.capsOf(shortcutRow.modelData)

                                Keycap {
                                    required property string modelData

                                    text: modelData
                                }
                            }
                        }
                    }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Layout.horizontalStretchFactor: 4
            Layout.columnSpan: home.across === 3 ? 1 : 2
            implicitHeight: apiColumn.implicitHeight + Theme.space.lg

            Column {
                id: apiColumn

                x: Theme.space.lg
                y: Theme.space.md + Theme.space.xxs
                width: parent.width - 2 * Theme.space.lg
                spacing: Theme.space.xs

                CardHeader {
                    iconName: "terminal"
                    tone: "rose"
                    title: qsTr("Script API")
                    linkText: qsTr("Overview")
                    onLinkClicked: home.linkActivated("help:api")
                }

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: qsTr("Made from the program itself, so it always matches your version.")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size + 0.5
                    lineHeight: Theme.type.caption.lineHeight + 3
                    lineHeightMode: Text.FixedHeight
                    wrapMode: Text.Wrap
                }

                Flow {
                    width: parent.width
                    topPadding: Theme.space.xs
                    spacing: Theme.space.xs + Theme.space.xxs

                    Repeater {
                        model: home.reference

                        delegate: T.AbstractButton {
                            id: space

                            required property var modelData

                            objectName: "helpNamespace"
                            implicitWidth: spaceText.implicitWidth + 2 * Theme.space.sm
                            implicitHeight: Theme.size.badge + Theme.space.sm - Theme.space.xxs
                            hoverEnabled: true
                            onClicked: home.linkActivated("help:" + space.modelData.slug)

                            Accessible.role: Accessible.Link
                            Accessible.name: spaceText.text

                            background: Rectangle {
                                radius: Theme.radius.sm
                                color: space.hovered ? Theme.colors.accentSubtle : Theme.colors.statePress

                                FocusRing {
                                    visible: space.visualFocus
                                    baseRadius: Theme.radius.sm
                                }
                            }

                            contentItem: Text {
                                id: spaceText

                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                textFormat: Text.PlainText
                                text: space.modelData.title ?? ""
                                color: space.hovered ? Theme.colors.accentText : Theme.colors.textPrimary
                                font.family: /^[A-Za-z_$][\w$]*$/.test(space.modelData.title ?? "") ? Theme.type.monoFamily
                                                                                                    : Application.font.family
                                font.pixelSize: Theme.type.caption.size
                            }
                        }
                    }
                }
            }
        }
    }
}
