pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Dialog {
    id: dialog

    property var info: ({})
    property string packageUrl

    readonly property string version: info.version ?? ""
    readonly property var authors: info.authors ?? []
    readonly property string description: info.description ?? ""
    readonly property string installedVersion: info.installedVersion ?? ""
    readonly property var permissions: info.permissions ?? []
    readonly property var dependencies: info.dependencies ?? []
    readonly property var problems: info.problems ?? []
    readonly property var security: info.security ?? ({})
    readonly property bool nativeCode: dialog.security.native === true
    readonly property bool refused: dialog.nativeCode && dialog.security.refused === true
    readonly property bool signedByAuthor: dialog.nativeCode && dialog.security.author === "valid"
    readonly property var capabilities: dialog.nativeCode ? (dialog.security.capabilities ?? []) : []
    readonly property var warnings: dialog.nativeCode ? (dialog.security.warnings ?? []) : []
    readonly property bool readable: (info.id ?? "") !== ""
    readonly property bool installable: info.ok === true && !ExtensionHost.installing
    readonly property real bodyLimit: (dialog.parent ? dialog.parent.height : 0) - 2 * Theme.space.x6l
                                      - dialog.implicitHeaderHeight - dialog.implicitFooterHeight
                                      - dialog.topPadding - dialog.bottomPadding

    title: info.name || info.id || decodeURIComponent(packageUrl.substring(packageUrl.lastIndexOf("/") + 1))

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
                text: dialog.nativeCode ? qsTr("Trust and install") : qsTr("Install")
                enabled: dialog.installable
                onClicked: {
                    ExtensionHost.installPackage(dialog.packageUrl)
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

            Column {
                width: parent.width
                spacing: Theme.space.xxs

                Text {
                    width: parent.width
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: [dialog.version !== "" ? qsTr("Version %1").arg(dialog.version) : "",
                           dialog.authors.length > 0 ? qsTr("By %1").arg(dialog.authors.join(qsTr(", ", "list separator"))) : ""]
                          .filter(part => part !== "").join(" · ")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: dialog.installedVersion !== ""
                    textFormat: Text.PlainText
                    text: qsTr("Replaces version %1").arg(dialog.installedVersion)
                    color: Theme.colors.infoText
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }

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
                text: qsTr("This package can't be installed.")
                color: Theme.colors.dangerText
                font.pixelSize: Theme.type.body.size
                wrapMode: Text.Wrap
            }

            Column {
                objectName: "nativeCheck"
                width: parent.width
                visible: dialog.nativeCode
                spacing: Theme.space.sm

                Heading {
                    text: qsTr("Native code")
                }

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: qsTr("It runs its own Windows code in a separate process. GameScrcpy checked that code before installing:")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                InlineAlert {
                    objectName: "nativeRefused"
                    width: parent.width
                    visible: dialog.refused
                    tone: "bad"
                    title: qsTr("Not installed, for your safety")
                    detail: dialog.refused ? [dialog.security.reason ?? "", dialog.security.detail ?? ""]
                                             .filter(part => part !== "").join("\n")
                                           : ""
                }

                InlineAlert {
                    objectName: "nativeAuthor"
                    width: parent.width
                    visible: dialog.nativeCode && dialog.security.author !== "invalid"
                    tone: dialog.signedByAuthor ? "info" : "warn"
                    title: dialog.signedByAuthor ? qsTr("Signed by its author") : qsTr("Author not verified")
                    detail: dialog.signedByAuthor
                            ? qsTr("Key %1. Updates have to be signed with the same key.").arg(dialog.security.keyId ?? "")
                            : qsTr("The package is not signed, so there is no way to tell who made it or whether it was changed. Install it only if you trust where it came from.")
                }

                Row {
                    width: parent.width
                    visible: dialog.nativeCode && dialog.security.scan === "clean"
                    spacing: Theme.space.sm

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "circle-check"
                        size: Theme.size.iconSmall
                        color: Theme.colors.successText
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("Windows Security found no threat")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                    }
                }

                Text {
                    width: parent.width
                    visible: dialog.nativeCode && dialog.capabilities.length === 0
                    textFormat: Text.PlainText
                    text: qsTr("Its code uses none of the system functions that are checked: network, files, keyboard and mouse, other programs.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: dialog.capabilities

                    delegate: Row {
                        id: capability

                        required property var modelData

                        objectName: "nativeCapability"
                        width: parent ? parent.width : implicitWidth
                        spacing: Theme.space.sm

                        Icon {
                            y: Theme.space.xxs
                            name: capability.modelData.declared === true ? "check" : "triangle-alert"
                            size: Theme.size.iconSmall
                            color: capability.modelData.declared === true ? Theme.colors.textSecondary
                                                                          : Theme.colors.warningText
                        }

                        Column {
                            width: capability.width - Theme.size.iconSmall - capability.spacing
                            spacing: Theme.space.xxs

                            Text {
                                width: parent.width
                                textFormat: Text.PlainText
                                text: capability.modelData.title ?? ""
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                                font.weight: Theme.type.label.weight
                                wrapMode: Text.Wrap
                            }

                            Text {
                                width: parent.width
                                visible: text !== ""
                                textFormat: Text.PlainText
                                text: (capability.modelData.functions ?? []).join(", ")
                                color: Theme.colors.textTertiary
                                font.family: Theme.type.monoFamily
                                font.pixelSize: Theme.type.caption.size
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Repeater {
                    model: dialog.warnings

                    delegate: Row {
                        id: warning

                        required property string modelData

                        objectName: "nativeWarning"
                        width: parent ? parent.width : implicitWidth
                        spacing: Theme.space.sm

                        Icon {
                            y: Theme.space.xxs
                            name: "triangle-alert"
                            size: Theme.size.iconSmall
                            color: Theme.colors.warningText
                        }

                        Text {
                            width: warning.width - Theme.size.iconSmall - warning.spacing
                            textFormat: Text.PlainText
                            text: warning.modelData
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            Column {
                width: parent.width
                visible: dialog.readable
                spacing: Theme.space.sm

                Heading {
                    text: qsTr("Permissions")
                }

                Text {
                    width: parent.width
                    visible: dialog.permissions.length === 0
                    textFormat: Text.PlainText
                    text: qsTr("Needs no permissions")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: dialog.permissions

                    delegate: Column {
                        id: permission

                        required property var modelData

                        width: parent ? parent.width : implicitWidth
                        spacing: Theme.space.xxs

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: permission.modelData.title ?? ""
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Theme.type.label.weight
                            wrapMode: Text.Wrap
                        }

                        Text {
                            width: parent.width
                            visible: text !== ""
                            textFormat: Text.PlainText
                            text: permission.modelData.reason ?? ""
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            Column {
                width: parent.width
                visible: dialog.dependencies.length > 0
                spacing: Theme.space.sm

                Heading {
                    text: qsTr("Dependencies")
                }

                Repeater {
                    model: dialog.dependencies

                    delegate: Row {
                        id: dependency

                        required property var modelData

                        width: parent ? parent.width : implicitWidth
                        spacing: Theme.space.sm

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, dependency.width - range.width - dependency.spacing
                                            - (optional.visible ? optional.width + dependency.spacing : 0))
                            textFormat: Text.PlainText
                            text: dependency.modelData.id ?? ""
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Theme.type.label.weight
                            elide: Text.ElideMiddle
                        }

                        Text {
                            id: range

                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: dependency.modelData.range ?? ""
                            color: Theme.colors.textSecondary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.caption.size
                        }

                        StatusPill {
                            id: optional

                            anchors.verticalCenter: parent.verticalCenter
                            visible: dependency.modelData.optional === true
                            text: qsTr("Optional")
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
                visible: dialog.info.ok === true && ExtensionHost.installing
                textFormat: Text.PlainText
                text: qsTr("Another installation is in progress.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }
    }

    component Heading: Text {
        textFormat: Text.PlainText
        width: parent ? parent.width : implicitWidth
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        font.weight: Font.DemiBold
    }
}
