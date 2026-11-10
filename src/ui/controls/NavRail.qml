pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.Control {
    id: control

    property var model: []
    property int currentIndex: 0
    default property alias footer: bottom.data

    signal activated(int index)

    function select(index: int) {
        const page = Math.max(0, Math.min(index, pages.count - 1))
        if (page === currentIndex)
            return
        currentIndex = page
        activated(page)
    }

    implicitWidth: Theme.size.navRail
    implicitHeight: top.implicitHeight + bottom.implicitHeight + topPadding + bottomPadding
    topPadding: Theme.space.sm
    bottomPadding: Theme.space.md
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.PageTabList

    Keys.onUpPressed: select(currentIndex - 1)
    Keys.onDownPressed: select(currentIndex + 1)

    contentItem: Item {
        Column {
            id: top

            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space.xs

            Repeater {
                id: pages

                property int generation: 0

                model: control.model
                onItemAdded: generation++
                onItemRemoved: generation++

                delegate: NavItem {
                    required property var modelData
                    required property int index

                    text: modelData.text ?? ""
                    iconName: modelData.icon ?? ""
                    current: index === control.currentIndex
                    focusShown: current && control.visualFocus
                    onClicked: control.select(index)
                }
            }
        }

        Rectangle {
            readonly property Item target: pages.generation >= 0 && pages.count > 0 ? pages.itemAt(control.currentIndex)
                                                                                    : null

            visible: target !== null
            x: -control.leftPadding
            y: target ? top.y + target.y + (target.height - height) / 2 : 0
            width: Theme.size.navIndicatorWidth
            height: Theme.size.navIndicatorHeight
            radius: width / 2
            color: Theme.colors.accent

            Behavior on y {
                NumberAnimation {
                    duration: Theme.motion.base
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.motion.emphasized
                }
            }
        }

        Column {
            id: bottom

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            spacing: Theme.space.xs
        }
    }
}
