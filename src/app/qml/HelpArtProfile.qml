pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Item {
    id: art

    property string kind: "link"

    implicitWidth: art.kind === "share" ? share.width : link.width
    implicitHeight: art.kind === "share" ? share.height : link.height

    component Caption: Text {
        textFormat: Text.PlainText
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        horizontalAlignment: Text.AlignHCenter
    }

    component GameTile: Rectangle {
        width: 52
        height: width
        radius: Theme.radius.xl
        border.width: Theme.size.stroke
        border.color: "#1FFFFFFF"
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "#F5C542" }
            GradientStop { position: 1; color: "#E8873A" }
        }

        Icon {
            anchors.centerIn: parent
            name: "gamepad-2"
            size: Theme.size.iconLarge + Theme.space.xs
            color: "#99000000"
        }
    }

    component ProfileCard: Rectangle {
        id: card

        property string name
        property bool chosen: false

        width: 168
        height: 112
        radius: Theme.radius.lg
        color: Theme.colors.bgOverlay
        border.width: card.chosen ? Theme.size.focusRing : Theme.size.stroke
        border.color: card.chosen ? Theme.colors.accentLine : Theme.colors.strokeDefault

        Rectangle {
            id: preview

            x: Theme.space.sm
            y: Theme.space.sm
            width: parent.width - 2 * Theme.space.sm
            height: 58
            radius: Theme.radius.md
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Rectangle {
                x: Theme.space.md
                y: (parent.height - height) / 2
                width: 26
                height: width
                radius: width / 2
                color: Theme.colors.accentSubtle
                border.width: Theme.size.stroke
                border.color: Theme.colors.accentLine
            }

            Repeater {
                model: [[0.72, 0.6], [0.84, 0.3], [0.86, 0.72]]

                Rectangle {
                    required property var modelData

                    x: preview.width * modelData[0]
                    y: preview.height * modelData[1] - height / 2
                    width: Theme.size.previewDot
                    height: width
                    radius: width / 2
                    color: Theme.colors.accent
                }
            }
        }

        Text {
            x: Theme.space.sm + Theme.space.xxs
            anchors.top: preview.bottom
            anchors.topMargin: Theme.space.sm
            width: parent.width - 2 * Theme.space.md
            textFormat: Text.PlainText
            text: card.name
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.caption.size
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        StatusPill {
            visible: card.chosen
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space.sm
            tone: "live"
            text: qsTr("This game")
        }
    }

    component FileRow: Row {
        id: file

        property string iconName
        property string label

        spacing: Theme.space.sm

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: file.iconName
            size: Theme.size.iconSmall - Theme.space.xxs
            color: Theme.colors.textTertiary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: file.label
            color: Theme.colors.textSecondary
            font.family: Theme.type.monoFamily
            font.pixelSize: Theme.type.caption.size
        }
    }

    component Arrow: Icon {
        name: "chevron-right"
        size: Theme.size.iconLarge
        color: Theme.colors.textTertiary
    }

    Item {
        id: link

        visible: art.kind === "link"
        width: 600
        height: 176

        HelpScreen {
            id: phone

            kind: "phone"
            scene: "moba"
            x: 24
            y: 24
            width: 180
            height: 88

            GameTile {
                anchors.centerIn: parent
                scale: 0.7
            }
        }

        Caption {
            anchors.horizontalCenter: phone.horizontalCenter
            anchors.top: phone.bottom
            anchors.topMargin: Theme.space.md
            text: qsTr("The game in front")
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Theme.colors.strokeStrong
                strokeWidth: 1.5
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 3]
                fillColor: "transparent"
                startX: phone.x + phone.width + Theme.space.md
                startY: phone.y + phone.height / 2

                PathLine { x: chosen.x - Theme.space.md; y: phone.y + phone.height / 2 }
            }
        }

        Rectangle {
            x: (phone.x + phone.width + chosen.x) / 2 - width / 2
            y: phone.y + phone.height / 2 - height / 2
            width: 36
            height: width
            radius: width / 2
            color: Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeStrong

            Icon {
                anchors.centerIn: parent
                name: "link"
                size: Theme.size.iconSmall
                color: Theme.colors.accentText
            }
        }

        ProfileCard {
            x: 404
            y: 6
            opacity: 0.5
            name: qsTr("Another profile")
        }

        ProfileCard {
            id: chosen

            x: 384
            y: 22
            chosen: true
            name: qsTr("My MOBA keys")
        }
    }

    Item {
        id: share

        visible: art.kind === "share"
        width: 640
        height: 168

        Rectangle {
            id: folder

            x: 8
            y: 14
            width: 176
            height: 122
            radius: Theme.radius.lg
            color: Theme.colors.bgOverlay
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Column {
                x: Theme.space.md
                y: Theme.space.md
                spacing: Theme.space.sm

                Row {
                    spacing: Theme.space.sm

                    Tile {
                        iconName: "folder-open"
                        tone: "teal"
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("My MOBA keys")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: Font.DemiBold
                    }
                }

                FileRow { iconName: "keyboard"; label: "profile.json" }
                FileRow { iconName: "code-xml"; label: "logic.ts" }
                FileRow { iconName: "folder-open"; label: "res" }
            }
        }

        Caption {
            anchors.horizontalCenter: folder.horizontalCenter
            anchors.top: folder.bottom
            anchors.topMargin: Theme.space.sm
            text: qsTr("Profile folder")
        }

        Arrow {
            x: (folder.x + folder.width + packed.x) / 2 - width / 2
            y: folder.y + folder.height / 2 - height / 2
        }

        Rectangle {
            id: packed

            x: 238
            y: 40
            width: 154
            height: 70
            radius: Theme.radius.lg
            color: Theme.colors.accentSubtle
            border.width: Theme.size.stroke
            border.color: Theme.colors.accentLine

            Row {
                anchors.centerIn: parent
                spacing: Theme.space.sm

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "share"
                    size: Theme.size.iconLarge
                    color: Theme.colors.accentText
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.xxs

                    Text {
                        textFormat: Text.PlainText
                        text: ".gsprofile"
                        color: Theme.colors.textPrimary
                        font.family: Theme.type.monoFamily
                        font.pixelSize: Theme.type.label.size
                        font.weight: Font.DemiBold
                    }

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("one file to send")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.micro.size
                        font.weight: Font.Normal
                    }
                }
            }
        }

        Caption {
            anchors.horizontalCenter: packed.horizontalCenter
            y: folder.y + folder.height + Theme.space.sm
            text: qsTr("Export…")
        }

        Arrow {
            x: (packed.x + packed.width + friend.x) / 2 - width / 2
            y: folder.y + folder.height / 2 - height / 2
        }

        HelpScreen {
            id: friend

            kind: "monitor"
            scene: ""
            color: Theme.colors.bgElevated
            x: 446
            y: 22
            width: 180
            height: 106

            Rectangle {
                anchors.fill: parent
                color: Theme.colors.bgBase

                Rectangle {
                    anchors.centerIn: parent
                    width: importRow.implicitWidth + 2 * Theme.space.md
                    height: Theme.size.controlSmall
                    radius: Theme.radius.md
                    color: Theme.colors.bgOverlay
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeStrong

                    Row {
                        id: importRow

                        anchors.centerIn: parent
                        spacing: Theme.space.xs

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "import"
                            size: Theme.size.iconSmall - Theme.space.xxs
                            color: Theme.colors.textPrimary
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: qsTr("Import…")
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.caption.size
                        }
                    }
                }
            }
        }

        Caption {
            anchors.horizontalCenter: friend.horizontalCenter
            y: folder.y + folder.height + Theme.space.sm
            text: qsTr("A friend’s GameScrcpy")
        }
    }
}
