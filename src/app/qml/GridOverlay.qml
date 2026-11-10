pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: overlay

    property var cells: []
    readonly property var chosen: overlay.cells.find(entry => entry.target === true) ?? null
    readonly property real topInset: titleBar.height + toolbar.height
    readonly property int headerHeight: Theme.size.control
    readonly property int menuWidth: Theme.size.controlSmall + Theme.space.xs
    readonly property int gap: Theme.space.sm
    readonly property int margin: Theme.space.sm
    readonly property int inset: Theme.size.focusRing
    readonly property bool popupOpen: cellMenu.visible || syncDialog.visible
    readonly property rect noticeArea: notice.visible ? Qt.rect(notice.x, notice.y, notice.width, notice.height)
                                                      : Qt.rect(0, 0, 0, 0)
    readonly property var syncHues: ["azure", "teal", "rose", "lime", "amber", "orange"].filter(
                                        hue => Theme.toneHues[hue] !== Theme.accentName)

    signal releaseWanted()

    function toast(text, icon) {
        if (notice.visible)
            notice.close()
        notice.iconName = icon ?? ""
        notice.show(text, "info")
    }

    function cellOf(serial: string): var {
        return overlay.cells.find(entry => entry.serial === serial) ?? null
    }

    function hueOf(tone: int): string {
        return tone >= 0 && overlay.syncHues.length > 0 ? overlay.syncHues[tone % overlay.syncHues.length] : ""
    }

    function openMenu(button: Item, serial: string) {
        cellMenu.serial = serial
        const corner = button.mapToItem(overlay, button.width, button.height)
        cellMenu.x = corner.x - cellMenu.width
        cellMenu.y = corner.y + Theme.space.xs
        cellMenu.open()
    }

    function openSync(serial: string) {
        if (serial === "")
            return
        internal.syncSerial = serial
        syncDialog.show()
    }

    objectName: "gridOverlay"
    Window.onWindowChanged: internal.attachChrome()
    Component.onCompleted: internal.attachChrome()
    onCellsChanged: {
        if (cellMenu.visible && overlay.cellOf(cellMenu.serial) === null)
            cellMenu.close()
        if (syncDialog.visible && overlay.cellOf(internal.syncSerial) === null)
            syncDialog.close()
    }

    QtObject {
        id: internal

        property bool chromeAttached: false
        property string syncSerial

        function attachChrome() {
            if (chromeAttached || overlay.Window.window === null)
                return
            chrome.window = overlay.Window.window
            chromeAttached = chrome.attach()
        }
    }

    WindowChrome {
        id: chrome

        titleBar: titleBar
        minimizeButton: minimizeButton
        maximizeButton: maximizeButton
        closeButton: closeButton
        darkMode: Theme.dark
    }

    Binding {
        target: overlay.Window.window
        property: "color"
        value: Theme.colors.bgBase
        when: overlay.Window.window !== null
    }

    CommandState {
        id: muteCommand

        command: "device.mute"
        target: cellMenu.serial
    }

    Item {
        id: titleBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: minimizeButton.implicitHeight

        Icon {
            id: titleIcon

            anchors.left: parent.left
            anchors.leftMargin: Theme.space.md + Theme.space.xxs
            anchors.verticalCenter: parent.verticalCenter
            name: "layout-grid"
            size: Theme.size.iconSmall
            color: Theme.colors.textTertiary
        }

        Text {
            objectName: "gridTitle"
            anchors.left: titleIcon.right
            anchors.leftMargin: Theme.space.sm + Theme.space.xxs
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, captions.x - x - Theme.space.md)
            textFormat: Text.PlainText
            text: overlay.Window.window !== null ? overlay.Window.window.title : ""
            elide: Text.ElideRight
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
            font.weight: Font.Medium
        }

        Row {
            id: captions

            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height

            CaptionButton {
                id: minimizeButton

                glyph: "\uE921"
                onClicked: overlay.Window.window.showMinimized()
            }

            CaptionButton {
                id: maximizeButton

                glyph: overlay.Window.visibility === Window.Maximized ? "\uE923" : "\uE922"
                onClicked: {
                    if (overlay.Window.visibility === Window.Maximized)
                        overlay.Window.window.showNormal()
                    else
                        overlay.Window.window.showMaximized()
                }
            }

            CaptionButton {
                id: closeButton

                glyph: "\uE8BB"
                destructive: true
                onClicked: overlay.Window.window.close()
            }
        }
    }

    Item {
        id: toolbar

        readonly property real leftPadding: Theme.space.md + Theme.space.xxs
        readonly property real rightPadding: Theme.space.sm + Theme.space.xxs
        readonly property bool hintShown: width >= leftPadding + who.implicitWidth + Theme.space.md + actions.fixedWidth
                                                   + 2 * actions.spacing + hintSeparator.implicitWidth
                                                   + hint.implicitWidth + rightPadding

        objectName: "gridToolbar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        height: Theme.size.toolbar

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }

        Rectangle {
            id: who

            readonly property bool aimed: overlay.chosen !== null

            objectName: "gridTargetCapsule"
            anchors.left: parent.left
            anchors.leftMargin: toolbar.leftPadding
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: Theme.space.md + Theme.size.iconSmall + Theme.space.sm + whoText.implicitWidth
                           + (aimed ? Theme.space.xs + release.implicitWidth + Theme.space.xxs : Theme.space.md)
            width: Math.max(0, Math.min(implicitWidth, actions.x - x - Theme.space.md))
            height: Theme.size.chip
            radius: height / 2
            color: aimed ? Theme.colors.accentSubtle : Theme.colors.statePress
            clip: true

            Icon {
                id: whoIcon

                anchors.left: parent.left
                anchors.leftMargin: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                name: "keyboard"
                size: Theme.size.iconSmall
                color: who.aimed ? Theme.colors.accentText : Theme.colors.textTertiary
            }

            Text {
                id: whoText

                objectName: "gridTargetText"
                x: whoIcon.x + whoIcon.width + Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, (who.aimed ? release.x - Theme.space.xs : who.width - Theme.space.md) - x)
                textFormat: Text.PlainText
                text: who.aimed ? qsTr("Keys and mouse go to: %1").arg(overlay.chosen.name)
                                : qsTr("Click a cell to give it the keys and mouse")
                elide: Text.ElideRight
                color: who.aimed ? Theme.colors.accentText : Theme.colors.textSecondary
                font.pixelSize: Theme.type.label.size
                font.weight: who.aimed ? Font.DemiBold : Font.Medium
            }

            T.AbstractButton {
                id: release

                objectName: "gridRelease"
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.xxs
                anchors.verticalCenter: parent.verticalCenter
                visible: who.aimed
                implicitWidth: Theme.size.chip - 2 * Theme.space.xxs
                implicitHeight: implicitWidth
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                text: qsTr("Let go of the keys and mouse")
                Accessible.name: text
                onClicked: overlay.releaseWanted()

                Tooltip {
                    y: release.height + Theme.space.xs
                    text: release.text
                    visible: release.hovered && !release.down
                }

                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        name: "x"
                        size: Theme.size.iconSmall
                        color: Theme.colors.accentText
                    }
                }

                background: Rectangle {
                    id: releaseFace

                    radius: height / 2
                    color: "transparent"

                    StateLayer {
                        anchors.fill: parent
                        radius: releaseFace.radius
                        hovered: release.hovered
                        pressed: release.down
                    }

                    FocusRing {
                        visible: release.visualFocus
                        baseRadius: releaseFace.radius
                    }
                }
            }
        }

        Row {
            id: actions

            readonly property real fixedWidth: (syncCapsule.visible ? syncCapsule.implicitWidth + spacing : 0)
                                               + syncButton.implicitWidth + spacing + openButton.implicitWidth

            anchors.right: parent.right
            anchors.rightMargin: toolbar.rightPadding
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            T.AbstractButton {
                id: syncCapsule

                readonly property int followers: overlay.chosen?.syncCount ?? 0
                readonly property string hue: overlay.hueOf(overlay.chosen?.syncTone ?? -1)

                objectName: "gridSyncCapsule"
                anchors.verticalCenter: parent.verticalCenter
                visible: overlay.chosen?.syncRole === "master"
                implicitWidth: syncRow.implicitWidth + 2 * Theme.space.md
                implicitHeight: Theme.size.controlSmall
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                text: qsTr("Syncing · %n phone(s)", "", syncCapsule.followers)
                Accessible.name: text
                onClicked: overlay.openSync(overlay.chosen?.serial ?? "")

                Tooltip {
                    y: syncCapsule.height + Theme.space.xs
                    text: qsTr("Change or stop syncing")
                    visible: syncCapsule.hovered && !syncCapsule.down
                }

                contentItem: Item {
                    Row {
                        id: syncRow

                        anchors.centerIn: parent
                        spacing: Theme.space.xs + Theme.space.xxs

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.size.pillDot
                            height: width
                            radius: width / 2
                            color: Theme.toneInk(syncCapsule.hue)
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: syncCapsule.text
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.caption.size
                            font.weight: Font.Medium
                        }
                    }
                }

                background: Rectangle {
                    id: syncFace

                    radius: height / 2
                    color: Theme.toneFill(syncCapsule.hue)

                    StateLayer {
                        anchors.fill: parent
                        radius: syncFace.radius
                        hovered: syncCapsule.hovered
                        pressed: syncCapsule.down
                    }

                    FocusRing {
                        visible: syncCapsule.visualFocus
                        baseRadius: syncFace.radius
                    }
                }
            }

            Button {
                id: syncButton

                objectName: "gridSync"
                anchors.verticalCenter: parent.verticalCenter
                tone: "ghost"
                small: true
                iconName: "sync"
                text: qsTr("Sync…")
                enabled: overlay.chosen !== null && overlay.chosen.syncRole !== "follower"
                focusPolicy: Qt.TabFocus
                onClicked: overlay.openSync(overlay.chosen?.serial ?? "")
            }

            Button {
                id: openButton

                objectName: "gridOwnWindow"
                anchors.verticalCenter: parent.verticalCenter
                tone: "ghost"
                small: true
                iconName: "popout"
                text: qsTr("Open in its own window")
                enabled: overlay.chosen !== null
                focusPolicy: Qt.TabFocus
                onClicked: DeviceManager.showWindow(overlay.chosen?.serial ?? "")
            }

            ToolbarSeparator {
                id: hintSeparator

                anchors.verticalCenter: parent.verticalCenter
                visible: toolbar.hintShown
            }

            Text {
                id: hint

                objectName: "gridHint"
                anchors.verticalCenter: parent.verticalCenter
                visible: toolbar.hintShown
                textFormat: Text.PlainText
                text: qsTr("Click a cell to give it the keys and mouse · double-click to open it in its own window")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }
        }
    }

    Repeater {
        model: overlay.cells.length

        delegate: Item {
            id: cell

            required property int index
            readonly property var entry: overlay.cells[index] ?? ({})
            readonly property bool chosen: entry.target === true
            readonly property bool away: entry.separate === true
            readonly property real rate: entry.rate ?? -1

            objectName: "gridCell"
            x: entry.x ?? 0
            y: entry.y ?? 0
            width: entry.width ?? 0
            height: entry.height ?? 0

            Rectangle {
                objectName: "gridFrame"
                anchors.fill: parent
                radius: Theme.radius.lg
                color: Theme.colors.bgRaised
                border.width: cell.chosen ? Theme.size.focusRing : Theme.size.stroke
                border.color: cell.chosen ? Theme.colors.accent : Theme.colors.strokeSubtle
            }

            Rectangle {
                objectName: "gridPicture"
                x: overlay.inset
                y: overlay.headerHeight
                width: Math.max(0, parent.width - 2 * overlay.inset)
                height: Math.max(0, parent.height - overlay.headerHeight - overlay.inset)
                bottomLeftRadius: Math.max(0, Theme.radius.lg - overlay.inset)
                bottomRightRadius: Math.max(0, Theme.radius.lg - overlay.inset)
                color: Theme.colors.bgSunken
            }

            Item {
                objectName: "gridHeader"
                width: parent.width
                height: overlay.headerHeight

                Rectangle {
                    x: overlay.inset
                    anchors.bottom: parent.bottom
                    width: Math.max(0, parent.width - 2 * overlay.inset)
                    height: Theme.size.stroke
                    color: Theme.colors.strokeSubtle
                }

                Rectangle {
                    id: dot

                    objectName: "gridStatusDot"
                    x: Theme.space.md
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.size.pillDot
                    height: width
                    radius: width / 2
                    color: cell.entry.tone === "warn" ? Theme.colors.warning
                         : cell.entry.tone === "ok" ? Theme.colors.success : Theme.colors.textTertiary
                }

                Text {
                    id: nameText

                    objectName: "gridName"
                    x: dot.x + dot.width + Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, Math.min(implicitWidth,
                                                (statusText.visible ? statusText.x : moreButton.x) - x - Theme.space.sm
                                                - (rateText.visible ? rateText.implicitWidth + Theme.space.sm : 0)))
                    textFormat: Text.PlainText
                    text: cell.entry.name ?? ""
                    elide: Text.ElideRight
                    color: cell.chosen ? Theme.colors.accentText : Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.DemiBold
                }

                Text {
                    id: rateText

                    objectName: "gridRate"
                    x: nameText.x + nameText.width + Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: cell.away ? "—" : cell.rate >= 0 ? qsTr("%1 fps").arg(cell.rate) : ""
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    font.features: ({ "tnum": 1 })
                }

                Text {
                    id: statusText

                    objectName: "gridStatus"
                    anchors.right: moreButton.left
                    anchors.rightMargin: Theme.space.xs
                    anchors.verticalCenter: parent.verticalCenter
                    visible: text !== ""
                    width: Math.min(implicitWidth, parent.width / 3)
                    textFormat: Text.PlainText
                    text: cell.entry.status ?? ""
                    elide: Text.ElideRight
                    color: Theme.colors.warningText
                    font.pixelSize: Theme.type.caption.size
                }

                Button {
                    id: moreButton

                    objectName: "gridMore"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.xs
                    anchors.verticalCenter: parent.verticalCenter
                    tone: "ghost"
                    small: true
                    iconName: "ellipsis"
                    toolTip: qsTr("More")
                    focusPolicy: Qt.TabFocus
                    onClicked: overlay.openMenu(moreButton, cell.entry.serial ?? "")
                }
            }

            Column {
                objectName: "gridSeparate"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: overlay.headerHeight / 2
                width: Math.max(0, parent.width - 2 * Theme.space.md)
                visible: cell.away
                spacing: Theme.space.sm

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "popout"
                    size: Theme.size.iconLarge
                    color: Theme.colors.textTertiary
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: qsTr("In its own window")
                    wrapMode: Text.Wrap
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.Medium
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: qsTr("Double-click to bring it back here")
                    wrapMode: Text.Wrap
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }
    }

    Column {
        objectName: "gridEmpty"
        anchors.centerIn: parent
        anchors.verticalCenterOffset: overlay.topInset / 2
        visible: overlay.cells.length === 0
        spacing: Theme.space.lg

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "layout-grid"
            size: Theme.size.iconHero
            color: Theme.colors.textTertiary
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            textFormat: Text.PlainText
            text: qsTr("No phone is mirroring")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }
    }

    Menu {
        id: cellMenu

        property string serial
        readonly property var entry: overlay.cellOf(cellMenu.serial)
        readonly property string syncRole: cellMenu.entry?.syncRole ?? ""
        readonly property int followers: cellMenu.entry?.syncCount ?? 0

        objectName: "gridCellMenu"
        modal: true
        dim: false

        MenuItem {
            objectName: "gridMenuOwnWindow"
            iconName: "popout"
            text: qsTr("Open in its own window")
            onTriggered: DeviceManager.showWindow(cellMenu.serial)
        }

        CommandMenuItem {
            objectName: "gridMenuMute"
            commandState: muteCommand
        }

        MenuItem {
            objectName: "gridMenuSync"
            iconName: "sync"
            text: qsTr("Sync…")
            enabled: cellMenu.syncRole !== "follower"
            shortcut: cellMenu.syncRole === "follower" ? qsTr("Following %1").arg(cellMenu.entry?.syncLeader ?? "")
                    : cellMenu.syncRole === "master" ? qsTr("%n phone(s)", "", cellMenu.followers) : ""
            onTriggered: overlay.openSync(cellMenu.serial)
        }
    }

    SyncDialog {
        id: syncDialog

        serial: internal.syncSerial
        syncing: overlay.cellOf(internal.syncSerial)?.syncRole === "master"
        onNotified: message => overlay.toast(message, "triangle-alert")
    }

    Toast {
        id: notice

        capsule: true
        y: overlay.topInset + Theme.space.sm
    }
}
