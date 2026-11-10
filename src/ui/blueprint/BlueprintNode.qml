pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

Item {
    id: node

    required property string key
    property var info: ({})
    property string shape: ""
    property real modelX: 0
    property real modelY: 0
    property real moveX: 0
    property real moveY: 0
    property bool selected: false
    property bool readOnly: false
    property bool faded: false
    property bool paused: false
    property var values: null
    property var targets: null
    property var keyName: null
    property bool showOptional: false
    property real glowLevel: 0
    readonly property string kind: info.kind ?? "action"
    readonly property string tone: info.tone ?? "flow"
    readonly property bool pure: kind === "pure"
    readonly property bool reroute: kind === "reroute"
    readonly property bool codeBlock: kind === "code"
    readonly property bool floating: info.floating === true
    readonly property string problem: info.problem ?? ""
    readonly property bool fixable: (info.fix ?? "") === "missing"
    readonly property string breakpoint: info.breakpoint ?? ""
    readonly property var layout: internal.arrange(info, showOptional)
    readonly property bool compact: pure && layout.rows.length === 0
    readonly property real bodyTop: pure ? Theme.size.bpPure : Theme.size.bpHeader + internal.pad
    readonly property real rowsBottom: bodyTop + layout.height
    readonly property real inX: reroute ? Theme.size.bpReroute / 2 : 0
    readonly property real outX: reroute ? Theme.size.bpReroute / 2 : width
    readonly property var compactPin: compact ? (info.outputs?.[0] ?? ({})) : ({})

    signal bodyPressed(int button, int modifiers, point scene)
    signal bodyMoved(point scene)
    signal bodyReleased(point scene)
    signal bodyDoubleClicked(int button)
    signal pinPressed(string pin, bool output, int button, int modifiers, point scene)
    signal pinMoved(point scene)
    signal pinReleased(point scene)
    signal valueEdited(string pin, var value)
    signal pickRequested(string pin, string kind)
    signal editFinished()
    signal codeEdited(string text)
    signal codeDocumentReady(var document)
    signal hintShown(string text, point scene)
    signal hintHidden()
    signal choicesRequested(string pin, point scene)
    signal fixReplaceRequested()
    signal fixDeleteRequested()

    function apply(next: var) {
        const copy = Object.assign({}, next)
        delete copy.x
        delete copy.y
        const text = JSON.stringify(copy)
        if (text !== shape) {
            shape = text
            info = next
        }
        modelX = next.x ?? 0
        modelY = next.y ?? 0
    }

    function glow() {
        pulse.restart()
    }

    function anchorY(side: string, pin: string): real {
        const found = layout.anchors[side + ":" + pin]
        if (found !== undefined)
            return found
        if (reroute)
            return Theme.size.bpReroute / 2
        return pure ? Theme.size.bpPure / 2 : Theme.size.bpHeader / 2
    }

    function pinAt(localY: real, side: string): string {
        if (reroute)
            return side === "in" ? (info.inputs?.[0]?.name ?? "") : (info.outputs?.[0]?.name ?? "")
        if (localY < bodyTop) {
            if (compact)
                return side === "out" ? (info.outputs?.[0]?.name ?? "") : ""
            const list = side === "in" ? info.inputs : info.outputs
            const main = (list ?? []).find(pin => pin.name === "exec")
            return main ? main.name : ""
        }
        let y = bodyTop
        for (const row of layout.rows) {
            if (localY >= y && localY < y + row.height) {
                const pin = side === "in" ? row.input : row.output
                return pin ? pin.name : ""
            }
            y += row.height
        }
        return ""
    }

    x: modelX + moveX
    y: modelY + moveY
    z: selected ? 1 : 0
    width: {
        if (reroute)
            return Theme.size.bpReroute
        const editor = codeLoader.item as Item
        const content = Math.max(header.implicitWidth, internal.widest,
                                 fixable ? fixRow.implicitWidth + 2 * internal.edge : 0,
                                 editor ? editor.implicitWidth + 2 * internal.edge : 0)
        if (pure)
            return content
        if (codeBlock)
            return Math.max(Theme.size.bpCodeWidth, Math.min(2 * Theme.size.bpCodeWidth, content))
        return Math.max(Theme.size.bpNodeWidth, content)
    }
    height: {
        if (reroute)
            return Theme.size.bpReroute
        if (compact)
            return Theme.size.bpPure
        let total = rowsBottom
        if (codeBlock)
            total += codeLoader.height + Theme.space.xs
        if (layout.optional > 0)
            total += Theme.size.bpChip
        if (fixable)
            total += fixRow.height + Theme.space.xs
        return total + internal.pad
    }
    opacity: faded || floating ? Theme.fadedOpacity : 1
    objectName: "bpNode_" + key

    Accessible.role: Accessible.Button
    Accessible.name: {
        const title = info.title ?? ""
        const badge = info.badge ?? ""
        const kindName = BlueprintLook.kindName(kind)
        return badge !== "" ? qsTr("%1: %2 · %3").arg(kindName).arg(title).arg(badge)
                            : qsTr("%1: %2").arg(kindName).arg(title)
    }
    Accessible.description: problem

    QtObject {
        id: internal

        readonly property real pad: Theme.space.xs + Theme.space.xxs
        readonly property real edge: Theme.space.sm + Theme.space.xxs
        readonly property real widest: {
            let widest = 0
            for (const row of rows.children)
                widest = Math.max(widest, row.implicitWidth)
            return widest
        }

        function shows(pin: var, all: bool): bool {
            return !pin.optional || pin.connected === true || (pin.value ?? "") !== "" || all
        }

        function hideable(pin: var): bool {
            return pin.optional === true && pin.connected !== true && (pin.value ?? "") === ""
        }

        function arrange(info: var, all: bool): var {
            const inputs = (info.inputs ?? []).filter(pin => internal.shows(pin, all))
            const outputs = (info.outputs ?? []).filter(pin => internal.shows(pin, all))
            const optional = (info.inputs ?? []).concat(info.outputs ?? []).filter(pin => internal.hideable(pin))
                .length
            const anchors = {}
            const rows = []
            const pure = (info.kind ?? "") === "pure"
            const top = pure ? Theme.size.bpPure : Theme.size.bpHeader + internal.pad
            if (pure && inputs.length === 0 && outputs.length === 1 && (outputs[0].label ?? "") === "") {
                anchors["out:" + outputs[0].name] = Theme.size.bpPure / 2
                return { rows: [], anchors: anchors, height: 0, optional: optional }
            }
            let y = top
            const count = Math.max(inputs.length, outputs.length)
            for (let index = 0; index < count; ++index) {
                const input = inputs[index] ?? null
                const output = outputs[index] ?? null
                const tall = input !== null && (input.editor ?? "") === "image" && input.connected !== true
                const height = tall ? Theme.size.bpImageRow : Theme.size.bpPinRow
                if (input !== null)
                    anchors["in:" + input.name] = y + height / 2
                if (output !== null)
                    anchors["out:" + output.name] = y + height / 2
                rows.push({ input: input, output: output, height: height })
                y += height
            }
            return { rows: rows, anchors: anchors, height: y - top, optional: optional }
        }
    }

    NumberAnimation {
        id: pulse

        target: node
        property: "glowLevel"
        from: 1
        to: 0
        duration: Theme.motion.pulse
        easing.type: Easing.OutCubic
    }

    Rectangle {
        anchors.fill: face
        anchors.margins: -(Theme.size.fieldGlow + Theme.size.focusRing)
        visible: node.glowLevel > 0
        opacity: node.glowLevel
        radius: face.radius + Theme.size.fieldGlow + Theme.size.focusRing
        color: Theme.colors.accentGlow
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accentEdge
    }

    Rectangle {
        anchors.fill: face
        anchors.margins: -Theme.size.fieldGlow
        visible: node.selected || node.paused
        radius: face.radius + Theme.size.fieldGlow
        color: "transparent"
        border.width: Theme.size.fieldGlow
        border.color: Theme.colors.accentHalo
    }

    RectangularShadow {
        anchors.fill: face
        visible: !node.reroute && !node.floating
        radius: face.radius
        offset.y: Theme.shadow.keyOffset
        blur: Theme.shadow.keyBlur
        color: Theme.shadow.key
    }

    Rectangle {
        id: face

        readonly property color rerouteTone: BlueprintLook.pin(node.info.inputs?.[0]?.type ?? "")

        anchors.fill: parent
        radius: node.reroute ? width / 2 : node.pure ? Theme.radius.xl : Theme.radius.lg
        color: {
            if (!node.reroute)
                return Theme.colors.bpNode
            const linked = node.info.inputs?.[0]?.connected === true || node.info.outputs?.[0]?.connected === true
            return linked ? face.rerouteTone : Theme.colors.bpCanvas
        }
        border.width: node.reroute ? Theme.size.focusRing : Theme.size.stroke
        border.color: {
            if (node.reroute)
                return node.selected ? Theme.colors.accent : face.rerouteTone
            if (node.floating)
                return "transparent"
            if (node.selected)
                return Theme.colors.accent
            if (node.paused)
                return Theme.colors.accentLine
            if (node.problem !== "")
                return Theme.colors.danger
            return Theme.colors.bpNodeStroke
        }
    }

    Rectangle {
        readonly property string mark: node.info.diff ?? ""

        objectName: "bpDiff_" + node.key
        visible: mark !== "" && !node.reroute
        anchors.fill: face
        anchors.margins: -Theme.size.stroke
        radius: face.radius + Theme.size.stroke
        color: "transparent"
        border.width: Theme.size.focusRing
        border.color: mark === "added" ? Theme.colors.success
                    : mark === "changed" ? Theme.colors.warning : Theme.colors.danger
        opacity: 0.9
    }

    Loader {
        anchors.fill: face
        active: node.floating

        sourceComponent: Shape {
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: node.selected ? Theme.colors.accent : Theme.colors.textTertiary
                strokeWidth: Theme.size.stroke
                strokeStyle: ShapePath.DashLine
                dashPattern: [Theme.space.xs, Theme.space.xs]
                fillColor: "transparent"

                PathRectangle {
                    x: Theme.size.stroke / 2
                    y: Theme.size.stroke / 2
                    width: face.width - Theme.size.stroke
                    height: face.height - Theme.size.stroke
                    radius: face.radius
                }
            }
        }
    }

    MouseArea {
        id: body

        anchors.fill: parent
        anchors.margins: node.reroute ? -Theme.space.xs : 0
        acceptedButtons: Qt.AllButtons
        preventStealing: true
        cursorShape: node.readOnly ? Qt.ArrowCursor : Qt.SizeAllCursor
        onPressed: mouse => node.bodyPressed(mouse.button, mouse.modifiers, body.mapToItem(null, mouse.x, mouse.y))
        onPositionChanged: mouse => node.bodyMoved(body.mapToItem(null, mouse.x, mouse.y))
        onReleased: mouse => node.bodyReleased(body.mapToItem(null, mouse.x, mouse.y))
        onDoubleClicked: mouse => node.bodyDoubleClicked(mouse.button)
    }

    Rectangle {
        id: header

        readonly property real gap: Theme.space.sm

        visible: !node.reroute
        width: parent.width
        height: node.pure ? Theme.size.bpPure : Theme.size.bpHeader
        implicitWidth: 2 * internal.edge + lead.implicitWidth + titleText.implicitWidth
                       + (tail.implicitWidth > 0 ? gap + tail.implicitWidth : 0)
        topLeftRadius: face.radius - Theme.size.stroke
        topRightRadius: face.radius - Theme.size.stroke
        bottomLeftRadius: node.compact ? face.radius - Theme.size.stroke : 0
        bottomRightRadius: node.compact ? face.radius - Theme.size.stroke : 0
        color: node.pure ? "transparent" : BlueprintLook.tint(node.tone)

        Rectangle {
            visible: !node.pure
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }

        Row {
            id: lead

            x: internal.edge
            anchors.verticalCenter: parent.verticalCenter
            spacing: header.gap

            Rectangle {
                id: breakpointDot

                objectName: "bpBreakpoint_" + node.key
                anchors.verticalCenter: parent.verticalCenter
                visible: node.breakpoint !== ""
                width: Theme.size.bpPin
                height: width
                radius: width / 2
                color: node.breakpoint === "on" ? Theme.colors.danger
                     : node.breakpoint === "invalid" ? Theme.colors.warning : "transparent"
                border.width: node.breakpoint === "off" ? Theme.size.focusRing : 0
                border.color: Theme.colors.danger

                Rectangle {
                    anchors.centerIn: parent
                    z: -1
                    visible: node.breakpoint === "on" || node.breakpoint === "invalid"
                    width: parent.width + 2 * Theme.size.fieldGlow
                    height: width
                    radius: width / 2
                    color: node.breakpoint === "invalid" ? Theme.colors.warningSubtle : Theme.colors.dangerLine
                }

                HoverHandler {
                    onHoveredChanged: {
                        const reason = node.info.breakpointReason ?? ""
                        if (hovered && node.breakpoint === "invalid" && reason !== "")
                            node.hintShown(reason, breakpointDot.mapToItem(null, breakpointDot.width / 2, 0))
                        else
                            node.hintHidden()
                    }
                }
            }

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: (node.info.icon ?? "") !== ""
                name: node.info.icon ?? ""
                size: node.pure ? Theme.size.iconSmall - Theme.space.xxs : Theme.size.iconSmall
                color: BlueprintLook.ink(node.tone)
            }

            Item {
                width: 0
                height: Theme.size.stroke
            }
        }

        Text {
            id: titleText

            x: lead.x + lead.width
            width: Math.max(0, Math.min(implicitWidth, tail.x - (tail.implicitWidth > 0 ? header.gap : 0) - x))
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: node.info.title ?? ""
            elide: Text.ElideRight
            color: Theme.colors.textPrimary
            font.pixelSize: node.pure ? Theme.type.caption.size : Theme.type.label.size
            font.weight: Font.DemiBold
        }

        Row {
            id: tail

            x: header.width - internal.edge - width
            anchors.verticalCenter: parent.verticalCenter
            spacing: header.gap

            Loader {
                anchors.verticalCenter: parent.verticalCenter
                active: (node.info.badge ?? "") !== ""
                visible: active

                sourceComponent: Text {
                    textFormat: Text.PlainText
                    text: node.info.badge ?? ""
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.micro.size
                    font.weight: Font.Medium
                }
            }

            Icon {
                id: problemMark

                objectName: "bpProblemMark_" + node.key
                anchors.verticalCenter: parent.verticalCenter
                visible: node.problem !== ""
                name: "triangle-alert"
                size: Theme.size.iconSmall - Theme.space.xxs
                color: Theme.colors.dangerText

                HoverHandler {
                    onHoveredChanged: {
                        if (hovered && node.problem !== "")
                            node.hintShown(node.problem, problemMark.mapToItem(null, problemMark.width / 2, 0))
                        else
                            node.hintHidden()
                    }
                }
            }

            Rectangle {
                readonly property bool output: true

                objectName: "bpPin_" + node.key + "_" + (node.compactPin.name ?? "")
                anchors.verticalCenter: parent.verticalCenter
                visible: node.compact
                width: Theme.size.bpPin
                height: width
                radius: width / 2
                color: node.compactPin.connected === true ? border.color : "transparent"
                border.width: Theme.size.focusRing
                border.color: BlueprintLook.pin(node.compactPin.type ?? "")

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Output: %1").arg(node.info.title ?? "")

                MouseArea {
                    id: compactArea

                    anchors.fill: parent
                    anchors.margins: -Theme.space.xs
                    preventStealing: true
                    cursorShape: node.readOnly ? Qt.ArrowCursor : Qt.CrossCursor
                    onPressed: mouse => node.pinPressed(node.compactPin.name ?? "", true, mouse.button,
                                                        mouse.modifiers, compactArea.mapToItem(null, mouse.x, mouse.y))
                    onPositionChanged: mouse => node.pinMoved(compactArea.mapToItem(null, mouse.x, mouse.y))
                    onReleased: mouse => node.pinReleased(compactArea.mapToItem(null, mouse.x, mouse.y))
                }
            }
        }
    }

    Column {
        id: rows

        y: node.bodyTop
        width: node.width
        visible: !node.reroute

        Repeater {
            model: node.reroute ? [] : node.layout.rows

            delegate: Item {
                id: row

                required property var modelData

                width: rows.width
                height: modelData.height
                implicitWidth: (inputSide.active ? inputSide.implicitWidth : 0)
                               + (outputSide.active ? outputSide.implicitWidth : 0) + Theme.space.lg

                Loader {
                    id: inputSide

                    active: row.modelData.input !== null

                    sourceComponent: NodePin {
                        id: inputPin

                        width: implicitWidth
                        height: row.height
                        pin: row.modelData.input ?? ({})
                        output: false
                        nodeKey: node.key
                        readOnly: node.readOnly
                        keyName: node.keyName
                        target: node.targets === null ? "" : node.targets["in:" + (inputPin.pin.name ?? "")] ?? ""
                        valueTag: node.values === null ? "" : node.values[inputPin.pin.name ?? ""] ?? ""
                        onHandlePressed: (button, modifiers, scene) => node.pinPressed(inputPin.pin.name, false,
                                                                                        button, modifiers, scene)
                        onHandleMoved: scene => node.pinMoved(scene)
                        onHandleReleased: scene => node.pinReleased(scene)
                        onCommitted: value => node.valueEdited(inputPin.pin.name, value)
                        onPickRequested: kind => node.pickRequested(inputPin.pin.name, kind)
                        onChoicesRequested: scene => node.choicesRequested(inputPin.pin.name, scene)
                        onHintShown: (text, scene) => node.hintShown(text, scene)
                        onHintHidden: node.hintHidden()
                        onFinished: node.editFinished()
                    }
                }

                Loader {
                    id: outputSide

                    x: parent.width - width
                    active: row.modelData.output !== null

                    sourceComponent: NodePin {
                        id: outputPin

                        width: implicitWidth
                        height: row.height
                        pin: row.modelData.output ?? ({})
                        output: true
                        nodeKey: node.key
                        readOnly: node.readOnly
                        target: node.targets === null ? "" : node.targets["out:" + (outputPin.pin.name ?? "")] ?? ""
                        valueTag: node.values === null ? "" : node.values[outputPin.pin.name ?? ""] ?? ""
                        onHandlePressed: (button, modifiers, scene) => node.pinPressed(outputPin.pin.name, true,
                                                                                        button, modifiers, scene)
                        onHandleMoved: scene => node.pinMoved(scene)
                        onHandleReleased: scene => node.pinReleased(scene)
                    }
                }
            }
        }
    }

    Loader {
        id: codeLoader

        x: internal.edge
        y: node.rowsBottom + Theme.space.xxs
        width: node.width - 2 * internal.edge
        active: node.codeBlock

        sourceComponent: Rectangle {
            implicitWidth: codeEdit.implicitWidth + 2 * Theme.space.sm
            height: codeEdit.contentHeight + 2 * Theme.space.xs
            radius: Theme.radius.sm
            color: Theme.colors.bgCode
            border.width: Theme.size.stroke
            border.color: codeEdit.activeFocus ? Theme.colors.accent : Theme.colors.strokeDefault
            clip: true

            TextEdit {
                id: codeEdit

                property string sent: ""

                function commit() {
                    const text = codeEdit.text
                    if (text === (node.info.code ?? "") || text === codeEdit.sent)
                        return
                    codeEdit.sent = text
                    node.codeEdited(text)
                }

                function revert() {
                    codeEdit.text = node.info.code ?? ""
                    codeEdit.sent = ""
                    node.editFinished()
                }

                objectName: "bpCode_" + node.key
                x: Theme.space.sm
                y: Theme.space.xs
                width: parent.width - 2 * x
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.NoWrap
                readOnly: node.readOnly
                selectByMouse: true
                color: Theme.colors.textPrimary
                selectionColor: Theme.colors.accentLine
                selectedTextColor: Theme.colors.textPrimary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                Accessible.name: qsTr("Code of “%1”").arg(node.info.title ?? "")

                Component.onCompleted: {
                    codeEdit.text = node.info.code ?? ""
                    node.codeDocumentReady(codeEdit.textDocument)
                }
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                            && (event.modifiers & Qt.ControlModifier)) {
                        event.accepted = true
                        codeEdit.commit()
                        node.editFinished()
                    } else if (event.key === Qt.Key_Escape) {
                        event.accepted = true
                        codeEdit.revert()
                    }
                }
                onActiveFocusChanged: {
                    if (!codeEdit.activeFocus)
                        codeEdit.commit()
                }

                Connections {
                    target: node

                    function onInfoChanged() {
                        const code = node.info.code ?? ""
                        if (code === codeEdit.text)
                            codeEdit.sent = ""
                        if (!codeEdit.activeFocus && codeEdit.text !== code) {
                            codeEdit.text = code
                            codeEdit.sent = ""
                        }
                    }
                }
            }
        }
    }

    Row {
        id: fixRow

        objectName: "bpFix_" + node.key
        visible: node.fixable && !node.reroute
        x: internal.edge
        y: node.height - internal.pad - height - (node.layout.optional > 0 ? Theme.size.bpChip : 0)
        width: node.width - 2 * internal.edge
        spacing: Theme.space.xs

        Button {
            objectName: "bpFixReplace_" + node.key
            small: true
            tone: "ghost"
            text: qsTr("Replace with another component…")
            onClicked: node.fixReplaceRequested()
        }

        Button {
            objectName: "bpFixDelete_" + node.key
            small: true
            tone: "danger"
            text: qsTr("Delete block")
            onClicked: node.fixDeleteRequested()
        }
    }

    Item {
        objectName: "bpMore_" + node.key
        visible: node.layout.optional > 0 && !node.reroute
        y: node.height - internal.pad - height
        width: node.width
        height: Theme.size.bpChip

        Row {
            anchors.centerIn: parent
            spacing: Theme.space.xs

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-down"
                size: Theme.size.iconSmall - Theme.space.xs
                rotation: node.showOptional ? 180 : 0
                color: moreArea.containsMouse ? Theme.colors.textPrimary : Theme.colors.textTertiary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: node.showOptional ? qsTr("Fewer") : qsTr("More")
                color: moreArea.containsMouse ? Theme.colors.textPrimary : Theme.colors.textTertiary
                font.pixelSize: Theme.type.micro.size
            }
        }

        MouseArea {
            id: moreArea

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: node.showOptional = !node.showOptional
        }
    }

    Column {
        y: -height - Theme.space.sm
        spacing: Theme.space.xs

        Loader {
            active: (node.info.note ?? "") !== ""
            visible: active

            sourceComponent: Rectangle {
                width: noteText.width + 2 * Theme.space.sm
                height: noteText.implicitHeight + 2 * Theme.space.xs + Theme.space.xxs
                radius: Theme.radius.md
                color: Theme.colors.bpBubble
                border.width: Theme.size.stroke
                border.color: Theme.colors.bpBubbleStroke

                Rectangle {
                    x: Theme.space.md
                    y: parent.height - height / 2
                    z: -1
                    width: Theme.space.sm
                    height: width
                    rotation: 45
                    color: Theme.colors.bpBubble
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.bpBubbleStroke
                }

                Text {
                    id: noteText

                    x: Theme.space.sm
                    width: Math.min(implicitWidth, Theme.size.bpNoteWidth - 2 * Theme.space.sm)
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: node.info.note ?? ""
                    wrapMode: Text.Wrap
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }

        Row {
            spacing: Theme.space.xs
            visible: node.floating || node.paused

            Rectangle {
                objectName: "bpFloating_" + node.key
                visible: node.floating
                width: floatingRow.implicitWidth + 2 * Theme.space.sm
                height: Theme.size.badge
                radius: Theme.radius.xs
                color: Theme.colors.statePress

                Row {
                    id: floatingRow

                    anchors.centerIn: parent
                    spacing: Theme.space.xs

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "unlink"
                        size: Theme.size.keyHintTagIcon
                        color: Theme.colors.textSecondary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("Not connected, will not run")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.micro.size
                        font.weight: Theme.type.micro.weight
                    }
                }
            }

            Rectangle {
                objectName: "bpPaused_" + node.key
                visible: node.paused
                width: pausedRow.implicitWidth + 2 * Theme.space.sm
                height: Theme.size.badge
                radius: Theme.radius.xs
                color: Theme.colors.accent

                Row {
                    id: pausedRow

                    anchors.centerIn: parent
                    spacing: Theme.space.xs

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "pause"
                        size: Theme.size.keyHintTagIcon
                        color: Theme.colors.textOnAccent
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("Paused here")
                        color: Theme.colors.textOnAccent
                        font.pixelSize: Theme.type.micro.size
                        font.weight: Theme.type.micro.weight
                    }
                }
            }
        }
    }
}
