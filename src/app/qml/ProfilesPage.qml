pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: page

    property string failure
    property string filter: "all"
    property string fresh
    property int tourBaseProfiles: 0
    readonly property var profiles: ProfileLibrary.entries
    readonly property var mirrors: DeviceManager.mirrors
    readonly property var running: runningApps("")
    readonly property string query: search.text.trim().toLowerCase()
    readonly property int gap: Theme.space.md + Theme.space.xxs
    readonly property int columns: Math.max(1, Math.min(3, Math.floor((content.width + gap) / (Theme.size.profileCard + gap))))
    readonly property real cardWidth: (content.width - (columns - 1) * gap) / columns
    readonly property var gameProfiles: profiles.filter(entry => running.includes(entry.app))
    readonly property var contributed: profiles.filter(entry => entry.readOnly)
    readonly property var troubled: profiles.filter(entry => entry.error !== "" || entry.warnings > 0)
    readonly property var shown: profiles.filter(entry => page.matches(entry) && page.passes(entry))
    readonly property var groups: {
        if (running.length === 0 || filter === "problems")
            return [{ title: "", entries: shown }]
        const result = []
        for (const app of running) {
            const members = shown.filter(entry => entry.app === app)
            if (members.length > 0)
                result.push({ title: app, subtitle: qsTr("Running on %1").arg(phonesOn(app)), game: true, entries: members })
        }
        const others = shown.filter(entry => !running.includes(entry.app))
        if (others.length > 0)
            result.push({ title: result.length > 0 ? qsTr("Other games") : "",
                          subtitle: qsTr("%n profile(s)", "", others.length), game: false, entries: others })
        return result
    }

    signal notify(string message, string tone)

    function failed(message: string) {
        if (ProfileLibrary.lastWriteBusy())
            page.notify(qsTr("Not saved: another program is using the file"), "bad")
        else
            page.failure = message
    }

    function importUrls(urls: var) {
        page.importChecked(urls, false)
    }

    function importChecked(urls: var, allowLogic: bool) {
        page.failure = ""
        const result = ProfileLibrary.importFiles(urls, allowLogic)
        if (result.failed.length > 0) {
            const first = result.failed[0]
            page.failure = qsTr("Could not import %1: %2").arg(first.file).arg(first.reason)
        }
        if (result.imported.length === 1)
            page.notify(qsTr("Imported %1").arg(result.imported[0]), "ok")
        else if (result.imported.length > 1)
            page.notify(qsTr("Imported %n profile(s)", "", result.imported.length), "ok")
        if (result.logic.length > 0)
            logicImport.ask(result.logic)
    }

    function usersOf(profileId: string): var {
        return page.mirrors.filter(mirror => mirror.profileId === profileId)
    }

    function runningApps(except: string): var {
        const apps = []
        for (const mirror of page.mirrors) {
            if (mirror.app !== "" && mirror.app !== except && !apps.includes(mirror.app))
                apps.push(mirror.app)
        }
        return apps
    }

    function phonesOn(app: string): string {
        return page.mirrors.filter(mirror => mirror.app === app).map(mirror => mirror.name)
                   .join(qsTr(", ", "list separator"))
    }

    function matches(entry: var): bool {
        return query === "" || entry.name.toLowerCase().includes(query) || entry.app.toLowerCase().includes(query)
    }

    function passes(entry: var): bool {
        if (filter === "game")
            return running.includes(entry.app)
        if (filter === "mine")
            return !entry.readOnly
        if (filter === "extension")
            return entry.readOnly
        if (filter === "problems")
            return entry.error !== "" || entry.warnings > 0
        return true
    }

    function conflictText(conflicts: var): string {
        if (conflicts.length !== 1)
            return qsTr("%n item(s) changed by more than one extension", "", conflicts.length)
        const by = conflicts[0].by
        if (by.length === 2)
            return qsTr("%1 and %2 both change %3").arg(by[0]).arg(by[1]).arg(conflicts[0].id)
        return qsTr("%1 all change %2").arg(by.join(qsTr(", ", "list separator"))).arg(conflicts[0].id)
    }

    function link(entry: var, app: string) {
        page.failure = ""
        if (!ProfileLibrary.setApp(entry.id, app))
            page.failed(qsTr("Could not change %1.").arg(entry.name))
        else if (app !== "")
            page.notify(qsTr("%1 will be used whenever %2 is open").arg(entry.name).arg(app), "ok")
    }

    function changedText(when: var): string {
        if (!(when instanceof Date) || isNaN(when.getTime()))
            return ""
        const now = new Date()
        const today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        const day = new Date(when.getFullYear(), when.getMonth(), when.getDate())
        const days = Math.round((today.getTime() - day.getTime()) / 86400000)
        const time = Qt.formatTime(when, "HH:mm")
        if (days === 0)
            return qsTr("Changed today %1").arg(time)
        if (days === 1)
            return qsTr("Changed yesterday %1").arg(time)
        return qsTr("Changed %1").arg(when.toLocaleDateString(Qt.locale(), Locale.ShortFormat))
    }

    function describe(entry: var): string {
        const parts = [entry.portrait ? qsTr("Portrait") : qsTr("Landscape")]
        const changed = changedText(entry.modified)
        if (changed !== "")
            parts.push(changed)
        return parts.join(" · ")
    }

    function duplicate(entry: var) {
        page.failure = ""
        if (entry.readOnly)
            copyToMine.ask("profile", entry.id, entry.name)
        else
            page.copyProfile(entry.id, entry.name, false)
    }

    function copyProfile(id: string, name: string, contributed: bool) {
        page.failure = ""
        if (ProfileLibrary.duplicate(id) === "")
            page.failed(qsTr("Could not duplicate %1.").arg(name))
        else if (contributed)
            page.notify(qsTr("Copied %1 to your profiles").arg(name), "ok")
    }

    function startExport(entry: var) {
        page.failure = ""
        exportDialog.profileId = entry.id
        exportDialog.profileName = entry.name
        exportDialog.packed = entry.folder ?? false
        exportDialog.selectedFile = ProfileLibrary.exportName(entry.id)
        exportDialog.open()
    }

    function confirmRemoval(entry: var) {
        page.failure = ""
        removeDialog.profileId = entry.id
        removeDialog.profileName = entry.name
        removeDialog.open()
    }

    function confirmRestore(entry: var) {
        page.failure = ""
        restoreDialog.profileId = entry.id
        restoreDialog.profileName = entry.name
        restoreDialog.extensionName = entry.extensionName ?? ""
        restoreDialog.open()
    }

    function startNewProfile() {
        page.failure = ""
        startChoices.failure = ""
        startDialog.open()
    }

    function showProfile(id: string) {
        page.filter = "all"
        search.clear()
        page.fresh = id
    }

    function showCreated(id: string, editing: bool) {
        if (editing)
            return
        page.filter = "all"
        search.clear()
        page.fresh = id
        const entry = page.profiles.find(each => each.id === id)
        if (entry !== undefined)
            page.notify(qsTr("Created %1").arg(entry.name), "ok")
    }

    function reveal(card: Item) {
        const top = card.mapToItem(groupList, 0, 0).y
        content.contentY = Math.max(0, Math.min(top - Theme.space.lg, content.contentHeight - content.height))
    }

    onVisibleChanged: {
        if (!visible)
            fresh = ""
    }

    onRunningChanged: {
        if (filter === "game" && running.length === 0)
            filter = "all"
    }

    onContributedChanged: {
        if ((filter === "mine" || filter === "extension") && contributed.length === 0)
            filter = "all"
    }

    Row {
        id: header

        x: Theme.size.panelPaddingH
        y: Theme.size.panelPaddingV
        spacing: Theme.space.lg

        Text {
            id: title

            textFormat: Text.PlainText
            text: qsTr("Profiles")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.display.size
            font.weight: Theme.type.display.weight
        }

        Text {
            anchors.baseline: title.baseline
            visible: page.profiles.length > 0
            textFormat: Text.PlainText
            text: qsTr("%n profile(s)", "", page.profiles.length)
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
            width: Theme.size.fieldWidth + Theme.space.xl
            visible: page.profiles.length > 0
            placeholderText: qsTr("Search profiles or games")
            Keys.onEscapePressed: clear()
        }

        Button {
            tone: "ghost"
            iconName: "folder-open"
            text: qsTr("Open folder")
            onClicked: ProfileLibrary.openFolder()
        }

        Button {
            objectName: "importProfile"
            iconName: "import"
            text: qsTr("Import…")
            onClicked: importDialog.open()
        }

        Button {
            objectName: "newProfile"
            visible: page.profiles.length > 0
            tone: "primary"
            iconName: "plus"
            text: qsTr("New profile…")
            onClicked: page.startNewProfile()
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
            visible: page.failure !== ""
            tone: "bad"
            title: page.failure
            actionText: qsTr("Dismiss")
            onActionTriggered: page.failure = ""
        }

        Row {
            visible: page.profiles.length > 0
            spacing: Theme.space.xs + Theme.space.xxs

            FilterChip {
                objectName: "allChip"
                text: qsTr("All")
                count: page.profiles.length
                selected: page.filter === "all"
                onClicked: page.filter = "all"
            }

            FilterChip {
                objectName: "gameChip"
                visible: page.running.length > 0
                iconName: "gamepad-2"
                text: qsTr("This game")
                count: page.gameProfiles.length
                selected: page.filter === "game"
                onClicked: page.filter = "game"
            }

            FilterChip {
                objectName: "mineChip"
                visible: page.contributed.length > 0
                text: qsTr("Mine")
                count: page.profiles.length - page.contributed.length
                selected: page.filter === "mine"
                onClicked: page.filter = "mine"
            }

            FilterChip {
                objectName: "extensionChip"
                visible: page.contributed.length > 0
                iconName: "puzzle"
                text: qsTr("From extensions")
                count: page.contributed.length
                selected: page.filter === "extension"
                onClicked: page.filter = "extension"
            }

            FilterChip {
                objectName: "problemChip"
                visible: page.troubled.length > 0
                text: qsTr("Problems")
                count: page.troubled.length
                selected: page.filter === "problems"
                onClicked: page.filter = "problems"
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
        anchors.topMargin: Theme.space.lg
        anchors.bottomMargin: Theme.size.panelPaddingV
        contentWidth: width
        contentHeight: Math.max(groupList.implicitHeight, empty.visible ? empty.implicitHeight : 0)
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: empty

            width: content.width
            visible: page.profiles.length === 0
            topPadding: Theme.space.x4l
            bottomPadding: Theme.space.x4l
            spacing: Theme.space.lg

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "keyboard"
                size: Theme.size.iconHero
                color: Theme.colors.textTertiary
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                textFormat: Text.PlainText
                text: qsTr("No profile yet")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.subtitle.size
                font.weight: Theme.type.subtitle.weight
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, empty.width)
                textFormat: Text.PlainText
                text: qsTr("Start from a template for your kind of game, import a profile file, or drop one into this window.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.space.sm

                Button {
                    objectName: "firstProfile"
                    tone: "primary"
                    iconName: "plus"
                    text: qsTr("New profile…")
                    onClicked: page.startNewProfile()
                }

                Button {
                    iconName: "import"
                    text: qsTr("Import…")
                    onClicked: importDialog.open()
                }

                Button {
                    tone: "ghost"
                    iconName: "folder-open"
                    text: qsTr("Open folder")
                    onClicked: ProfileLibrary.openFolder()
                }
            }
        }

        Column {
            id: groupList

            width: content.width
            spacing: Theme.space.xl

            Text {
                visible: page.profiles.length > 0 && page.shown.length === 0
                width: parent.width
                topPadding: Theme.space.x3l
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.PlainText
                text: page.query !== "" ? qsTr("No profile matches “%1”.").arg(search.text.trim())
                                        : qsTr("No profile here.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }

            Repeater {
                model: page.groups

                delegate: Column {
                    id: group

                    required property var modelData

                    width: groupList.width
                    visible: group.modelData.entries.length > 0
                    spacing: Theme.space.md

                    Row {
                        visible: group.modelData.title !== ""
                        spacing: Theme.space.sm + Theme.space.xxs

                        Tile {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: group.modelData.game ? "gamepad-2" : "layers"
                            tone: group.modelData.game ? "orange" : ""
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: group.modelData.title
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Font.DemiBold

                            Binding on font.family {
                                when: group.modelData.game === true
                                value: Theme.type.monoFamily
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: group.modelData.subtitle ?? ""
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                        }
                    }

                    Flow {
                        id: cards

                        width: parent.width
                        spacing: page.gap

                        Repeater {
                            model: group.modelData.entries

                            delegate: Card {
                                id: card

                                required property var modelData

                                readonly property bool usable: card.modelData.error === ""
                                readonly property bool readOnly: card.modelData.readOnly === true
                                readonly property var users: page.usersOf(card.modelData.id)
                                readonly property bool fresh: page.fresh !== "" && card.modelData.id === page.fresh

                                function reveal() {
                                    cards.forceLayout()
                                    group.forceLayout()
                                    groupList.forceLayout()
                                    page.reveal(card)
                                }

                                objectName: "profileCard"
                                width: page.cardWidth
                                active: card.users.length > 0 || card.fresh
                                padding: 0
                                horizontalPadding: 0
                                onFreshChanged: {
                                    if (card.fresh)
                                        Qt.callLater(card.reveal)
                                }
                                Component.onCompleted: {
                                    if (card.fresh)
                                        Qt.callLater(card.reveal)
                                }

                                TapHandler {
                                    objectName: "profileCardMenu"
                                    acceptedButtons: Qt.RightButton
                                    enabled: card.usable
                                    gesturePolicy: TapHandler.ReleaseWithinBounds
                                    onTapped: eventPoint => moreButton.openAt(card, eventPoint.position)
                                }

                                Column {
                                    width: card.availableWidth

                                    Rectangle {
                                        width: parent.width
                                        height: Theme.size.profileThumb
                                        topLeftRadius: Theme.radius.xl
                                        topRightRadius: Theme.radius.xl
                                        color: Theme.colors.bgSunken

                                        MappingPreview {
                                            anchors.centerIn: parent
                                            scale: Theme.size.profileThumb / Theme.size.previewShort * 0.85
                                            visible: card.usable
                                            markers: card.modelData.markers
                                            portrait: card.modelData.portrait
                                        }

                                        Icon {
                                            anchors.centerIn: parent
                                            visible: !card.usable
                                            name: "triangle-alert"
                                            size: Theme.size.iconHero
                                            color: Theme.colors.dangerText
                                        }

                                        StatusPill {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: Theme.space.sm
                                            visible: card.users.length > 0
                                            tone: "live"
                                            text: qsTr("In use · %1").arg(card.users.map(user => user.name).join(qsTr(", ", "list separator")))
                                        }

                                        StatusPill {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: Theme.space.sm
                                            visible: !card.usable
                                            tone: "bad"
                                            text: qsTr("Problem")
                                        }

                                        StatusPill {
                                            objectName: "readOnly"
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.margins: Theme.space.sm
                                            visible: card.readOnly
                                            text: qsTr("Read-only")
                                        }

                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            width: parent.width
                                            height: Theme.size.stroke
                                            color: Theme.colors.strokeSubtle
                                        }
                                    }

                                    ColumnLayout {
                                        x: Theme.space.md + Theme.space.xxs
                                        width: parent.width - 2 * x
                                        spacing: Theme.space.xxs

                                        Item {
                                            Layout.preferredHeight: Theme.space.md - Theme.space.xxs
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.space.sm

                                            Text {
                                                Layout.fillWidth: true
                                                textFormat: Text.PlainText
                                                text: card.modelData.name
                                                color: Theme.colors.textPrimary
                                                font.pixelSize: Theme.type.body.size
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }

                                            Text {
                                                visible: card.usable
                                                textFormat: Text.PlainText
                                                text: qsTr("%n control(s)", "", card.modelData.controls)
                                                color: Theme.colors.textTertiary
                                                font.family: Theme.type.monoFamily
                                                font.pixelSize: Theme.type.micro.size
                                            }
                                        }

                                        Text {
                                            objectName: "source"
                                            Layout.fillWidth: true
                                            visible: card.readOnly
                                            textFormat: Text.PlainText
                                            text: qsTr("From %1 · %2").arg(card.modelData.extensionName ?? "")
                                                                      .arg(card.modelData.version ?? "")
                                            color: Theme.colors.textSecondary
                                            font.pixelSize: Theme.type.caption.size
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            objectName: "userEdits"
                                            Layout.fillWidth: true
                                            visible: card.readOnly && (card.modelData.userEdits ?? 0) > 0
                                            textFormat: Text.PlainText
                                            text: qsTr("You changed %n item(s)", "", card.modelData.userEdits ?? 0)
                                            color: Theme.colors.accentText
                                            font.pixelSize: Theme.type.caption.size
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            textFormat: Text.PlainText
                                            text: card.usable ? page.describe(card.modelData) : card.modelData.error
                                            color: card.usable ? Theme.colors.textTertiary : Theme.colors.dangerText
                                            font.pixelSize: Theme.type.caption.size
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 3
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            visible: card.usable
                                            textFormat: Text.PlainText
                                            text: card.modelData.app !== "" ? card.modelData.app : qsTr("No game linked")
                                            color: Theme.colors.textTertiary
                                            font.pixelSize: Theme.type.caption.size
                                            elide: Text.ElideRight

                                            Binding on font.family {
                                                when: card.modelData.app !== ""
                                                value: Theme.type.monoFamily
                                            }
                                        }

                                        Text {
                                            objectName: "patchedBy"
                                            Layout.fillWidth: true
                                            visible: card.usable && (card.modelData.patchedBy ?? []).length > 0
                                            textFormat: Text.PlainText
                                            text: qsTr("Patched by %1").arg((card.modelData.patchedBy ?? [])
                                                                            .join(qsTr(", ", "list separator")))
                                            color: Theme.colors.textTertiary
                                            font.pixelSize: Theme.type.caption.size
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        Button {
                                            objectName: "profileDraft"
                                            Layout.fillWidth: true
                                            visible: (McpServer.draftProfiles[card.modelData.id] ?? "") !== ""
                                            small: true
                                            tone: "secondary"
                                            iconName: "bot"
                                            text: qsTr("%1's draft").arg(McpServer.draftProfiles[card.modelData.id] ?? "")
                                            toolTip: qsTr("See what %1 changed; nothing changes until you apply it")
                                                     .arg(McpServer.draftProfiles[card.modelData.id] ?? "")
                                            onClicked: DeviceManager.openDraftPreview("profile:" + card.modelData.id)
                                        }

                                        StatusPill {
                                            visible: card.usable && card.modelData.warnings > 0
                                            tone: "warn"
                                            text: qsTr("%n warning(s)", "", card.modelData.warnings)
                                        }

                                        StatusPill {
                                            objectName: "conflicts"
                                            visible: card.usable && (card.modelData.conflicts ?? []).length > 0
                                            tone: "warn"
                                            text: visible ? page.conflictText(card.modelData.conflicts) : ""
                                        }

                                        StatusPill {
                                            objectName: "userPatchProblem"
                                            visible: (card.modelData.userPatchProblem ?? "") !== ""
                                            tone: "warn"
                                            text: card.modelData.userPatchProblem ?? ""
                                        }

                                        Item {
                                            Layout.preferredHeight: Theme.space.sm
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.space.xs

                                            Button {
                                                id: useButton

                                                objectName: "useProfile"
                                                visible: card.usable
                                                enabled: page.mirrors.length > 1
                                                         || (page.mirrors.length === 1 && card.users.length === 0)
                                                small: true
                                                tone: card.users.length > 0 ? "secondary" : "primary"
                                                text: page.mirrors.length === 1 ? qsTr("Use on %1").arg(page.mirrors[0].name) : qsTr("Use on…")
                                                toolTip: page.mirrors.length === 0 ? qsTr("Start mirroring a phone first") : ""
                                                onClicked: {
                                                    if (page.mirrors.length === 1)
                                                        DeviceManager.useProfile(page.mirrors[0].serial, card.modelData.id)
                                                    else
                                                        deviceMenu.open()
                                                }

                                                Menu {
                                                    id: deviceMenu

                                                    y: useButton.height + Theme.space.xs

                                                    Instantiator {
                                                        model: page.mirrors

                                                        delegate: MenuItem {
                                                            required property var modelData

                                                            choice: true
                                                            chosen: modelData.profileId === card.modelData.id
                                                            text: modelData.name
                                                            onTriggered: DeviceManager.useProfile(modelData.serial, card.modelData.id)
                                                        }

                                                        onObjectAdded: (index, object) => deviceMenu.insertItem(index, object)
                                                        onObjectRemoved: (index, object) => deviceMenu.removeItem(object)
                                                    }
                                                }
                                            }

                                            Button {
                                                objectName: "duplicate"
                                                visible: card.usable
                                                small: true
                                                tone: "ghost"
                                                iconName: "copy"
                                                text: card.readOnly ? qsTr("Copy to mine") : qsTr("Duplicate")
                                                onClicked: page.duplicate(card.modelData)
                                            }

                                            Button {
                                                visible: !card.usable && !card.readOnly
                                                small: true
                                                tone: "ghost"
                                                iconName: "folder-open"
                                                text: qsTr("Show in folder")
                                                onClicked: ProfileLibrary.reveal(card.modelData.id)
                                            }

                                            Button {
                                                visible: !card.usable && !card.readOnly
                                                small: true
                                                tone: "ghost"
                                                iconName: "trash-2"
                                                text: qsTr("Delete…")
                                                onClicked: page.confirmRemoval(card.modelData)
                                            }

                                            Item {
                                                Layout.fillWidth: true
                                            }

                                            Button {
                                                id: moreButton

                                                objectName: "profileMore"

                                                function openAt(source: Item, at: point) {
                                                    const spot = source.mapToItem(moreButton, at.x, at.y)
                                                    moreMenu.x = spot.x
                                                    moreMenu.y = spot.y
                                                    moreMenu.open()
                                                }

                                                visible: card.usable
                                                small: true
                                                tone: "ghost"
                                                iconName: "ellipsis"
                                                toolTip: qsTr("More")
                                                onClicked: {
                                                    moreMenu.x = moreButton.width - moreMenu.width
                                                    moreMenu.y = moreButton.height + Theme.space.xs
                                                    moreMenu.open()
                                                }

                                                Menu {
                                                    id: moreMenu

                                                    Instantiator {
                                                        model: card.usable && !card.readOnly ? page.runningApps(card.modelData.app) : []

                                                        delegate: MenuItem {
                                                            required property string modelData

                                                            iconName: "link"
                                                            text: qsTr("Link to %1").arg(modelData)
                                                            onTriggered: page.link(card.modelData, modelData)
                                                        }

                                                        onObjectAdded: (index, object) => moreMenu.insertItem(index, object)
                                                        onObjectRemoved: (index, object) => moreMenu.removeItem(object)
                                                    }

                                                    MenuItem {
                                                        visible: card.usable && !card.readOnly && card.modelData.app !== ""
                                                        height: visible ? implicitHeight : 0
                                                        iconName: "unlink"
                                                        text: qsTr("Unlink from %1").arg(card.modelData.app)
                                                        onTriggered: page.link(card.modelData, "")
                                                    }

                                                    MenuSeparator {
                                                        visible: card.usable && !card.readOnly
                                                                 && (card.modelData.app !== ""
                                                                     || page.runningApps(card.modelData.app).length > 0)
                                                        height: visible ? implicitHeight : 0
                                                    }

                                                    MenuItem {
                                                        objectName: "visionResources"
                                                        iconName: "target"
                                                        text: qsTr("Vision resources…")
                                                        onTriggered: {
                                                            const problem = DeviceManager.openVisionWorkbench("", card.modelData.id)
                                                            if (problem !== "")
                                                                page.notify(problem, "warn")
                                                        }
                                                    }

                                                    MenuItem {
                                                        objectName: "editLogic"
                                                        iconName: "code-xml"
                                                        text: qsTr("Edit logic…")
                                                        onTriggered: {
                                                            const problem = DeviceManager.openLogic(card.modelData.id)
                                                            if (problem !== "")
                                                                page.notify(problem, "warn")
                                                        }
                                                    }

                                                    MenuItem {
                                                        iconName: "share"
                                                        text: qsTr("Export…")
                                                        onTriggered: page.startExport(card.modelData)
                                                    }

                                                    MenuItem {
                                                        objectName: "exportProfilePackage"
                                                        visible: !card.readOnly
                                                        height: visible ? implicitHeight : 0
                                                        iconName: "puzzle"
                                                        text: qsTr("Export as extension package…")
                                                        onTriggered: exportPackage.start("profile", card.modelData.id,
                                                                                         card.modelData.name)
                                                    }

                                                    MenuItem {
                                                        objectName: "restoreOriginal"
                                                        visible: card.readOnly && card.modelData.userPatch === true
                                                        height: visible ? implicitHeight : 0
                                                        iconName: "rotate-ccw"
                                                        text: qsTr("Restore original…")
                                                        onTriggered: page.confirmRestore(card.modelData)
                                                    }

                                                    MenuItem {
                                                        visible: !card.readOnly
                                                        height: visible ? implicitHeight : 0
                                                        iconName: "folder-open"
                                                        text: qsTr("Show in folder")
                                                        onTriggered: ProfileLibrary.reveal(card.modelData.id)
                                                    }

                                                    MenuSeparator {
                                                        visible: !card.readOnly
                                                        height: visible ? implicitHeight : 0
                                                    }

                                                    MenuItem {
                                                        visible: !card.readOnly
                                                        height: visible ? implicitHeight : 0
                                                        iconName: "trash-2"
                                                        danger: true
                                                        text: qsTr("Delete…")
                                                        onTriggered: page.confirmRemoval(card.modelData)
                                                    }
                                                }
                                            }
                                        }

                                        Item {
                                            Layout.preferredHeight: Theme.space.md
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

    FileDialog {
        id: importDialog

        title: qsTr("Import profiles")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Key mapping profiles (*.json *.gsprofile)")]
        onAccepted: page.importUrls(importDialog.selectedFiles)
    }

    FileDialog {
        id: exportDialog

        property string profileId
        property string profileName
        property bool packed

        title: qsTr("Export %1").arg(exportDialog.profileName)
        fileMode: FileDialog.SaveFile
        defaultSuffix: exportDialog.packed ? "gsprofile" : "json"
        nameFilters: exportDialog.packed
                     ? [qsTr("Profile with its logic and images (*.gsprofile)"), qsTr("Key mapping profile (*.json)")]
                     : [qsTr("Key mapping profile (*.json)")]
        onAccepted: {
            if (!ProfileLibrary.exportTo(exportDialog.profileId, exportDialog.selectedFile))
                page.failed(qsTr("Could not export %1.").arg(exportDialog.profileName))
        }
    }

    ExportPackageDialog {
        id: exportPackage

        onFinished: (ok, message) => {
            if (ok)
                page.notify(message, "ok")
            else
                page.failed(message)
        }
    }

    Dialog {
        id: removeDialog

        property string profileId
        property string profileName

        title: qsTr("Delete %1?").arg(removeDialog.profileName)
        acceptText: qsTr("Delete")
        danger: true
        onAccepted: {
            if (!ProfileLibrary.remove(removeDialog.profileId))
                page.failure = qsTr("Could not delete %1.").arg(removeDialog.profileName)
        }

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: qsTr("The file moves to the Recycle Bin. Phones using this profile stop using it.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }
    }

    RestoreDialog {
        id: restoreDialog

        property string profileId

        onAccepted: {
            if (!DeviceManager.restoreProfile(restoreDialog.profileId))
                page.failed(qsTr("Could not restore %1.").arg(restoreDialog.profileName))
        }
    }

    LogicImportDialog {
        id: logicImport

        objectName: "logicImportDialog"
        onConfirmed: urls => page.importChecked(urls, true)
    }

    CopyToMineDialog {
        id: copyToMine

        onConfirmed: (kind, source, sourceName) => page.copyProfile(source, sourceName, true)
    }

    NewProfileDialog {
        id: newProfileDialog

        objectName: "newProfileDialog"
        importable: true
        onImportRequested: importDialog.open()
        onCreated: (id, editing) => page.showCreated(id, editing)
    }

    Dialog {
        id: startDialog

        objectName: "startDialog"
        implicitWidth: parent ? Math.min(startChoices.implicitWidth, parent.width - 2 * Theme.space.xxl)
                              : startChoices.implicitWidth
        horizontalPadding: 0
        topPadding: 0
        bottomPadding: 0
        header: null
        footer: null
        background: null

        StartPanel {
            id: startChoices

            objectName: "startPanel"
            width: startDialog.availableWidth
            game: page.running.length > 0 ? page.running[0] : ""
            onTemplateRequested: {
                startDialog.close()
                newProfileDialog.start(startChoices.game)
            }
            onCommunityRequested: id => {
                startDialog.close()
                page.showProfile(id)
            }
            onImportRequested: {
                startDialog.close()
                importDialog.open()
            }
            onCreated: (id, editing) => {
                startDialog.close()
                page.showCreated(id, editing)
            }
            onCloseRequested: startDialog.close()
        }
    }

    TourOverlay {
        id: tour

        anchors.fill: parent
        objectName: "profilesTour"
        tourKey: "profiles"
        settings: AppSettings
        startWhen: page.visible && !AppSettings.toursQuiet
        version: page.profiles.length
        steps: [
            {
                centered: true,
                title: qsTr("Profiles"),
                text: qsTr("A profile is a set of keys: which key taps where on the screen and how it feels, "
                           + "optionally with logic and pictures to look for. A game can have several, and you can "
                           + "switch between them at any time.")
            },
            {
                target: function() { return tour.findItem(page, "newProfile") },
                title: qsTr("New profile"),
                text: qsTr("Start from a template or a blank one, or let the AI assistant build one with you. Then "
                           + "edit its keys in the mirror window.")
            },
            {
                target: search,
                title: qsTr("Search"),
                text: qsTr("Narrow the list down by profile or game name.")
            },
            {
                target: function() { return tour.findItem(page, "allChip") },
                title: qsTr("Filters"),
                text: qsTr("Show only the profiles for this game, the ones you made, the ones that came with "
                           + "extensions, or the ones with a problem to fix.")
            },
            {
                target: function() { return tour.findItem(page, "profileCard") },
                optional: true,
                title: qsTr("A profile card"),
                text: qsTr("The card tells which game the profile is for, which phone is using it and whether "
                           + "something needs fixing.")
            },
            {
                target: function() { return tour.findItem(page, "useProfile") },
                optional: true,
                title: qsTr("Use it on a phone"),
                text: qsTr("Use puts the profile on a phone that is mirroring. A profile linked to a game comes on "
                           + "by itself when the game opens.")
            },
            {
                target: function() { return tour.findItem(page, "profileMore") },
                optional: true,
                title: qsTr("More for a profile"),
                text: qsTr("Linking a game, vision resources, editing logic, exporting, restoring the original and "
                           + "deleting are in this menu.")
            },
            {
                target: function() { return tour.findItem(page, "importProfile") },
                title: qsTr("Import"),
                text: qsTr("Add a profile file someone shared with you; dropping the file on the window works too.")
            },
            {
                centered: true,
                title: qsTr("That's profiles"),
                text: qsTr("Share a profile you made by exporting it, or pack it as an extension. Help can replay "
                           + "this tour.")
            }
        ]
        advance: {
            switch (tour.step) {
            case 1:
                return page.profiles.length > page.tourBaseProfiles
            case 2:
                return page.query !== ""
            case 7:
                return importDialog.visible
            }
            return false
        }
        onStarted: page.tourBaseProfiles = page.profiles.length
    }

    function startTour() {
        tour.restart()
    }
}
