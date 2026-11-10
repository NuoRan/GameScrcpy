pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.App

Item {
    id: view

    property var entry: null
    property string waitingFor: view.entry?.extensionName ?? ""
    property int inset: Theme.space.lg
    property var tree: null
    readonly property string key: view.entry?.key ?? ""

    signal released()

    implicitHeight: view.tree !== null ? scroller.contentHeight : waiting.implicitHeight + 2 * view.inset

    function refresh() {
        view.tree = view.key !== "" ? ExtensionPanels.tree(view.key) ?? null : null
    }

    function nodeAt(path: var): var {
        let found = view.tree ?? null
        for (let at = 0; found !== null && at < path.length; ++at)
            found = (found.children ?? [])[path[at]] ?? null
        return found
    }

    function send(kind: string, target: string, value: var) {
        if (view.entry !== null)
            ExtensionPanels.send(view.entry.logic, view.entry.name, kind, target, value)
    }

    function release() {
        view.released()
    }

    onKeyChanged: view.refresh()
    Component.onCompleted: view.refresh()

    Connections {
        target: ExtensionPanels

        function onTreeChanged(key: string) {
            if (key === view.key)
                view.refresh()
        }
    }

    Flickable {
        id: scroller

        objectName: "panelScroller"
        anchors.fill: parent
        visible: view.tree !== null
        contentWidth: width
        contentHeight: rootNode.implicitHeight + 2 * view.inset
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        UiNode {
            id: rootNode

            x: view.inset
            y: view.inset
            width: scroller.width - 2 * view.inset
            host: view
        }
    }

    Text {
        id: waiting

        objectName: "panelWaiting"
        anchors.centerIn: parent
        width: parent.width - 2 * view.inset
        visible: view.tree === null && view.waitingFor !== ""
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        text: qsTr("Waiting for %1 to draw this panel").arg(view.waitingFor)
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.label.size
        wrapMode: Text.Wrap
    }
}
