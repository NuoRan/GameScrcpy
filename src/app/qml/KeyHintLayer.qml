pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

Item {
    id: hintLayer

    property var hints: []
    property var live: []
    property var aiming: []
    property rect picture
    property bool shown: false
    property bool marks: true
    property bool names: false
    readonly property var conflictPalette: [Theme.colors.danger, Theme.accentChoices.azure, Theme.accentChoices.lime,
                                            Theme.accentChoices.amber, Theme.accentChoices.teal,
                                            Theme.accentChoices.rose, Theme.accentChoices.ember]
    readonly property var selectedGroups: (hintLayer.hints ?? []).filter(hint => hint.selected === true
                                                                         && hint.conflict === true)
                                                                 .map(hint => hint.conflictGroup ?? -1)
    property real restingOpacity: 0.6
    property bool revealed: false
    property bool intro: true
    property int introDuration: Theme.motion.hintIntro
    property string introKey
    property real dimmedOpacity: 0.45
    property var flashStarts: ({})
    property real flashClock: 0
    property var flashSerials: ({})

    readonly property bool introducing: internal.introducing

    function startFlash(control: int, serial: int) {
        if (serial <= 0 || hintLayer.flashSerials[control] === serial)
            return
        hintLayer.flashSerials[control] = serial
        const now = Date.now()
        const starts = Object.assign({}, hintLayer.flashStarts)
        starts[control] = now
        hintLayer.flashClock = now
        hintLayer.flashStarts = starts
        flashFrames.start()
    }

    function liveAt(item: var): bool {
        const flag = hintLayer.live[item.control]
        return flag === undefined ? item.live : flag
    }

    function conflictColor(group: int): color {
        const palette = hintLayer.conflictPalette
        return palette[Math.max(0, group) % palette.length]
    }

    function textOf(key: var): string {
        return key.glyph ? (key.prefix ?? "") : key.text
    }

    function glyphOf(key: var): string {
        return key.glyph ?? ""
    }

    function shapeOf(item: var): string {
        switch (item.type) {
        case "camera":
        case "padCamera":
            return "camera"
        case "skill":
            return "aim"
        case "multiPress":
        case "dial":
            return "hold"
        case "freeLook":
        case "swipe":
        case "hold":
        case "longPress":
        case "turbo":
            return item.type
        }
        return "tap"
    }

    function isStick(type: string): bool {
        return type === "wasd" || type === "rightWalk" || type === "padStick"
    }

    function frameOf(index: int): rect {
        const spot = spots.itemAt(index) as Spot
        if (spot === null)
            return Qt.rect(0, 0, 0, 0)
        return Qt.rect(spot.x + spot.frame.x, spot.y + spot.frame.y, spot.frame.width, spot.frame.height)
    }

    function keyIn(item: var, px: real, py: real): var {
        for (const child of item.children) {
            if (!child.visible)
                continue
            if (child.objectName === "hintKey") {
                const local = child.mapFromItem(hintLayer, px, py)
                if (local.x >= 0 && local.y >= 0 && local.x <= child.width && local.y <= child.height)
                    return child
            }
            const nested = keyIn(child, px, py)
            if (nested !== null)
                return nested
        }
        return null
    }

    function keyAt(px: real, py: real): var {
        for (let i = spots.count - 1; i >= 0; --i) {
            const spot = spots.itemAt(i)
            if (spot === null || !spot.visible)
                continue
            const key = keyIn(spot, px, py)
            if (key !== null)
                return { index: i, slot: key.slot, text: key.label }
        }
        return null
    }

    component Spot: Item {
        id: spot

        required property var modelData

        readonly property string type: modelData.type
        readonly property int flash: modelData.flash ?? 0
        readonly property bool stick: hintLayer.isStick(type)
        readonly property bool dial: type === "dial"
        readonly property var keys: modelData.keys
        readonly property var directions: keys.filter(key => ["up", "down", "left", "right"].indexOf(key.slot) >= 0)
        readonly property var extras: keys.filter(key => ["up", "down", "left", "right"].indexOf(key.slot) < 0)
        readonly property bool custom: (modelData.component ?? "") !== ""
        readonly property string customIcon: custom ? (modelData.missing === true ? "triangle-alert" : modelData.icon ?? "")
                                                    : type === "tempCursor" ? "cursor-arrow"
                                                    : type === "actionWheel" ? "wheel-slices" : ""
        readonly property var places: custom || type === "multiPress" || (type === "actionWheel" && hintLayer.marks)
                                      ? (modelData.points ?? []).map(point => Qt.point(point.x - modelData.x,
                                                                                         point.y - modelData.y))
                                      : []
        readonly property Item frame: stick ? ring : type === "pinch" ? pinchKeys : keycap
        readonly property bool roundFrame: frame === keycap && keycap.shape !== "key"
        readonly property var track: {
            if (type !== "swipe")
                return []
            const points = modelData.path ?? []
            if (points.length >= 2)
                return points.map(point => Qt.point(point.x - modelData.x, point.y - modelData.y))
            return [Qt.point(0, 0), Qt.point(keycap.width / 2 + Theme.size.keyHintHalo + 3 * Theme.size.keyHintTick, 0)]
        }
        readonly property var arrow: {
            if (track.length < 2)
                return []
            const end = track[track.length - 1]
            const from = track[track.length - 2]
            const angle = Math.atan2(end.y - from.y, end.x - from.x)
            const size = Theme.size.keyHintTick
            const open = Math.PI * 0.8
            return [Qt.point(end.x + size * Math.cos(angle + open), end.y + size * Math.sin(angle + open)), end,
                    Qt.point(end.x + size * Math.cos(angle - open), end.y + size * Math.sin(angle - open))]
        }

        x: hintLayer.picture.x + modelData.x
        y: hintLayer.picture.y + modelData.y
        visible: hintLayer.liveAt(modelData)
        onFlashChanged: hintLayer.startFlash(spot.modelData.control, spot.flash)
        Component.onCompleted: hintLayer.startFlash(spot.modelData.control, spot.flash)

        Item {
            opacity: spot.modelData.ghost === true ? 0.5
                     : spot.modelData.dim === true ? hintLayer.dimmedOpacity : 1

            Item {
                objectName: "castRange"
                visible: spot.modelData.castRx !== undefined && hintLayer.aiming[spot.modelData.control] === true
                x: (spot.modelData.castX ?? 0) - spot.modelData.x
                y: (spot.modelData.castY ?? 0) - spot.modelData.y

                Rectangle {
                    x: -width / 2
                    y: -height / 2
                    width: 2 * (spot.modelData.castRx ?? 0)
                    height: 2 * (spot.modelData.castRy ?? 0)
                    radius: Math.min(width, height) / 2
                    color: Theme.colors.keyHintRingFill
                    border.width: Theme.size.keyHintRing
                    border.color: Theme.colors.keyHintRing
                }

                Rectangle {
                    x: -width / 2
                    y: -height / 2
                    width: 4 * Theme.size.keyHintRing
                    height: width
                    radius: width / 2
                    color: Theme.colors.textPrimary
                }
            }

            Shape {
                objectName: "swipeTrack"
                visible: spot.type === "swipe" && (!hintLayer.marks || spot.modelData.selected !== true)
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: Theme.colors.keyHintText
                    strokeWidth: Theme.size.keyHintRing
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [Theme.size.keyHintRing, Theme.size.keyHintRing]

                    PathPolyline {
                        path: spot.track
                    }
                }

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: Theme.colors.keyHintText
                    strokeWidth: Theme.size.keyHintRing
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin

                    PathPolyline {
                        path: spot.arrow
                    }
                }
            }

            Shape {
                objectName: "placeTrack"
                visible: spot.places.length > 0
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: Theme.colors.keyHintText
                    strokeWidth: Theme.size.keyHintRing
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [Theme.size.keyHintRing, Theme.size.keyHintRing]

                    PathMultiline {
                        paths: spot.type === "multiPress" || spot.type === "actionWheel"
                               ? spot.places.map(place => [Qt.point(0, 0), place])
                               : [[Qt.point(0, 0)].concat(spot.places)]
                    }
                }
            }

            Repeater {
                model: spot.places

                delegate: Rectangle {
                    required property var modelData
                    required property int index

                    objectName: "placeDot"
                    x: modelData.x - width / 2
                    y: modelData.y - height / 2
                    width: Theme.size.keyHintTick + 2 * Theme.size.keyHintRing
                    height: width
                    radius: width / 2
                    color: Theme.colors.keyHintFill
                    border.width: Theme.size.keyHintRing
                    border.color: Theme.colors.keyHintText

                    Text {
                        visible: spot.type === "actionWheel"
                        x: parent.width + Theme.space.xxs
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: {
                            const name = (spot.modelData.points ?? [])[parent.index]?.name ?? ""
                            return name.startsWith("cells.") ? String(Number(name.slice(6)) + 1) : ""
                        }
                        color: Theme.colors.keyHintText
                        font.pixelSize: Theme.type.caption.size
                        font.weight: Theme.type.label.weight
                    }
                }
            }

            KeyHint {
                id: keycap

                readonly property var lead: spot.keys.length > 0 ? spot.keys[0] : ({ slot: "", text: "" })
                readonly property string slot: lead.slot
                readonly property string label: lead.text
                readonly property bool keyless: spot.modelData.unbound === true

                objectName: "hintKey"
                visible: !spot.stick && spot.type !== "pinch"
                x: -width / 2
                y: -height / 2
                text: keyless && (spot.modelData.name ?? "") !== "" ? spot.modelData.name : hintLayer.textOf(lead)
                iconName: spot.customIcon
                mouseButton: keyless ? "" : hintLayer.glyphOf(lead)
                shape: keyless ? "unbound" : hintLayer.shapeOf(spot.modelData)
            }

            Row {
                id: pinchKeys

                visible: spot.type === "pinch"
                x: -width / 2
                y: -height / 2
                spacing: 2 * (Theme.size.keyHintHalo + Theme.size.keyHintTick) + Theme.space.xs

                Repeater {
                    model: spot.type === "pinch" ? spot.keys.filter(key => key.slot === "zoomIn" || key.slot === "zoomOut")
                                                 : []

                    delegate: KeyHint {
                        required property var modelData
                        readonly property string slot: modelData.slot
                        readonly property string label: modelData.text

                        objectName: "hintKey"
                        small: true
                        text: hintLayer.textOf(modelData)
                        mouseButton: hintLayer.glyphOf(modelData)
                        shape: modelData.slot === "zoomIn" ? "zoomIn" : "zoomOut"
                    }
                }
            }

            Shape {
                id: sprintRing

                readonly property real reach: spot.modelData.sprint ?? 0

                objectName: "sprintRing"
                visible: hintLayer.marks && spot.stick && sprintRing.reach > 1
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: Theme.colors.keyHintRing
                    strokeWidth: Theme.size.keyHintRing
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [3, 3]

                    PathAngleArc {
                        radiusX: spot.modelData.rx * sprintRing.reach
                        radiusY: spot.modelData.ry * sprintRing.reach
                        startAngle: 0
                        sweepAngle: 360
                    }
                }
            }

            Rectangle {
                id: ring

                visible: spot.stick || spot.dial
                x: -spot.modelData.rx
                y: -spot.modelData.ry
                width: 2 * spot.modelData.rx
                height: 2 * spot.modelData.ry
                radius: Math.min(width, height) / 2
                color: Theme.colors.keyHintRingFill
                border.width: Theme.size.keyHintRing
                border.color: Theme.colors.keyHintRing
            }

            Repeater {
                model: spot.dial ? Math.max(0, spot.modelData.cells ?? 0) : 0

                delegate: Rectangle {
                    required property int index

                    readonly property real angle: ((spot.modelData.firstDeg ?? 0) + index * 360 / spot.modelData.cells)
                                                  * Math.PI / 180

                    objectName: "dialCell"
                    x: spot.modelData.rx * Math.sin(angle) - width / 2
                    y: -spot.modelData.ry * Math.cos(angle) - height / 2
                    width: Theme.size.keyHintTick + 2 * Theme.size.keyHintRing
                    height: width
                    radius: width / 2
                    color: index === 0 ? Theme.colors.keyHintText : Theme.colors.keyHintFill
                    border.width: Theme.size.keyHintRing
                    border.color: Theme.colors.keyHintText
                }
            }

            Icon {
                objectName: "ringIcon"
                visible: spot.stick && spot.customIcon !== ""
                x: -width / 2
                y: -height / 2
                name: spot.customIcon
                size: Theme.size.iconSmall
                color: Theme.colors.keyHintText
            }

            Repeater {
                model: spot.stick ? spot.directions : []

                delegate: KeyHint {
                    required property var modelData
                    readonly property string slot: modelData.slot

                    readonly property real dx: modelData.slot === "left" ? -spot.modelData.rx
                                             : modelData.slot === "right" ? spot.modelData.rx : 0
                    readonly property real dy: modelData.slot === "up" ? -spot.modelData.ry
                                             : modelData.slot === "down" ? spot.modelData.ry : 0

                    readonly property string label: modelData.text

                    objectName: "hintKey"
                    small: true
                    x: dx - width / 2
                    y: dy - height / 2
                    text: hintLayer.textOf(modelData)
                    mouseButton: hintLayer.glyphOf(modelData)
                }
            }

            Row {
                visible: spot.stick && spot.extras.length > 0
                x: -width / 2
                y: spot.modelData.ry + Theme.size.keyHintSmall / 2 + Theme.space.xs
                spacing: Theme.space.xs

                Repeater {
                    model: spot.stick ? spot.extras : []

                    delegate: KeyHint {
                        required property var modelData
                        readonly property string slot: modelData.slot
                        readonly property string label: modelData.text

                        objectName: "hintKey"
                        small: true
                        text: hintLayer.textOf(modelData)
                        mouseButton: hintLayer.glyphOf(modelData)
                    }
                }
            }
        }

        Rectangle {
            objectName: "selection"
            visible: hintLayer.marks && spot.modelData.selected === true && !spot.stick
                     && spot.type !== "skill" && !spot.dial
            x: spot.frame.x - Theme.space.xs
            y: spot.frame.y - Theme.space.xs
            width: spot.frame.width + 2 * Theme.space.xs
            height: spot.frame.height + 2 * Theme.space.xs
            radius: spot.roundFrame ? height / 2 : Theme.radius.md + Theme.space.xs
            color: "transparent"
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accent
        }

        Rectangle {
            readonly property color tone: hintLayer.conflictColor(spot.modelData.conflictGroup ?? 0)
            readonly property bool grouped: hintLayer.selectedGroups.indexOf(spot.modelData.conflictGroup ?? -1) >= 0

            objectName: "conflictOutline"
            visible: hintLayer.marks && spot.modelData.conflict === true && spot.modelData.selected !== true
            x: spot.frame.x - Theme.space.xs
            y: spot.frame.y - Theme.space.xs
            width: spot.frame.width + 2 * Theme.space.xs
            height: spot.frame.height + 2 * Theme.space.xs
            radius: spot.roundFrame || spot.stick || spot.dial ? Math.min(width, height) / 2
                                                                : Theme.radius.md + Theme.space.xs
            color: grouped ? Qt.rgba(tone.r, tone.g, tone.b, 0.18) : "transparent"
            border.width: grouped ? Theme.size.focusRing : Theme.size.stroke
            border.color: tone
        }

        Rectangle {
            readonly property real inset: spot.roundFrame ? spot.frame.height / 2 * (1 - Math.SQRT1_2) : 0

            objectName: "conflict"
            visible: hintLayer.marks && spot.modelData.conflict === true
            x: spot.stick ? -spot.modelData.rx * Math.SQRT1_2 - width / 2 : spot.frame.x + inset - width / 2
            y: spot.stick ? -spot.modelData.ry * Math.SQRT1_2 - height / 2 : spot.frame.y + inset - height / 2
            width: Theme.size.pillDot + Theme.size.focusRing
            height: width
            radius: width / 2
            color: hintLayer.conflictColor(spot.modelData.conflictGroup ?? 0)
        }

        Rectangle {
            readonly property string diffMark: spot.modelData.diff ?? ""

            objectName: "diffOutline"
            visible: hintLayer.marks && diffMark !== ""
            x: spot.frame.x - Theme.space.xs
            y: spot.frame.y - Theme.space.xs
            width: spot.frame.width + 2 * Theme.space.xs
            height: spot.frame.height + 2 * Theme.space.xs
            radius: spot.roundFrame || spot.stick || spot.dial ? Math.min(width, height) / 2
                                                                : Theme.radius.md + Theme.space.xs
            color: "transparent"
            border.width: diffMark === "removed" ? Theme.size.stroke : Theme.size.focusRing
            border.color: diffMark === "added" ? Theme.colors.success
                        : diffMark === "changed" ? Theme.colors.warning : Theme.colors.danger
            opacity: diffMark === "removed" ? 0.85 : 1
        }

        Rectangle {
            readonly property real inset: spot.roundFrame ? spot.frame.height / 2 * (1 - Math.SQRT1_2) : 0

            objectName: "logicKey"
            visible: hintLayer.marks && spot.modelData.logicKey === true && !spot.stick
            x: spot.frame.x + spot.frame.width - inset - width / 2
            y: spot.frame.y + inset - height / 2
            width: Theme.size.iconSmall + Theme.space.xxs
            height: width
            radius: width / 2
            color: Theme.colors.accent
            border.width: Theme.size.stroke
            border.color: Theme.colors.keyHintFill

            Icon {
                anchors.centerIn: parent
                name: "workflow"
                size: Theme.size.iconSmall - Theme.space.xxs
                color: Theme.colors.textOnAccent
            }
        }

        Text {
            objectName: "hintName"
            visible: hintLayer.marks && hintLayer.names && spot.modelData.unbound !== true
                     && (spot.modelData.name ?? "") !== ""
            x: -width / 2
            y: spot.frame.y + spot.frame.height + Theme.space.xxs
               + (spot.stick && spot.extras.length > 0 ? Theme.size.keyHintSmall + Theme.space.xs : 0)
            width: Math.min(implicitWidth, Theme.size.fieldWidth)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: spot.modelData.name ?? ""
            color: Theme.colors.keyHintText
            style: Text.Outline
            styleColor: Theme.colors.keyHintFill
            font.pixelSize: Theme.type.caption.size
        }

        Rectangle {
            readonly property int pad: Theme.space.sm
            readonly property real phase: {
                const start = hintLayer.flashStarts[spot.modelData.control]
                return start === undefined ? 2 : (hintLayer.flashClock - start) / (Theme.motion.pulse / 2)
            }

            objectName: "flashRing"
            x: spot.frame.x - pad
            y: spot.frame.y - pad
            width: spot.frame.width + 2 * pad
            height: spot.frame.height + 2 * pad
            radius: spot.stick || spot.roundFrame ? Math.min(width, height) / 2 : Theme.radius.md + pad
            color: "transparent"
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accent
            opacity: phase >= 0 && phase < 2 ? 1 - phase % 1 : 0
            visible: opacity > 0
        }
    }

    opacity: shown ? (internal.introducing || revealed ? 1 : restingOpacity) : 0
    visible: opacity > 0

    onIntroKeyChanged: internal.introduce()

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.motion.slow
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.standard
        }
    }

    QtObject {
        id: internal

        property bool introducing: false

        function introduce() {
            introducing = hintLayer.intro && hintLayer.introKey !== ""
            if (introducing)
                introTimer.restart()
            else
                introTimer.stop()
        }
    }

    FrameAnimation {
        id: flashFrames

        onTriggered: {
            const now = Date.now()
            const starts = hintLayer.flashStarts
            hintLayer.flashClock = now
            if (!Object.keys(starts).some(key => now - starts[key] < Theme.motion.pulse)) {
                hintLayer.flashStarts = ({})
                flashFrames.stop()
            }
        }
    }

    Timer {
        id: introTimer

        interval: hintLayer.introDuration
        onTriggered: internal.introducing = false
    }

    Repeater {
        id: spots

        model: hintLayer.hints

        delegate: Spot {}
    }
}
