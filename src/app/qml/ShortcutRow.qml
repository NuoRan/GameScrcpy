pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: row

    required property string commandId
    required property string title
    required property var keys
    required property string defaultText
    required property string defaultPortable
    required property bool custom
    required property bool conflicted
    property bool divider: false
    property bool capturing: false
    property string pending: ""
    readonly property var taken: pending === "" ? [] : Commands.conflicts(commandId, pending)
    readonly property bool loose: pending !== "" && Commands.plainKeys(pending)
    readonly property var shown: pending !== "" ? Commands.keyCapsOf(pending) : keys
    readonly property int inset: Theme.space.lg + Theme.space.xxs

    function begin() {
        capturing = true
        capture.forceActiveFocus(Qt.OtherFocusReason)
    }

    function finish() {
        capturing = false
        pending = ""
    }

    function take(keys: string) {
        if (Commands.conflicts(commandId, keys).length > 0 || Commands.plainKeys(keys)) {
            pending = keys
            return
        }
        finish()
        Commands.setShortcut(commandId, keys)
    }

    function confirm() {
        const keys = pending
        finish()
        Commands.setShortcut(commandId, keys)
    }

    function restore() {
        if (defaultPortable !== "" && Commands.conflicts(commandId, defaultPortable).length > 0) {
            capturing = true
            pending = defaultPortable
            return
        }
        Commands.resetShortcut(commandId)
    }

    implicitWidth: inset + titleText.implicitWidth + Theme.space.xxl + trailing.implicitWidth + inset
    implicitHeight: Math.max(texts.implicitHeight, trailing.implicitHeight) + 2 * (Theme.space.md + Theme.space.xxs)

    KeyCapture {
        id: capture

        anchors.fill: parent
        enabled: row.capturing && row.pending === ""
        onCaptured: keys => row.take(keys)
        onCancelled: row.finish()
        onCleared: {
            row.finish()
            Commands.setShortcut(row.commandId, "")
        }
        onActiveFocusChanged: {
            if (!activeFocus && row.pending === "")
                row.capturing = false
        }
    }

    Rectangle {
        visible: row.divider
        x: row.inset
        width: row.width - 2 * row.inset
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    Column {
        id: texts

        anchors.left: parent.left
        anchors.right: trailing.left
        anchors.leftMargin: row.inset
        anchors.rightMargin: Theme.space.xxl
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space.xxs

        Row {
            width: parent.width
            spacing: Theme.space.sm + Theme.space.xxs

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: Commands.icon(row.commandId)
                size: Theme.size.iconSmall
                color: Theme.colors.textTertiary
            }

            Text {
                id: titleText

                width: parent.width - Theme.size.iconSmall - parent.spacing
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: row.title
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
                font.weight: Font.Medium
            }
        }

        Text {
            readonly property string warning: [
                row.taken.length > 0 ? qsTr("Also used by “%1”. Using it here removes it there.").arg(row.taken.join("”, “")) : "",
                row.loose ? qsTr("Keys without Ctrl, Alt or Win also work buttons and lists, which then stop responding to this one.") : ""
            ].filter(line => line !== "").join(" ")
            readonly property string message: warning !== "" ? warning
                : !row.capturing && row.conflicted ? qsTr("Not in effect: another command uses these keys.") : ""

            visible: message !== ""
            width: parent.width
            textFormat: Text.PlainText
            text: message
            color: warning !== "" || (!row.capturing && row.conflicted) ? Theme.colors.warningText
                                                                         : Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
            wrapMode: Text.Wrap
        }
    }

    RowLayout {
        id: trailing

        anchors.right: parent.right
        anchors.rightMargin: row.inset
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space.sm

        Row {
            visible: row.shown.length > 0 && (!row.capturing || row.pending !== "")
            spacing: Theme.space.xxs

            Repeater {
                model: row.shown

                delegate: Keycap {
                    required property string modelData

                    text: modelData
                }
            }
        }

        Text {
            visible: row.shown.length === 0 && !row.capturing
            textFormat: Text.PlainText
            text: qsTr("Not set")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Rectangle {
            objectName: "captureField"
            visible: row.capturing && row.pending === ""
            Layout.preferredWidth: Theme.size.captureField
            Layout.preferredHeight: Theme.size.control
            radius: Theme.radius.sm
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.accent

            Rectangle {
                anchors.fill: parent
                anchors.margins: -Theme.size.fieldGlow
                radius: parent.radius + Theme.size.fieldGlow
                color: "transparent"
                border.width: Theme.size.fieldGlow
                border.color: Theme.colors.accentSubtle
            }

            Rectangle {
                id: recording

                x: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.space.sm
                height: width
                radius: width / 2
                color: Theme.colors.accent
            }

            Text {
                anchors.left: recording.right
                anchors.leftMargin: Theme.space.sm
                anchors.right: escapeHint.left
                anchors.rightMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: qsTr("Press the new keys…")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
            }

            Text {
                id: escapeHint

                anchors.right: parent.right
                anchors.rightMargin: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("Esc cancels · Backspace clears")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }
        }

        Button {
            visible: row.pending !== ""
            tone: "primary"
            small: true
            text: row.taken.length > 0 ? qsTr("Use here") : qsTr("Use anyway")
            onClicked: row.confirm()
        }

        Button {
            visible: row.pending !== ""
            tone: "ghost"
            small: true
            text: qsTr("Cancel")
            onClicked: row.finish()
        }

        Button {
            visible: !row.capturing
            tone: "ghost"
            small: true
            text: row.keys.length > 0 ? qsTr("Change") : qsTr("Set")
            Accessible.description: row.title
            onClicked: row.begin()
        }

        Button {
            visible: !row.capturing && row.keys.length > 0
            tone: "ghost"
            small: true
            iconName: "x"
            toolTip: qsTr("Remove the shortcut")
            Accessible.name: toolTip
            Accessible.description: row.title
            onClicked: Commands.setShortcut(row.commandId, "")
        }

        Button {
            visible: !row.capturing && row.custom
            tone: "ghost"
            small: true
            iconName: "rotate-ccw"
            toolTip: row.defaultText === "" ? qsTr("Restore the default (none)")
                                            : qsTr("Restore the default (%1)").arg(row.defaultText)
            Accessible.name: toolTip
            Accessible.description: row.title
            onClicked: row.restore()
        }
    }
}
