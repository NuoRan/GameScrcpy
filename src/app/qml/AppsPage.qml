pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: page

    required property string serial
    readonly property string selected: AppCatalog.current
    readonly property string selectedLabel: AppCatalog.currentApp.label ?? ""
    readonly property int iconSize: Theme.size.iconHero + Theme.space.md
    readonly property int cellMinimum: Theme.size.componentTile + Theme.space.x3l
    readonly property bool canLaunch: selected !== "" && AppCatalog.ready && AppCatalog.currentApp.enabled === true
    readonly property bool canManage: selected !== "" && AppCatalog.ready && !AppCatalog.busy

    signal back()

    function choose(packageName: string) {
        AppCatalog.current = packageName
    }

    function launch(independent: bool) {
        if (!canLaunch || (independent && !AppCatalog.independentDisplays))
            return
        AppCatalog.launch(selected, independent)
        notice.show(independent ? qsTr("Opening %1 on an independent display").arg(selectedLabel)
                                : qsTr("Starting %1 on the phone").arg(selectedLabel), "info")
    }

    function act(action: string, batch: bool) {
        const plan = AppCatalog.plan(action, batch)
        if ((plan.count ?? 0) === 0)
            return
        if (plan.confirm === true) {
            confirmDialog.plan = plan
            confirmDialog.open()
        } else {
            AppCatalog.run(action, plan.packages)
        }
    }

    function apksIn(urls: list<url>): int {
        let found = 0
        for (let i = 0; i < urls.length; ++i) {
            const text = urls[i].toString()
            if (text.startsWith("file:") && text.toLowerCase().endsWith(".apk"))
                ++found
        }
        return found
    }

    onSerialChanged: AppCatalog.open(serial)
    Component.onCompleted: AppCatalog.open(serial)
    Component.onDestruction: AppCatalog.close()

    Row {
        id: crumbs

        x: Theme.size.panelPaddingH - Theme.space.md
        y: Theme.size.panelPaddingV - Theme.space.sm
        spacing: Theme.space.xs

        Button {
            objectName: "appsBack"
            anchors.verticalCenter: parent.verticalCenter
            small: true
            tone: "ghost"
            iconName: "arrow-left"
            text: qsTr("Devices")
            onClicked: page.back()
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"
            size: Theme.size.iconSmall
            color: Theme.colors.textTertiary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: AppCatalog.phoneName
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.label.size
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"
            size: Theme.size.iconSmall
            color: Theme.colors.textTertiary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: qsTr("Apps")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: Font.Medium
        }
    }

    Row {
        id: header

        x: Theme.size.panelPaddingH
        anchors.top: crumbs.bottom
        anchors.topMargin: Theme.space.sm
        spacing: Theme.space.lg

        Text {
            id: title

            textFormat: Text.PlainText
            text: qsTr("Apps")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.display.size
            font.weight: Theme.type.display.weight
        }

        Text {
            objectName: "appCounts"
            anchors.baseline: title.baseline
            visible: AppCatalog.activity !== "" || AppCatalog.total > 0
            textFormat: Text.PlainText
            text: AppCatalog.activity !== "" ? AppCatalog.activity
                                             : qsTr("%n app(s)", "", AppCatalog.total) + " · "
                                               + qsTr("%n game(s)", "", AppCatalog.games)
            color: AppCatalog.activity !== "" ? Theme.colors.textSecondary : Theme.colors.textTertiary
            font.pixelSize: Theme.type.body.size
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.verticalCenter: header.verticalCenter
        spacing: Theme.space.sm

        SearchField {
            objectName: "appSearch"
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.size.fieldWidth + Theme.space.xl
            placeholderText: qsTr("Search apps or package names")
            onTextChanged: AppCatalog.query = text
            Keys.onEscapePressed: clear()
        }

        Button {
            objectName: "installApkButton"
            anchors.verticalCenter: parent.verticalCenter
            visible: !AppCatalog.selecting
            iconName: "import"
            text: qsTr("Install APK…")
            enabled: AppCatalog.ready
            onClicked: apkDialog.open()
        }

        Button {
            objectName: "selectButton"
            anchors.verticalCenter: parent.verticalCenter
            visible: !AppCatalog.selecting
            iconName: "circle-check"
            text: qsTr("Select")
            enabled: AppCatalog.ready && AppCatalog.total > 0
            onClicked: AppCatalog.selecting = true
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            visible: !AppCatalog.selecting
            spacing: Theme.size.stroke

            Button {
                objectName: "launchButton"
                tone: "primary"
                joined: "start"
                text: qsTr("Launch")
                enabled: page.canLaunch
                onClicked: page.launch(false)
            }

            Button {
                id: launchMore

                objectName: "launchMenuButton"
                tone: "primary"
                joined: "end"
                iconName: "chevron-down"
                description: qsTr("More actions")
                enabled: page.canLaunch || page.canManage
                onClicked: launchMenu.open()

                Menu {
                    id: launchMenu

                    objectName: "launchMenu"
                    x: launchMore.width - width
                    y: launchMore.height + Theme.space.xs

                    MenuItem {
                        objectName: "launchHere"
                        iconName: "play"
                        text: qsTr("Launch")
                        description: qsTr("Opens on the phone's screen")
                        enabled: page.canLaunch
                        onTriggered: page.launch(false)
                    }
                    MenuItem {
                        objectName: "launchIndependent"
                        iconName: "monitor-smartphone"
                        text: qsTr("Launch on an independent display")
                        description: qsTr("Opens in its own window; the phone's screen is not affected")
                        enabled: page.canLaunch && AppCatalog.independentDisplays
                        onTriggered: page.launch(true)
                    }
                    MenuSeparator {
                    }
                    MenuItem {
                        objectName: "launchForceStop"
                        visible: AppCatalog.currentApp.forceStop === true
                        height: visible ? implicitHeight : 0
                        iconName: "square"
                        text: qsTr("Force stop")
                        enabled: page.canManage
                        onTriggered: page.act("forceStop", false)
                    }
                    MenuItem {
                        objectName: "launchDisable"
                        visible: AppCatalog.currentApp.disable === true
                        height: visible ? implicitHeight : 0
                        iconName: "power"
                        text: qsTr("Disable")
                        enabled: page.canManage
                        onTriggered: page.act("disable", false)
                    }
                    MenuItem {
                        objectName: "launchEnable"
                        visible: AppCatalog.currentApp.enable === true
                        height: visible ? implicitHeight : 0
                        iconName: "power"
                        text: qsTr("Enable")
                        enabled: page.canManage
                        onTriggered: page.act("enable", false)
                    }
                    MenuItem {
                        objectName: "launchClearData"
                        visible: AppCatalog.currentApp.clearData === true
                        height: visible ? implicitHeight : 0
                        iconName: "rotate-ccw"
                        text: qsTr("Clear data…")
                        enabled: page.canManage
                        onTriggered: page.act("clearData", false)
                    }
                    MenuItem {
                        objectName: "launchUninstall"
                        visible: AppCatalog.currentApp.uninstall === true
                        height: visible ? implicitHeight : 0
                        iconName: "trash-2"
                        danger: true
                        text: qsTr("Uninstall…")
                        enabled: page.canManage
                        onTriggered: page.act("uninstall", false)
                    }
                }
            }
        }
    }

    Tabs {
        id: tabs

        objectName: "appTabs"
        x: Theme.size.panelPaddingH
        anchors.top: header.bottom
        anchors.topMargin: Theme.space.lg
        model: [qsTr("All"), { text: qsTr("Games"), count: AppCatalog.games }, qsTr("System")]
        currentIndex: AppCatalog.filter
        onActivated: index => AppCatalog.filter = index
    }

    InlineAlert {
        id: alert

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: Theme.space.md
        visible: AppCatalog.problem !== ""
        tone: "warn"
        title: AppCatalog.problem
        actionText: AppCatalog.canRetry ? qsTr("Try again") : ""
        onActionTriggered: AppCatalog.reload()
    }

    Item {
        id: selectionBar

        objectName: "selectionBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: alert.visible ? alert.bottom : tabs.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: Theme.space.md
        visible: AppCatalog.selecting
        height: Theme.size.control

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Text {
                objectName: "selectionCount"
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("%n app(s) selected", "", AppCatalog.checkedCount)
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.Medium
            }

            Button {
                objectName: "selectAll"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                text: qsTr("Select all")
                enabled: grid.count > 0
                onClicked: AppCatalog.checkAll()
            }

            Button {
                objectName: "selectNone"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                text: qsTr("Select none")
                enabled: AppCatalog.checkedCount > 0
                onClicked: AppCatalog.clearChecked()
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Button {
                objectName: "batchForceStop"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                iconName: "square"
                text: qsTr("Force stop")
                enabled: AppCatalog.ready && !AppCatalog.busy && (AppCatalog.eligible.forceStop ?? 0) > 0
                onClicked: page.act("forceStop", true)
            }

            Button {
                objectName: "batchDisable"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                iconName: "power"
                text: qsTr("Disable")
                enabled: AppCatalog.ready && !AppCatalog.busy && (AppCatalog.eligible.disable ?? 0) > 0
                onClicked: page.act("disable", true)
            }

            Button {
                objectName: "batchEnable"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                iconName: "power"
                text: qsTr("Enable")
                enabled: AppCatalog.ready && !AppCatalog.busy && (AppCatalog.eligible.enable ?? 0) > 0
                onClicked: page.act("enable", true)
            }

            Button {
                objectName: "batchClearData"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                iconName: "rotate-ccw"
                text: qsTr("Clear data…")
                enabled: AppCatalog.ready && !AppCatalog.busy && (AppCatalog.eligible.clearData ?? 0) > 0
                onClicked: page.act("clearData", true)
            }

            Button {
                objectName: "batchUninstall"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "danger"
                iconName: "trash-2"
                text: qsTr("Uninstall…")
                enabled: AppCatalog.ready && !AppCatalog.busy && (AppCatalog.eligible.uninstall ?? 0) > 0
                onClicked: page.act("uninstall", true)
            }

            Button {
                objectName: "selectionDone"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "primary"
                text: qsTr("Done")
                onClicked: AppCatalog.selecting = false
            }
        }
    }

    GridView {
        id: grid

        readonly property int columns: Math.max(1, Math.floor(width / page.cellMinimum))

        objectName: "appGrid"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: selectionBar.visible ? selectionBar.bottom : alert.visible ? alert.bottom : tabs.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: Theme.space.md
        anchors.bottomMargin: Theme.size.panelPaddingV
        cellWidth: Math.floor(width / columns)
        cellHeight: 2 * Theme.space.md + Theme.space.xxs + page.iconSize + Theme.space.sm + Theme.type.caption.lineHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: AppCatalog.apps

        delegate: Item {
            id: tile

            required property string packageName
            required property string label
            required property string iconUrl
            required property bool appEnabled
            required property bool checked
            readonly property bool chosen: AppCatalog.selecting ? checked : page.selected === packageName

            objectName: "appTile"
            width: grid.cellWidth
            height: grid.cellHeight
            Accessible.role: Accessible.ListItem
            Accessible.name: label
            Accessible.description: appEnabled ? "" : qsTr("Disabled")
            Accessible.selected: chosen
            Accessible.checkable: AppCatalog.selecting
            Accessible.checked: checked
            Accessible.onPressAction: {
                if (AppCatalog.selecting)
                    AppCatalog.toggleChecked(tile.packageName)
                else
                    page.choose(tile.packageName)
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: Theme.space.xxs
                radius: Theme.radius.lg
                color: tile.chosen ? Theme.colors.stateSelected : hover.hovered ? Theme.colors.stateHover
                                                                               : "transparent"
                border.width: tile.chosen ? Theme.size.stroke : 0
                border.color: Theme.colors.strokeDefault
            }

            Column {
                x: Theme.space.sm
                y: Theme.space.md + Theme.space.xxs
                width: tile.width - 2 * Theme.space.sm
                spacing: Theme.space.sm

                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: page.iconSize
                    height: page.iconSize

                    Rectangle {
                        anchors.fill: parent
                        visible: picture.status !== Image.Ready
                        opacity: tile.appEnabled ? 1 : Theme.fadedOpacity
                        radius: Theme.radius.xl
                        color: Theme.colors.bgSunken
                        border.width: Theme.size.stroke
                        border.color: Theme.colors.strokeSubtle

                        Icon {
                            anchors.centerIn: parent
                            name: "smartphone"
                            size: Theme.size.icon
                            color: Theme.colors.textTertiary
                        }
                    }

                    Image {
                        id: picture

                        anchors.fill: parent
                        opacity: tile.appEnabled ? 1 : Theme.fadedOpacity
                        source: tile.iconUrl
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        smooth: true
                        mipmap: true
                    }

                    Rectangle {
                        objectName: "disabledBadge"
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        visible: !tile.appEnabled
                        width: Theme.size.badge
                        height: Theme.size.badge
                        radius: width / 2
                        color: Theme.colors.bgElevated
                        border.width: Theme.size.stroke
                        border.color: Theme.colors.strokeDefault

                        Icon {
                            anchors.centerIn: parent
                            name: "power"
                            size: Theme.size.badge - Theme.space.sm
                            color: Theme.colors.textSecondary
                        }
                    }
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: tile.label
                    elide: Text.ElideRight
                    color: tile.appEnabled ? Theme.colors.textPrimary : Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }
            }

            Checkbox {
                objectName: "appCheck"
                x: Theme.space.sm
                y: Theme.space.sm
                visible: AppCatalog.selecting
                enabled: false
                opacity: 1
                checked: tile.checked
            }

            HoverHandler {
                id: hover
            }

            TapHandler {
                onTapped: {
                    if (AppCatalog.selecting)
                        AppCatalog.toggleChecked(tile.packageName)
                    else
                        page.choose(tile.packageName)
                }
                onDoubleTapped: {
                    if (AppCatalog.selecting)
                        return
                    page.choose(tile.packageName)
                    page.launch(false)
                }
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                enabled: !AppCatalog.selecting
                onTapped: {
                    page.choose(tile.packageName)
                    appMenu.popup()
                }
            }
        }
    }

    Text {
        anchors.centerIn: grid
        width: Math.min(implicitWidth, grid.width)
        visible: grid.count === 0 && AppCatalog.problem === ""
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        text: AppCatalog.loading ? qsTr("Reading the apps on the phone…")
                                 : AppCatalog.query.trim() !== "" ? qsTr("No app matches “%1”.").arg(AppCatalog.query.trim())
                                                                  : qsTr("No app here.")
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.body.size
        wrapMode: Text.Wrap
    }

    Menu {
        id: appMenu

        objectName: "appMenu"

        MenuItem {
            objectName: "appMenuLaunch"
            iconName: "play"
            text: qsTr("Launch")
            enabled: page.canLaunch
            onTriggered: page.launch(false)
        }
        MenuItem {
            objectName: "appMenuIndependent"
            iconName: "monitor-smartphone"
            text: qsTr("Launch on an independent display")
            enabled: page.canLaunch && AppCatalog.independentDisplays
            onTriggered: page.launch(true)
        }
        MenuSeparator {
        }
        MenuItem {
            objectName: "appMenuCopy"
            iconName: "copy"
            text: qsTr("Copy package name")
            enabled: page.selected !== ""
            onTriggered: AppCatalog.copyPackage(page.selected)
        }
        MenuSeparator {
        }
        MenuItem {
            objectName: "appMenuForceStop"
            visible: AppCatalog.currentApp.forceStop === true
            height: visible ? implicitHeight : 0
            iconName: "square"
            text: qsTr("Force stop")
            enabled: page.canManage
            onTriggered: page.act("forceStop", false)
        }
        MenuItem {
            objectName: "appMenuDisable"
            visible: AppCatalog.currentApp.disable === true
            height: visible ? implicitHeight : 0
            iconName: "power"
            text: qsTr("Disable")
            enabled: page.canManage
            onTriggered: page.act("disable", false)
        }
        MenuItem {
            objectName: "appMenuEnable"
            visible: AppCatalog.currentApp.enable === true
            height: visible ? implicitHeight : 0
            iconName: "power"
            text: qsTr("Enable")
            enabled: page.canManage
            onTriggered: page.act("enable", false)
        }
        MenuItem {
            objectName: "appMenuClearData"
            visible: AppCatalog.currentApp.clearData === true
            height: visible ? implicitHeight : 0
            iconName: "rotate-ccw"
            text: qsTr("Clear data…")
            enabled: page.canManage
            onTriggered: page.act("clearData", false)
        }
        MenuItem {
            objectName: "appMenuUninstall"
            visible: AppCatalog.currentApp.uninstall === true
            height: visible ? implicitHeight : 0
            iconName: "trash-2"
            danger: true
            text: qsTr("Uninstall…")
            enabled: page.canManage
            onTriggered: page.act("uninstall", false)
        }
    }

    Dialog {
        id: confirmDialog

        property var plan: ({})

        objectName: "appConfirmDialog"
        title: confirmDialog.plan.title ?? ""
        acceptText: confirmDialog.plan.accept ?? ""
        danger: confirmDialog.plan.danger === true
        onAccepted: AppCatalog.run(confirmDialog.plan.action ?? "", confirmDialog.plan.packages ?? [])

        Text {
            width: parent.width
            visible: text !== ""
            textFormat: Text.PlainText
            text: confirmDialog.plan.text ?? ""
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }
    }

    FileDialog {
        id: apkDialog

        title: qsTr("Install APK on %1").arg(AppCatalog.phoneName)
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Android apps (*.apk)")]
        onAccepted: AppCatalog.install(apkDialog.selectedFiles)
    }

    DropArea {
        id: apkDrop

        objectName: "apkDrop"
        anchors.fill: parent
        onEntered: drag => {
            if (!AppCatalog.ready || AppCatalog.selecting || page.apksIn(drag.urls) === 0)
                drag.accepted = false
            else
                drag.accept(Qt.CopyAction)
        }
        onDropped: drop => {
            if (page.apksIn(drop.urls) === 0)
                return
            drop.accept(Qt.CopyAction)
            AppCatalog.install(drop.urls)
        }

        Rectangle {
            anchors.fill: parent
            visible: apkDrop.containsDrag
            topLeftRadius: Theme.radius.xl
            color: Theme.colors.bgFrosted
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accentLine

            Column {
                anchors.centerIn: parent
                spacing: Theme.space.lg

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "import"
                    size: Theme.size.iconHero
                    color: Theme.colors.accentText
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    textFormat: Text.PlainText
                    text: qsTr("Drop to install on %1").arg(AppCatalog.phoneName)
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.subtitle.size
                    font.weight: Theme.type.subtitle.weight
                }
            }
        }
    }

    Connections {
        target: AppCatalog

        function onReported(text: string, tone: string) {
            notice.show(text, tone)
        }
    }

    Toast {
        id: notice
    }
}
