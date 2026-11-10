pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: page

    property string appsSerial
    property string filesSerial
    readonly property bool listShown: appsSerial === "" && filesSerial === ""

    onListShownChanged: {
        if (!page.listShown)
            Qt.callLater(tour.cancel)
    }

    function showApps(serial: string) {
        filesSerial = ""
        appsSerial = serial
    }

    function showFiles(serial: string) {
        appsSerial = ""
        filesSerial = serial
    }

    function startPairing() {
        appsSerial = ""
        filesSerial = ""
        AppSettings.wirelessCollapsed = false
        Qt.callLater(() => {
            list.positionViewAtEnd()
            const footer = list.footerItem as DevicesFooter
            if (footer)
                footer.wireless.focusPairing()
        })
    }

    function openCaptureSheet(serial: string): bool {
        return captureSheet.show(serial)
    }

    component DevicesFooter: Column {
        id: footer

        required property bool listed
        property alias wireless: wirelessSection
        readonly property var others: DeviceManager.otherVideoDevices

        topPadding: footer.listed && footer.others.length > 0 ? Theme.space.sm + Theme.space.xxs : 0
        spacing: Theme.space.xxl + Theme.space.xxs

        Card {
            id: otherVideo

            objectName: "otherVideo"
            width: footer.width
            visible: footer.others.length > 0
            topPadding: Theme.space.sm + Theme.space.xxs
            bottomPadding: Theme.space.sm + Theme.space.xxs
            horizontalPadding: Theme.space.lg

            ColumnLayout {
                width: otherVideo.availableWidth
                spacing: Theme.space.sm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.md

                    Tile {
                        iconName: "camera"
                    }

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Other video devices")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        font.weight: Font.DemiBold
                    }

                    Text {
                        objectName: "otherVideoNames"
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: {
                            const count = footer.others.length
                            const names = footer.others.map(other => other.name).join(qsTr(", ", "list separator"))
                            return footer.others.every(other => other.hidden === true)
                                   ? qsTr("%1 · hidden by you").arg(names)
                                   : qsTr("%1 · don't look like capture cards", "", count).arg(names)
                        }
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        elide: Text.ElideRight
                    }

                    Button {
                        objectName: "otherVideoToggle"
                        small: true
                        tone: "ghost"
                        text: AppSettings.otherVideoExpanded ? qsTr("Hide") : qsTr("Show")
                        onClicked: AppSettings.otherVideoExpanded = !AppSettings.otherVideoExpanded
                    }
                }

                Repeater {
                    model: AppSettings.otherVideoExpanded ? footer.others : []

                    delegate: Item {
                        id: entry

                        required property var modelData

                        objectName: "otherVideoDevice"
                        Layout.fillWidth: true
                        implicitHeight: entryRow.implicitHeight + Theme.space.sm

                        Rectangle {
                            width: parent.width
                            height: Theme.size.stroke
                            color: Theme.colors.strokeSubtle
                        }

                        RowLayout {
                            id: entryRow

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            spacing: Theme.space.md

                            Item {
                                implicitWidth: Theme.size.tile
                                implicitHeight: Theme.size.controlSmall

                                Icon {
                                    anchors.centerIn: parent
                                    name: "camera"
                                    size: Theme.size.iconSmall
                                    color: Theme.colors.textSecondary
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: entry.modelData.name
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                                elide: Text.ElideRight
                            }

                            Button {
                                objectName: "treatAsCaptureCard"
                                small: true
                                tone: "ghost"
                                text: qsTr("Treat as capture card")
                                onClicked: DeviceManager.showCaptureCard(entry.modelData.serial)
                            }
                        }
                    }
                }
            }
        }

        WirelessSection {
            id: wirelessSection

            width: footer.width
            topPadding: footer.listed && footer.others.length === 0 ? Theme.space.xxl + Theme.space.xxs : 0
        }
    }

    Row {
        id: header

        x: Theme.size.panelPaddingH
        y: Theme.size.panelPaddingV
        visible: page.listShown
        spacing: Theme.space.lg

        Text {
            id: title

            textFormat: Text.PlainText
            text: qsTr("Devices")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.display.size
            font.weight: Theme.type.display.weight
        }

        Text {
            anchors.baseline: title.baseline
            visible: DeviceManager.deviceCount > 0
            textFormat: Text.PlainText
            text: DeviceManager.mirroringCount > 0
                  ? qsTr("%n device(s)", "", DeviceManager.deviceCount) + " · "
                    + qsTr("%n mirroring", "", DeviceManager.mirroringCount)
                  : qsTr("%n device(s)", "", DeviceManager.deviceCount)
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.body.size
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.verticalCenter: header.verticalCenter
        visible: page.listShown
        spacing: Theme.space.sm

        Button {
            objectName: "gridButton"
            enabled: gridCommand.enabled
            iconName: gridCommand.icon
            text: qsTr("Grid")
            onClicked: gridCommand.trigger()
        }

        Button {
            id: pairButton

            iconName: "wifi"
            text: qsTr("Wireless pairing")
            onClicked: Commands.trigger("wireless.pair")
        }

        Button {
            id: headerMore

            objectName: "devicesMore"
            tone: "ghost"
            iconName: "ellipsis"
            toolTip: qsTr("More")
            onClicked: headerMenu.open()

            Menu {
                id: headerMenu

                x: headerMore.width - width
                y: headerMore.height + Theme.space.xs

                MenuItem {
                    objectName: "tileWindows"
                    enabled: tileCommand.enabled
                    iconName: tileCommand.icon
                    text: tileCommand.title
                    onTriggered: tileCommand.trigger()
                }
            }
        }
    }

    CommandState {
        id: gridCommand

        command: "devices.grid"
    }

    CommandState {
        id: tileCommand

        command: "devices.tile"
    }

    CommandState {
        id: retryCommand

        command: "adb.retry"
    }

    Column {
        id: alerts

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: Theme.space.xl
        visible: page.listShown
        spacing: Theme.space.md

        InlineAlert {
            width: parent.width
            visible: DeviceManager.setupProblem !== ""
            tone: "warn"
            title: DeviceManager.setupProblem
            detail: DeviceManager.setupAction
            actionText: DeviceManager.canRetryAdb ? qsTr("Retry") : ""
            actionEnabled: retryCommand.enabled
            onActionTriggered: retryCommand.trigger()
        }

        InlineAlert {
            width: parent.width
            visible: DeviceManager.renderNotice !== ""
            tone: "info"
            title: DeviceManager.renderNotice
            detail: DeviceManager.renderDetail
        }
    }

    ListView {
        id: list

        function keepTabOrder() {
            const last = list.footerItem
            if (!last)
                return
            for (let index = 0; index < list.count; ++index) {
                const card = list.itemAtIndex(index)
                if (card)
                    ItemOrder.placeBefore(card, last)
            }
        }

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: alerts.bottom
        anchors.bottom: batchBar.top
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: alerts.height > 0 ? Theme.space.md : 0
        anchors.bottomMargin: batchBar.shown ? Theme.space.md : Theme.size.panelPaddingV
        visible: page.listShown
        spacing: Theme.space.sm + Theme.space.xxs
        clip: true
        model: DeviceManager.sorted
        onCountChanged: Qt.callLater(list.keepTabOrder)
        onFooterItemChanged: Qt.callLater(list.keepTabOrder)

        delegate: DeviceCard {
            id: deviceCard

            width: ListView.view.width
            onCaptureSettingsRequested: captureSheet.show(deviceCard.serial)
            onCaptureAssignRequested: assignDialog.show(deviceCard.serial, deviceCard.capture)
            Component.onCompleted: Qt.callLater(list.keepTabOrder)
        }

        header: Item {
            width: ListView.view.width
            height: visible ? empty.implicitHeight + 2 * Theme.space.x4l : 0
            visible: list.count === 0 && DeviceManager.adbReady

            Column {
                id: empty

                anchors.centerIn: parent
                spacing: Theme.space.lg

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "smartphone"
                    size: Theme.size.iconHero
                    color: Theme.colors.textTertiary
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    textFormat: Text.PlainText
                    text: qsTr("No device yet. Connect a phone with USB debugging turned on.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    small: true
                    tone: "ghost"
                    iconName: "circle-help"
                    text: qsTr("How to turn on USB debugging")
                    onClicked: SetupGuide.open(1, "", "")
                }
            }
        }

        footer: DevicesFooter {
            width: ListView.view.width
            listed: list.count > 0
        }

        add: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        remove: Transition {
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.exit
            }
        }
        move: Transition {
            NumberAnimation {
                property: "y"
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        displaced: Transition {
            NumberAnimation {
                property: "y"
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
    }

    BatchBar {
        id: batchBar

        objectName: "batchBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.bottomMargin: batchBar.shown ? Theme.size.panelPaddingV : 0
        visible: page.listShown && batchBar.shown
    }

    Loader {
        anchors.fill: parent
        active: page.appsSerial !== ""
        sourceComponent: AppsPage {
            serial: page.appsSerial
            onBack: page.appsSerial = ""
        }
    }

    Loader {
        objectName: "filesPageLoader"
        anchors.fill: parent
        active: page.filesSerial !== ""
        sourceComponent: FilesPage {
            serial: page.filesSerial
            onBack: page.filesSerial = ""
        }
    }

    CaptureCardSheet {
        id: captureSheet

        onAssignWanted: assignDialog.show(captureSheet.serial, captureSheet.capture)
    }

    CaptureAssignDialog {
        id: assignDialog
    }

    TourOverlay {
        id: tour

        anchors.fill: parent
        objectName: "devicesTour"
        tourKey: "devices"
        settings: AppSettings
        startWhen: page.listShown && DeviceManager.adbReady && !AppSettings.toursQuiet
        version: list.count + Math.round(list.contentHeight)
        steps: [
            {
                centered: true,
                title: qsTr("Devices"),
                text: qsTr("Every phone connected to this computer gets a card here, and so do capture cards and "
                           + "ESP32 boards. A card shows up as soon as the device connects; there is nothing to "
                           + "refresh.")
            },
            {
                target: function() { return list.count > 0 ? list.itemAtIndex(0) : list.headerItem },
                title: qsTr("Connect a phone"),
                text: qsTr("Plug the phone in with a USB cable and turn on USB debugging in its developer options. "
                           + "The first time, the phone asks whether to allow USB debugging from this computer: tap "
                           + "Allow.")
            },
            {
                target: pairButton,
                title: qsTr("Wireless pairing"),
                text: qsTr("No cable at hand? Turn on Wireless debugging on the phone and pair with the code or QR "
                           + "code it shows. After that the phone reconnects by itself on the same Wi-Fi.")
            },
            {
                target: function() { return tour.findItem(list, "deviceTransport") },
                title: qsTr("How the picture travels"),
                text: qsTr("The adb tunnel goes through USB or wireless debugging and works everywhere. WiFi direct "
                           + "sends the picture straight over Wi-Fi with less delay. Auto picks for you.")
            },
            {
                target: function() { return tour.findItem(list, "deviceMirror") },
                optional: true,
                title: qsTr("Start mirroring"),
                text: qsTr("Mirroring opens the phone's screen on this computer. Click and type there to work the "
                           + "phone, or pick a profile so that keys tap the screen for you. Try it now.")
            },
            {
                target: function() { return tour.findItem(list, "deviceSettings") },
                optional: true,
                title: qsTr("Settings for this phone"),
                text: qsTr("Resolution, frame rate, bitrate and the other mirroring settings can be set for each "
                           + "phone. Whatever you leave alone follows the settings for all phones.")
            },
            {
                target: function() { return tour.findItem(list, "deviceMore") },
                optional: true,
                title: qsTr("More for this phone"),
                text: qsTr("Apps, files, keeping the screen on and ending a wireless connection are in this menu.")
            },
            {
                target: function() { return tour.findItem(page, "gridButton") },
                title: qsTr("Grid"),
                text: qsTr("With several phones connected, Grid puts them side by side in one window so you can "
                           + "watch and work them together.")
            },
            {
                centered: true,
                title: qsTr("That's the devices page"),
                text: qsTr("The other pages are on the left: profiles hold your key mappings, tasks run things on a "
                           + "schedule, extensions add features. Each page has its own tour, and Help can replay "
                           + "them.")
            }
        ]
        advance: {
            switch (tour.step) {
            case 1:
                return DeviceManager.deviceCount > 0
            case 4:
                return DeviceManager.mirroringCount > 0
            }
            return false
        }
    }

    function startTour() {
        tour.restart()
    }
}
