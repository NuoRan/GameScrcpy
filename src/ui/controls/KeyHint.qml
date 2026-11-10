import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Icons

Rectangle {
    id: hint

    property string text
    property string iconName
    property string tagIcon
    property bool small: false
    property string shape: "key"
    property string mouseButton

    readonly property bool round: shape !== "key" && shape !== "freeLook"
    readonly property real cap: height / 2
    readonly property real span: Math.max(0, width - height)
    readonly property real halo: cap + Theme.size.keyHintHalo
    readonly property real echoAngle: {
        const apart = Theme.size.keyHintEcho * Math.SQRT2
        return Math.acos(-Math.min(1, apart / (2 * Math.max(cap, apart)))) * 180 / Math.PI
    }

    implicitWidth: shape === "freeLook"
                   ? Math.max(2 * implicitHeight, content.implicitWidth + 2 * Theme.space.md)
                   : Math.max(implicitHeight, content.implicitWidth + 2 * (round ? Theme.space.xs : Theme.space.sm))
    implicitHeight: small ? Theme.size.keyHintSmall : shape === "key" ? Theme.size.keyHint : Theme.size.keyHintRound
    radius: round ? cap : Theme.radius.md
    color: shape === "freeLook" ? "transparent" : Theme.colors.keyHintFill
    border.width: shape === "freeLook" || shape === "unbound" ? 0 : Theme.size.stroke
    border.color: Theme.colors.keyHintStroke

    Accessible.role: Accessible.StaticText
    Accessible.name: text

    component Echo: Shape {
        id: echo

        required property int step
        required property color fill
        required property real cap
        required property real span
        required property real angle
        readonly property real shift: Theme.size.keyHintEcho
        readonly property real leftCap: cap
        readonly property real rightCap: cap + span

        z: -step
        x: (step - 1) * shift
        y: -(step - 1) * shift
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: echo.fill
            strokeColor: Theme.colors.keyHintStroke
            strokeWidth: Theme.size.stroke

            PathAngleArc {
                centerX: echo.leftCap + echo.shift
                centerY: echo.cap - echo.shift
                radiusX: echo.cap
                radiusY: echo.cap
                startAngle: -45 - echo.angle
                sweepAngle: echo.angle - 45
            }
            PathLine {
                x: echo.rightCap + echo.shift
                y: -echo.shift
            }
            PathAngleArc {
                moveToStart: false
                centerX: echo.rightCap + echo.shift
                centerY: echo.cap - echo.shift
                radiusX: echo.cap
                radiusY: echo.cap
                startAngle: -90
                sweepAngle: 45 + echo.angle
            }
            PathAngleArc {
                moveToStart: false
                centerX: echo.rightCap
                centerY: echo.cap
                radiusX: echo.cap
                radiusY: echo.cap
                startAngle: 135 - echo.angle
                sweepAngle: echo.angle - 225
            }
            PathLine {
                x: echo.leftCap
                y: 0
            }
            PathAngleArc {
                moveToStart: false
                centerX: echo.leftCap
                centerY: echo.cap
                radiusX: echo.cap
                radiusY: echo.cap
                startAngle: -90
                sweepAngle: echo.angle - 135
            }
        }
    }

    component Halo: ShapePath {
        id: outline

        required property real cap
        required property real span
        required property real reach
        readonly property real leftCap: cap
        readonly property real rightCap: cap + span

        fillColor: "transparent"

        PathAngleArc {
            centerX: outline.leftCap
            centerY: outline.cap
            radiusX: outline.reach
            radiusY: outline.reach
            startAngle: 90
            sweepAngle: 180
        }
        PathLine {
            x: outline.rightCap
            y: outline.cap - outline.reach
        }
        PathAngleArc {
            moveToStart: false
            centerX: outline.rightCap
            centerY: outline.cap
            radiusX: outline.reach
            radiusY: outline.reach
            startAngle: -90
            sweepAngle: 180
        }
        PathLine {
            x: outline.leftCap
            y: outline.cap + outline.reach
        }
    }

    component Tick: Rectangle {
        required property bool upright

        width: upright ? Theme.size.keyHintRing : Theme.size.keyHintTick
        height: upright ? Theme.size.keyHintTick : Theme.size.keyHintRing
        radius: Theme.size.keyHintRing / 2
        color: Theme.colors.keyHintText
    }

    component Chevron: Shape {
        id: chevron

        required property bool leading
        required property bool outward
        required property real hostWidth
        required property real cap
        readonly property real arm: Theme.size.keyHintTick * 2 / 3
        readonly property real tip: outward === leading ? 0 : arm

        x: leading ? -Theme.size.keyHintHalo - arm : hostWidth + Theme.size.keyHintHalo
        y: cap - arm
        width: arm
        height: 2 * arm
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: "transparent"
            strokeColor: Theme.colors.keyHintText
            strokeWidth: Theme.size.keyHintRing
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                path: [Qt.point(chevron.arm - chevron.tip, 0), Qt.point(chevron.tip, chevron.arm),
                       Qt.point(chevron.arm - chevron.tip, 2 * chevron.arm)]
            }
        }
    }

    component MouseGlyph: Item {
        id: glyph

        required property string button
        required property bool compact
        readonly property real line: Theme.size.keyHintRing * 0.75
        readonly property real bodyHeight: compact ? Theme.size.keyHintMouseSmall : Theme.size.keyHintMouse
        readonly property real bodyWidth: Math.round(bodyHeight * 0.68)
        readonly property real cap: bodyWidth / 2
        readonly property real split: Math.round(bodyHeight * 0.42)
        readonly property real arrow: Theme.size.keyHintTick / 2
        readonly property bool vertical: button === "wheelUp" || button === "wheelDown"
        readonly property bool sideways: button === "wheelLeft" || button === "wheelRight"
        readonly property bool side: button === "back" || button === "forward"
        readonly property bool wheel: button === "middle" || vertical || sideways
        readonly property real padX: sideways ? arrow + 2 * line : side ? 2 * line : 0
        readonly property real padY: vertical ? arrow + 2 * line : 0
        readonly property real wheelTop: Math.round(bodyHeight * 0.16)
        readonly property real wheelHeight: Math.round(bodyHeight * 0.26)
        readonly property var pointer: {
            const cx = padX + cap
            const cy = padY + wheelTop + wheelHeight / 2
            if (button === "wheelUp")
                return [Qt.point(cx - arrow, padY - line), Qt.point(cx, padY - line - arrow), Qt.point(cx + arrow, padY - line)]
            if (button === "wheelDown") {
                const bottom = padY + bodyHeight + line
                return [Qt.point(cx - arrow, bottom), Qt.point(cx, bottom + arrow), Qt.point(cx + arrow, bottom)]
            }
            if (button === "wheelLeft")
                return [Qt.point(padX - line, cy - arrow), Qt.point(padX - line - arrow, cy), Qt.point(padX - line, cy + arrow)]
            if (button === "wheelRight") {
                const right = padX + bodyWidth + line
                return [Qt.point(right, cy - arrow), Qt.point(right + arrow, cy), Qt.point(right, cy + arrow)]
            }
            return []
        }

        implicitWidth: bodyWidth + 2 * padX
        implicitHeight: bodyHeight + 2 * padY

        Rectangle {
            x: glyph.padX
            y: glyph.padY
            width: glyph.bodyWidth
            height: glyph.bodyHeight
            radius: width / 2
            color: "transparent"
            border.width: glyph.line
            border.color: Theme.colors.keyHintText
        }

        Rectangle {
            x: glyph.padX + glyph.cap - width / 2
            y: glyph.padY
            width: glyph.line
            height: glyph.split
            color: Theme.colors.keyHintText
        }

        Shape {
            objectName: "pressedHalf"
            visible: glyph.button === "left" || glyph.button === "right"
            x: glyph.padX
            y: glyph.padY
            width: glyph.bodyWidth
            height: glyph.bodyHeight
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                fillColor: Theme.colors.keyHintText
                strokeColor: "transparent"

                PathAngleArc {
                    centerX: glyph.cap
                    centerY: glyph.cap
                    radiusX: glyph.cap - glyph.line / 2
                    radiusY: glyph.cap - glyph.line / 2
                    startAngle: -90
                    sweepAngle: glyph.button === "left" ? -90 : 90
                }
                PathLine {
                    x: glyph.button === "left" ? glyph.line / 2 : glyph.bodyWidth - glyph.line / 2
                    y: glyph.split
                }
                PathLine {
                    x: glyph.cap
                    y: glyph.split
                }
            }
        }

        Rectangle {
            objectName: "wheel"
            visible: glyph.wheel
            x: glyph.padX + glyph.cap - width / 2
            y: glyph.padY + glyph.wheelTop
            width: 2 * glyph.line
            height: glyph.wheelHeight
            radius: width / 2
            color: Theme.colors.keyHintText
        }

        Shape {
            objectName: "wheelArrow"
            visible: glyph.pointer.length > 0
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                fillColor: Theme.colors.keyHintText
                strokeColor: "transparent"

                PathPolyline {
                    path: glyph.pointer
                }
            }
        }

        Rectangle {
            objectName: "sideButton"
            visible: glyph.side
            x: glyph.padX - 2 * glyph.line
            y: glyph.padY + Math.round(glyph.bodyHeight * (glyph.button === "forward" ? 0.4 : 0.6))
            width: 1.5 * glyph.line
            height: Math.round(glyph.bodyHeight * 0.18)
            radius: width / 2
            color: Theme.colors.keyHintText
        }
    }

    Echo {
        objectName: "echoFar"
        visible: hint.shape === "turbo"
        width: hint.width
        height: hint.height
        step: 2
        fill: Theme.colors.keyHintRingFill
        cap: hint.cap
        span: hint.span
        angle: hint.echoAngle
    }

    Echo {
        objectName: "echo"
        visible: hint.shape === "turbo"
        width: hint.width
        height: hint.height
        step: 1
        fill: Theme.colors.keyHintFill
        cap: hint.cap
        span: hint.span
        angle: hint.echoAngle
    }

    Shape {
        objectName: "holdArc"
        visible: hint.shape === "longPress"
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        Halo {
            strokeColor: Theme.colors.keyHintStroke
            strokeWidth: Theme.size.stroke
            cap: hint.cap
            span: hint.span
            reach: hint.halo
        }

        ShapePath {
            fillColor: "transparent"
            strokeColor: Theme.colors.keyHintText
            strokeWidth: Theme.size.keyHintRing
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: hint.cap
                centerY: hint.cap
                radiusX: hint.halo
                radiusY: hint.halo
                startAngle: -135
                sweepAngle: 45
            }
            PathLine {
                x: hint.cap + hint.span
                y: hint.cap - hint.halo
            }
            PathAngleArc {
                moveToStart: false
                centerX: hint.cap + hint.span
                centerY: hint.cap
                radiusX: hint.halo
                radiusY: hint.halo
                startAngle: -90
                sweepAngle: 180
            }
            PathLine {
                x: hint.cap
                y: hint.cap + hint.halo
            }
            PathAngleArc {
                moveToStart: false
                centerX: hint.cap
                centerY: hint.cap
                radiusX: hint.halo
                radiusY: hint.halo
                startAngle: 90
                sweepAngle: 45
            }
        }
    }

    Shape {
        objectName: "holdRing"
        visible: hint.shape === "hold"
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        Halo {
            strokeColor: Theme.colors.keyHintText
            strokeWidth: Theme.size.keyHintRing
            cap: hint.cap
            span: hint.span
            reach: hint.halo
        }
    }

    Shape {
        objectName: "unboundRing"
        visible: hint.shape === "unbound"
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        Halo {
            strokeColor: Theme.colors.keyHintText
            strokeWidth: Theme.size.stroke
            strokeStyle: ShapePath.DashLine
            dashPattern: [Theme.size.keyHintRing + 1, Theme.size.keyHintRing]
            cap: hint.cap
            span: hint.span
            reach: hint.cap
        }
    }

    Shape {
        objectName: "castRing"
        visible: hint.shape === "aim"
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        Halo {
            strokeColor: Theme.colors.keyHintText
            strokeWidth: Theme.size.stroke
            strokeStyle: ShapePath.DashLine
            dashPattern: [Theme.size.keyHintRing + 1, Theme.size.keyHintRing]
            cap: hint.cap
            span: hint.span
            reach: hint.halo
        }
    }

    Item {
        objectName: "reticle"
        visible: hint.shape === "camera"
        anchors.fill: parent

        Tick {
            upright: true
            x: (hint.width - width) / 2
            y: -Theme.size.keyHintRing - height
        }
        Tick {
            upright: true
            x: (hint.width - width) / 2
            y: hint.height + Theme.size.keyHintRing
        }
        Tick {
            upright: false
            x: -Theme.size.keyHintRing - width
            y: (hint.height - height) / 2
        }
        Tick {
            upright: false
            x: hint.width + Theme.size.keyHintRing
            y: (hint.height - height) / 2
        }
    }

    Shape {
        objectName: "lens"
        visible: hint.shape === "freeLook"
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: Theme.colors.keyHintFill
            strokeColor: Theme.colors.keyHintStroke
            strokeWidth: Theme.size.stroke
            startX: 0
            startY: hint.cap

            PathCubic {
                x: hint.width
                y: hint.cap
                control1X: hint.width * 0.22
                control1Y: -hint.height * 0.08
                control2X: hint.width * 0.78
                control2Y: -hint.height * 0.08
            }
            PathCubic {
                x: 0
                y: hint.cap
                control1X: hint.width * 0.78
                control1Y: hint.height * 1.08
                control2X: hint.width * 0.22
                control2Y: hint.height * 1.08
            }
        }
    }

    Item {
        objectName: "chevrons"
        visible: hint.shape === "zoomIn" || hint.shape === "zoomOut"
        anchors.fill: parent

        Chevron {
            leading: true
            outward: hint.shape === "zoomIn"
            hostWidth: hint.width
            cap: hint.cap
        }
        Chevron {
            leading: false
            outward: hint.shape === "zoomIn"
            hostWidth: hint.width
            cap: hint.cap
        }
    }

    Row {
        id: content

        anchors.centerIn: parent
        spacing: Theme.space.xxs

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: hint.iconName !== ""
            name: hint.iconName
            size: Theme.size.iconSmall
            color: Theme.colors.keyHintText
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: text !== ""
            textFormat: Text.PlainText
            text: hint.text
            color: Theme.colors.keyHintText
            font.pixelSize: hint.small ? Theme.type.caption.size : Theme.type.label.size
            font.weight: Font.DemiBold
        }

        MouseGlyph {
            objectName: "mouseGlyph"
            anchors.verticalCenter: parent.verticalCenter
            visible: hint.mouseButton !== ""
            button: hint.mouseButton
            compact: hint.small
        }
    }

    Rectangle {
        objectName: "tag"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -Theme.space.xs
        anchors.topMargin: -Theme.space.xs
        visible: hint.tagIcon !== ""
        width: Theme.size.keyHintTag
        height: width
        radius: width / 2
        color: Theme.colors.accent

        Icon {
            anchors.centerIn: parent
            name: hint.tagIcon
            size: Theme.size.keyHintTagIcon
            color: Theme.colors.textOnAccent
        }
    }
}
