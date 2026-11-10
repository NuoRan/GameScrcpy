import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.Pane {
    id: control

    property bool active: false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    padding: Theme.space.lg
    horizontalPadding: Theme.space.lg + Theme.space.xxs

    background: Rectangle {
        id: face

        radius: Theme.radius.xl
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: control.active ? Theme.colors.accentLine : Theme.colors.strokeSubtle

        Behavior on border.color {
            ColorAnimation {
                duration: Theme.motion.fast
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: face.radius
            opacity: control.active ? 1 : 0
            gradient: Gradient {
                orientation: Gradient.Horizontal

                GradientStop {
                    position: 0
                    color: Theme.colors.accentGlow
                }
                GradientStop {
                    position: 0.45
                    color: "transparent"
                }
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.motion.fast
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.motion.standard
                }
            }
        }

        Rectangle {
            x: face.radius
            y: Theme.size.stroke
            width: face.width - 2 * face.radius
            height: Theme.size.stroke
            color: Theme.colors.highlight
        }
    }
}
