pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Window {
    id: root

    required property ScriptWorkbench bench
    readonly property string helpTopic: "scripts"
    property bool leaving: false
    readonly property ScriptWorkbenchEditor currentEditor: logic.currentEditor

    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 600
    visible: false
    title: root.bench.component !== "" ? qsTr("%1 — Component").arg(root.bench.name)
                                        : qsTr("%1 — Logic").arg(root.bench.name)
    color: Theme.colors.bgBase

    onClosing: close => {
        if (root.leaving || root.bench.dirtyCount === 0)
            return
        close.accepted = false
        unsaved.open()
    }

    Shortcut {
        sequences: [StandardKey.Save]
        onActivated: root.bench.saveAll()
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

    Workbench {
        id: frame

        objectName: "logicWindowBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        height: Theme.size.workbenchBar
        bodyShown: false

        barContent: [
            Row {
                id: titleRow

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.md

                Text {
                    objectName: "profileName"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, Math.max(0, modeSwitch.x - Theme.space.lg - saveState.width
                                                            - titleRow.spacing))
                    textFormat: Text.PlainText
                    text: root.bench.name
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.DemiBold
                }

                Text {
                    id: saveState

                    objectName: "saveState"
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: root.bench.locked ? qsTr("Read only")
                                            : root.bench.dirtyCount > 0 ? qsTr("Not saved") : qsTr("Saved")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }
            },
            SegmentedControl {
                id: modeSwitch

                objectName: "modeSwitch"
                anchors.verticalCenter: parent.verticalCenter
                visible: root.bench.task === "" && root.bench.component === ""
                x: Math.max(0, Math.min((parent.width - width) / 2, actions.x - width - Theme.space.lg))
                model: [{ text: qsTr("Picture"), icon: "image" }, { text: qsTr("Logic"), icon: "workflow" }]
                currentIndex: 1
                Accessible.name: qsTr("Edit the picture or the logic")
                onActivated: index => {
                    if (index === 0)
                        toast.show(qsTr("Components are placed on a phone's picture. Mirror a phone and choose this profile on it"),
                                   "info")
                    modeSwitch.currentIndex = 1
                }
            },
            Row {
                id: actions

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xs

                ToolbarButton {
                    objectName: "debug"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "activity"
                    text: root.bench.debugging ? qsTr("Stop debugging") : qsTr("Debug")
                    selected: root.bench.debugging
                    onClicked: root.bench.debug(!root.bench.debugging)
                }

                ToolbarButton {
                    objectName: "undo"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "undo-2"
                    text: qsTr("Undo")
                    shortcut: "Ctrl+Z"
                    enabled: logic.canUndo
                    onClicked: logic.undo()
                }

                ToolbarButton {
                    objectName: "redo"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "redo-2"
                    text: qsTr("Redo")
                    shortcut: "Ctrl+Y"
                    enabled: logic.canRedo
                    onClicked: logic.redo()
                }

                Select {
                    id: phoneSelect

                    readonly property int chosen: root.bench.phones.findIndex(phone => phone.serial === root.bench.phone)

                    objectName: "phoneSelect"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.bench.component === ""
                    width: Theme.size.fieldWidth
                    enabled: count > 0
                    model: root.bench.phones
                    textRole: "name"
                    currentIndex: chosen
                    displayText: count > 0 ? currentText : qsTr("No phone is mirroring")
                    Accessible.name: qsTr("Phone to run the logic on")
                    onActivated: index => {
                        root.bench.phone = root.bench.phones[index].serial
                        phoneSelect.currentIndex = Qt.binding(() => phoneSelect.chosen)
                    }
                }

                ToolbarButton {
                    id: moreButton

                    objectName: "more"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "ellipsis"
                    text: qsTr("More")
                    onClicked: moreMenu.open()

                    Menu {
                        id: moreMenu

                        x: moreButton.width - width
                        y: moreButton.height + Theme.space.xs

                        MenuItem {
                            objectName: "openFolder"
                            iconName: "folder-open"
                            text: qsTr("Open folder")
                            onTriggered: root.bench.openFolder()
                        }

                        MenuItem {
                            objectName: "openExternal"
                            iconName: "share"
                            text: qsTr("Open in external editor")
                            onTriggered: root.bench.openExternal()
                        }
                    }
                }

                Item {
                    width: Theme.space.xs
                    height: 1
                }

                Button {
                    objectName: "save"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    text: qsTr("Save")
                    toolTip: qsTr("Save all files (Ctrl+S)")
                    enabled: root.bench.dirtyCount > 0
                    onClicked: root.bench.saveAll()
                }

                Button {
                    objectName: "done"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    tone: "primary"
                    text: qsTr("Done")
                    onClicked: {
                        if (root.bench.dirtyCount === 0 || root.bench.saveAll()) {
                            root.leaving = true
                            root.close()
                        }
                    }
                }
            }
        ]
    }

    LogicView {
        id: logic

        objectName: "logicView"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: frame.bottom
        anchors.bottom: parent.bottom
        bench: root.bench
    }

    Dialog {
        id: unsaved

        objectName: "unsavedDialog"
        title: qsTr("Save the changes before closing?")
        acceptText: qsTr("Save")
        alternativeText: qsTr("Don't save")
        rejectText: qsTr("Cancel")
        onAccepted: {
            if (root.bench.saveAll()) {
                root.leaving = true
                root.close()
            }
        }
        onAlternativeChosen: {
            root.bench.discardAll()
            root.leaving = true
            root.close()
        }

        Text {
            width: parent.width
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("%n file(s) have changes that are not saved.", "", root.bench.dirtyCount)
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }
    }

    Toast {
        id: toast
    }

    AiCorner {
        objectName: "logicAiCorner"
        window: "script"
        context: ({
            selection: [root.bench.task !== "" ? "the task " + root.bench.name
                        : root.bench.component !== "" ? "the component " + root.bench.name : root.bench.name,
                        logic.selectionText].filter(part => part !== "").join("; ")
        })
    }

    Component.onCompleted: {
        chrome.attach()
        root.visible = true
    }
}
