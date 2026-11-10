pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Column {
    id: about

    readonly property var licenses: AppInfo.licenses

    signal diagnosticsRequested()

    width: parent ? parent.width : implicitWidth
    spacing: Theme.space.md + Theme.space.xxs

    Card {
        objectName: "aboutCard"
        width: parent.width

        RowLayout {
            width: parent.width
            spacing: Theme.space.lg

            Tile {
                Layout.alignment: Qt.AlignTop
                large: true
                iconName: "info"
                tone: "orange"
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space.xxs

                Text {
                    textFormat: Text.PlainText
                    text: "GameScrcpy"
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.subtitle.size
                    font.weight: Theme.type.subtitle.weight
                }

                Text {
                    objectName: "version"
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: qsTr("Version %1 · %2").arg(AppInfo.version).arg(AppInfo.revision)
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                    elide: Text.ElideRight
                }

                Text {
                    objectName: "build"
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: qsTr("Build %1").arg(AppInfo.build)
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.xs
                    textFormat: Text.PlainText
                    text: qsTr("Android mirroring, key mapping and automation for Windows. Open source under the Apache License 2.0.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    SettingCard {
        objectName: "diagnosticsCard"
        visible: true

        SettingsRow {
            width: parent ? parent.width : implicitWidth
            title: qsTr("Diagnostics")
            note: qsTr("Saves the logs, the settings, the device list and the newest crash report in one zip file "
                       + "to attach to a problem report. Serial numbers, addresses, user names, Wi-Fi names and "
                       + "clipboard text are taken out.")

            Button {
                objectName: "exportDiagnostics"
                text: qsTr("Export diagnostics…")
                iconName: "share"
                enabled: !DiagnosticsBundle.exporting
                onClicked: about.diagnosticsRequested()
            }
        }
    }

    SettingCard {
        objectName: "licensesCard"
        visible: true
        title: qsTr("Open-source licenses")
        note: qsTr("%n component(s)", "", about.licenses.length)
        iconName: "code-xml"
        tone: "teal"

        Repeater {
            model: about.licenses

            delegate: LicenseRow {}
        }
    }

    component LicenseRow: Column {
        id: licenseRow

        required property var modelData
        required property int index
        property bool expanded: false

        width: parent ? parent.width : implicitWidth
        bottomPadding: expanded ? Theme.space.lg : 0

        SettingsRow {
            width: parent.width
            divider: licenseRow.index > 0
            title: licenseRow.modelData.name
            note: qsTr("%1 · %2").arg(licenseRow.modelData.version).arg(licenseRow.modelData.license)

            Button {
                objectName: "toggle"
                tone: "ghost"
                small: true
                text: licenseRow.expanded ? qsTr("Hide") : qsTr("Show")
                Accessible.description: licenseRow.modelData.name
                onClicked: licenseRow.expanded = !licenseRow.expanded
            }
        }

        Rectangle {
            objectName: "licenseText"
            x: Theme.space.lg + Theme.space.xxs
            width: parent.width - 2 * x
            height: licenseText.implicitHeight + 2 * Theme.space.md
            visible: licenseRow.expanded
            radius: Theme.radius.md
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle

            TextEdit {
                id: licenseText

                x: Theme.space.md
                y: Theme.space.md
                width: parent.width - 2 * Theme.space.md
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                text: licenseRow.expanded ? licenseRow.modelData.text : ""
                color: Theme.colors.textSecondary
                selectionColor: Theme.colors.accentLine
                selectedTextColor: Theme.colors.textPrimary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size
            }
        }
    }
}
