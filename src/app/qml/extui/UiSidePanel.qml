pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

FocusScope {
    id: side

    required property string serial
    property var panels: []
    property int topInset: 0
    property string current: ""
    property bool open: false
    property bool allowed: true
    property int panelWidth: Theme.size.extensionPanel
    property var opened: null
    property real reveal: side.shown ? 1 : 0
    readonly property var entry: side.panels.find(each => each.id === side.current) ?? null
    readonly property bool shown: side.open && side.allowed && side.entry !== null
    readonly property bool typing: side.activeFocus && side.shown

    signal focusReleased()

    function clampWidth(wanted: real): int {
        return Math.round(Math.max(Theme.size.extensionPanelMin, Math.min(Theme.size.extensionPanelMax, wanted)))
    }

    function show(id: string) {
        side.current = id
        side.open = true
        side.remember()
    }

    function reopen() {
        const remembered = side.panels.find(each => each.id === side.current)
        if (remembered !== undefined)
            side.show(remembered.id)
        else if (side.panels.length > 0)
            side.show(side.panels[0].id)
    }

    function close() {
        side.open = false
        side.remember()
    }

    function remember() {
        ExtensionPanels.setSideState(side.serial, side.current, side.open, side.panelWidth)
    }

    function announce() {
        const wanted = side.shown ? side.entry : null
        const before = side.opened !== null ? side.opened.key : ""
        const after = wanted !== null ? wanted.key : ""
        if (before === after)
            return
        if (side.opened !== null)
            ExtensionPanels.send(side.opened.logic, side.opened.name, "close", "", null)
        side.opened = wanted
        if (wanted !== null)
            ExtensionPanels.send(wanted.logic, wanted.name, "open", "", null)
    }

    objectName: "extensionSidePanel"
    x: parent.width - width - Theme.space.sm
    y: side.topInset + Theme.space.sm
    width: Math.max(0, Math.min(side.panelWidth, parent.width - 2 * Theme.space.sm))
    height: Math.max(0, parent.height - side.topInset - 2 * Theme.space.sm)
    visible: side.reveal > 0

    Behavior on reveal {
        NumberAnimation {
            duration: Theme.motion.base
            easing.type: Easing.BezierSpline
            easing.bezierCurve: side.shown ? Theme.motion.emphasized : Theme.motion.exit
        }
    }

    transform: Translate {
        x: (1 - side.reveal) * (side.width + Theme.space.sm)
    }

    Accessible.role: Accessible.Pane
    Accessible.name: side.entry !== null ? side.entry.title : qsTr("Extension panel")

    onShownChanged: {
        side.announce()
        if (!side.shown && side.activeFocus)
            side.focusReleased()
    }
    onEntryChanged: side.announce()
    Keys.onEscapePressed: side.focusReleased()
    Component.onCompleted: {
        const state = ExtensionPanels.sideState(side.serial)
        side.current = state.panel ?? ""
        side.open = state.open === true
        if ((state.width ?? 0) > 0)
            side.panelWidth = side.clampWidth(state.width)
        side.announce()
    }
    Component.onDestruction: {
        if (side.opened !== null)
            ExtensionPanels.send(side.opened.logic, side.opened.name, "close", "", null)
    }

    Connections {
        target: ExtensionPanels

        function onCloseRequested(key: string) {
            if (side.shown && side.entry.key === key)
                side.close()
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }

    PopupSurface {
        anchors.fill: parent
        radius: Theme.radius.xl
    }

    Item {
        id: header

        width: parent.width
        height: Theme.size.toolbar

        T.AbstractButton {
            id: titleButton

            readonly property real room: titleButton.width - 3 * Theme.space.sm - Theme.size.icon
                                         - (titleButton.enabled ? Theme.size.iconSmall + Theme.space.xs : 0)

            objectName: "extensionPanelTitle"
            x: Theme.space.xs
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, closeButton.x - titleButton.x - Theme.space.xs)
            height: Theme.size.toolbarButton
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            enabled: side.panels.length > 1
            text: side.entry !== null ? side.entry.title : ""
            Accessible.name: titleButton.text
            Accessible.description: side.entry !== null ? side.entry.extensionName : ""
            onClicked: switchMenu.open()

            background: Rectangle {
                id: titleFace

                radius: Theme.radius.md
                color: "transparent"

                StateLayer {
                    anchors.fill: parent
                    radius: titleFace.radius
                    hovered: titleButton.hovered && titleButton.enabled
                    pressed: titleButton.down
                }

                FocusRing {
                    visible: titleButton.visualFocus
                    baseRadius: titleFace.radius
                }
            }

            contentItem: Item {
                Row {
                    x: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.sm

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: side.entry !== null && side.entry.icon !== "" ? side.entry.icon : "puzzle"
                        size: Theme.size.icon
                        color: Theme.colors.accentText
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, titleButton.room)

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: titleButton.text
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            visible: text !== ""
                            textFormat: Text.PlainText
                            text: side.entry !== null ? side.entry.extensionName : ""
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                            elide: Text.ElideRight
                        }
                    }

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: titleButton.enabled
                        name: "chevron-down"
                        size: Theme.size.iconSmall
                        color: Theme.colors.textSecondary
                    }
                }
            }

            Menu {
                id: switchMenu

                objectName: "extensionPanelSwitch"
                y: titleButton.height + Theme.space.xs

                Instantiator {
                    model: side.panels

                    delegate: MenuItem {
                        required property var modelData

                        choice: true
                        chosen: modelData.id === side.current
                        text: modelData.title
                        shortcut: modelData.extensionName
                        onTriggered: side.show(modelData.id)
                    }

                    onObjectAdded: (index, object) => switchMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => switchMenu.removeItem(object)
                }
            }
        }

        ToolbarButton {
            id: closeButton

            objectName: "extensionPanelClose"
            x: header.width - closeButton.width - Theme.space.xs
            anchors.verticalCenter: parent.verticalCenter
            iconName: "x"
            text: qsTr("Close the panel")
            onClicked: side.close()
        }
    }

    Rectangle {
        y: header.height
        width: parent.width
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    UiPanelView {
        objectName: "extensionPanelView"
        y: header.height + Theme.size.stroke
        width: parent.width
        height: Math.max(0, parent.height - y)
        entry: side.entry
        onReleased: side.focusReleased()
    }

    MouseArea {
        id: grip

        property real startX: 0
        property int startWidth: 0

        objectName: "extensionPanelGrip"
        x: -Theme.space.xs
        width: Theme.space.sm
        height: parent.height
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        onPressed: mouse => {
            grip.startX = grip.mapToItem(null, mouse.x, mouse.y).x
            grip.startWidth = side.panelWidth
        }
        onPositionChanged: mouse => {
            if (grip.pressed)
                side.panelWidth = side.clampWidth(grip.startWidth + grip.startX - grip.mapToItem(null, mouse.x, mouse.y).x)
        }
        onReleased: side.remember()
    }
}
