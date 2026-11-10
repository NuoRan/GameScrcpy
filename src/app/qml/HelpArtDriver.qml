pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Item {
    id: art

    property string kind: "open"
    readonly property bool listing: art.kind === "list"
    readonly property bool picking: art.kind === "pick"
    readonly property bool installing: art.kind === "install"

    implicitWidth: art.kind === "done" ? done.width : zadig.width
    implicitHeight: art.kind === "done" ? done.height : zadig.height

    component ZadigText: Text {
        textFormat: Text.PlainText
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.caption.size
    }

    component Ring: Rectangle {
        required property Item around

        x: around.x - Theme.space.xs
        y: around.y - Theme.space.xs
        width: around.width + 2 * Theme.space.xs
        height: around.height + 2 * Theme.space.xs
        radius: Theme.radius.md
        color: "transparent"
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accent
    }

    Rectangle {
        id: zadig

        objectName: "zadigWindow"
        visible: art.kind !== "done"
        width: 360
        height: content.y + content.height + Theme.space.lg
        radius: Theme.radius.lg
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong

        Item {
            id: title

            width: parent.width
            height: Theme.size.pill + Theme.space.md

            Icon {
                id: titleIcon

                x: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                name: "usb"
                size: Theme.size.iconSmall
                color: Theme.colors.textSecondary
            }

            Text {
                anchors.left: titleIcon.right
                anchors.leftMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: "Zadig"
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.caption.size
                font.weight: Font.DemiBold
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.sm

                Repeater {
                    model: ["minus", "square", "x"]

                    Icon {
                        required property string modelData

                        name: modelData
                        size: Theme.size.infoIcon
                        color: Theme.colors.textTertiary
                    }
                }
            }
        }

        Row {
            id: menu

            x: Theme.space.md
            y: title.height
            spacing: Theme.space.lg

            ZadigText { text: "Device" }
            ZadigText {
                id: options

                text: "Options"
                color: art.listing ? Theme.colors.accentText : Theme.colors.textSecondary
                font.weight: art.listing ? Font.DemiBold : Font.Normal
            }
            ZadigText { text: "Help" }
        }

        Item {
            id: content

            x: Theme.space.md
            y: menu.y + menu.height + Theme.space.md
            width: parent.width - 2 * Theme.space.md
            height: install.y + install.height

            Rectangle {
                id: device

                width: parent.width
                height: Theme.size.menuItem
                radius: Theme.radius.sm
                color: Theme.colors.bgSunken
                border.width: Theme.size.stroke
                border.color: art.picking ? Theme.colors.accent : Theme.colors.strokeDefault

                Text {
                    x: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: "Pixel 8 (Interface 0)"
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.caption.size
                }

                Icon {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-down"
                    size: Theme.size.infoIcon
                    color: Theme.colors.textSecondary
                }
            }

            Row {
                id: driver

                y: device.y + device.height + Theme.space.md
                spacing: Theme.space.sm

                ZadigText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Driver"
                }

                Rectangle {
                    width: none.implicitWidth + 2 * Theme.space.sm
                    height: Theme.size.pill + Theme.space.xs
                    radius: Theme.radius.sm
                    color: Theme.colors.bgSunken
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeDefault

                    Text {
                        id: none

                        anchors.centerIn: parent
                        textFormat: Text.PlainText
                        text: "(NONE)"
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                    }
                }

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-right"
                    size: Theme.size.iconSmall
                    color: Theme.colors.textTertiary
                }

                Rectangle {
                    id: winUsb

                    width: winUsbText.implicitWidth + 2 * Theme.space.sm
                    height: Theme.size.pill + Theme.space.xs
                    radius: Theme.radius.sm
                    color: art.installing ? Theme.colors.accentSubtle : Theme.colors.bgSunken
                    border.width: Theme.size.stroke
                    border.color: art.installing ? Theme.colors.accent : Theme.colors.strokeDefault

                    Text {
                        id: winUsbText

                        anchors.centerIn: parent
                        textFormat: Text.PlainText
                        text: "WinUSB"
                        color: art.installing ? Theme.colors.accentText : Theme.colors.textPrimary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: Font.DemiBold
                    }
                }
            }

            Rectangle {
                id: install

                y: driver.y + driver.height + Theme.space.md
                width: installText.implicitWidth + 2 * Theme.space.lg
                height: Theme.size.menuItem
                radius: Theme.radius.md
                color: art.installing ? Theme.colors.accent : Theme.colors.bgSunken
                border.width: Theme.size.stroke
                border.color: art.installing ? Theme.colors.accent : Theme.colors.strokeDefault

                Text {
                    id: installText

                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: "Replace Driver"
                    color: art.installing ? Theme.colors.textOnAccent : Theme.colors.textPrimary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                }
            }

            Ring {
                visible: art.picking
                around: device
            }

            Ring {
                visible: art.installing
                around: install
            }
        }

        Rectangle {
            objectName: "zadigOptions"
            visible: art.listing
            x: menu.x + options.x - Theme.space.sm
            y: menu.y + menu.height + Theme.space.xs
            width: listAll.implicitWidth + checkIcon.width + 3 * Theme.space.md
            height: Theme.size.menuItem + Theme.space.sm
            radius: Theme.radius.md
            color: Theme.colors.bgOverlay
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accent

            Icon {
                id: checkIcon

                x: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: Theme.size.iconSmall
                color: Theme.colors.accentText
            }

            Text {
                id: listAll

                anchors.left: checkIcon.right
                anchors.leftMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: "List All Devices"
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.caption.size
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            visible: art.kind === "open"
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accent
        }
    }

    Rectangle {
        id: done

        objectName: "driverDone"
        visible: art.kind === "done"
        width: 340
        height: doneColumn.implicitHeight + 2 * Theme.space.lg
        radius: Theme.radius.xl
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeDefault

        Column {
            id: doneColumn

            x: Theme.space.lg
            y: Theme.space.lg
            width: parent.width - 2 * Theme.space.lg
            spacing: Theme.space.sm

            Row {
                spacing: Theme.space.md

                Rectangle {
                    width: Theme.size.phoneWidth
                    height: Theme.size.phoneHeight
                    radius: Theme.radius.lg
                    color: Theme.colors.bgSunken
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeStrong

                    Icon {
                        anchors.centerIn: parent
                        name: "usb"
                        size: Theme.size.iconLarge
                        color: Theme.colors.textTertiary
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.xs

                    Row {
                        spacing: Theme.space.sm

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: "Pixel 8"
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.body.size
                            font.weight: Font.DemiBold
                        }

                        StatusPill {
                            anchors.verticalCenter: parent.verticalCenter
                            tone: "ok"
                            text: qsTr("Online")
                        }
                    }

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("USB · AOA")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }
            }

            Row {
                spacing: Theme.space.xs

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "circle-check"
                    size: Theme.size.iconSmall
                    color: Theme.colors.successText
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: qsTr("The driver is installed")
                    color: Theme.colors.successText
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.Medium
                }
            }
        }
    }
}
