pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: editor

    property rect picture
    property rect box: Qt.rect(0, 0, 1, 1)
    property rect area: Qt.rect(0, 0, 1, 1)
    readonly property real smallest: 0.05
    readonly property rect shown: Qt.rect(picture.x + area.x * picture.width, picture.y + area.y * picture.height,
                                          area.width * picture.width, area.height * picture.height)

    signal applied(rect area)
    signal cancelled()
    signal automatic()

    function place(left: real, top: real, right: real, bottom: real) {
        const x1 = Math.max(0, Math.min(left, 1 - editor.smallest))
        const y1 = Math.max(0, Math.min(top, 1 - editor.smallest))
        const x2 = Math.min(1, Math.max(right, x1 + editor.smallest))
        const y2 = Math.min(1, Math.max(bottom, y1 + editor.smallest))
        editor.area = Qt.rect(x1, y1, x2 - x1, y2 - y1)
    }

    function fractionX(at: real): real {
        return editor.picture.width > 0 ? (at - editor.picture.x) / editor.picture.width : 0
    }

    function fractionY(at: real): real {
        return editor.picture.height > 0 ? (at - editor.picture.y) / editor.picture.height : 0
    }

    onBoxChanged: area = box
    onVisibleChanged: {
        if (visible)
            area = box
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        x: editor.picture.x
        y: editor.picture.y
        width: editor.picture.width
        height: editor.shown.y - editor.picture.y
        color: Theme.colors.canvasDim
    }

    Rectangle {
        x: editor.picture.x
        y: editor.shown.y + editor.shown.height
        width: editor.picture.width
        height: editor.picture.y + editor.picture.height - y
        color: Theme.colors.canvasDim
    }

    Rectangle {
        x: editor.picture.x
        y: editor.shown.y
        width: editor.shown.x - editor.picture.x
        height: editor.shown.height
        color: Theme.colors.canvasDim
    }

    Rectangle {
        x: editor.shown.x + editor.shown.width
        y: editor.shown.y
        width: editor.picture.x + editor.picture.width - x
        height: editor.shown.height
        color: Theme.colors.canvasDim
    }

    Rectangle {
        id: frame

        objectName: "cropBox"
        x: editor.shown.x
        y: editor.shown.y
        width: editor.shown.width
        height: editor.shown.height
        color: "transparent"
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accent

        MouseArea {
            property point grabbed
            property rect started

            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            preventStealing: true
            onPressed: mouse => {
                grabbed = mapToItem(editor, mouse.x, mouse.y)
                started = editor.area
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const at = mapToItem(editor, mouse.x, mouse.y)
                const dx = editor.picture.width > 0 ? (at.x - grabbed.x) / editor.picture.width : 0
                const dy = editor.picture.height > 0 ? (at.y - grabbed.y) / editor.picture.height : 0
                const left = Math.max(0, Math.min(started.x + dx, 1 - started.width))
                const top = Math.max(0, Math.min(started.y + dy, 1 - started.height))
                editor.area = Qt.rect(left, top, started.width, started.height)
            }
        }
    }

    component Handle: Rectangle {
        id: handle

        required property bool atLeft
        required property bool atTop

        x: (handle.atLeft ? editor.shown.x : editor.shown.x + editor.shown.width) - width / 2
        y: (handle.atTop ? editor.shown.y : editor.shown.y + editor.shown.height) - height / 2
        width: Theme.space.md
        height: width
        radius: width / 2
        color: Theme.colors.bgElevated
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accent

        MouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.space.xs
            cursorShape: handle.atLeft === handle.atTop ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
            preventStealing: true
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const at = mapToItem(editor, mouse.x, mouse.y)
                const x = editor.fractionX(at.x)
                const y = editor.fractionY(at.y)
                const area = editor.area
                editor.place(handle.atLeft ? x : area.x, handle.atTop ? y : area.y,
                             handle.atLeft ? area.x + area.width : x, handle.atTop ? area.y + area.height : y)
            }
        }
    }

    Handle {
        objectName: "cropHandleTopLeft"
        atLeft: true
        atTop: true
    }

    Handle {
        objectName: "cropHandleTopRight"
        atLeft: false
        atTop: true
    }

    Handle {
        objectName: "cropHandleBottomLeft"
        atLeft: true
        atTop: false
    }

    Handle {
        objectName: "cropHandleBottomRight"
        atLeft: false
        atTop: false
    }

    Rectangle {
        id: bar

        objectName: "cropBar"
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(editor.picture.y + Theme.space.md,
                    editor.picture.y + editor.picture.height - height - Theme.space.lg)
        width: row.implicitWidth + 2 * Theme.space.md
        height: row.implicitHeight + 2 * Theme.space.sm
        radius: Theme.radius.lg
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeDefault

        Row {
            id: row

            anchors.centerIn: parent
            spacing: Theme.space.sm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("Drag the box onto the phone screen")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            Button {
                objectName: "cropApply"
                small: true
                tone: "primary"
                text: qsTr("Apply")
                onClicked: editor.applied(editor.area)
            }

            Button {
                objectName: "cropAutomatic"
                small: true
                tone: "ghost"
                text: qsTr("Back to automatic")
                onClicked: editor.automatic()
            }

            Button {
                objectName: "cropCancel"
                small: true
                tone: "ghost"
                text: qsTr("Cancel")
                onClicked: editor.cancelled()
            }
        }
    }
}
