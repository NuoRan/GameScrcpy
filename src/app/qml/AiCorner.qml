pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: corner

    property string window: "main"
    property var context: ({})
    property bool allowed: true
    property bool editorUnsaved: false
    property bool open: false
    property int tours: 0
    property real bubbleRight: Theme.space.lg
    property real bubbleBottom: Theme.space.lg
    property real rightGap: Theme.space.lg
    property real bottomGap: Theme.space.lg
    property var moved: ({})
    readonly property var spot: corner.moved[corner.window] ?? null
    readonly property bool bubbleShown: corner.allowed && AppSettings.values["assistant.bubble"] !== false
    readonly property var shownContext: Object.assign({}, corner.context,
                                                      { window: corner.window, editorUnsaved: corner.editorUnsaved })

    function show() {
        if (AiChats.windowOpen) {
            AiChats.openWindow()
            return
        }
        corner.open = corner.allowed
    }

    function toggle() {
        if (AiChats.windowOpen) {
            AiChats.openWindow()
            return
        }
        corner.open = corner.allowed && !corner.open
    }

    function noteHere() {
        if (corner.Window.active)
            AiChats.noteContext(corner.shownContext)
    }

    function moveBubble(right: real, bottom: real) {
        const spots = Object.assign({}, corner.moved)
        spots[corner.window] = {
            right: Math.max(0, Math.min(corner.width - bubble.width, right)),
            bottom: Math.max(0, Math.min(corner.height - bubble.height, bottom))
        }
        corner.moved = spots
    }

    objectName: "aiCorner"
    anchors.fill: parent
    z: 9000

    Window.onActiveChanged: corner.noteHere()
    onShownContextChanged: corner.noteHere()

    Connections {
        target: AiChats

        function onToggleRequested() {
            if (corner.Window.active)
                corner.toggle()
        }

        function onWindowOpenChanged() {
            if (AiChats.windowOpen)
                corner.open = false
        }
    }

    Rectangle {
        id: bubble

        readonly property bool wanted: corner.bubbleShown && !corner.open && corner.tours === 0

        objectName: "aiBubble"
        visible: opacity > 0
        opacity: bubble.wanted ? 1 : 0
        scale: !bubble.wanted ? Theme.motion.popScale
                              : bubbleHover.hovered && !bubbleDrag.active && !Theme.motion.reduced
                                ? Theme.motion.hoverScale : 1
        x: corner.width - width - Math.max(0, Math.min(corner.width - width,
                                                       corner.spot !== null ? corner.spot.right : corner.bubbleRight))
        y: corner.height - height - Math.max(0, Math.min(corner.height - height,
                                                         corner.spot !== null ? corner.spot.bottom : corner.bubbleBottom))
        width: Theme.size.aiBubble
        height: Theme.size.aiBubble
        radius: width / 2
        color: Theme.colors.accent
        border.width: Theme.size.stroke
        border.color: Theme.colors.accentEdge

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.fade
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }

        Behavior on scale {
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("AI assistant")
        Accessible.onPressAction: corner.toggle()

        Icon {
            anchors.centerIn: parent
            name: "sparkles"
            size: Theme.size.iconLarge
            color: Theme.colors.textOnAccent
        }

        StateLayer {
            anchors.fill: parent
            radius: bubble.radius
            hovered: bubbleHover.hovered
            pressed: bubbleTap.pressed
        }

        HoverHandler {
            id: bubbleHover

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            id: bubbleTap

            onTapped: corner.toggle()
        }

        DragHandler {
            id: bubbleDrag

            property real startRight: 0
            property real startBottom: 0

            target: null
            cursorShape: Qt.ClosedHandCursor
            onActiveChanged: {
                if (!bubbleDrag.active)
                    return
                bubbleDrag.startRight = corner.width - bubble.x - bubble.width
                bubbleDrag.startBottom = corner.height - bubble.y - bubble.height
            }
            onTranslationChanged: {
                if (bubbleDrag.active)
                    corner.moveBubble(bubbleDrag.startRight - bubbleDrag.translation.x,
                                      bubbleDrag.startBottom - bubbleDrag.translation.y)
            }
        }

        Tooltip {
            x: bubble.width - width
            y: -height - Theme.space.xs
            text: qsTr("AI assistant")
            shortcut: assistantCommand.shortcut
            visible: bubbleHover.hovered && !bubbleTap.pressed && !bubbleDrag.active
        }
    }

    Item {
        id: card

        readonly property bool wanted: corner.open && corner.allowed

        objectName: "aiCard"
        visible: opacity > 0
        opacity: card.wanted ? 1 : 0
        scale: card.wanted ? 1 : Theme.motion.enterScale
        transformOrigin: Item.BottomRight
        width: Math.min(Theme.size.aiCardWidth, corner.width - 2 * Theme.space.sm)
        height: Math.min(Theme.size.aiCardHeight, corner.height - 2 * Theme.space.sm)
        x: Math.max(0, corner.width - card.width - Math.min(corner.rightGap, corner.width - card.width))
        y: Math.max(0, corner.height - card.height - Math.min(corner.bottomGap, corner.height - card.height))
        transform: Translate {
            y: card.wanted ? 0 : Theme.space.md

            Behavior on y {
                NumberAnimation {
                    duration: Theme.motion.medium
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.motion.emphasized
                }
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.fade
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }

        Behavior on scale {
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }

        PopupSurface {
            anchors.fill: parent
            radius: Theme.radius.lg
        }

        Item {
            id: header

            objectName: "aiCardHeader"
            width: parent.width
            height: Theme.size.toolbar

            DragHandler {
                id: drag

                property real startRight: 0
                property real startBottom: 0

                target: null
                onActiveChanged: {
                    if (drag.active) {
                        drag.startRight = corner.rightGap
                        drag.startBottom = corner.bottomGap
                    }
                }
                onTranslationChanged: {
                    if (!drag.active)
                        return
                    corner.rightGap = Math.max(0, Math.min(corner.width - card.width,
                                                           drag.startRight - drag.translation.x))
                    corner.bottomGap = Math.max(0, Math.min(corner.height - card.height,
                                                            drag.startBottom - drag.translation.y))
                }
            }

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
                anchors.left: headerIcon.right
                anchors.leftMargin: Theme.space.sm
                anchors.right: actions.left
                anchors.rightMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("AI assistant")
                elide: Text.ElideRight
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.DemiBold
            }

            Row {
                id: actions

                anchors.right: parent.right
                anchors.rightMargin: Theme.space.xs
                anchors.verticalCenter: parent.verticalCenter

                ToolbarButton {
                    objectName: "aiNewConversation"
                    iconName: "plus"
                    text: qsTr("New conversation")
                    onClicked: AiChats.newConversation()
                }

                ToolbarButton {
                    id: recentButton

                    objectName: "aiRecent"
                    iconName: "history"
                    text: qsTr("Recent conversations")
                    onClicked: recentMenu.popup(recentButton, recentButton.width - recentMenu.width, recentButton.height)
                }

                ToolbarButton {
                    objectName: "aiPopout"
                    iconName: "popout"
                    text: qsTr("Open in a separate window")
                    onClicked: {
                        corner.open = false
                        AiChats.openWindow()
                    }
                }

                ToolbarButton {
                    objectName: "aiCollapse"
                    iconName: "chevron-down"
                    text: qsTr("Collapse")
                    shortcut: assistantCommand.shortcut
                    onClicked: corner.open = false
                }
            }
        }

        Rectangle {
            id: divider

            y: header.height
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }

        AiPanel {
            id: chat

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: divider.bottom
            anchors.bottom: parent.bottom
            color: "transparent"
            bottomLeftRadius: Theme.radius.lg
            bottomRightRadius: Theme.radius.lg
            context: corner.shownContext
            shown: card.wanted
            editorUnsaved: corner.editorUnsaved
        }

        Menu {
            id: recentMenu

            objectName: "aiRecentMenu"

            Instantiator {
                model: AiChats.recent.slice(0, AiChats.menuShown)

                delegate: MenuItem {
                    required property var modelData

                    objectName: "aiRecentItem"
                    choice: true
                    chosen: modelData.id === AiChats.current
                    text: modelData.title !== "" ? modelData.title : qsTr("Untitled conversation")
                    description: Qt.formatDateTime(modelData.updated, "yyyy-MM-dd hh:mm")
                    onTriggered: AiChats.open(modelData.id)
                }

                onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => recentMenu.removeItem(object)
            }

            MenuItem {
                objectName: "aiRecentEmpty"
                visible: AiChats.recent.length === 0
                height: visible ? implicitHeight : 0
                enabled: false
                text: qsTr("No saved conversations yet")
            }

            MenuItem {
                objectName: "aiRecentAll"
                visible: AiChats.recent.length > 0
                height: visible ? implicitHeight : 0
                iconName: "history"
                text: AiChats.recent.length > AiChats.menuShown
                      ? qsTr("Show all %n conversations…", "", AiChats.recent.length) : qsTr("Show all…")
                onTriggered: {
                    corner.open = false
                    AiChats.openWindow()
                }
            }

            MenuSeparator {
                visible: AiChats.recent.length > 0
                height: visible ? implicitHeight : 0
            }

            MenuItem {
                objectName: "aiClearAll"
                visible: AiChats.recent.length > 0
                height: visible ? implicitHeight : 0
                danger: true
                iconName: "trash-2"
                text: qsTr("Delete all conversations")
                onTriggered: AiChats.clearAll()
            }
        }
    }

    CommandState {
        id: assistantCommand

        command: "app.assistant"
    }

    TourOverlay {
        id: tour

        anchors.fill: parent
        objectName: "assistantTour"
        tourKey: "assistant"
        settings: AppSettings
        startWhen: card.wanted && !AppSettings.toursQuiet
        steps: [
            {
                target: card,
                title: qsTr("The AI assistant"),
                text: qsTr("It knows which window you are in and which profile you are looking at. Ask it to set "
                           + "up keys, write blueprints and scripts, explain blocks or find out why a phone does not "
                           + "connect. Whatever it changes goes into a draft first and only counts once you agree.")
            },
            {
                target: function() { return tour.findItem(card, "aiNewConversation") },
                title: qsTr("New conversation"),
                text: qsTr("Start a new conversation for a new topic; the earlier ones are kept.")
            },
            {
                target: recentButton,
                title: qsTr("Recent conversations"),
                text: qsTr("Every conversation is kept on this computer. The newest %n are listed here; Show all "
                           + "opens the rest in a separate window. Pick one up again or delete them.", "",
                           AiChats.menuShown)
            },
            {
                target: function() { return tour.findItem(card, "aiPopout") },
                title: qsTr("A separate window"),
                text: qsTr("Chat in a window of its own, with the recent conversations on the left; it remembers "
                           + "its size and place.")
            },
            {
                target: function() { return tour.findItem(card, "aiCollapse") },
                title: qsTr("Collapse"),
                text: qsTr("The card folds away and the bubble stays in the corner. The shortcut brings it back in "
                           + "any window."),
                keys: assistantCommand.shortcut
            },
            {
                target: chat,
                title: qsTr("Ask"),
                text: qsTr("Type a question below and press Enter. While it answers you can follow its thinking and "
                           + "the tools it uses. If no service is set up yet, set one up under AI in Settings "
                           + "first.")
            }
        ]
    }
}
