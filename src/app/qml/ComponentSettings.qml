pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls
import GameScrcpy.App

Flickable {
    id: settings

    required property string ref
    required property bool locked
    readonly property var info: ComponentLibrary.mine.length + ComponentLibrary.examples.length >= 0
                                ? ComponentLibrary.info(settings.ref) : ({})
    readonly property var users: ProfileLibrary.entries.length >= 0 ? ProfileLibrary.usersOf(settings.ref) : []

    function store(key: string, field: TextField) {
        if (!ComponentLibrary.setInfo(settings.ref, key, field.text))
            field.text = settings.info[key] ?? ""
    }

    function copyMine(ref: string) {
        const made = ComponentLibrary.copyToMine(ref)
        if (made !== "")
            DeviceManager.openComponent(made)
    }

    objectName: "componentSettings"
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    contentHeight: column.implicitHeight + 2 * Theme.space.lg

    Column {
        id: column

        x: Theme.space.lg
        y: Theme.space.lg
        width: settings.width - 2 * Theme.space.lg
        spacing: Theme.space.lg

        Row {
            width: parent.width
            spacing: Theme.space.md

            Rectangle {
                width: Theme.size.control
                height: width
                radius: Theme.radius.md
                color: Theme.colors.accentSubtle

                Icon {
                    anchors.centerIn: parent
                    name: settings.info.icon ?? "sparkles"
                    size: Theme.size.iconLarge
                    color: Theme.colors.accentText
                }
            }

            Column {
                width: parent.width - Theme.size.control - parent.spacing
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xxs

                Text {
                    objectName: "componentTitle"
                    width: parent.width
                    textFormat: Text.PlainText
                    text: settings.info.name ?? ""
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.DemiBold
                }

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: settings.locked ? qsTr("Example component · read only") : qsTr("Custom component · made with logic")
                    elide: Text.ElideRight
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }

        Column {
            width: parent.width
            visible: settings.locked
            spacing: Theme.space.md

            Text {
                width: parent.width
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("You can look at an example but not change it. Copy it to your components to change your own copy.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.label.size
            }

            Button {
                objectName: "copyToMine"
                iconName: "copy"
                text: qsTr("Copy to my components")
                onClicked: {
                    if ((settings.info.extensionId ?? "") !== "")
                        copyToMineDialog.ask("component", settings.ref, settings.info.name ?? "")
                    else
                        settings.copyMine(settings.ref)
                }
            }
        }

        Column {
            width: parent.width
            visible: !settings.locked
            spacing: Theme.space.xs

            Text {
                textFormat: Text.PlainText
                text: qsTr("Name")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Theme.type.label.weight
            }

            TextField {
                id: nameField

                objectName: "componentName"
                width: parent.width
                text: settings.info.name ?? ""
                maximumLength: 64
                onEditingFinished: settings.store("name", nameField)
            }
        }

        Column {
            width: parent.width
            visible: !settings.locked
            spacing: Theme.space.xs

            Item {
                width: parent.width
                height: descriptionLabel.implicitHeight

                Text {
                    id: descriptionLabel

                    textFormat: Text.PlainText
                    text: qsTr("One-line description")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Theme.type.label.weight
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: descriptionLabel.verticalCenter
                    textFormat: Text.PlainText
                    text: qsTr("Shown in the library")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }
            }

            TextField {
                id: descriptionField

                objectName: "componentDescription"
                width: parent.width
                text: settings.info.description ?? ""
                maximumLength: 128
                onEditingFinished: settings.store("description", descriptionField)
            }
        }

        Column {
            width: parent.width
            visible: !settings.locked
            spacing: Theme.space.xs

            Text {
                textFormat: Text.PlainText
                text: qsTr("Icon")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Theme.type.label.weight
            }

            Flow {
                objectName: "componentIcons"
                width: parent.width
                spacing: Theme.space.xxs

                Repeater {
                    model: ComponentLibrary.icons

                    delegate: ToolbarButton {
                        required property string modelData

                        iconName: modelData
                        text: modelData
                        selected: modelData === (settings.info.icon ?? "")
                        onClicked: ComponentLibrary.setInfo(settings.ref, "icon", modelData)
                    }
                }
            }
        }

        Column {
            width: parent.width
            visible: !settings.locked
            spacing: Theme.space.xs

            Text {
                textFormat: Text.PlainText
                text: qsTr("Category")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Theme.type.label.weight
            }

            TextField {
                id: categoryField

                objectName: "componentCategory"
                width: parent.width
                text: settings.info.category ?? ""
                maximumLength: 64
                onEditingFinished: settings.store("category", categoryField)
            }
        }

        Column {
            width: parent.width
            spacing: Theme.space.xs

            Item {
                width: parent.width
                height: usersLabel.implicitHeight

                Text {
                    id: usersLabel

                    textFormat: Text.PlainText
                    text: qsTr("Profiles that use it")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Theme.type.label.weight
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: usersLabel.verticalCenter
                    textFormat: Text.PlainText
                    text: qsTr("They change with it")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }
            }

            Text {
                width: parent.width
                visible: settings.users.length === 0
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("No profile uses it yet")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.label.size
            }

            Repeater {
                model: settings.users

                delegate: Item {
                    id: user

                    required property var modelData

                    objectName: "componentUser"
                    width: column.width
                    height: Theme.size.listItem

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width / 2
                        textFormat: Text.PlainText
                        text: user.modelData.name
                        elide: Text.ElideRight
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("%n place(s)", "", user.modelData.count)
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }
            }
        }
    }

    CopyToMineDialog {
        id: copyToMineDialog

        onConfirmed: (kind, source, sourceName) => settings.copyMine(source)
    }
}
