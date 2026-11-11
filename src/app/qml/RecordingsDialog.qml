pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    property string serial
    property var entries: []
    property int ticket: 0
    property bool loading: false
    property var trashing: ({})
    readonly property real bodyLimit: (parent ? parent.height : 0) - 2 * Theme.space.x3l - implicitHeaderHeight
                                      - implicitFooterHeight - topPadding - bottomPadding

    signal chosen(string folder, string serial)
    signal notified(string message, string icon)

    function show(serial: string) {
        dialog.serial = serial
        dialog.entries = []
        dialog.loading = true
        dialog.ticket = DeviceManager.listRecordings(serial)
        dialog.open()
    }

    function choose(entry: var) {
        if (entry.broken === true)
            return
        dialog.close()
        dialog.chosen(entry.folder ?? "", entry.serial ?? "")
    }

    function askTrash(entry: var) {
        dialog.trashing = entry
        trashConfirm.open()
    }

    function exportAs(entry: var, exporter: var) {
        exportPicker.entry = entry
        exportPicker.exporter = exporter
        exportPicker.open()
    }

    function trash() {
        const entry = dialog.trashing
        const problem = Recordings.moveToTrash(entry.folder ?? "")
        if (problem !== "") {
            dialog.notified(problem, "triangle-alert")
            return
        }
        dialog.entries = dialog.entries.filter(item => item.folder !== entry.folder)
        dialog.notified(qsTr("Moved the recording to the Recycle Bin"), "trash-2")
    }

    objectName: "recordingsDialog"
    title: qsTr("Recordings")
    acceptText: qsTr("Done")
    rejectText: ""
    alternativeText: qsTr("Open the recordings folder")
    onAlternativeChosen: Qt.openUrlExternally(Recordings.locationUrl)

    Flickable {
        id: scroller

        width: dialog.availableWidth
        height: implicitHeight
        implicitHeight: dialog.bodyLimit > 0 ? Math.min(list.implicitHeight, dialog.bodyLimit) : list.implicitHeight
        contentWidth: width
        contentHeight: list.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: list

            objectName: "recordingsList"
            width: scroller.width
            spacing: Theme.space.sm

            Text {
                objectName: "recordingsState"
                width: parent.width
                visible: dialog.loading || dialog.entries.length === 0
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: dialog.loading ? qsTr("Reading the recordings…")
                                     : qsTr("No recordings of this phone yet. Start one with Record on the toolbar.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }

            Repeater {
                model: dialog.entries

                delegate: RecordingRow {}
            }
        }
    }

    Connections {
        target: Recordings

        function onRecordingsListed(ticket: int, recordings: var) {
            if (ticket !== dialog.ticket)
                return
            dialog.entries = recordings
            dialog.loading = false
        }
    }

    FileDialog {
        id: exportPicker

        property var entry: ({})
        property var exporter: ({})

        objectName: "recordingExportPicker"
        title: qsTr("Export the recording as %1").arg(exportPicker.exporter.title ?? "")
        fileMode: FileDialog.SaveFile
        defaultSuffix: exportPicker.exporter.suffix ?? ""
        nameFilters: [qsTr("%1 files (*.%2)").arg(exportPicker.exporter.title ?? "").arg(exportPicker.exporter.suffix ?? "")]
        onAccepted: {
            Recordings.exportWith(exportPicker.entry.folder ?? "", exportPicker.entry.serial ?? "",
                                  exportPicker.exporter.key ?? "", exportPicker.selectedFile)
            dialog.notified(qsTr("Exporting the recording as %1…").arg(exportPicker.exporter.title ?? ""), "share")
        }
    }

    Dialog {
        id: trashConfirm

        objectName: "recordingTrashConfirm"
        title: qsTr("Move this recording to the Recycle Bin?")
        acceptText: qsTr("Move to the Recycle Bin")
        danger: true
        onAccepted: dialog.trash()

        Text {
            width: trashConfirm.availableWidth
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("%1 · %2. You can restore it from the Recycle Bin.")
                  .arg(dialog.trashing.when ?? "").arg(dialog.trashing.size ?? "")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }
    }

    component RecordingRow: T.AbstractButton {
        id: row

        required property var modelData
        readonly property string thumbnail: row.modelData.thumbnail ?? ""
        readonly property bool broken: row.modelData.broken === true

        objectName: "recordingRow"
        width: parent ? parent.width : implicitWidth
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        padding: Theme.space.md
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: row.modelData.when ?? ""
        Accessible.description: details.text
        onClicked: dialog.choose(row.modelData)

        background: Rectangle {
            radius: Theme.radius.lg
            color: row.down && !row.broken ? Theme.colors.statePress
                                           : row.hovered && !row.broken ? Theme.colors.stateSelected
                                                                        : Theme.colors.stateHover
            border.width: Theme.size.stroke
            border.color: row.hovered && !row.broken ? Theme.colors.strokeStrong : Theme.colors.strokeDefault

            FocusRing {
                visible: row.visualFocus
                baseRadius: Theme.radius.lg
            }
        }

        contentItem: Row {
            spacing: Theme.space.md

            Item {
                id: picture

                readonly property real aspect: Math.max(0.25, row.modelData.thumbnailAspect ?? 0.5625)

                anchors.verticalCenter: parent.verticalCenter
                width: Theme.size.tileLarge
                height: Theme.size.tileLarge

                ThumbnailView {
                    objectName: "recordingRowThumbnail"
                    anchors.centerIn: parent
                    visible: row.thumbnail !== ""
                    width: picture.aspect >= 1 ? Math.round(picture.height / picture.aspect) : picture.width
                    height: picture.aspect >= 1 ? picture.height : Math.round(picture.width * picture.aspect)
                    radius: Theme.radius.xs
                    source: row.thumbnail
                }

                Tile {
                    anchors.centerIn: parent
                    visible: row.thumbnail === ""
                    iconName: row.broken ? "triangle-alert" : "monitor-play"
                    tone: row.broken ? "gray" : "orange"
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, row.availableWidth - picture.width - more.width - 2 * Theme.space.md)
                spacing: Theme.space.xxs

                Text {
                    objectName: "recordingRowWhen"
                    width: parent.width
                    textFormat: Text.PlainText
                    text: row.modelData.when ?? ""
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                    font.features: ({ "tnum": 1 })
                }

                Text {
                    id: details

                    objectName: "recordingRowDetails"
                    width: parent.width
                    textFormat: Text.PlainText
                    text: row.broken ? qsTr("This recording cannot be read: %1").arg(row.modelData.reason ?? "")
                                     : [row.modelData.duration ?? "", row.modelData.size ?? "",
                                        qsTr("%n operation(s)", "", row.modelData.operations ?? 0),
                                        row.modelData.profileName ?? ""].filter(part => part !== "").join(" · ")
                    elide: Text.ElideRight
                    color: row.broken ? Theme.colors.dangerText : Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }
            }

            Button {
                id: more

                objectName: "recordingRowMore"
                anchors.verticalCenter: parent.verticalCenter
                tone: "ghost"
                small: true
                iconName: "ellipsis"
                toolTip: qsTr("More")
                onClicked: rowMenu.open()

                Menu {
                    id: rowMenu

                    x: more.width - width
                    y: more.height + Theme.space.xs

                    MenuItem {
                        objectName: "recordingRowOpen"
                        iconName: "monitor-play"
                        text: qsTr("Open")
                        enabled: !row.broken
                        onTriggered: dialog.choose(row.modelData)
                    }

                    MenuItem {
                        objectName: "recordingRowShow"
                        iconName: "folder-open"
                        text: qsTr("Show in folder")
                        onTriggered: Recordings.showInFolder(row.modelData.folder ?? "")
                    }

                    Instantiator {
                        model: Recordings.exporters

                        delegate: MenuItem {
                            id: exportItem

                            required property var modelData

                            objectName: "recordingRowExport"
                            iconName: "share"
                            text: qsTr("Export as %1…").arg(exportItem.modelData.title ?? "")
                            shortcut: exportItem.modelData.extensionName ?? ""
                            enabled: !row.broken
                            onTriggered: dialog.exportAs(row.modelData, exportItem.modelData)
                        }

                        onObjectAdded: (index, object) => rowMenu.insertItem(2 + index, object)
                        onObjectRemoved: (index, object) => rowMenu.removeItem(object)
                    }

                    MenuItem {
                        objectName: "recordingRowTrash"
                        iconName: "trash-2"
                        text: qsTr("Move to the Recycle Bin…")
                        onTriggered: dialog.askTrash(row.modelData)
                    }
                }
            }
        }
    }
}
