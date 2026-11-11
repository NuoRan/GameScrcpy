import QtQuick
import QtQuick.Dialogs
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Window {
    id: root

    width: 1180
    height: 760
    minimumWidth: 880
    minimumHeight: 560
    visible: false
    title: qsTr("GameScrcpy")
    color: Theme.colors.bgBase

    property bool wasMaximized: false
    property AiWindow assistantWindow: null
    readonly property var pageNames: ["devices", "profiles", "tasks", "extensions", "settings"]
    readonly property var extensionPages: ExtensionPanels.panels.filter(entry => entry.where === "page")
    readonly property bool esp32Enabled: ExtensionHost.entries.some(entry => entry.id === "gamescrcpy.esp32"
                                                                    && entry.enabled === true)

    onVisibilityChanged: {
        if (visibility === Window.Maximized || visibility === Window.Windowed)
            wasMaximized = visibility === Window.Maximized
    }

    onClosing: close => {
        if (!DeviceManager.hideToTrayOnClose()) {
            if (root.assistantWindow !== null)
                root.assistantWindow.close()
            return
        }
        close.accepted = false
        root.hide()
    }

    function bringForward() {
        if (!visible || visibility === Window.Minimized) {
            if (wasMaximized)
                showMaximized()
            else
                showNormal()
        }
        raise()
        requestActivate()
    }

    function attend(title: string, question: string) {
        const behind = !root.active
        root.bringForward()
        if (!behind)
            return
        root.alert(0)
        DeviceManager.notifyMain(title, question)
    }

    function confirmInstall(packageUrl: string) {
        installDialog.info = ExtensionHost.inspect(packageUrl)
        installDialog.packageUrl = packageUrl
        installDialog.source = ""
        installDialog.open()
    }

    function confirmDownloaded(packageUrl: string, source: string) {
        root.confirmInstall(packageUrl)
        installDialog.source = source
    }

    function showExtension(id: string) {
        rail.currentIndex = 3
        if (!extensionsPage.reveal(id))
            root.notify(qsTr("This extension is not installed"), "warn")
    }

    function confirmBundle(bundleUrl: string) {
        bundleDialog.info = ExtensionHost.inspectBundle(bundleUrl)
        bundleDialog.bundleUrl = bundleUrl
        bundleDialog.open()
    }

    function confirmPackage(url: string) {
        if (url.toLowerCase().endsWith(".gsbundle"))
            root.confirmBundle(url)
        else
            root.confirmInstall(url)
    }

    function notify(message: string, tone: string) {
        toast.show(message, tone)
    }

    function exportDiagnostics() {
        diagnosticsDialog.selectedFile = DiagnosticsBundle.suggestedName()
        diagnosticsDialog.open()
    }

    function showHelp(topic: string) {
        const pages = helpPage.parent.children
        for (let i = 0; i < pages.length; ++i) {
            if (pages[i] === helpPage) {
                rail.currentIndex = i
                break
            }
        }
        (helpPage.ensure() as HelpPage).open(topic)
    }

    function showExtensionPage(entry: var) {
        extensionPage.entryId = entry.id
        const pages = extensionPage.parent.children
        for (let i = 0; i < pages.length; ++i) {
            if (pages[i] === extensionPage) {
                rail.currentIndex = i
                break
            }
        }
    }

    function openEsp32Settings() {
        const sheet = esp32Settings.item as Esp32SettingsSheet
        if (sheet !== null)
            sheet.show()
    }

    function replayTour(key: string) {
        switch (key) {
        case "devices":
            AppSettings.resetTour(key)
            rail.currentIndex = 0
            Qt.callLater(() => devicesPage.startTour())
            break
        case "profiles":
            AppSettings.resetTour(key)
            rail.currentIndex = 1
            Qt.callLater(() => (profilesPage.ensure() as ProfilesPage).startTour())
            break
        case "tasks":
            AppSettings.resetTour(key)
            rail.currentIndex = 2
            Qt.callLater(() => (tasksPage.ensure() as TasksPage).startTour())
            break
        case "extensions":
            AppSettings.resetTour(key)
            rail.currentIndex = 3
            Qt.callLater(() => extensionsPage.startTour())
            break
        case "settings":
            AppSettings.resetTour(key)
            rail.currentIndex = 4
            Qt.callLater(() => (settingsPage.ensure() as SettingsPage).startTour())
            break
        case "assistant":
            AppSettings.resetTour(key)
            root.notify(qsTr("The tour plays the next time you open the AI assistant"))
            break
        case "mirror":
            AppSettings.resetTour(key)
            root.notify(qsTr("The tour plays the next time you open a mirror window"))
            break
        case "keymap":
            AppSettings.resetTour(key)
            root.notify(qsTr("The tour plays the next time you edit the key mapping"))
            break
        case "logic":
            AppSettings.resetTour(key)
            root.notify(qsTr("The tour plays the next time you open the logic view"))
            break
        case "all":
            AppSettings.resetTours()
            root.notify(qsTr("The tours play again the next time you open each of them"))
            break
        default: {
            const found = ExtensionHost.tours.find(tour => tour.key === key)
            if (found === undefined) {
                root.notify(qsTr("The extension of this tour is not on"))
                break
            }
            AppSettings.resetTour(key)
            extensionTour.entry = found
            extensionTour.restart()
            break
        }
        }
        root.bringForward()
    }

    function tourStep(step: var): var {
        const target = step.target ?? ""
        if (target === "")
            return { centered: true, title: step.title, text: step.text }
        return { target: () => root.tourTarget(target), title: step.title, text: step.text }
    }

    function tourTarget(target: string): Item {
        const extension = extensionTour.entry !== null ? extensionTour.entry.extension : ""
        if (target === "card") {
            rail.currentIndex = 3
            return root.findWhere(extensionsPage, item => item.objectName === "extensionCard"
                                                         && item.modelData?.id === extension)
        }
        if (target === "device") {
            rail.currentIndex = 0
            return root.findWhere(devicesPage, item => typeof item.serial === "string"
                                                      && item.serial.startsWith("box:" + extension + "/"))
        }
        if (target.startsWith("page:")) {
            const name = target.slice(5)
            return root.findWhere(extensionNav, item => item.objectName === "extensionNavItem"
                                                       && item.modelData?.extension === extension
                                                       && item.modelData?.name === name)
        }
        if (target.startsWith("settings:")) {
            const name = target.slice(9)
            rail.currentIndex = 4
            const page = settingsPage.ensure() as SettingsPage
            if (page === null)
                return null
            if (page.current !== "extensions")
                page.open("extensions")
            return root.findWhere(page, item => item.objectName === "extensionSettingsPanel"
                                               && item.panelEntry?.extension === extension
                                               && item.panelEntry?.name === name)
        }
        return null
    }

    function findWhere(from: Item, wanted: var): Item {
        if (from === null)
            return null
        if (wanted(from))
            return from
        const kids = from.children
        for (let index = 0; index < kids.length; ++index) {
            const found = root.findWhere(kids[index], wanted)
            if (found !== null)
                return found
        }
        return null
    }

    function playWaitingTour() {
        if (extensionTour.running || AppSettings.toursQuiet || !root.visible)
            return
        const waiting = ExtensionHost.tours.find(tour => tour.start === "enabled" && !AppSettings.tourSeen(tour.key))
        if (waiting === undefined)
            return
        extensionTour.entry = waiting
        extensionTour.restart()
    }

    Binding {
        target: AppSettings
        property: "toursQuiet"
        value: setupGuide.visible || sponsorDialog.visible || commandPalette.visible || about.visible
               || installDialog.visible || bundleDialog.visible || recordingDialog.visible
    }

    Binding {
        target: Theme
        property: "dark"
        value: AppSettings.theme === "system" ? Application.styleHints.colorScheme !== Qt.ColorScheme.Light
                                              : AppSettings.theme !== "light"
    }
    Binding {
        target: Theme
        property: "accentName"
        value: AppSettings.accent
    }
    Binding {
        target: Theme
        property: "reducedMotion"
        value: AppSettings.reducedMotion
    }
    Binding {
        target: Theme
        property: "reducedTransparency"
        value: AppSettings.reducedTransparency
    }
    Binding {
        target: Theme
        property: "palette"
        value: ExtensionHost.themes.find(theme => theme.key === AppSettings.palette) ?? ({})
    }

    Connections {
        target: SettingsCatalog

        function onOpenRequested() {
            rail.currentIndex = 4
        }
    }

    Connections {
        target: profilesPage.item as ProfilesPage

        function onNotify(message: string, tone: string) {
            root.notify(message, tone)
        }
    }

    Connections {
        target: tasksPage.item as TasksPage

        function onNotify(message: string, tone: string) {
            root.notify(message, tone)
        }
    }

    Connections {
        target: settingsPage.item as SettingsPage

        function onDiagnosticsRequested() {
            root.exportDiagnostics()
        }
    }

    Connections {
        target: helpPage.item as HelpPage

        function onGuideRequested() {
            setupGuide.show(1, "", "")
        }

        function onTourRequested(key: string) {
            root.replayTour(key)
        }

        function onSponsorRequested() {
            sponsorDialog.offer()
        }
    }

    Connections {
        target: AppSettings

        function onSaveBusy() {
            root.notify(qsTr("Not saved: another program is using the file"), "bad")
        }
    }

    Connections {
        target: ExtensionLinks

        function onExtensionWanted(id: string) {
            root.showExtension(id)
            root.bringForward()
        }

        function onPackageReady(fileUrl: string, source: string) {
            linkToast.close()
            rail.currentIndex = 3
            root.confirmDownloaded(fileUrl, source)
            root.bringForward()
        }

        function onFailed(message: string) {
            linkToast.close()
            root.notify(message, "bad")
            root.bringForward()
        }

        function onDownloadChanged() {
            if (ExtensionLinks.downloading && !linkToast.opened)
                linkToast.open()
            else if (!ExtensionLinks.downloading)
                linkToast.close()
        }
    }

    Connections {
        target: ExtensionHost

        function onInstallFinished(result: var) {
            ExtensionLinks.release()
            if (result.ok === true) {
                if (result.consentRequired === true)
                    root.notify(qsTr("Installed %1 %2. It needs more than before: turn it on in Extensions to allow it")
                                .arg(result.id).arg(result.version), "warn")
                else
                    root.notify(qsTr("Installed %1 %2").arg(result.id).arg(result.version), "ok")
                return
            }
            const problem = (result.problems ?? [])[0] ?? {}
            root.notify(problem.hint || problem.detail || problem.code || qsTr("Installation failed"), "bad")
        }

        function onBundleFinished(result: var) {
            const installed = result.installed ?? []
            if (result.ok === true) {
                root.notify(qsTr("Installed %n extension(s) from %1", "", installed.length).arg(result.name ?? ""), "ok")
                return
            }
            const problem = (result.problems ?? [])[0] ?? {}
            root.notify(problem.hint || problem.detail || problem.code || qsTr("Installation failed"), "bad")
        }

        function onSaveBusy() {
            root.notify(qsTr("Not saved: another program is using the file"), "bad")
        }

        function onNativeStopped(name: string) {
            root.notify(qsTr("%1 stopped working three times within a minute and was turned off").arg(name), "bad")
        }

        function onModulesFailed(name: string) {
            root.notify(qsTr("%1 failed three times within 10 seconds and was turned off").arg(name), "bad")
        }
    }

    Connections {
        target: DeviceManager

        function onFilterBudgetExceeded(serial: string, filter: string, frameMs: real) {
            const id = filter.split("/")[0]
            const entry = ExtensionHost.entries.find(item => item.id === id)
            budgetToast.extensionId = id
            budgetToast.show(qsTr("%1: video filters take %2 ms a frame").arg(entry?.name ?? id).arg(frameMs.toFixed(1)),
                             "warn")
        }

        function onUsbDriverReady(name: string) {
            root.notify(qsTr("The driver is installed: %1 can be controlled over AOA now").arg(name), "ok")
        }

        function onRecordingFinished(info: var) {
            recordingDialog.show(info)
            root.bringForward()
        }

        function onRecordingNotice(text: string, tone: string) {
            root.notify(text, tone)
        }

        function onMainWindowWanted() {
            root.bringForward()
        }

        function onTasksPageWanted(task: string) {
            Commands.trigger("hub.tasks")
            if (task !== "")
                (tasksPage.ensure() as TasksPage).chosen = task
        }

        function onExtensionStopped(name: string) {
            root.notify(qsTr("%1 stopped working three times within a minute and was turned off").arg(name), "bad")
        }
    }

    Connections {
        target: DiagnosticsBundle

        function onExportFinished(result: var) {
            if (result.ok === true)
                root.notify(qsTr("Diagnostics saved as %1").arg(result.name ?? ""), "ok")
            else
                root.notify(qsTr("Could not save the diagnostics"), "bad")
        }
    }

    Connections {
        target: SetupGuide

        function onOpenRequested(page: int, vendor: string, step: string) {
            setupGuide.show(page, vendor, step)
            root.bringForward()
        }
    }

    Connections {
        target: AiAssistant

        function onSettingsWanted() {
            const settings = settingsPage.ensure() as SettingsPage
            SettingsCatalog.scoped = false
            rail.currentIndex = 4
            settings.open("assistant")
            root.bringForward()
        }
    }

    Connections {
        target: AiChats

        function onWindowWanted() {
            if (root.assistantWindow === null) {
                root.assistantWindow = assistantWindowMaker.createObject(null) as AiWindow
                AiChats.windowOpen = root.assistantWindow !== null
                return
            }
            if (root.assistantWindow.visibility === Window.Minimized)
                root.assistantWindow.showNormal()
            root.assistantWindow.raise()
            root.assistantWindow.requestActivate()
        }

        function onPageWanted(page: string, section: string) {
            if (page === "help") {
                root.showHelp(section)
            } else {
                const index = root.pageNames.indexOf(page)
                if (index < 0)
                    return
                if (page === "settings") {
                    const settings = settingsPage.ensure() as SettingsPage
                    SettingsCatalog.scoped = false
                    rail.currentIndex = index
                    if (section !== "")
                        settings.open(section)
                } else {
                    rail.currentIndex = index
                }
            }
            root.bringForward()
        }
    }

    Connections {
        target: Commands

        function onUiRequested(id: string, target: string) {
            switch (id) {
            case "hub.devices":
                rail.currentIndex = 0
                break
            case "hub.profiles":
                rail.currentIndex = 1
                break
            case "hub.tasks":
                rail.currentIndex = 2
                root.bringForward()
                break
            case "hub.extensions":
                if (target === "gamescrcpy.esp32") {
                    root.openEsp32Settings()
                    break
                }
                rail.currentIndex = 3
                break
            case "esp32.settings":
                root.openEsp32Settings()
                break
            case "hub.settings":
                SettingsCatalog.scoped = false
                rail.currentIndex = 4
                break
            case "settings.search":
                if (rail.currentIndex !== 4) {
                    SettingsCatalog.scoped = false
                    rail.currentIndex = 4
                }
                (settingsPage.ensure() as SettingsPage).focusSearch()
                break
            case "wireless.pair":
                rail.currentIndex = 0
                devicesPage.startPairing()
                break
            case "app.about":
                about.open()
                break
            case "app.sponsor":
                sponsorDialog.offer()
                break
            case "app.help":
                root.showHelp(target)
                break
            case "app.guide":
                setupGuide.show(0, "", "")
                break
            case "app.commandPalette":
                commandPalette.open()
                break
            case "app.assistant":
                AiChats.toggle()
                return
            case "device.settings": {
                if (devicesPage.openCaptureSheet(target)) {
                    rail.currentIndex = 0
                    break
                }
                const device = Commands.device(target)
                if ((device.deviceId ?? "") === "")
                    return
                SettingsCatalog.openForDevice(device.deviceId, device.name)
                break
            }
            case "device.apps":
                rail.currentIndex = 0
                devicesPage.showApps(target)
                break
            case "device.files":
                rail.currentIndex = 0
                devicesPage.showFiles(target)
                break
            case "devices.grid":
                DeviceManager.showGrid()
                return
            case "devices.tile":
                DeviceManager.tileMirrors()
                return
            default:
                console.warn("no handler for the command", id)
                return
            }
            root.bringForward()
        }
    }

    WindowChrome {
        id: chrome
        window: root
        titleBar: titleBar
        minimizeButton: titleBar.minimizeButton
        maximizeButton: titleBar.maximizeButton
        closeButton: titleBar.closeButton
        darkMode: Theme.dark
    }

    TitleBar {
        id: titleBar
        window: root
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    NavRail {
        id: rail

        anchors.left: parent.left
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        model: [
            { text: qsTr("Devices"), icon: "smartphone" },
            { text: qsTr("Profiles"), icon: "keyboard" },
            { text: qsTr("Tasks"), icon: "bot" },
            { text: qsTr("Extensions"), icon: "puzzle" },
            { text: qsTr("Settings"), icon: "settings-2" }
        ]
        onActivated: index => {
            if (index === 4)
                SettingsCatalog.scoped = false
        }

        UiNavSection {
            id: extensionNav

            pages: root.extensionPages
            currentId: extensionPage.entryId
            active: pageHost.current === extensionPage
            maximumHeight: rail.availableHeight - rail.model.length * (Theme.size.navItemHeight + Theme.space.xs)
                           - helpItem.height - Theme.space.xl
            onPicked: entry => root.showExtensionPage(entry)
        }

        NavItem {
            id: helpItem

            text: qsTr("Help")
            iconName: "circle-help"
            current: pageHost.current === helpPage
            onClicked: helpMenu.open()

            Menu {
                id: helpMenu

                x: helpItem.width + Theme.space.xs
                y: helpItem.height - height

                CommandMenuItem { commandState: helpCommand }
                CommandMenuItem { commandState: guideCommand }
                CommandMenuItem { commandState: aboutCommand }
                CommandMenuItem { commandState: sponsorCommand }
            }
        }
    }

    CommandState {
        id: helpCommand

        command: "app.help"
    }

    CommandState {
        id: guideCommand

        command: "app.guide"
    }

    CommandState {
        id: aboutCommand

        command: "app.about"
    }

    CommandState {
        id: sponsorCommand

        command: "app.sponsor"
    }

    ContentPanel {
        id: panel

        anchors.left: rail.right
        anchors.top: titleBar.bottom
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        BisectBar {
            id: bisectBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.space.lg
        }

        PageHost {
            id: pageHost

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.top: bisectBar.visible ? bisectBar.bottom : parent.top
            currentIndex: rail.currentIndex

            DevicesPage {
                id: devicesPage
            }

            LazyPage {
                id: profilesPage

                page: Component {
                    ProfilesPage {}
                }
            }

            LazyPage {
                id: tasksPage

                page: Component {
                    TasksPage {}
                }
            }

            ExtensionsPage {
                id: extensionsPage
                onPackageChosen: packageUrl => root.confirmPackage(packageUrl)
                onTourRequested: key => root.replayTour(key)
            }

            LazyPage {
                id: settingsPage

                page: Component {
                    SettingsPage {}
                }
            }

            LazyPage {
                id: helpPage

                page: Component {
                    HelpPage {}
                }
            }

            UiPage {
                id: extensionPage
            }
        }
    }

    DropArea {
        id: packageDrop

        property bool profiles: false

        function packagesIn(urls: list<url>): list<string> {
            const found = []
            for (let i = 0; i < urls.length; ++i) {
                const candidate = urls[i].toString()
                const lower = candidate.toLowerCase()
                if (candidate.startsWith("file:") && (lower.endsWith(".gsext") || lower.endsWith(".gsbundle")))
                    found.push(candidate)
            }
            return found
        }

        function profilesIn(urls: list<url>): list<url> {
            const found = []
            for (let i = 0; i < urls.length; ++i) {
                if (ProfileLibrary.isProfileFile(urls[i]))
                    found.push(urls[i])
            }
            return found
        }

        anchors.fill: panel
        enabled: rail.currentIndex !== 0 || devicesPage.filesSerial === ""
        onEntered: drag => {
            packageDrop.profiles = packageDrop.profilesIn(drag.urls).length > 0
            if (!packageDrop.profiles && packageDrop.packagesIn(drag.urls).length === 0)
                drag.accepted = false
            else
                drag.accept(Qt.CopyAction)
        }
        onDropped: drop => {
            const profiles = packageDrop.profilesIn(drop.urls)
            if (profiles.length > 0) {
                drop.accept(Qt.CopyAction)
                rail.currentIndex = 1
                Qt.callLater(() => (profilesPage.ensure() as ProfilesPage).importUrls(profiles))
                return
            }
            const found = packageDrop.packagesIn(drop.urls)
            if (found.length === 0)
                return
            if (!found.every(url => ExtensionHost.isLocalFile(url))) {
                root.notify(qsTr("Only packages on this computer can be installed. Copy it to this computer first"), "warn")
                return
            }
            drop.accept(Qt.CopyAction)
            if (found.length > 1) {
                root.notify(qsTr("Drop one extension package at a time"), "warn")
                return
            }
            Qt.callLater(root.confirmPackage, found[0])
        }

        Rectangle {
            anchors.fill: parent
            visible: packageDrop.containsDrag
            topLeftRadius: Theme.radius.xl
            color: Theme.colors.bgFrosted
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accentLine

            Column {
                anchors.centerIn: parent
                spacing: Theme.space.lg

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: packageDrop.profiles ? "keyboard" : "puzzle"
                    size: Theme.size.iconHero
                    color: Theme.colors.accentText
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    textFormat: Text.PlainText
                    text: packageDrop.profiles ? qsTr("Drop to import the profile") : qsTr("Drop to install the extension")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.subtitle.size
                    font.weight: Theme.type.subtitle.weight
                }
            }
        }
    }

    CommandPalette {
        id: commandPalette

        backdrop: panel
    }

    Dialog {
        id: about

        title: qsTr("About GameScrcpy")
        acceptText: qsTr("Close")
        rejectText: ""
        alternativeText: qsTr("Sponsor")
        onAlternativeChosen: Qt.openUrlExternally(AppInfo.sponsorUrl)

        Column {
            width: parent.width
            spacing: Theme.space.sm

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("Version %1 · %2").arg(AppInfo.version).arg(AppInfo.revision)
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
            }

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("Android mirroring, key mapping and automation for Windows. Open source under the Apache License 2.0.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
                wrapMode: Text.Wrap
            }
        }
    }

    GuideOverlay {
        id: setupGuide
    }

    SponsorDialog {
        id: sponsorDialog
    }

    InstallDialog {
        id: installDialog
    }

    BundleDialog {
        id: bundleDialog
    }

    RecordingDialog {
        id: recordingDialog

        objectName: "recordingDialog"
        onNotified: message => root.notify(message, "bad")
    }

    McpPrompts {
        onAttentionWanted: question => root.attend(qsTr("An AI connection is waiting for you"), question)
    }

    HardwarePicker {
        onAttentionWanted: question => root.attend(qsTr("A device permission is waiting for you"), question)
    }

    Loader {
        id: esp32Settings

        objectName: "esp32SettingsLoader"
        active: root.esp32Enabled
        sourceComponent: Esp32SettingsSheet {}
    }

    Toast {
        id: toast
    }

    Toast {
        id: linkToast

        objectName: "linkDownload"
        y: parent ? parent.height - height - Theme.space.xxl - (toast.visible ? toast.height + Theme.space.sm : 0) : 0
        tone: "info"
        text: ExtensionLinks.progress >= 0
              ? qsTr("Downloading an extension from %1 · %2%").arg(ExtensionLinks.source)
                                                              .arg(Math.round(ExtensionLinks.progress * 100))
              : qsTr("Downloading an extension from %1…").arg(ExtensionLinks.source)
        actionText: qsTr("Cancel")
        onActionTriggered: ExtensionLinks.cancel()
    }

    Toast {
        id: budgetToast

        property string extensionId

        y: parent ? parent.height - height - Theme.space.xxl - (toast.visible ? toast.height + Theme.space.sm : 0) : 0
        actionText: qsTr("Turn off")
        onActionTriggered: ExtensionHost.enable(budgetToast.extensionId, false)
    }

    Toast {
        id: crashToast

        objectName: "crashNotice"
        y: parent ? parent.height - height - Theme.space.xxl - (toast.visible ? toast.height + Theme.space.sm : 0) : 0
        tone: "warn"
        text: qsTr("GameScrcpy closed unexpectedly last time.")
        actionText: qsTr("Export diagnostics…")
        onActionTriggered: root.exportDiagnostics()
        onClosed: CrashReport.acknowledge()
    }

    FileDialog {
        id: diagnosticsDialog

        title: qsTr("Export diagnostics")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTr("Zip archives (*.zip)")]
        onAccepted: DiagnosticsBundle.exportTo(diagnosticsDialog.selectedFile.toString())
    }

    Component {
        id: assistantWindowMaker

        AiWindow {}
    }

    AiCorner {
        objectName: "hubAiCorner"
        window: "main"
        context: ({
            page: pageHost.current === helpPage ? "help"
                : pageHost.current === extensionPage ? "extension " + extensionPage.entryId
                : root.pageNames[rail.currentIndex] ?? ""
        })
    }

    TourOverlay {
        id: extensionTour

        property var entry: null

        anchors.left: rail.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        objectName: "extensionTour"
        tourKey: extensionTour.entry !== null ? extensionTour.entry.key : ""
        settings: AppSettings
        steps: extensionTour.entry !== null ? extensionTour.entry.steps.map(step => root.tourStep(step)) : []
        onEnded: Qt.callLater(root.playWaitingTour)
    }

    Connections {
        target: ExtensionHost

        function onExtensionsChanged() {
            Qt.callLater(root.playWaitingTour)
        }
    }

    Connections {
        target: AppSettings

        function onToursQuietChanged() {
            Qt.callLater(root.playWaitingTour)
        }
    }

    Component.onCompleted: {
        chrome.attach()
        if (AppSettings.placeWindow(root))
            root.showMaximized()
        else
            root.visible = true
        if (!AppSettings.setupGuideShown)
            Qt.callLater(setupGuide.show, 0, "", "")
        else if (AppSettings.sponsorMilestone > 0)
            Qt.callLater(sponsorDialog.ask, AppSettings.sponsorMilestone)
        if (CrashReport.pending)
            crashToast.open()
        Qt.callLater(root.playWaitingTour)
    }
}
