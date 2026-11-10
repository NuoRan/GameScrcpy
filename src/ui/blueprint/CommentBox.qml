pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: box

    required property string key
    property var info: ({})
    property string shape: ""
    property real moveX: 0
    property real moveY: 0
    property bool selected: false
    property bool readOnly: false
    property real stretchWidth: -1
    property real stretchHeight: -1
    property bool renaming: false
    readonly property string tint: info.color ?? "neutral"

    signal titlePressed(int button, int modifiers, point scene)
    signal titleMoved(point scene)
    signal titleReleased(point scene)
    signal renamed(string title)
    signal resized(real width, real height)
    signal renameFinished()

    function apply(next: var) {
        const text = JSON.stringify(next)
        if (text !== shape) {
            shape = text
            info = next
        }
    }

    function rename() {
        if (readOnly)
            return
        renaming = true
        field.text = info.title ?? ""
        field.selectAll()
        field.forceActiveFocus(Qt.MouseFocusReason)
    }

    function finishRename(keep: bool) {
        if (!renaming)
            return
        renaming = false
        if (keep && field.text !== (info.title ?? ""))
            renamed(field.text)
        renameFinished()
    }

    objectName: "bpComment_" + key
    x: (info.x ?? 0) + moveX
    y: (info.y ?? 0) + moveY
    width: stretchWidth >= 0 ? stretchWidth : info.width ?? 0
    height: stretchHeight >= 0 ? stretchHeight : info.height ?? 0

    Accessible.role: Accessible.Grouping
    Accessible.name: info.title ?? ""

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.lg + Theme.space.xxs
        color: BlueprintLook.commentFill(box.tint)
        border.width: box.selected ? Theme.size.focusRing : Theme.size.stroke
        border.color: box.selected ? Theme.colors.accent : BlueprintLook.commentStroke(box.tint)
    }

    Text {
        x: Theme.space.md
        y: Theme.space.sm
        width: parent.width - 2 * x
        visible: !box.renaming
        textFormat: Text.PlainText
        text: box.info.title ?? ""
        elide: Text.ElideRight
        color: box.tint === "neutral" ? Theme.colors.textTertiary : Theme.colors.textSecondary
        font.pixelSize: Theme.type.caption.size
        font.weight: Font.DemiBold
    }

    MouseArea {
        id: titleArea

        width: parent.width
        height: Theme.size.bpCommentTitle
        acceptedButtons: Qt.AllButtons
        preventStealing: true
        enabled: !box.renaming
        cursorShape: box.readOnly ? Qt.ArrowCursor : Qt.SizeAllCursor
        onPressed: mouse => box.titlePressed(mouse.button, mouse.modifiers,
                                             titleArea.mapToItem(null, mouse.x, mouse.y))
        onPositionChanged: mouse => box.titleMoved(titleArea.mapToItem(null, mouse.x, mouse.y))
        onReleased: mouse => box.titleReleased(titleArea.mapToItem(null, mouse.x, mouse.y))
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                box.rename()
        }
    }

    TextField {
        id: field

        objectName: "bpCommentTitle_" + box.key
        x: Theme.space.sm
        y: Theme.space.xxs
        width: Math.min(parent.width - 2 * x, Theme.size.fieldWidth)
        visible: box.renaming
        dense: true
        onAccepted: box.finishRename(true)
        onActiveFocusChanged: {
            if (!field.activeFocus)
                box.finishRename(true)
        }
        Keys.onEscapePressed: box.finishRename(false)
    }

    Item {
        objectName: "bpCommentResize_" + box.key
        visible: !box.readOnly
        x: parent.width - width
        y: parent.height - height
        width: Theme.size.bpResize
        height: Theme.size.bpResize

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space.xs
            width: Theme.space.xs + Theme.space.xxs
            height: width
            radius: Theme.radius.xs / 2
            color: grip.containsMouse || grip.pressed ? Theme.colors.textSecondary : Theme.colors.strokeStrong
        }

        MouseArea {
            id: grip

            property point origin
            property real startWidth: 0
            property real startHeight: 0

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeFDiagCursor
            onPressed: mouse => {
                grip.origin = grip.mapToItem(box.parent, mouse.x, mouse.y)
                grip.startWidth = box.width
                grip.startHeight = box.height
                box.stretchWidth = box.width
                box.stretchHeight = box.height
            }
            onPositionChanged: mouse => {
                if (!grip.pressed)
                    return
                const at = grip.mapToItem(box.parent, mouse.x, mouse.y)
                box.stretchWidth = Math.max(Theme.size.bpCommentMinWidth, grip.startWidth + at.x - grip.origin.x)
                box.stretchHeight = Math.max(Theme.size.bpCommentMinHeight, grip.startHeight + at.y - grip.origin.y)
            }
            onReleased: {
                const width = Math.round(box.stretchWidth)
                const height = Math.round(box.stretchHeight)
                const changed = width !== Math.round(box.info.width ?? 0) || height !== Math.round(box.info.height ?? 0)
                if (changed)
                    box.resized(width, height)
                box.stretchWidth = -1
                box.stretchHeight = -1
            }
            onCanceled: {
                box.stretchWidth = -1
                box.stretchHeight = -1
            }
        }
    }
}
