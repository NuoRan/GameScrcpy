pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    property var info: ({})
    property string bundleUrl

    readonly property var extensions: info.extensions ?? []
    readonly property var problems: info.problems ?? []
    readonly property string description: info.description ?? ""
    readonly property bool nativeCode: dialog.extensions.some(entry => (entry.security ?? {}).native === true)
    readonly property bool installable: info.ok === true && !ExtensionHost.installing && ExtensionHost.bisect.active !== true
    readonly property real bodyLimit: (dialog.parent ? dialog.parent.height : 0) - 2 * Theme.space.x6l
                                      - dialog.implicitHeaderHeight - dialog.implicitFooterHeight
                                      - dialog.topPadding - dialog.bottomPadding

    function statusText(status: string): string {
        switch (status) {
        case "new":
            return qsTr("New")
        case "update":
            return qsTr("Replaces the installed version")
        case "same":
            return qsTr("Already installed")
        case "builtin":
            return qsTr("Built-in")
        default:
            return status
        }
    }

    title: info.name || decodeURIComponent(bundleUrl.substring(bundleUrl.lastIndexOf("/") + 1))

    footer: Item {
        implicitHeight: buttons.implicitHeight + 2 * Theme.space.xl

        Row {
            id: buttons

            anchors.right: parent.right
            anchors.rightMargin: Theme.space.xxl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Button {
                objectName: "reject"
                text: qsTr("Cancel")
                onClicked: dialog.reject()
            }

            Button {
                objectName: "accept"
                tone: "primary"
                text: dialog.nativeCode ? qsTr("Trust and install all") : qsTr("Install all")
                enabled: dialog.installable
                onClicked: {
                    ExtensionHost.installBundle(dialog.bundleUrl)
                    dialog.accept()
                }
            }
        }
    }

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

            width: scroller.width
            spacing: Theme.space.lg

            Text {
                width: parent.width
                visible: dialog.description !== ""
                textFormat: Text.PlainText
                text: dialog.description
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                visible: dialog.info.ok !== true
                textFormat: Text.PlainText
                text: qsTr("This bundle can't be installed.")
                color: Theme.colors.dangerText
                font.pixelSize: Theme.type.body.size
                wrapMode: Text.Wrap
            }

            Column {
                width: parent.width
                visible: dialog.extensions.length > 0
                spacing: Theme.space.md

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: qsTr("Installing all of these also allows what they ask for:")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: dialog.extensions

                    delegate: Column {
                        id: entry

                        required property var modelData

                        readonly property var permissions: modelData.permissions ?? []
                        readonly property var dependencies: modelData.dependencies ?? []
                        readonly property var security: modelData.security ?? ({})
                        readonly property bool nativeCode: entry.security.native === true
                        readonly property bool refused: entry.nativeCode && entry.security.refused === true
                        readonly property bool restricted: entry.nativeCode && entry.security.restricted === true
                        readonly property var notes: entry.nativeCode
                                                     ? [entry.restricted ? qsTr("Restricted: it can only do what it declares")
                                                                         : qsTr("Native code"),
                                                        entry.security.author === "valid" ? qsTr("Signed by its author")
                                                                                          : qsTr("Author not verified")]
                                                       .concat(entry.security.warnings ?? [])
                                                     : []

                        width: parent ? parent.width : implicitWidth
                        spacing: Theme.space.xxs

                        Row {
                            width: parent.width
                            spacing: Theme.space.sm

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, parent.width - version.width - pill.width
                                                - 2 * parent.spacing)
                                textFormat: Text.PlainText
                                text: entry.modelData.name ?? ""
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                                font.weight: Theme.type.label.weight
                                elide: Text.ElideRight
                            }

                            Text {
                                id: version

                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: entry.modelData.version ?? ""
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }

                            StatusPill {
                                id: pill

                                anchors.verticalCenter: parent.verticalCenter
                                text: dialog.statusText(entry.modelData.status ?? "")
                            }
                        }

                        Text {
                            width: parent.width
                            visible: entry.permissions.length > 0
                            textFormat: Text.PlainText
                            text: qsTr("Asks for: %1").arg(entry.permissions.map(permission => permission.title)
                                                                 .join(qsTr(", ", "list separator")))
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }

                        Text {
                            width: parent.width
                            visible: entry.dependencies.length > 0
                            textFormat: Text.PlainText
                            text: qsTr("Needs: %1").arg(entry.dependencies.map(dependency => dependency.id)
                                                              .join(qsTr(", ", "list separator")))
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }

                        Text {
                            width: parent.width
                            visible: entry.modelData.enabled === false
                            textFormat: Text.PlainText
                            text: qsTr("Stays off after installing")
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }

                        Text {
                            objectName: "nativeNotes"
                            width: parent.width
                            visible: entry.nativeCode && !entry.refused
                            textFormat: Text.PlainText
                            text: entry.notes.join(" · ")
                            color: (entry.security.author === "valid" || entry.restricted)
                                   && (entry.security.warnings ?? []).length === 0
                                   ? Theme.colors.textSecondary : Theme.colors.warningText
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }

                        Text {
                            objectName: "nativeRefused"
                            width: parent.width
                            visible: entry.refused
                            textFormat: Text.PlainText
                            text: qsTr("Not installed, for your safety: %1").arg(entry.security.reason ?? "")
                            color: Theme.colors.dangerText
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            ProblemList {
                width: parent.width
                problems: dialog.problems
            }

            Text {
                width: parent.width
                visible: dialog.info.ok === true && (ExtensionHost.installing || ExtensionHost.bisect.active === true)
                textFormat: Text.PlainText
                text: ExtensionHost.installing ? qsTr("Another installation is in progress.")
                                               : qsTr("Finish finding the problem extension first.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }
    }
}
