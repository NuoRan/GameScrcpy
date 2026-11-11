pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App
import "ExtensionLook.js" as ExtensionLook

Item {
    id: page

    property string failure
    property string filter: ""
    property int tourBaseEnabled: 0
    readonly property var extensions: ExtensionHost.entries
    readonly property int extensionCount: page.extensions.length
    readonly property int loadedCount: page.extensions.filter(entry => entry.active).length
    readonly property int enabledCount: page.extensions.filter(entry => entry.enabled).length
    readonly property bool bisecting: ExtensionHost.bisect.active === true
    readonly property string query: search.text.trim().toLowerCase()
    readonly property var capabilities: ["filter", "theme", "profile", "script", "command", "preset", "hardware",
                                         "component", "nodes", "task", "panel"]
        .filter(kind => page.extensions.some(entry => ExtensionLook.capabilities(entry).includes(kind)))
    readonly property var shown: page.extensions.filter(entry => page.matches(entry)
                                                         && (page.filter === ""
                                                             || ExtensionLook.capabilities(entry).includes(page.filter)))
    readonly property int gap: Theme.space.md
    readonly property int columns: Math.max(1, Math.min(3, Math.floor((content.width + gap) / (Theme.size.extensionCard + gap))))
    readonly property real cardWidth: (content.width - (columns - 1) * gap) / columns

    signal packageChosen(string url)
    signal tourRequested(string key)

    Component.onCompleted: SettingsCatalog.scoped = false
    onVisibleChanged: {
        if (page.visible)
            SettingsCatalog.scoped = false
    }
    onCapabilitiesChanged: {
        if (page.filter !== "" && !page.capabilities.includes(page.filter))
            page.filter = ""
    }

    function pointName(point: string): string {
        switch (point) {
        case "profile":
            return qsTr("key mapping profiles")
        case "theme":
            return qsTr("themes")
        case "filter":
            return qsTr("video filters")
        case "script":
            return qsTr("scripts")
        case "command":
            return qsTr("commands")
        case "preset":
            return qsTr("presets")
        case "native":
            return qsTr("touch methods and system nodes from a DLL")
        case "component":
            return qsTr("components")
        case "task":
            return qsTr("tasks")
        case "panel":
            return qsTr("panels in GameScrcpy windows")
        case "help":
            return qsTr("help articles")
        case "tour":
            return qsTr("guided tours")
        default:
            return point
        }
    }

    function placeName(where: string): string {
        switch (where) {
        case "mirror":
            return qsTr("Mirror window side panel")
        case "menu":
            return qsTr("Right-click menu")
        case "status":
            return qsTr("Status bar")
        case "page":
            return qsTr("Main window page")
        case "device":
            return qsTr("Device settings on the devices page")
        case "settings":
            return qsTr("Section in Settings")
        case "extension":
            return qsTr("Its settings window")
        case "toolbar":
            return qsTr("Mirror window toolbar button")
        case "contextMenu":
            return qsTr("More menu of cards")
        case "tray":
            return qsTr("Tray menu")
        default:
            return where
        }
    }

    function nativeText(dll: var): string {
        if (dll === undefined || dll === null)
            return ""
        const lines = []
        if (dll.touchMethods.length > 0)
            lines.push(qsTr("Touch methods: %1").arg(dll.touchMethods.join(qsTr(", ", "list separator"))))
        if (dll.nodes.length > 0)
            lines.push(qsTr("System nodes: %1").arg(dll.nodes.join(qsTr(", ", "list separator"))))
        if ((dll.attachments ?? []).length > 0)
            lines.push(qsTr("Add-ons: %1").arg(dll.attachments.join(qsTr(", ", "list separator"))))
        if ((dll.behaviors ?? []).length > 0)
            lines.push(qsTr("Components: %1").arg(dll.behaviors.join(qsTr(", ", "list separator"))))
        if ((dll.speechEngines ?? []).length > 0)
            lines.push(qsTr("Speech engines: %1").arg(dll.speechEngines.join(qsTr(", ", "list separator"))))
        if ((dll.inputSources ?? []).length > 0)
            lines.push(qsTr("Input devices: %1").arg(dll.inputSources.join(qsTr(", ", "list separator"))))
        lines.push(dll.ownProcess
                   ? qsTr("Its DLL runs in a process of its own; when that stops three times within a minute, the "
                          + "extension is turned off.")
                   : qsTr("Its DLL runs inside GameScrcpy."))
        return lines.join("\n")
    }

    function pointLabel(point: string): string {
        switch (point) {
        case "profile":
            return qsTr("Profiles")
        case "theme":
            return qsTr("Theme")
        case "filter":
            return qsTr("Video filter")
        case "script":
            return qsTr("Scripts")
        case "command":
            return qsTr("Commands")
        case "preset":
            return qsTr("Presets")
        case "hardware":
            return qsTr("Hardware")
        case "nodes":
            return qsTr("System nodes")
        case "component":
            return qsTr("Components")
        case "task":
            return qsTr("Tasks")
        case "panel":
            return qsTr("Panels")
        case "help":
            return qsTr("Help")
        case "tour":
            return qsTr("Tours")
        default:
            return point
        }
    }

    function failureText(errors: var): string {
        switch (errors.length > 0 ? errors[0].code : "") {
        case "host-api-missing":
        case "host-api-version":
            return qsTr("Needs a newer GameScrcpy")
        case "dependency-missing":
            return qsTr("Needs an extension that is not installed")
        case "dependency-version":
        case "dependency-conflict":
            return qsTr("Needs a different version of another extension")
        case "filter-compile-failed":
            return qsTr("The video filter does not compile")
        case "texture-unreadable":
            return qsTr("A picture it needs could not be read")
        default:
            return qsTr("Could not load")
        }
    }

    function healthText(health: var): string {
        if ((health.off ?? "") === "stopped")
            return qsTr("Stopped working three times within a minute, so it was turned off")
        if ((health.off ?? "") === "modulesFailed")
            return qsTr("Failed three times within 10 seconds, so it was turned off")
        if ((health.recent ?? 0) > 0)
            return qsTr("Stopped working %n time(s) in the last 7 days", "", health.recent)
        return ""
    }

    function eventText(event: var): string {
        switch (event.kind) {
        case "stopped":
            return qsTr("Turned off after its process stopped three times within a minute")
        case "modulesFailed":
            return qsTr("Turned off after its attachments or behaviors failed three times within 10 seconds")
        default:
            if (event.code === "0x00000009")
                return qsTr("Its process did not answer in time and was stopped")
            return event.code !== "" ? qsTr("Its process stopped unexpectedly (%1)").arg(event.code)
                                     : qsTr("Its process stopped unexpectedly")
        }
    }

    function byline(entry: var): string {
        const parts = []
        if (entry.authors.length > 0)
            parts.push(qsTr("By %1").arg(entry.authors.join(qsTr(", ", "list separator"))))
        if (entry.license)
            parts.push(entry.license)
        return parts.join(" · ")
    }

    function matches(entry: var): bool {
        if (query === "")
            return true
        return [entry.name, entry.id, entry.description ?? "", entry.authors.join(" ")]
            .some(text => text.toLowerCase().includes(query))
    }

    function startExport(entry: var) {
        page.failure = ""
        exportDialog.extensionId = entry.id
        exportDialog.extensionVersion = entry.version
        exportDialog.extensionName = entry.name
        exportDialog.selectedFile = entry.id + "-" + entry.version + ".gsext"
        exportDialog.open()
    }

    function confirmRemoval(entry: var) {
        page.failure = ""
        removeDialog.extensionId = entry.id
        removeDialog.extensionVersion = entry.version
        removeDialog.extensionName = entry.name
        removeDialog.extensionFolder = entry.usable ? "" : entry.folder
        removeDialog.superseded = entry.superseded === true
        removeDialog.open()
    }

    function showDetails(entry: var) {
        detailsDialog.entry = entry
        detailsDialog.open()
    }

    function openSettings(entry: var) {
        if (entry.id === "gamescrcpy.esp32")
            Commands.trigger("esp32.settings", "")
        else
            settingsSheet.openFor(entry)
    }

    component Badge: Rectangle {
        id: badge

        property string tone
        property string iconName
        property string text

        implicitWidth: badgeRow.implicitWidth + 2 * Theme.space.xs + Theme.space.xxs
        implicitHeight: Theme.size.badge
        radius: Theme.radius.xs
        color: Theme.toneFill(badge.tone)

        Row {
            id: badgeRow

            anchors.centerIn: parent
            spacing: Theme.space.xs

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: badge.iconName !== ""
                name: badge.iconName
                size: Theme.size.infoIcon - Theme.space.xxs
                color: Theme.toneInk(badge.tone)
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: badge.text
                color: Theme.toneInk(badge.tone)
                font.pixelSize: Theme.type.micro.size
                font.weight: Theme.type.micro.weight
            }
        }
    }

    Row {
        id: header

        x: Theme.size.panelPaddingH
        y: Theme.size.panelPaddingV
        spacing: Theme.space.lg

        Text {
            id: title

            textFormat: Text.PlainText
            text: qsTr("Extensions")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.display.size
            font.weight: Theme.type.display.weight
        }

        Text {
            anchors.baseline: title.baseline
            visible: page.extensionCount > 0
            textFormat: Text.PlainText
            text: page.loadedCount > 0
                  ? qsTr("%n extension(s)", "", page.extensionCount) + " · "
                    + qsTr("%n loaded", "", page.loadedCount)
                  : qsTr("%n extension(s)", "", page.extensionCount)
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.body.size
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.verticalCenter: header.verticalCenter
        spacing: Theme.space.sm

        SearchField {
            id: search

            anchors.verticalCenter: parent.verticalCenter
            width: Theme.size.fieldWidth
            visible: page.extensionCount > 0
            placeholderText: qsTr("Search installed")
            Keys.onEscapePressed: clear()
        }

        Button {
            objectName: "findProblem"
            visible: page.enabledCount > 0 && ExtensionHost.mode !== "safeRequested"
            enabled: !ExtensionHost.installing && !page.bisecting
            tone: "ghost"
            iconName: "search"
            text: qsTr("Find a problem extension")
            onClicked: ExtensionHost.startBisect()
        }

        Button {
            objectName: "exportBundle"
            visible: page.enabledCount > 0
            enabled: !ExtensionHost.installing
            tone: "ghost"
            text: qsTr("Export bundle…")
            onClicked: bundleExport.open()
        }

        Button {
            objectName: "installExtension"
            enabled: !ExtensionHost.installing
            iconName: "import"
            text: ExtensionHost.installing ? qsTr("Installing…") : qsTr("Install from file…")
            onClicked: installDialog.open()
        }
    }

    Column {
        id: alerts

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: Theme.space.xl
        spacing: Theme.space.md

        InlineAlert {
            width: parent.width
            visible: ExtensionHost.mode !== "normal"
            tone: "warn"
            title: qsTr("No extension is loaded this time")
            detail: ExtensionHost.mode === "safeRequested"
                    ? qsTr("Started with --safe-mode. Start GameScrcpy without it to load extensions again.")
                    : qsTr("The last run ended while extensions were loaded. They load again at the next start; "
                           + "if it happens again, turn off the extension you installed last.")
            actionText: ExtensionHost.mode === "safeAfterCrash" && page.enabledCount > 0 ? qsTr("Find the extension") : ""
            onActionTriggered: ExtensionHost.startBisect()
        }

        InlineAlert {
            width: parent.width
            visible: page.failure !== ""
            tone: "bad"
            title: page.failure
            actionText: qsTr("Dismiss")
            onActionTriggered: page.failure = ""
        }

        Row {
            visible: page.extensionCount > 0 && page.capabilities.length > 1
            spacing: Theme.space.xs + Theme.space.xxs

            FilterChip {
                text: qsTr("All")
                count: page.extensionCount
                selected: page.filter === ""
                onClicked: page.filter = ""
            }

            Repeater {
                model: page.capabilities

                delegate: FilterChip {
                    required property string modelData

                    iconName: ExtensionLook.icon(modelData)
                    text: page.pointLabel(modelData)
                    count: page.extensions.filter(entry => ExtensionLook.capabilities(entry).includes(modelData)).length
                    selected: page.filter === modelData
                    onClicked: page.filter = modelData
                }
            }
        }
    }

    Flickable {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: alerts.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: alerts.height > 0 ? Theme.space.lg : 0
        anchors.bottomMargin: Theme.size.panelPaddingV
        contentWidth: width
        contentHeight: cards.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: cards

            width: content.width
            spacing: page.gap

            Column {
                id: empty

                width: cards.width
                visible: page.extensionCount === 0
                topPadding: Theme.space.x4l
                bottomPadding: Theme.space.x4l
                spacing: Theme.space.lg

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "puzzle"
                    size: Theme.size.iconHero
                    color: Theme.colors.textTertiary
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, empty.width)
                    textFormat: Text.PlainText
                    text: qsTr("No extension yet. Extensions add video filters and themes; install one from a .gsext package.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }

            Text {
                width: cards.width
                visible: page.extensionCount > 0 && page.shown.length === 0
                topPadding: Theme.space.x3l
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.PlainText
                text: page.query !== "" ? qsTr("No extension matches “%1”.").arg(search.text.trim())
                                        : qsTr("No extension here.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }

            GridLayout {
                objectName: "extensionGrid"
                width: cards.width
                visible: page.shown.length > 0
                columns: page.columns
                columnSpacing: page.gap
                rowSpacing: page.gap
                uniformCellHeights: true

                Repeater {
                    model: page.shown

                    delegate: Card {
                        id: card

                        required property var modelData

                        readonly property bool superseded: card.modelData.superseded === true
                        readonly property var errors: card.modelData.problems.filter(item => item.severity === "error")
                        readonly property bool failing: (!card.modelData.usable && !card.superseded) || card.errors.length > 0
                        readonly property var kinds: ExtensionLook.capabilities(card.modelData)
                        readonly property string lead: card.kinds.length > 0 ? card.kinds[0] : ""
                        readonly property string stateText: card.superseded ? qsTr("Not used")
                                                            : !card.modelData.usable ? ""
                                                            : !card.modelData.enabled ? qsTr("Off")
                                                            : !card.modelData.active && !card.failing ? qsTr("Not loaded")
                                                            : ""
                        readonly property var health: card.modelData.health ?? ({})
                        readonly property string healthText: page.healthText(card.health)
                        readonly property string noticeText: card.modelData.problems.some(item => item.code === "consent-required")
                                                             ? qsTr("Turn it on again to allow what's new")
                                                             : card.healthText

                        objectName: "extensionCard"
                        Layout.preferredWidth: page.cardWidth
                        Layout.fillHeight: true
                        padding: 0
                        horizontalPadding: 0

                        ColumnLayout {
                            width: card.availableWidth
                            height: card.availableHeight
                            spacing: Theme.space.sm + Theme.space.xxs

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.space.md + Theme.space.xxs
                                Layout.leftMargin: Theme.space.lg
                                Layout.rightMargin: Theme.space.lg
                                spacing: Theme.space.md

                                Tile {
                                    Layout.alignment: Qt.AlignTop
                                    iconName: ExtensionLook.icon(card.lead)
                                    tone: ExtensionLook.tone(card.lead)
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.space.xxs

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.space.xs + Theme.space.xxs

                                        Text {
                                            id: nameText

                                            Layout.fillWidth: true
                                            Layout.maximumWidth: Math.ceil(implicitWidth)
                                            textFormat: Text.PlainText
                                            text: card.modelData.name
                                            color: Theme.colors.textPrimary
                                            font.pixelSize: Theme.type.body.size
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight

                                            HoverHandler {
                                                id: nameHover
                                            }

                                            Tooltip {
                                                text: card.modelData.name
                                                visible: nameText.truncated && nameHover.hovered
                                            }
                                        }

                                        Text {
                                            visible: text !== ""
                                            textFormat: Text.PlainText
                                            text: card.modelData.version
                                            color: Theme.colors.textTertiary
                                            font.family: Theme.type.monoFamily
                                            font.pixelSize: Theme.type.micro.size
                                        }

                                        Badge {
                                            visible: card.modelData.builtin
                                            text: qsTr("Built-in")
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: text !== ""
                                        textFormat: Text.PlainText
                                        text: page.byline(card.modelData)
                                        color: Theme.colors.textTertiary
                                        font.pixelSize: Theme.type.caption.size
                                        elide: Text.ElideRight
                                    }
                                }

                                Switch {
                                    id: toggle

                                    objectName: "extensionToggle"
                                    Layout.alignment: Qt.AlignTop
                                    enabled: card.modelData.usable && !ExtensionHost.installing && !page.bisecting
                                    checked: card.modelData.enabled
                                    Accessible.name: card.modelData.name
                                    onToggled: {
                                        if (!ExtensionHost.enable(card.modelData.id, toggle.checked))
                                            toggle.checked = Qt.binding(() => card.modelData.enabled)
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.space.lg
                                Layout.rightMargin: Theme.space.lg
                                Layout.preferredHeight: 2 * Theme.type.label.lineHeight
                                textFormat: Text.PlainText
                                text: card.modelData.description || (card.failing ? qsTr("Open Details to see what went wrong.") : "")
                                color: card.modelData.description ? Theme.colors.textSecondary : Theme.colors.textTertiary
                                font.pixelSize: Theme.type.label.size
                                lineHeight: Theme.type.label.lineHeight
                                lineHeightMode: Text.FixedHeight
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            Flow {
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.space.lg
                                Layout.rightMargin: Theme.space.lg
                                visible: card.kinds.length > 0
                                spacing: Theme.space.xs + Theme.space.xxs

                                Repeater {
                                    model: card.kinds

                                    delegate: Badge {
                                        required property string modelData

                                        objectName: "capabilityBadge"
                                        tone: ExtensionLook.tone(modelData)
                                        iconName: ExtensionLook.icon(modelData)
                                        text: page.pointLabel(modelData)
                                    }
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.space.lg
                                Layout.rightMargin: Theme.space.lg
                                Layout.preferredHeight: Theme.size.stroke
                                color: Theme.colors.strokeSubtle
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.space.lg
                                Layout.rightMargin: Theme.space.md
                                Layout.bottomMargin: Theme.space.sm
                                Layout.minimumHeight: Theme.size.control
                                spacing: Theme.space.xs

                                Icon {
                                    name: card.failing ? "circle-x" : card.noticeText !== "" ? "triangle-alert" : "shield"
                                    size: Theme.size.infoIcon
                                    color: card.failing ? Theme.colors.dangerText
                                         : card.noticeText !== "" ? Theme.colors.warningText
                                         : Theme.colors.textTertiary
                                }

                                Text {
                                    objectName: "footNote"
                                    Layout.fillWidth: true
                                    textFormat: Text.PlainText
                                    text: card.failing
                                          ? page.failureText(card.errors)
                                          : card.noticeText !== ""
                                            ? card.noticeText
                                            : card.modelData.permissions.length > 0
                                              ? card.modelData.permissions.map(permission => permission.title).join(" · ")
                                              : qsTr("No extra permission")
                                    color: card.failing ? Theme.colors.dangerText
                                         : card.noticeText !== "" ? Theme.colors.warningText
                                         : Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.caption.size
                                    elide: Text.ElideRight
                                }

                                Text {
                                    visible: card.stateText !== ""
                                    textFormat: Text.PlainText
                                    text: card.stateText
                                    color: Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.caption.size
                                }

                                Button {
                                    objectName: "extensionSettings"
                                    visible: ExtensionLook.showsSettings(card.modelData,
                                                                         (HardwareAccess.grants[card.modelData.id] ?? []).length)
                                             || ExtensionPanels.panels.some(each => each.where === "extension"
                                                                                    && each.extension === card.modelData.id)
                                    small: true
                                    tone: "ghost"
                                    text: qsTr("Settings")
                                    Accessible.description: card.modelData.name
                                    onClicked: page.openSettings(card.modelData)
                                }

                                Button {
                                    objectName: "extensionDetails"
                                    visible: card.failing || card.noticeText !== "" || (card.health.events ?? []).length > 0
                                    small: true
                                    text: qsTr("Details")
                                    Accessible.description: card.modelData.name
                                    onClicked: page.showDetails(card.modelData)
                                }

                                Button {
                                    id: moreButton

                                    small: true
                                    tone: "ghost"
                                    iconName: "ellipsis"
                                    toolTip: qsTr("More")
                                    onClicked: moreMenu.open()

                                    Menu {
                                        id: moreMenu

                                        x: moreButton.width - width
                                        y: moreButton.height + Theme.space.xs

                                        MenuItem {
                                            objectName: "extensionDetailsItem"
                                            iconName: "info"
                                            text: qsTr("Details…")
                                            onTriggered: page.showDetails(card.modelData)
                                        }

                                        MenuItem {
                                            enabled: card.modelData.usable
                                            iconName: "share"
                                            text: qsTr("Export…")
                                            onTriggered: page.startExport(card.modelData)
                                        }

                                        MenuItem {
                                            visible: !card.modelData.builtin
                                            height: visible ? implicitHeight : 0
                                            enabled: !ExtensionHost.installing
                                            iconName: "trash-2"
                                            danger: true
                                            text: qsTr("Remove…")
                                            onTriggered: page.confirmRemoval(card.modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ExtensionSettingsSheet {
        id: settingsSheet
    }

    Dialog {
        id: detailsDialog

        property var entry: ({ name: "", description: "", points: [], permissions: [], problems: [], authors: [],
                               panels: [] })

        title: detailsDialog.entry.name
        acceptText: qsTr("Close")
        rejectText: ""

        ColumnLayout {
            width: parent.width
            spacing: Theme.space.md

            Text {
                Layout.fillWidth: true
                visible: text !== ""
                textFormat: Text.PlainText
                text: detailsDialog.entry.authors ? page.byline(detailsDialog.entry) : ""
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                visible: text !== ""
                textFormat: Text.PlainText
                text: detailsDialog.entry.description ?? ""
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                visible: (detailsDialog.entry.points ?? []).length > 0
                textFormat: Text.PlainText
                text: qsTr("Adds %1").arg((detailsDialog.entry.points ?? []).map(point => page.pointName(point))
                                          .join(qsTr(", ", "list separator")))
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "nativeDetails"
                Layout.fillWidth: true
                visible: text !== ""
                textFormat: Text.PlainText
                text: page.nativeText(detailsDialog.entry.native)
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: (detailsDialog.entry.permissions ?? []).length > 0
                spacing: Theme.space.xs

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Permissions")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: detailsDialog.entry.permissions ?? []

                    delegate: RowLayout {
                        id: permissionRow

                        required property var modelData

                        Layout.fillWidth: true
                        spacing: Theme.space.sm

                        Text {
                            Layout.alignment: Qt.AlignTop
                            textFormat: Text.PlainText
                            text: permissionRow.modelData.title
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.caption.size
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            textFormat: Text.PlainText
                            text: permissionRow.modelData.reason
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "extensionPanelList"
                Layout.fillWidth: true
                visible: (detailsDialog.entry.panels ?? []).length > 0
                spacing: Theme.space.xs

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Panels it adds")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: detailsDialog.entry.panels ?? []

                    delegate: RowLayout {
                        id: panelRow

                        required property var modelData
                        readonly property bool troubled: (panelRow.modelData.problem ?? "") !== ""

                        Layout.fillWidth: true
                        spacing: Theme.space.sm

                        Icon {
                            Layout.alignment: Qt.AlignTop
                            name: panelRow.troubled ? "triangle-alert"
                                  : ExtensionPanels.iconOf(panelRow.modelData.icon ?? "") || "panel-top"
                            customPath: panelRow.troubled ? "" : panelRow.modelData.iconPath ?? ""
                            size: Theme.size.iconSmall
                            color: panelRow.troubled ? Theme.colors.warningText : Theme.colors.textTertiary
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.xxs

                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: qsTr("%1 · %2").arg(panelRow.modelData.title)
                                                     .arg(page.placeName(panelRow.modelData.where))
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.caption.size
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: panelRow.troubled
                                textFormat: Text.PlainText
                                text: panelRow.modelData.problem ?? ""
                                color: Theme.colors.warningText
                                font.pixelSize: Theme.type.caption.size
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "extensionServiceList"
                Layout.fillWidth: true
                visible: (detailsDialog.entry.services ?? []).length > 0
                spacing: Theme.space.xs

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Services it offers to other extensions")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: detailsDialog.entry.services ?? []

                    delegate: ColumnLayout {
                        id: serviceRow

                        required property var modelData

                        objectName: "extensionServiceItem"
                        Layout.fillWidth: true
                        spacing: Theme.space.xxs

                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: serviceRow.modelData.title
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.caption.size
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: text !== ""
                            textFormat: Text.PlainText
                            text: serviceRow.modelData.description ?? ""
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: serviceRow.modelData.call
                            color: Theme.colors.textTertiary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: (detailsDialog.entry.problems ?? []).length > 0
                spacing: Theme.space.sm

                Repeater {
                    model: detailsDialog.entry.problems ?? []

                    delegate: RowLayout {
                        id: problemRow

                        required property var modelData
                        readonly property color tint: problemRow.modelData.severity === "error"
                                                      ? Theme.colors.dangerText
                                                      : problemRow.modelData.severity === "warning"
                                                        ? Theme.colors.warningText
                                                        : Theme.colors.infoText

                        Layout.fillWidth: true
                        spacing: Theme.space.sm

                        Icon {
                            Layout.alignment: Qt.AlignTop
                            name: problemRow.modelData.severity === "error"
                                  ? "circle-x"
                                  : problemRow.modelData.severity === "warning" ? "triangle-alert" : "info"
                            size: Theme.size.iconSmall
                            color: problemRow.tint
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.xxs

                            Text {
                                Layout.fillWidth: true
                                visible: text !== ""
                                textFormat: Text.PlainText
                                text: problemRow.modelData.detail
                                color: problemRow.tint
                                font.pixelSize: Theme.type.label.size
                                font.weight: Theme.type.label.weight
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: text !== ""
                                textFormat: Text.PlainText
                                text: problemRow.modelData.hint
                                color: Theme.colors.textSecondary
                                font.pixelSize: Theme.type.caption.size
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: problemRow.modelData.path
                                      ? problemRow.modelData.code + " · " + problemRow.modelData.path
                                      : problemRow.modelData.code
                                color: Theme.colors.textTertiary
                                font.family: Theme.type.monoFamily
                                font.pixelSize: Theme.type.caption.size
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            RowLayout {
                objectName: "localConnections"
                Layout.fillWidth: true
                visible: detailsDialog.entry.localConnections !== undefined
                spacing: Theme.space.sm

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.xxs

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: qsTr("Accept connections from programs on this computer")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }

                    Text {
                        readonly property var open: LocalServers.ports[detailsDialog.entry.id] ?? []

                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: open.length > 0 ? qsTr("Listening on %1").arg(open.map(port => "127.0.0.1:" + port).join(", "))
                                              : qsTr("Not listening now")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }
                }

                Switch {
                    objectName: "localConnectionsToggle"
                    checked: detailsDialog.entry.localConnections === true
                    Accessible.name: qsTr("Accept connections from programs on this computer")
                    onToggled: {
                        const key = detailsDialog.entry.key
                        ExtensionHost.setLocalConnections(detailsDialog.entry.id, checked)
                        detailsDialog.entry = ExtensionHost.entries.find(each => each.key === key) ?? detailsDialog.entry
                    }
                }
            }

            Flow {
                id: tourButtons

                readonly property var tours: ExtensionHost.tours.filter(tour => tour.extension === detailsDialog.entry.id)

                objectName: "extensionTours"
                Layout.fillWidth: true
                visible: tourButtons.tours.length > 0
                spacing: Theme.space.xs

                Repeater {
                    model: tourButtons.tours

                    delegate: Button {
                        required property var modelData

                        small: true
                        tone: "ghost"
                        iconName: "navigation"
                        text: qsTr("Tour: %1").arg(modelData.title)
                        onClicked: {
                            detailsDialog.close()
                            page.tourRequested(modelData.key)
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "extensionHealth"
                Layout.fillWidth: true
                visible: (detailsDialog.entry.health?.events ?? []).length > 0
                spacing: Theme.space.xs

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: qsTr("Health")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: Font.DemiBold
                    }

                    Button {
                        objectName: "clearHealth"
                        small: true
                        tone: "ghost"
                        text: qsTr("Clear the record")
                        onClicked: {
                            const key = detailsDialog.entry.key
                            ExtensionHost.clearHealth(detailsDialog.entry.id)
                            detailsDialog.entry = ExtensionHost.entries.find(each => each.key === key) ?? detailsDialog.entry
                        }
                    }
                }

                Repeater {
                    model: detailsDialog.entry.health?.events ?? []

                    delegate: RowLayout {
                        id: eventRow

                        required property var modelData

                        Layout.fillWidth: true
                        spacing: Theme.space.sm

                        Icon {
                            Layout.alignment: Qt.AlignTop
                            name: eventRow.modelData.kind === "crashed" ? "circle-x" : "power"
                            size: Theme.size.iconSmall
                            color: Theme.colors.warningText
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.xxs

                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: page.eventText(eventRow.modelData)
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.caption.size
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: [new Date(eventRow.modelData.at).toLocaleString(Qt.locale(), Locale.ShortFormat),
                                       eventRow.modelData.version,
                                       eventRow.modelData.report !== ""
                                           ? qsTr("crash report %1").arg(eventRow.modelData.report) : ""]
                                      .filter(part => part !== "").join(" · ")
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: installDialog

        title: qsTr("Install extension")
        nameFilters: [qsTr("GameScrcpy extensions and bundles (*.gsext *.gsbundle)")]
        onAccepted: page.packageChosen(installDialog.selectedFile.toString())
    }

    FileDialog {
        id: bundleExport

        title: qsTr("Export bundle")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "gsbundle"
        nameFilters: [qsTr("GameScrcpy bundle (*.gsbundle)")]
        onAccepted: {
            page.failure = ""
            const url = bundleExport.selectedFile.toString()
            const name = decodeURIComponent(url.substring(url.lastIndexOf("/") + 1)).replace(/\.gsbundle$/i, "")
            const ids = [...new Set(page.extensions.filter(entry => entry.enabled).map(entry => entry.id))]
            if (!ExtensionHost.exportBundle(ids, name, url))
                page.failure = qsTr("Could not export the bundle.")
        }
    }

    FileDialog {
        id: exportDialog

        property string extensionId
        property string extensionVersion
        property string extensionName

        title: qsTr("Export %1").arg(exportDialog.extensionName)
        fileMode: FileDialog.SaveFile
        defaultSuffix: "gsext"
        nameFilters: [qsTr("GameScrcpy extension (*.gsext)")]
        onAccepted: {
            if (!ExtensionHost.exportTo(exportDialog.extensionId, exportDialog.extensionVersion,
                                        exportDialog.selectedFile.toString()))
                page.failure = qsTr("Could not export %1.").arg(exportDialog.extensionName)
        }
    }

    Dialog {
        id: removeDialog

        property string extensionId
        property string extensionVersion
        property string extensionName
        property string extensionFolder
        property bool superseded

        title: qsTr("Remove %1?").arg(removeDialog.extensionName)
        acceptText: qsTr("Remove")
        danger: true
        onAccepted: {
            const removed = removeDialog.extensionFolder !== ""
                          ? ExtensionHost.removeFolder(removeDialog.extensionFolder)
                          : ExtensionHost.remove(removeDialog.extensionId, removeDialog.extensionVersion)
            if (!removed)
                page.failure = qsTr("Could not remove %1.").arg(removeDialog.extensionName)
        }

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: removeDialog.superseded
                  ? qsTr("This copy of version %1 is deleted from this computer. The built-in version stays.").arg(removeDialog.extensionVersion)
                  : qsTr("Version %1 is deleted from this computer. To use it again, install its .gsext package.").arg(removeDialog.extensionVersion)
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }
    }

    TourOverlay {
        id: tour

        anchors.fill: parent
        objectName: "extensionsTour"
        tourKey: "extensions"
        settings: AppSettings
        startWhen: page.visible && !AppSettings.toursQuiet
        version: page.extensions.length
        steps: [
            {
                centered: true,
                title: qsTr("Extensions"),
                text: qsTr("Extensions add to GameScrcpy: new components, picture filters, themes, hardware such as "
                           + "an ESP32 board, side panels, and new blocks for profiles and logic.")
            },
            {
                target: function() { return tour.findItem(page, "extensionCard") },
                optional: true,
                title: qsTr("An extension card"),
                text: qsTr("The card tells what the extension adds and what it may do. Built-in ones come with "
                           + "GameScrcpy.")
            },
            {
                target: function() { return tour.findItem(page, "extensionToggle") },
                optional: true,
                title: qsTr("Turn it on"),
                text: qsTr("An extension only loads once its switch is on, and the first time it asks what it may "
                           + "do.")
            },
            {
                target: function() { return tour.findItem(page, "extensionSettings") },
                optional: true,
                title: qsTr("Settings"),
                text: qsTr("Extensions that have settings are set up here.")
            },
            {
                target: function() { return tour.findItem(page, "extensionDetails") },
                optional: true,
                title: qsTr("Details"),
                text: qsTr("See everything it adds, the permissions it asked for and any problem it ran into.")
            },
            {
                target: function() { return tour.findItem(page, "installExtension") },
                title: qsTr("Install from file"),
                text: qsTr("Add an extension from a .gsext package. Before it installs, it shows what the extension "
                           + "may do.")
            },
            {
                target: function() { return tour.findItem(page, "findProblem") },
                optional: true,
                title: qsTr("Find a problem extension"),
                text: qsTr("When something goes wrong, this turns half of the extensions off at a time to find the "
                           + "one that causes it.")
            },
            {
                target: function() { return tour.findItem(page, "exportBundle") },
                optional: true,
                title: qsTr("Export bundle"),
                text: qsTr("Pack the extensions you use into one bundle file to set up another computer or share "
                           + "them.")
            },
            {
                centered: true,
                title: qsTr("That's extensions"),
                text: qsTr("Extensions only do what you allowed. Help can replay this tour.")
            }
        ]
        advance: {
            switch (tour.step) {
            case 2:
                return page.enabledCount !== page.tourBaseEnabled
            case 4:
                return settingsSheet.opened || detailsDialog.opened
            case 5:
                return installDialog.visible
            }
            return false
        }
        onStarted: page.tourBaseEnabled = page.enabledCount
    }

    function startTour() {
        tour.restart()
    }
}
