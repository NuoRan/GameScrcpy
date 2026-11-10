pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls
import GameScrcpy.App

FocusScope {
    id: panel

    property string serial
    property string game
    property bool offered: false
    property bool portrait: false
    property string failure
    readonly property var community: game === "" ? []
                                     : ProfileLibrary.entries.filter(entry => entry.readOnly && entry.error === ""
                                                                     && entry.app === panel.game)
    readonly property var target: {
        const mirrors = DeviceManager.mirrors
        if (serial !== "")
            return mirrors.find(mirror => mirror.serial === panel.serial) ?? null
        const free = mirrors.filter(mirror => (mirror.editingId ?? "") === "")
        return free.find(mirror => panel.game !== "" && mirror.app === panel.game) ?? free[0] ?? null
    }
    readonly property string kinds: visible ? ProfileLibrary.templates().filter(each => each.controls > 0)
                                                  .map(each => each.name).join(qsTr(", ", "list separator"))
                                            : ""
    readonly property real gap: Theme.space.sm + Theme.space.xxs
    readonly property real choiceWidth: Theme.size.previewLong + Theme.space.x5l
    readonly property int columns: width - 2 * Theme.space.lg >= 4 * choiceWidth + 3 * gap ? 4 : 2
    readonly property real cellWidth: (layout.width - (columns - 1) * gap) / columns
    readonly property real cellHeight: Math.max(templateChoice.implicitHeight, communityChoice.implicitHeight,
                                                draftChoice.implicitHeight, blankChoice.implicitHeight)

    signal templateRequested()
    signal communityRequested(string id)
    signal importRequested()
    signal created(string id, bool editing)
    signal closeRequested()
    signal focusReleased()

    function startBlank(ai: bool) {
        panel.failure = ""
        const id = ProfileLibrary.createFromTemplate("blank",
                                                     panel.freeName(qsTr("New profile", "name of a new blank profile")),
                                                     panel.game, panel.portrait)
        if (id === "") {
            panel.failure = qsTr("The profile could not be created.")
            return
        }
        const device = panel.target
        const editing = device !== null && DeviceManager.editProfile(device.serial, id)
        if (editing && ai)
            DeviceManager.showAssistant(device.serial)
        panel.created(id, editing)
    }

    function startDraft() {
        if (AiAssistant.ready) {
            panel.startBlank(true)
            return
        }
        AiAssistant.openSettings()
        if (panel.serial === "")
            panel.closeRequested()
    }

    function freeName(base: string): string {
        const taken = ProfileLibrary.entries.map(entry => entry.name)
        if (!taken.includes(base))
            return base
        for (let number = 2; ; ++number) {
            const candidate = base + " " + number
            if (!taken.includes(candidate))
                return candidate
        }
    }

    implicitWidth: 4 * choiceWidth + 3 * gap + 2 * Theme.space.lg
    implicitHeight: layout.implicitHeight + 2 * Theme.space.lg

    Accessible.role: Accessible.Grouping
    Accessible.name: title.text

    Keys.onEscapePressed: panel.closeRequested()
    onVisibleChanged: {
        if (!visible && activeFocus)
            focusReleased()
    }
    onActiveFocusChanged: {
        if (!activeFocus && !visible)
            focusReleased()
    }

    component Choice: T.AbstractButton {
        id: choice

        property string detail
        property string note
        property string glyph
        property string tone

        width: panel.cellWidth
        height: panel.cellHeight
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        padding: Theme.space.md
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.name: text
        Accessible.description: [detail, note].filter(part => part !== "").join(" ")

        background: Rectangle {
            radius: Theme.radius.lg
            color: choice.down ? Theme.colors.statePress
                 : choice.hovered ? Theme.colors.stateSelected : Theme.colors.stateHover
            border.width: Theme.size.stroke
            border.color: choice.hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault
        }

        contentItem: Column {
            spacing: Theme.space.sm + Theme.space.xxs

            Tile {
                iconName: choice.glyph
                tone: choice.tone
            }

            Column {
                width: parent.width
                spacing: Theme.space.xxs

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: choice.text
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: choice.detail
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: choice.note
                    color: Theme.colors.infoText
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    PopupSurface {
        anchors.fill: parent
        radius: Theme.radius.xl
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: panel.forceActiveFocus(Qt.MouseFocusReason)
        onWheel: wheel => {
            wheel.accepted = true
        }
    }

    Column {
        id: layout

        x: Theme.space.lg
        y: Theme.space.lg
        width: panel.width - 2 * Theme.space.lg
        spacing: Theme.space.md

        Item {
            width: parent.width
            implicitHeight: Math.max(gameTile.implicitHeight, heading.implicitHeight, closeButton.implicitHeight)

            Tile {
                id: gameTile

                anchors.verticalCenter: parent.verticalCenter
                large: true
                iconName: "gamepad-2"
                tone: "orange"
            }

            Column {
                id: heading

                anchors.left: gameTile.right
                anchors.leftMargin: Theme.space.md
                anchors.right: closeButton.left
                anchors.rightMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xxs

                Text {
                    id: title

                    objectName: "startTitle"
                    width: parent.width
                    textFormat: Text.PlainText
                    text: {
                        if (panel.offered && panel.game !== "")
                            return qsTr("%1 has no key mapping profile yet").arg(panel.game)
                        if (panel.game !== "")
                            return qsTr("New profile for %1").arg(panel.game)
                        return qsTr("New key mapping profile")
                    }
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.heading.size
                    font.weight: Theme.type.heading.weight
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: qsTr("Pick a starting point, then drag the keys onto the game's buttons in the editor.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }

            Button {
                id: closeButton

                objectName: "startClose"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                tone: "ghost"
                small: true
                iconName: "x"
                description: qsTr("Close")
                focusPolicy: Qt.NoFocus
                onClicked: panel.closeRequested()
            }
        }

        Grid {
            objectName: "startChoices"
            columns: panel.columns
            spacing: panel.gap

            Choice {
                id: templateChoice

                objectName: "startTemplate"
                glyph: "layers"
                tone: "orange"
                text: qsTr("Use a template")
                detail: panel.kinds
                onClicked: panel.templateRequested()
            }

            Choice {
                id: communityChoice

                objectName: "startCommunity"
                glyph: "puzzle"
                tone: "teal"
                text: qsTr("Community profiles")
                detail: panel.community.length > 0
                        ? qsTr("%n from installed extensions, or import a profile file", "", panel.community.length)
                        : qsTr("Import a profile file someone shared")
                onClicked: {
                    if (panel.community.length > 0)
                        communityMenu.open()
                    else
                        panel.importRequested()
                }

                Menu {
                    id: communityMenu

                    objectName: "startCommunityMenu"
                    y: -height - Theme.space.xs

                    Instantiator {
                        model: panel.community

                        delegate: MenuItem {
                            required property var modelData

                            text: modelData.name
                            shortcut: modelData.extensionName
                            onTriggered: panel.communityRequested(modelData.id)
                        }

                        onObjectAdded: (index, object) => communityMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => communityMenu.removeItem(object)
                    }

                    MenuSeparator {}

                    MenuItem {
                        objectName: "startImport"
                        iconName: "import"
                        text: qsTr("Import profile file…")
                        onTriggered: panel.importRequested()
                    }
                }
            }

            Choice {
                id: draftChoice

                objectName: "startDraft"
                glyph: "sparkles"
                tone: "azure"
                text: qsTr("AI draft")
                detail: qsTr("Drafts the keys from the picture; nothing applies until you confirm")
                note: AiAssistant.ready ? "" : qsTr("Set up a model in Settings > AI first")
                onClicked: panel.startDraft()
            }

            Choice {
                id: blankChoice

                objectName: "startBlank"
                glyph: "plus"
                text: qsTr("Start blank")
                detail: qsTr("Start from nothing and place every key yourself")
                onClicked: panel.startBlank(false)
            }
        }

        Text {
            objectName: "startFailure"
            width: parent.width
            visible: panel.failure !== ""
            textFormat: Text.PlainText
            text: panel.failure
            color: Theme.colors.dangerText
            font.pixelSize: Theme.type.caption.size
            wrapMode: Text.Wrap
        }

        Row {
            visible: panel.offered
            spacing: Theme.space.sm

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "info"
                size: Theme.size.iconSmall
                color: Theme.colors.textTertiary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, layout.width - Theme.size.iconSmall - parent.spacing)
                textFormat: Text.PlainText
                text: qsTr("Once closed it won't come back by itself for this game. New profile… in the key mapping options on the toolbar opens it again.")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }
    }
}
