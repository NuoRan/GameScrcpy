pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Window {
    id: root

    readonly property var context: AiChats.lastContext
    readonly property string windowText: {
        switch (root.context.window ?? "") {
        case "mirror":
            return qsTr("Mirror window")
        case "editor":
            return qsTr("Key mapping editor")
        case "logic":
            return qsTr("Logic editor")
        case "vision":
            return qsTr("Vision workbench")
        case "script":
            return qsTr("Logic window")
        default:
            return qsTr("Main window")
        }
    }
    readonly property string pageText: {
        switch (root.context.page ?? "") {
        case "devices":
            return qsTr("Devices")
        case "profiles":
            return qsTr("Profiles")
        case "tasks":
            return qsTr("Tasks")
        case "extensions":
            return qsTr("Extensions")
        case "settings":
            return qsTr("Settings")
        case "help":
            return qsTr("Help")
        default:
            return ""
        }
    }
    readonly property string whereText: {
        const subject = root.context.profileName || root.context.phone || root.pageText
        return subject !== "" ? qsTr("Looking at: %1 · %2").arg(root.windowText).arg(subject)
                              : qsTr("Looking at: %1").arg(root.windowText)
    }

    objectName: "aiWindow"
    width: 960
    height: 720
    minimumWidth: 640
    minimumHeight: 480
    visible: false
    title: qsTr("AI assistant — GameScrcpy")
    color: Theme.colors.bgBase

    onClosing: AiChats.windowOpen = false
    onVisibleChanged: {
        if (!root.visible)
            Qt.callLater(root.destroy)
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

    Rectangle {
        id: sidebar

        objectName: "aiWindowSidebar"
        anchors.left: parent.left
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        width: Theme.size.aiWindowSidebar
        color: Theme.colors.bgRaised

        Rectangle {
            anchors.right: parent.right
            width: Theme.size.stroke
            height: parent.height
            color: Theme.colors.strokeSubtle
        }

        Button {
            id: newButton

            objectName: "aiWindowNew"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.space.md
            iconName: "plus"
            text: qsTr("New conversation")
            onClicked: AiChats.newConversation()
        }

        Text {
            id: recentHeading

            anchors.left: parent.left
            anchors.leftMargin: Theme.space.lg
            anchors.top: newButton.bottom
            anchors.topMargin: Theme.space.lg
            textFormat: Text.PlainText
            text: qsTr("Recent conversations")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.micro.size
            font.weight: Theme.type.micro.weight
        }

        Text {
            objectName: "aiWindowEmpty"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: recentHeading.bottom
            anchors.margins: Theme.space.lg
            visible: AiChats.recent.length === 0
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("No saved conversations yet")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        ListView {
            id: recentList

            objectName: "aiWindowRecent"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: recentHeading.bottom
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.space.sm
            anchors.rightMargin: Theme.space.sm + Theme.size.stroke
            anchors.topMargin: Theme.space.xs
            anchors.bottomMargin: Theme.space.md
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: AiChats.recent
            spacing: Theme.space.xxs

            delegate: Rectangle {
                id: recentRow

                required property var modelData
                readonly property bool chosen: recentRow.modelData.id === AiChats.current

                objectName: "aiWindowRecentRow"
                width: ListView.view.width
                height: Theme.size.listItem + Theme.space.sm
                radius: Theme.radius.md
                color: recentRow.chosen ? Theme.colors.stateSelected
                                        : rowHover.hovered ? Theme.colors.stateHover : "transparent"

                Accessible.role: Accessible.ListItem
                Accessible.name: recentTitle.text
                Accessible.onPressAction: AiChats.open(recentRow.modelData.id)

                Text {
                    id: recentTitle

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space.sm
                    anchors.right: removeButton.left
                    anchors.rightMargin: Theme.space.xs
                    y: Theme.space.xs
                    textFormat: Text.PlainText
                    text: recentRow.modelData.title !== "" ? recentRow.modelData.title
                                                           : qsTr("Untitled conversation")
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                }

                Text {
                    anchors.left: recentTitle.left
                    anchors.right: recentTitle.right
                    anchors.top: recentTitle.bottom
                    textFormat: Text.PlainText
                    text: Qt.formatDateTime(recentRow.modelData.updated, "yyyy-MM-dd hh:mm")
                    elide: Text.ElideRight
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                HoverHandler {
                    id: rowHover

                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: AiChats.open(recentRow.modelData.id)
                }

                ToolbarButton {
                    id: removeButton

                    objectName: "aiWindowRemove"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.xxs
                    anchors.verticalCenter: parent.verticalCenter
                    visible: rowHover.hovered || recentRow.chosen
                    iconName: "trash-2"
                    text: qsTr("Delete this conversation")
                    onClicked: AiChats.remove(recentRow.modelData.id)
                }
            }
        }
    }

    Item {
        id: main

        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom

        Item {
            id: header

            width: parent.width
            height: Theme.size.toolbar

            Icon {
                id: headerIcon

                anchors.left: parent.left
                anchors.leftMargin: Theme.space.lg
                anchors.verticalCenter: parent.verticalCenter
                name: "sparkles"
                size: Theme.size.iconSmall
                color: Theme.colors.accentText
            }

            Text {
                objectName: "aiWindowWhere"
                anchors.left: headerIcon.right
                anchors.leftMargin: Theme.space.sm
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.lg
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: root.whereText
                elide: Text.ElideRight
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.label.size
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }
        }

        AiPanel {
            objectName: "aiWindowPanel"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            color: "transparent"
            context: root.context
            shown: root.active
            editorUnsaved: root.context.editorUnsaved === true
        }
    }

    Component.onCompleted: {
        chrome.attach()
        if (AppSettings.placeWindow(root, "assistant"))
            root.showMaximized()
        else
            root.show()
    }
}
