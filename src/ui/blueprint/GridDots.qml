import QtQuick
import GameScrcpy.Theme

ShaderEffect {
    id: dots

    property real viewX: 0
    property real viewY: 0
    property real zoom: 1

    readonly property real step: {
        let gap = Theme.size.bpGrid * zoom
        while (gap < Theme.size.bpGrid / 2)
            gap *= 2
        return gap
    }
    property size area: Qt.size(width, height)
    property point shift: Qt.point(viewX * zoom + step / 2, viewY * zoom + step / 2)
    property real spacing: step
    property real radius: Theme.size.stroke
    property color tint: Theme.colors.bpGrid

    visible: GraphicsInfo.shaderType === GraphicsInfo.RhiShader
    fragmentShader: "shaders/griddots.frag.qsb"
}
