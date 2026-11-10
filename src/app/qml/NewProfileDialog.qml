pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    property string serial
    property bool importable: false
    property var templates: []
    property int chosen: 0
    property bool nameEdited: false
    property string failure
    readonly property var mirrors: DeviceManager.mirrors
    readonly property var games: {
        const apps = []
        for (const mirror of mirrors) {
            if (mirror.app !== "" && !apps.includes(mirror.app))
                apps.push(mirror.app)
        }
        return apps
    }
    readonly property var gameChoices: [""].concat(games)
    readonly property string game: gameChoices[gameSelect.currentIndex] ?? ""
    readonly property var target: {
        if (serial !== "")
            return mirrors.find(mirror => mirror.serial === serial) ?? null
        const free = mirrors.filter(mirror => (mirror.editingId ?? "") === "")
        return free.find(mirror => game !== "" && mirror.app === game) ?? free[0] ?? null
    }
    readonly property var current: templates[chosen] ?? null
    readonly property bool portrait: orientation.currentIndex === 1
    readonly property real tileWidth: Theme.size.previewLong + 2 * Theme.space.sm
    readonly property real tileGap: Theme.space.sm + Theme.space.xxs
    readonly property real sideGap: Theme.space.xl + Theme.space.xxs
    readonly property real sideWidth: Theme.size.fieldWidth + Theme.space.x5l
    readonly property real room: parent ? parent.width - 2 * Theme.space.xxl : 0
    readonly property real wideWidth: 3 * tileWidth + 2 * tileGap + sideGap + sideWidth + leftPadding + rightPadding
    readonly property bool narrow: room > 0 && room < wideWidth
    readonly property real bodyLimit: (parent ? parent.height : 0) - 2 * Theme.space.x3l - implicitHeaderHeight
                                      - implicitFooterHeight - topPadding - bottomPadding
    readonly property string runningOn: mirrors.filter(mirror => game !== "" && mirror.app === game)
                                               .map(mirror => mirror.name).join(qsTr(", ", "list separator"))

    signal created(string id, bool editing)
    signal importRequested()

    function start(app: string) {
        dialog.templates = ProfileLibrary.templates()
        dialog.failure = ""
        dialog.chosen = 0
        dialog.nameEdited = false
        const at = dialog.gameChoices.indexOf(app)
        gameSelect.currentIndex = at >= 0 ? at : 0
        orientation.currentIndex = 0
        nameField.text = dialog.suggestedName()
        dialog.open()
    }

    function choose(index: int) {
        dialog.chosen = index
        if (!dialog.nameEdited)
            nameField.text = dialog.suggestedName()
    }

    function suggestedName(): string {
        const base = dialog.current === null || dialog.current.controls === 0
                     ? qsTr("New profile", "name of a new blank profile") : dialog.current.name
        const taken = ProfileLibrary.entries.map(entry => entry.name)
        if (!taken.includes(base))
            return base
        for (let number = 2; ; ++number) {
            const candidate = base + " " + number
            if (!taken.includes(candidate))
                return candidate
        }
    }

    function create() {
        dialog.failure = ""
        if (dialog.current === null || nameField.text.trim() === "")
            return
        const id = ProfileLibrary.createFromTemplate(dialog.current.id, nameField.text, dialog.game, dialog.portrait)
        if (id === "") {
            dialog.failure = qsTr("The profile could not be created.")
            return
        }
        const device = dialog.target
        dialog.close()
        const editing = device !== null && DeviceManager.editProfile(device.serial, id)
        dialog.created(id, editing)
    }

    title: qsTr("New profile")
    implicitWidth: narrow ? Math.max(2 * tileWidth + tileGap, sideWidth) + leftPadding + rightPadding : wideWidth

    header: Column {
        topPadding: Theme.space.xxl
        leftPadding: Theme.space.xxl
        rightPadding: Theme.space.xxl
        spacing: Theme.space.xs

        Text {
            width: dialog.availableWidth
            textFormat: Text.PlainText
            text: dialog.title
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.subtitle.size
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            width: dialog.availableWidth
            textFormat: Text.PlainText
            text: qsTr("Pick the closest kind of game, then drag the keys onto the game's buttons in the editor.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
            wrapMode: Text.Wrap
        }
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + 2 * Theme.space.xl

        Button {
            objectName: "importProfile"
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.xxl
            anchors.verticalCenter: parent.verticalCenter
            visible: dialog.importable && !dialog.narrow
            tone: "ghost"
            iconName: "import"
            text: qsTr("Import from file…")
            onClicked: {
                dialog.close()
                dialog.importRequested()
            }
        }

        Row {
            id: buttons

            anchors.right: parent.right
            anchors.rightMargin: Theme.space.xxl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Button {
                objectName: "reject"
                text: qsTr("Cancel")
                onClicked: dialog.reject()
            }

            Button {
                id: createButton

                objectName: "accept"
                tone: "primary"
                text: dialog.target !== null ? qsTr("Create and edit") : qsTr("Create")
                enabled: dialog.current !== null && nameField.text.trim() !== ""
                onClicked: dialog.create()
            }
        }
    }

    component Field: Column {
        id: field

        required property string label

        width: parent ? parent.width : implicitWidth
        spacing: Theme.space.xs + Theme.space.xxs

        Text {
            textFormat: Text.PlainText
            text: field.label
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: Theme.type.label.weight
        }
    }

    Flickable {
        id: scroller

        width: dialog.availableWidth
        height: implicitHeight
        implicitHeight: dialog.bodyLimit > 0 ? Math.min(body.implicitHeight, dialog.bodyLimit) : body.implicitHeight
        contentWidth: width
        contentHeight: body.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Flow {
            id: body

            width: scroller.width
            spacing: dialog.sideGap

            Grid {
                objectName: "templates"
                columns: dialog.narrow ? 2 : 3
                spacing: dialog.tileGap

                Repeater {
                    model: dialog.templates

                    delegate: T.AbstractButton {
                        id: tile

                        required property var modelData
                        required property int index
                        readonly property bool selected: index === dialog.chosen
                        readonly property bool blank: modelData.controls === 0

                        objectName: "templateTile"
                        width: dialog.tileWidth
                        implicitHeight: implicitContentHeight + topPadding + bottomPadding
                        padding: Theme.space.sm
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.name
                        Accessible.checked: selected
                        onClicked: dialog.choose(index)
                        onDoubleClicked: {
                            dialog.choose(index)
                            dialog.create()
                        }

                        background: Rectangle {
                            id: face

                            radius: Theme.radius.lg
                            color: Theme.colors.bgRaised
                            border.width: Theme.size.stroke
                            border.color: tile.selected ? Theme.colors.accent
                                                        : tile.hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -Theme.size.fieldGlow
                                visible: tile.selected
                                radius: face.radius + Theme.size.fieldGlow
                                color: "transparent"
                                border.width: Theme.size.fieldGlow
                                border.color: Theme.colors.accentSubtle
                            }

                            FocusRing {
                                visible: tile.visualFocus
                                baseRadius: face.radius
                            }
                        }

                        contentItem: Column {
                            spacing: Theme.space.sm

                            Item {
                                width: parent.width
                                height: Theme.size.previewShort

                                MappingPreview {
                                    anchors.centerIn: parent
                                    markers: dialog.portrait ? tile.modelData.portraitMarkers : tile.modelData.markers
                                    portrait: dialog.portrait
                                }

                                Icon {
                                    anchors.centerIn: parent
                                    visible: tile.blank
                                    name: tile.modelData.icon
                                    size: Theme.size.iconLarge
                                    color: Theme.colors.textTertiary
                                }
                            }

                            Item {
                                width: parent.width
                                height: Math.max(Theme.size.iconSmall, label.implicitHeight)

                                Icon {
                                    id: glyph

                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: !tile.blank
                                    name: tile.modelData.icon
                                    size: Theme.size.iconSmall
                                    color: tile.selected ? Theme.colors.accentText : Theme.colors.textSecondary
                                }

                                Text {
                                    id: label

                                    anchors.left: tile.blank ? parent.left : glyph.right
                                    anchors.leftMargin: tile.blank ? 0 : Theme.space.xs + Theme.space.xxs
                                    anchors.right: count.left
                                    anchors.rightMargin: Theme.space.xs
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: tile.modelData.name
                                    color: Theme.colors.textPrimary
                                    font.pixelSize: Theme.type.label.size
                                    font.weight: Theme.type.label.weight
                                    elide: Text.ElideRight
                                }

                                Text {
                                    id: count

                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: qsTr("%n key(s)", "", tile.modelData.controls)
                                    color: Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.caption.size
                                }
                            }
                        }
                    }
                }
            }

            Column {
                width: dialog.sideWidth
                spacing: Theme.space.lg

                Field {
                    label: qsTr("Name")

                    TextField {
                        id: nameField

                        objectName: "newProfileName"
                        width: parent.width
                        maximumLength: 64
                        selectByMouse: true
                        Accessible.name: qsTr("Name")
                        onTextEdited: dialog.nameEdited = true
                        onAccepted: {
                            if (createButton.enabled)
                                dialog.create()
                        }
                    }
                }

                Field {
                    label: qsTr("Linked game")

                    Select {
                        id: gameSelect

                        objectName: "newProfileGame"
                        width: parent.width
                        model: dialog.gameChoices.map(app => app === "" ? qsTr("No game") : app)
                        Accessible.name: qsTr("Linked game")
                    }

                    Text {
                        width: parent.width
                        visible: text !== ""
                        textFormat: Text.PlainText
                        text: dialog.runningOn !== "" ? qsTr("Running on %1").arg(dialog.runningOn)
                              : dialog.serial === "" && dialog.target !== null
                                ? qsTr("The editor opens on %1").arg(dialog.target.name) : ""
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }
                }

                Field {
                    label: qsTr("Orientation")

                    SegmentedControl {
                        id: orientation

                        objectName: "newProfileOrientation"
                        model: [qsTr("Landscape"), qsTr("Portrait")]
                        Accessible.name: qsTr("Orientation")
                    }
                }

                Field {
                    label: qsTr("Template includes")

                    Text {
                        objectName: "templateIncludes"
                        width: parent.width
                        textFormat: Text.PlainText
                        text: dialog.current === null ? ""
                              : dialog.current.controls === 0 ? qsTr("No keys yet. Add them in the editor.")
                                                              : dialog.current.includes.join(" · ")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                    }
                }

                Text {
                    objectName: "newProfileFailure"
                    width: parent.width
                    visible: dialog.failure !== ""
                    textFormat: Text.PlainText
                    text: dialog.failure
                    color: Theme.colors.dangerText
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
