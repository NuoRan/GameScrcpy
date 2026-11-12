pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    property var list: []

    readonly property bool hasUpdates: dialog.list.some(entry => entry.updates === true)
    readonly property real bodyLimit: (dialog.parent ? dialog.parent.height : 0) - 2 * Theme.space.x6l
                                      - dialog.implicitHeaderHeight - dialog.implicitFooterHeight
                                      - dialog.topPadding - dialog.bottomPadding

    signal checkRequested()
    signal extensionsRequested()

    function reasonText(code: string): string {
        return code === "host-api-outdated" ? qsTr("Made for an older GameScrcpy")
                                            : qsTr("Needs a newer GameScrcpy")
    }

    objectName: "extensionCompatibility"
    title: qsTr("Some extensions do not work with GameScrcpy %1").arg(AppInfo.version)
    acceptText: dialog.hasUpdates ? qsTr("Check for updates") : qsTr("Open Extensions")
    rejectText: qsTr("Close")
    alternativeText: dialog.hasUpdates ? qsTr("Open Extensions") : ""
    onAccepted: {
        if (dialog.hasUpdates)
            dialog.checkRequested()
        else
            dialog.extensionsRequested()
    }
    onAlternativeChosen: dialog.extensionsRequested()

    Flickable {
        id: scroller

        width: dialog.availableWidth
        height: implicitHeight
        implicitHeight: Math.max(0, Math.min(body.implicitHeight, dialog.bodyLimit))
        contentWidth: width
        contentHeight: body.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: body

            width: parent.width
            spacing: Theme.space.lg

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("Newer versions of these extensions may fit this GameScrcpy, from their authors or through Check for updates.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
                wrapMode: Text.Wrap
            }

            Column {
                width: parent.width
                spacing: Theme.space.md

                Repeater {
                    model: dialog.list

                    delegate: Column {
                        id: entry

                        required property var modelData

                        objectName: "extensionCompatibilityRow"
                        width: parent ? parent.width : implicitWidth
                        spacing: Theme.space.xxs

                        RowLayout {
                            width: parent.width
                            spacing: Theme.space.sm

                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: entry.modelData.name ?? ""
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                                font.weight: Theme.type.label.weight
                                elide: Text.ElideRight
                            }

                            Text {
                                textFormat: Text.PlainText
                                text: entry.modelData.version ?? ""
                                color: Theme.colors.textTertiary
                                font.family: Theme.type.monoFamily
                                font.pixelSize: Theme.type.caption.size
                            }
                        }

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: dialog.reasonText(entry.modelData.reason ?? "")
                            color: Theme.colors.warningText
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
