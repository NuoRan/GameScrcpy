pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.Control {
    id: control

    property var keys: []
    property string placeholderText: qsTr("Click, then press a key")
    property string conflictText
    property bool clearable: true
    readonly property bool capturing: internal.capturing
    readonly property bool bound: keys.length > 0
    readonly property real messageHeight: conflictText !== "" ? message.implicitHeight + Theme.space.xs : 0

    signal keyCaptured(int key, int nativeScanCode, int modifiers)
    signal mouseCaptured(int button, int modifiers)
    signal wheelCaptured(int dx, int dy, int modifiers)
    signal cleared()

    function begin() {
        internal.pendingKey = 0
        internal.capturing = true
        if (!activeFocus)
            forceActiveFocus(Qt.OtherFocusReason)
    }

    function cancel() {
        internal.capturing = false
        internal.pendingKey = 0
    }

    implicitWidth: Theme.size.fieldWidth
    implicitHeight: Theme.size.control + messageHeight
    padding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity

    Accessible.role: Accessible.Button
    Accessible.description: capturing ? waiting.text : keys.map(key => key.text).join(" + ")

    onActiveFocusChanged: {
        if (!activeFocus)
            cancel()
    }

    Keys.onShortcutOverride: event => event.accepted = internal.capturing || internal.clears(event)
    Keys.onPressed: event => {
        if (!internal.capturing) {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                event.accepted = true
                control.begin()
            } else if (internal.clears(event)) {
                event.accepted = true
                control.cleared()
            }
            return
        }
        event.accepted = true
        if (event.isAutoRepeat)
            return
        if (internal.isModifier(event.key)) {
            internal.pendingKey = event.key
            internal.pendingScanCode = event.nativeScanCode
            return
        }
        const key = event.key
        const scanCode = event.nativeScanCode
        const modifiers = event.modifiers
        control.cancel()
        control.keyCaptured(key, scanCode, modifiers)
    }
    Keys.onReleased: event => {
        if (!internal.capturing)
            return
        event.accepted = true
        if (event.isAutoRepeat || internal.pendingKey === 0 || event.key !== internal.pendingKey)
            return
        const key = internal.pendingKey
        const scanCode = internal.pendingScanCode
        control.cancel()
        control.keyCaptured(key, scanCode, Qt.NoModifier)
    }

    QtObject {
        id: internal

        property bool capturing: false
        property int pendingKey: 0
        property int pendingScanCode: 0

        function isModifier(key: int): bool {
            return key === Qt.Key_Control || key === Qt.Key_Shift || key === Qt.Key_Alt || key === Qt.Key_Meta
        }

        function clears(event: KeyEvent): bool {
            return control.clearable && control.bound && event.modifiers === Qt.NoModifier
                   && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)
        }
    }

    background: FieldFrame {
        implicitWidth: Theme.size.fieldWidth
        height: Theme.size.control
        focused: control.capturing || control.visualFocus
        warning: control.conflictText !== ""
        hovered: area.containsMouse
    }

    Rectangle {
        objectName: "capturePulse"
        width: control.width
        height: Theme.size.control
        radius: Theme.radius.sm
        color: "transparent"
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accent
        visible: control.capturing && !Theme.reducedMotion
        opacity: Theme.disabledOpacity

        SequentialAnimation on opacity {
            running: control.capturing && !Theme.reducedMotion
            loops: Animation.Infinite

            NumberAnimation {
                from: Theme.disabledOpacity
                to: 1
                duration: Theme.motion.pulse / 2
                easing.type: Easing.InOutSine
            }

            NumberAnimation {
                from: 1
                to: Theme.disabledOpacity
                duration: Theme.motion.pulse / 2
                easing.type: Easing.InOutSine
            }
        }
    }

    Item {
        id: face

        objectName: "keyCaptureFace"
        width: control.width
        height: Theme.size.control

        MouseArea {
            id: area

            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.PointingHandCursor

            onPressed: mouse => {
                if (!control.capturing) {
                    if (mouse.button !== Qt.LeftButton) {
                        mouse.accepted = false
                        return
                    }
                    control.begin()
                    return
                }
                const button = mouse.button
                const modifiers = mouse.modifiers
                control.cancel()
                control.mouseCaptured(button, modifiers)
            }
            onWheel: wheel => {
                if (!control.capturing || (wheel.angleDelta.x === 0 && wheel.angleDelta.y === 0)) {
                    wheel.accepted = control.capturing
                    return
                }
                const dx = wheel.angleDelta.x
                const dy = wheel.angleDelta.y
                const modifiers = wheel.modifiers
                control.cancel()
                control.wheelCaptured(dx, dy, modifiers)
            }
        }

        Item {
            id: caps

            objectName: "keyCaps"
            visible: !control.capturing && control.bound
            x: Theme.space.sm
            width: end.x - x - Theme.space.xs
            height: parent.height
            clip: true

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xs

                Repeater {
                    model: control.keys

                    delegate: Row {
                        id: cap

                        required property var modelData
                        required property int index

                        spacing: Theme.space.xs

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: cap.index > 0
                            textFormat: Text.PlainText
                            text: "+"
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.micro.size
                        }

                        Keycap {
                            anchors.verticalCenter: parent.verticalCenter
                            large: true
                            text: cap.modelData.text
                            iconName: cap.modelData.mouse === true ? "mouse" : ""
                        }
                    }
                }
            }
        }

        Text {
            objectName: "keyPlaceholder"
            visible: !control.capturing && !control.bound
            x: Theme.space.md
            width: end.x - x - Theme.space.xs
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: control.placeholderText
            elide: Text.ElideRight
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.label.size
        }

        Row {
            visible: control.capturing
            x: Theme.space.md
            width: end.x - x - Theme.space.xs
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.space.sm
                height: width
                radius: width / 2
                color: Theme.colors.accent

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width + 2 * Theme.size.fieldGlow
                    height: width
                    radius: width / 2
                    z: -1
                    color: Theme.colors.accentRing
                }
            }

            Text {
                id: waiting

                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - Theme.space.sm - parent.spacing
                textFormat: Text.PlainText
                text: qsTr("Waiting for a key…")
                elide: Text.ElideRight
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
            }
        }

        Item {
            id: end

            readonly property real inset: (Theme.size.control - Theme.size.controlSmall) / 2

            x: parent.width - width - inset
            width: endButton.visible ? endButton.width : Theme.size.iconSmall + Theme.space.sm
            height: parent.height

            Icon {
                anchors.centerIn: parent
                visible: !endButton.visible
                name: "keyboard"
                size: Theme.size.iconSmall
                color: Theme.colors.textTertiary
            }

            Button {
                id: endButton

                objectName: "keyCaptureEnd"
                anchors.centerIn: parent
                visible: control.capturing || (control.bound && control.clearable)
                small: true
                tone: "ghost"
                iconName: "x"
                focusPolicy: Qt.NoFocus
                toolTip: control.capturing ? qsTr("Stop waiting") : qsTr("Remove the key")
                onClicked: {
                    if (control.capturing)
                        control.cancel()
                    else
                        control.cleared()
                }
            }
        }
    }

    Text {
        id: message

        objectName: "keyConflict"
        visible: control.conflictText !== ""
        y: Theme.size.control + Theme.space.xs
        width: control.width
        textFormat: Text.PlainText
        text: control.conflictText
        color: Theme.colors.warningText
        font.pixelSize: Theme.type.caption.size
        wrapMode: Text.Wrap
    }
}
