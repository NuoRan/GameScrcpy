pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.size.control
    leftPadding: Theme.space.md
    rightPadding: Theme.space.md + Theme.size.iconSmall + Theme.space.sm
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    font.pixelSize: Theme.type.label.size

    function widestText(): real {
        let widest = 0
        for (let index = 0; index < count; ++index) {
            measure.text = textAt(index)
            widest = Math.max(widest, measure.advanceWidth)
        }
        return widest
    }

    TextMetrics {
        id: measure

        font: control.font
    }

    contentItem: Text {
        textFormat: Text.PlainText
        text: control.displayText
        font: control.font
        color: Theme.colors.textPrimary
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Icon {
        x: control.width - Theme.space.md - width
        y: (control.height - height) / 2
        name: "chevron-down"
        size: Theme.size.iconSmall
        color: Theme.colors.textSecondary
        rotation: control.popup.visible ? 180 : 0

        Behavior on rotation {
            NumberAnimation {
                duration: Theme.motion.fast
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }
    }

    background: FieldFrame {
        implicitWidth: Theme.size.fieldWidth
        focused: control.visualFocus || control.popup.visible
        hovered: control.hovered
    }

    delegate: T.ItemDelegate {
        id: row

        required property var model
        required property int index
        readonly property bool current: index === control.currentIndex

        width: ListView.view.width
        implicitHeight: Theme.size.menuItem
        horizontalPadding: Theme.space.sm + Theme.space.xxs
        text: control.textRole === "" ? model.modelData : model[control.textRole]
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        font: control.font

        contentItem: Item {
            implicitWidth: label.implicitWidth + Theme.space.md + Theme.size.iconSmall
            implicitHeight: label.implicitHeight

            Text {
                id: label

                anchors.left: parent.left
                anchors.right: check.left
                anchors.rightMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: row.text
                font: row.font
                color: Theme.colors.textPrimary
                elide: Text.ElideRight
            }

            Icon {
                id: check

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: row.current
                name: "check"
                size: Theme.size.iconSmall
                color: Theme.colors.accentText
            }
        }

        background: Rectangle {
            radius: Theme.radius.sm
            color: row.highlighted ? Theme.colors.statePress : "transparent"
        }
    }

    popup: T.Popup {
        id: choices

        y: control.height + Theme.space.xs
        width: control.width
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
        padding: Theme.space.xs + Theme.space.xxs
        margins: Theme.space.sm
        onAboutToShow: choices.width = Math.max(control.width, control.widestText() + leftPadding + rightPadding
                                                + 2 * (Theme.space.sm + Theme.space.xxs) + Theme.space.md
                                                + Theme.size.iconSmall + Theme.space.sm)

        contentItem: ListView {
            implicitHeight: contentHeight
            clip: true
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }

        background: PopupSurface {}

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.fast
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: Theme.motion.fast
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.exit
            }
        }
    }
}
