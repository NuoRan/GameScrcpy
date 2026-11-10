pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes

Item {
    id: scene

    property string kind: "fps"

    readonly property real w: width
    readonly property real h: height

    clip: true

    Item {
        anchors.fill: parent
        visible: scene.kind === "fps"

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "#7F9FB6" }
                GradientStop { position: 0.36; color: "#B9C8C6" }
                GradientStop { position: 0.37; color: "#9C9473" }
                GradientStop { position: 0.6; color: "#6C6648" }
                GradientStop { position: 1; color: "#3B382A" }
            }
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                fillColor: "#5B735F"
                strokeWidth: -1

                PathPolyline {
                    path: {
                        const shape = [[0, 1], [0, 0.55], [0.1, 0.3], [0.22, 0.6], [0.34, 0.22], [0.46, 0.52],
                                       [0.58, 0.28], [0.7, 0.58], [0.82, 0.18], [0.92, 0.46], [1, 0.3], [1, 1]]
                        const left = -0.04 * scene.w
                        const span = 1.08 * scene.w
                        const top = 0.2 * scene.h
                        const tall = 0.19 * scene.h
                        return shape.map(point => Qt.point(left + point[0] * span, top + point[1] * tall))
                    }
                }
            }

            ShapePath {
                fillColor: "#8A4B3A"
                strokeWidth: -1

                PathPolyline {
                    path: {
                        const left = 0.58 * scene.w
                        const wide = 0.14 * scene.w
                        const top = 0.26 * scene.h
                        const roof = 0.15 * scene.h * 0.46
                        return [Qt.point(left + wide / 2, top - roof), Qt.point(left + wide * 1.08, top),
                                Qt.point(left - wide * 0.08, top)]
                    }
                }
            }

            ShapePath {
                strokeWidth: -1
                fillGradient: LinearGradient {
                    x1: scene.w * 0.7
                    y1: scene.h * 0.7
                    x2: scene.w
                    y2: scene.h
                    GradientStop { position: 0; color: "#2A2C30" }
                    GradientStop { position: 1; color: "#17181B" }
                }

                PathPolyline {
                    path: {
                        const left = 0.62 * scene.w
                        const wide = 0.3 * scene.w
                        const top = 0.72 * scene.h
                        const tall = 0.3 * scene.h
                        return [[0.3, 1], [0.18, 0.6], [0.4, 0.4], [1, 0.18], [1, 1]]
                            .map(point => Qt.point(left + point[0] * wide, top + point[1] * tall))
                    }
                }
            }
        }

        Rectangle {
            x: 0.58 * scene.w
            y: 0.26 * scene.h
            width: 0.14 * scene.w
            height: 0.15 * scene.h
            color: "#6E5E4A"

            Rectangle {
                width: parent.width
                height: Math.max(1, parent.height * 0.18)
                color: "#4E4334"
            }
        }

        Rectangle {
            x: 0.22 * scene.w
            y: 0.48 * scene.h
            width: 0.1 * scene.w
            height: 0.13 * scene.h
            color: "#7A6A44"
            border.width: Math.max(1, Math.round(width * 0.12))
            border.color: "#5A4D30"
        }
    }

    Item {
        anchors.fill: parent
        visible: scene.kind === "moba"

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "#1E3A28" }
                GradientStop { position: 0.45; color: "#2B4D33" }
                GradientStop { position: 1; color: "#23402B" }
            }
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "#2F6A80"
                strokeWidth: Math.max(2, 0.07 * Math.min(scene.w, scene.h))
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                startX: 0
                startY: scene.h

                PathLine { x: scene.w; y: 0 }
            }

            ShapePath {
                strokeColor: "#80C9B07A"
                strokeWidth: Math.max(1.5, 0.03 * Math.min(scene.w, scene.h))
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                startX: 0.07 * scene.w
                startY: 0.09 * scene.h

                PathLine { x: 0.93 * scene.w; y: 0.91 * scene.h }
            }
        }

        Rectangle {
            x: 0.07 * scene.w
            y: 0.09 * scene.h
            width: 0.86 * scene.w
            height: 0.82 * scene.h
            radius: 3
            color: "transparent"
            border.width: Math.max(1.5, 0.035 * Math.min(scene.w, scene.h))
            border.color: "#80C9B07A"
        }

        Base {
            x: 0.04 * scene.w
            y: scene.h * (1 - 0.06) - height
            width: 0.09 * scene.w
            height: 0.16 * scene.h
            core: "#5BA8FF"
            halo: "#265BA8FF"
        }

        Base {
            x: scene.w * (1 - 0.04) - width
            y: 0.06 * scene.h
            width: 0.09 * scene.w
            height: 0.16 * scene.h
            core: "#FF6B6B"
            halo: "#26FF6B6B"
        }

        Rectangle {
            x: 0.47 * scene.w
            y: 0.52 * scene.h
            width: Math.max(4, 0.03 * scene.w)
            height: width
            radius: width / 2
            color: "#F5D27A"
            border.width: Math.max(1, width * 0.3)
            border.color: "#4DF5D27A"
        }
    }

    Item {
        anchors.fill: parent
        visible: scene.kind === "lobby"

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: 0.3 * scene.w
                    centerY: 0.4 * scene.h
                    centerRadius: 0.7 * Math.max(scene.w, scene.h)
                    focalX: centerX
                    focalY: centerY
                    GradientStop { position: 0; color: "#20405A" }
                    GradientStop { position: 1; color: "#0E1620" }
                }

                PathRectangle {
                    width: scene.w
                    height: scene.h
                }
            }
        }

        Rectangle {
            x: 0.08 * scene.w
            y: 0.14 * scene.h
            width: 0.3 * scene.w
            height: 0.12 * scene.h
            radius: Math.min(6, height / 2)
            opacity: 0.9
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#F5C542" }
                GradientStop { position: 1; color: "#F59E42" }
            }
        }

        Repeater {
            model: 3

            Rectangle {
                id: menuBar

                required property int index

                x: 0.08 * scene.w
                y: (0.38 + menuBar.index * 0.12) * scene.h
                width: 0.24 * scene.w
                height: 0.07 * scene.h
                radius: Math.min(5, height / 2)
                color: "#1FFFFFFF"
            }
        }

        Repeater {
            model: 2

            Rectangle {
                id: lobbyCard

                required property int index

                x: (0.42 + lobbyCard.index * 0.17) * scene.w
                y: 0.34 * scene.h
                width: 0.14 * scene.w
                height: 0.34 * scene.h
                radius: Math.min(8, width / 4)
                color: "#14FFFFFF"
                border.width: 1
                border.color: "#1FFFFFFF"
            }
        }

        Rectangle {
            objectName: "startButton"
            x: scene.w * (1 - 0.09) - width
            y: scene.h * (1 - 0.13) - height
            width: 0.22 * scene.w
            height: 0.13 * scene.h
            radius: Math.min(8, height / 2)
            color: "#F5C542"
            border.width: Math.max(1, height * 0.12)
            border.color: "#40F5C542"
        }
    }

    component Base: Rectangle {
        id: base

        property color core
        property color halo

        radius: Math.min(width, height) / 2
        color: base.halo

        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.6
            height: parent.height * 0.6
            radius: Math.min(width, height) / 2
            color: base.core
        }
    }
}
