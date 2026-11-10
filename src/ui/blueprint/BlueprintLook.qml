pragma Singleton

import QtQuick
import GameScrcpy.Theme

QtObject {
    id: look

    function tint(tone: string): color {
        switch (tone) {
        case "event":
            return Theme.colors.bpEventTint
        case "action":
            return Theme.colors.bpActionTint
        case "wait":
            return Theme.colors.bpWaitTint
        case "vision":
            return Theme.colors.bpVisionTint
        case "custom":
            return Theme.colors.bpCustomTint
        case "code":
            return Theme.colors.bpCodeTint
        default:
            return Theme.colors.bpFlowTint
        }
    }

    function ink(tone: string): color {
        switch (tone) {
        case "event":
            return Theme.colors.bpEventInk
        case "action":
            return Theme.colors.bpActionInk
        case "wait":
            return Theme.colors.bpWaitInk
        case "vision":
            return Theme.colors.bpVisionInk
        case "custom":
            return Theme.colors.bpCustomInk
        case "code":
            return Theme.colors.bpCodeInk
        default:
            return Theme.colors.bpFlowInk
        }
    }

    function pin(type: string): color {
        switch (type) {
        case "exec":
            return Theme.colors.bpExec
        case "bool":
            return Theme.colors.bpBool
        case "number":
            return Theme.colors.bpNumber
        case "text":
            return Theme.colors.bpText
        case "point":
            return Theme.colors.bpPoint
        case "region":
            return Theme.colors.bpRegion
        case "image":
            return Theme.colors.bpImage
        case "color":
            return Theme.colors.bpColor
        case "component":
            return Theme.colors.bpComponent
        case "key":
            return Theme.colors.bpKey
        case "duration":
            return Theme.colors.bpDuration
        case "list":
            return Theme.colors.bpList
        case "transition":
            return Theme.colors.bpTransition
        default:
            return Theme.colors.bpAny
        }
    }

    function commentFill(name: string): color {
        switch (name) {
        case "blue":
            return Theme.colors.bpCommentBlue
        case "green":
            return Theme.colors.bpCommentGreen
        case "amber":
            return Theme.colors.bpCommentAmber
        case "red":
            return Theme.colors.bpCommentRed
        case "purple":
            return Theme.colors.bpCommentPurple
        default:
            return Theme.colors.bpComment
        }
    }

    function commentStroke(name: string): color {
        switch (name) {
        case "blue":
            return Theme.colors.bpCommentBlueStroke
        case "green":
            return Theme.colors.bpCommentGreenStroke
        case "amber":
            return Theme.colors.bpCommentAmberStroke
        case "red":
            return Theme.colors.bpCommentRedStroke
        case "purple":
            return Theme.colors.bpCommentPurpleStroke
        default:
            return Theme.colors.bpCommentStroke
        }
    }

    function statusColor(tone: string): color {
        switch (tone) {
        case "good":
        case "ok":
        case "success":
            return Theme.colors.success
        case "warn":
        case "warning":
            return Theme.colors.warning
        case "bad":
        case "danger":
        case "error":
            return Theme.colors.danger
        case "info":
            return Theme.colors.info
        case "accent":
        case "live":
            return Theme.colors.accent
        case "event":
        case "action":
        case "wait":
        case "vision":
        case "flow":
        case "custom":
        case "code":
            return look.ink(tone)
        default:
            return Theme.colors.success
        }
    }

    function statusText(tone: string): color {
        switch (tone) {
        case "good":
        case "ok":
        case "success":
            return Theme.colors.successText
        case "warn":
        case "warning":
            return Theme.colors.warningText
        case "bad":
        case "danger":
        case "error":
            return Theme.colors.dangerText
        case "info":
            return Theme.colors.infoText
        case "accent":
        case "live":
            return Theme.colors.accentText
        default:
            return Theme.colors.textPrimary
        }
    }

    function tileTone(tone: string): string {
        switch (tone) {
        case "event":
        case "code":
            return "rose"
        case "action":
            return "azure"
        case "wait":
            return "amber"
        case "vision":
            return "lime"
        case "custom":
            return "teal"
        case "state":
        case "machine":
            return "orange"
        case "orange":
        case "teal":
        case "azure":
        case "lime":
        case "rose":
        case "amber":
            return tone
        default:
            return ""
        }
    }

    function kindName(kind: string): string {
        switch (kind) {
        case "event":
            return qsTr("Event")
        case "action":
            return qsTr("Action")
        case "pure":
            return qsTr("Value")
        case "flow":
            return qsTr("Flow")
        case "code":
            return qsTr("Code block")
        case "entry":
            return qsTr("Start")
        case "return":
            return qsTr("Return")
        case "reroute":
            return qsTr("Reroute point")
        case "machine":
            return qsTr("State machine")
        case "state":
            return qsTr("State")
        default:
            return qsTr("Block")
        }
    }

    function memberKindName(kind: string): string {
        switch (kind) {
        case "property":
            return qsTr("Property")
        case "state":
            return qsTr("State")
        case "event":
            return qsTr("Event")
        case "function":
            return qsTr("Function")
        default:
            return ""
        }
    }

    function keyName(capture: var): string {
        if (capture.kind === "mouse") {
            switch (capture.button) {
            case Qt.LeftButton:
                return "Mouse.Left"
            case Qt.RightButton:
                return "Mouse.Right"
            case Qt.MiddleButton:
                return "Mouse.Middle"
            case Qt.BackButton:
                return "Mouse.X1"
            case Qt.ForwardButton:
                return "Mouse.X2"
            default:
                return ""
            }
        }
        if (capture.kind === "wheel") {
            if (capture.dy !== 0)
                return capture.dy > 0 ? "Mouse.WheelUp" : "Mouse.WheelDown"
            return capture.dx > 0 ? "Mouse.WheelLeft" : "Mouse.WheelRight"
        }
        const key = capture.key
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            return "Key" + String.fromCharCode(key)
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return "Digit" + String.fromCharCode(key)
        if (key >= Qt.Key_F1 && key <= Qt.Key_F24)
            return "F" + (key - Qt.Key_F1 + 1)
        switch (key) {
        case Qt.Key_Space:
            return "Space"
        case Qt.Key_Return:
        case Qt.Key_Enter:
            return "Enter"
        case Qt.Key_Escape:
            return "Escape"
        case Qt.Key_Tab:
            return "Tab"
        case Qt.Key_Backspace:
            return "Backspace"
        case Qt.Key_Up:
            return "ArrowUp"
        case Qt.Key_Down:
            return "ArrowDown"
        case Qt.Key_Left:
            return "ArrowLeft"
        case Qt.Key_Right:
            return "ArrowRight"
        case Qt.Key_Shift:
            return "ShiftLeft"
        case Qt.Key_Control:
            return "ControlLeft"
        case Qt.Key_Alt:
            return "AltLeft"
        default:
            return ""
        }
    }
}
