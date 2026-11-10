import QtQuick
import GameScrcpy.Theme

Item {
    id: workbench

    property alias barContent: barSlot.data
    property alias leftContent: leftSlot.data
    property alias sidebarContent: sidebarSlot.data
    property alias toolsContent: tools.content
    property alias bottomContent: bottomSlot.data
    property bool leftOpen: false
    property bool bottomOpen: false
    property bool barShown: true
    property bool bodyShown: true
    property bool toolsShown: true
    property bool toolsBelow: false
    property int sidebarWidth: Theme.size.workbenchSidebar
    readonly property int barHeight: barShown ? Theme.size.workbenchBar : 0
    readonly property int leftWidth: leftOpen ? Theme.size.workbenchLeft : 0
    readonly property int toolsRoom: toolsBelow ? tools.height + 2 * Theme.space.md : 0
    readonly property rect canvasRect: Qt.rect(leftWidth, barHeight,
                                               Math.max(0, width - sidebarWidth - leftWidth),
                                               Math.max(0, height - barHeight - toolsRoom
                                                        - (bottomOpen ? Theme.size.workbenchBottom : 0)))

    Rectangle {
        id: barArea

        objectName: "workbenchBar"
        visible: workbench.barShown
        width: parent.width
        height: workbench.barHeight
        color: Theme.colors.bgBase

        Item {
            id: barSlot

            anchors.fill: parent
            anchors.leftMargin: Theme.space.lg
            anchors.rightMargin: Theme.space.lg
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }
    }

    Rectangle {
        id: leftArea

        objectName: "workbenchLeft"
        visible: workbench.leftOpen && workbench.bodyShown
        y: barArea.height
        width: Theme.size.workbenchLeft
        height: Math.max(0, parent.height - barArea.height)
        color: Theme.colors.bgRaised

        Item {
            id: leftSlot

            anchors.fill: parent
            anchors.rightMargin: Theme.size.stroke
        }

        Rectangle {
            anchors.right: parent.right
            width: Theme.size.stroke
            height: parent.height
            color: Theme.colors.strokeSubtle
        }
    }

    Rectangle {
        id: sidebarArea

        objectName: "workbenchSidebar"
        visible: workbench.bodyShown
        anchors.right: parent.right
        y: barArea.height
        width: workbench.sidebarWidth
        height: Math.max(0, parent.height - barArea.height)
        color: Theme.colors.bgRaised

        Rectangle {
            width: Theme.size.stroke
            height: parent.height
            color: Theme.colors.strokeSubtle
        }

        Item {
            id: sidebarSlot

            anchors.fill: parent
            anchors.leftMargin: Theme.size.stroke
        }
    }

    Rectangle {
        id: bottomArea

        objectName: "workbenchBottom"
        visible: workbench.bottomOpen && workbench.bodyShown
        x: workbench.canvasRect.x
        y: workbench.canvasRect.y + workbench.canvasRect.height + workbench.toolsRoom
        width: workbench.canvasRect.width
        height: Theme.size.workbenchBottom
        color: Theme.colors.bgRaised

        Rectangle {
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }

        Item {
            id: bottomSlot

            anchors.fill: parent
            anchors.topMargin: Theme.size.stroke
        }
    }

    FloatingToolbar {
        id: tools

        objectName: "workbenchTools"
        visible: workbench.toolsShown && workbench.bodyShown && content.length > 0 && opacity > 0
        pinned: true
        x: workbench.canvasRect.x + (workbench.canvasRect.width - width) / 2
        y: workbench.toolsBelow ? workbench.canvasRect.y + workbench.canvasRect.height + Theme.space.md
                                : workbench.canvasRect.y + workbench.canvasRect.height - height - Theme.space.lg
        Component.onCompleted: reveal()
    }
}
