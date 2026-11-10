pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls
import GameScrcpy.App

Item {
    id: panel

    property ProfileEditor editor
    property var row: null
    property string iconName
    property string typeText
    property string typeHint
    readonly property string keyId: row !== null ? row.id : ""
    readonly property bool onAtStart: row === null || row.enabled !== false
    readonly property string component: editor !== null && keyId !== "" && editor.revision >= 0
                                        ? editor.componentOf(keyId) : ""
    readonly property string placedRef: row !== null ? row.component ?? "" : ""
    readonly property var members: membersOf(row !== null ? row.type ?? "" : "")

    signal finished()
    signal referenced(string id)
    signal opened(string ref)

    function rename() {
        nameField.forceActiveFocus(Qt.ShortcutFocusReason)
        nameField.selectAll()
    }

    function membersOf(type: string): var {
        const event = (name, text) => ({ name: name, text: text, event: true })
        const call = (name, text) => ({ name: name, text: text, event: false })
        if (type === "component")
            return [event("onPress", qsTr("When the key goes down")), event("onRelease", qsTr("When the key comes up")),
                    event("onEnabled", qsTr("When turned on")), event("onDisabled", qsTr("When turned off")),
                    call("enable", qsTr("Turn on")), call("disable", qsTr("Turn off")),
                    call("toggle", qsTr("Turn on or off"))]
        let own = []
        switch (type) {
        case "tap":
            own = [call("tap", qsTr("Tap")), call("tapTimes", qsTr("Tap several times"))]
            break
        case "hold":
            own = [event("onPressed", qsTr("When pressed")), event("onReleased", qsTr("When released")),
                   call("press", qsTr("Press")), call("release", qsTr("Release")), call("holdFor", qsTr("Hold a while")),
                   call("toggleHold", qsTr("Press or release"))]
            break
        case "longPress":
            own = [event("onStart", qsTr("When the press starts")), event("onEnd", qsTr("When the press ends")),
                   call("longPress", qsTr("Long press"))]
            break
        case "turbo":
            own = [call("start", qsTr("Start firing")), call("stop", qsTr("Stop firing")), call("burst", qsTr("Fire a few"))]
            break
        case "swipe":
            own = [event("onStart", qsTr("When the swipe starts")), event("onEnd", qsTr("When the swipe ends")),
                   call("swipe", qsTr("Swipe"))]
            break
        case "pinch":
            own = [call("zoomIn", qsTr("Zoom in")), call("zoomOut", qsTr("Zoom out"))]
            break
        case "wasd":
        case "padStick":
            own = [event("onMoveStart", qsTr("When it starts moving")), event("onStop", qsTr("When it stops")),
                   event("onDirectionChange", qsTr("When it turns")), call("push", qsTr("Push toward")),
                   call("walkFor", qsTr("Walk a while")), call("release", qsTr("Let go"))]
            break
        case "rightWalk":
            own = [event("onWalkStart", qsTr("When walking starts")), event("onStop", qsTr("When walking stops")),
                   call("walkTo", qsTr("Walk to")), call("stop", qsTr("Stop walking"))]
            break
        case "camera":
            own = [event("onLock", qsTr("When locked")), event("onUnlock", qsTr("When unlocked")),
                   call("lock", qsTr("Lock")), call("unlock", qsTr("Unlock")), call("recenter", qsTr("Recenter")),
                   call("turn", qsTr("Turn"))]
            break
        case "padCamera":
            own = [event("onTurnStart", qsTr("When turning starts")), call("recenter", qsTr("Recenter")),
                   call("turn", qsTr("Turn"))]
            break
        case "freeLook":
            own = [event("onStart", qsTr("When looking starts")), event("onEnd", qsTr("When looking ends")),
                   call("begin", qsTr("Look around")), call("end", qsTr("Stop looking")), call("turn", qsTr("Turn"))]
            break
        case "skill":
            own = [event("onAimStart", qsTr("When aiming starts")), event("onCast", qsTr("When cast")),
                   event("onCancel", qsTr("When cancelled")), call("aim", qsTr("Aim")), call("cast", qsTr("Cast")),
                   call("cancel", qsTr("Cancel"))]
            break
        case "dial":
            own = [event("onOpen", qsTr("When it opens")), event("onChoose", qsTr("When a cell is chosen")),
                   call("choose", qsTr("Choose a cell"))]
            break
        case "multiPress":
            own = [event("onPressed", qsTr("When pressed")), event("onReleased", qsTr("When released")),
                   call("press", qsTr("Press")), call("release", qsTr("Release")), call("holdFor", qsTr("Hold a while")),
                   call("toggleHold", qsTr("Press or release"))]
            break
        case "tempCursor":
            own = [event("onShow", qsTr("When the cursor comes out")), event("onHide", qsTr("When the cursor goes back"))]
            break
        case "actionWheel":
            own = [event("onOpen", qsTr("When it opens")), event("onChoose", qsTr("When a cell is chosen")),
                   call("choose", qsTr("Run a cell"))]
            break
        }
        const common = [event("onPress", qsTr("When the key goes down")), event("onRelease", qsTr("When the key comes up")),
                        event("onEnabled", qsTr("When turned on")), event("onDisabled", qsTr("When turned off")),
                        call("enable", qsTr("Turn on")), call("disable", qsTr("Turn off")), call("reset", qsTr("Reset"))]
        const all = own.concat(common.filter(entry => !own.some(mine => mine.name === entry.name)))
        return all.filter(entry => entry.event).concat(all.filter(entry => !entry.event))
    }

    Keys.onEscapePressed: panel.finished()

    ProfileForm {
        id: propertyForm

        editor: panel.editor
        keyId: panel.keyId
    }

    ProfileForm {
        id: conditionForm

        editor: panel.editor
        keyId: panel.keyId
        behavior: true
    }

    component Group: Item {
        id: group

        property string title
        property string caption
        default property alias content: body.data

        width: parent !== null ? parent.width : 0
        implicitHeight: heading.height + body.implicitHeight + Theme.space.md + 2 * Theme.space.lg

        Rectangle {
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }

        Item {
            id: heading

            x: Theme.space.lg
            y: Theme.space.lg
            width: parent.width - 2 * Theme.space.lg
            height: titleText.implicitHeight

            Text {
                id: titleText

                width: Math.min(implicitWidth, parent.width)
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: group.title
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Theme.type.label.weight
            }

            Text {
                anchors.right: parent.right
                anchors.baseline: titleText.baseline
                width: Math.max(0, parent.width - titleText.width - Theme.space.sm)
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: group.caption
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }
        }

        Item {
            id: body

            x: Theme.space.lg
            y: heading.y + heading.height + Theme.space.md
            width: parent.width - 2 * Theme.space.lg
            implicitHeight: childrenRect.height
        }
    }

    MouseArea {
        anchors.fill: parent
        onPressed: mouse => {
            mouse.accepted = false
            panel.finished()
        }
    }

    Item {
        id: header

        objectName: "propertyHeader"
        width: parent.width
        height: Theme.space.lg + Theme.size.tileLarge + Theme.space.lg
                + (openButton.visible ? openButton.height + Theme.space.md : 0)

        Tile {
            id: tile

            objectName: "propertyTile"
            x: Theme.space.lg
            y: Theme.space.lg
            large: true
            iconName: panel.iconName
        }

        TextField {
            id: nameField

            property string editingId
            readonly property string shownName: panel.row !== null ? panel.row.name : ""

            function commit() {
                const id = editingId
                editingId = ""
                if (id !== "" && panel.editor !== null && text.trim() !== "" && text !== shownName)
                    panel.editor.rename(id, text)
                text = shownName
            }

            objectName: "propertyName"
            x: tile.x + tile.width + Theme.space.md - Theme.space.sm
            y: tile.y - Theme.space.xxs
            width: more.x - x - Theme.space.sm
            height: Theme.size.controlSmall
            dense: true
            bare: true
            maximumLength: 64
            font.pixelSize: Theme.type.heading.size
            font.weight: Theme.type.heading.weight
            Accessible.name: qsTr("Name")

            Component.onCompleted: text = shownName
            onShownNameChanged: {
                if (!activeFocus)
                    text = shownName
            }
            onActiveFocusChanged: {
                if (activeFocus)
                    editingId = panel.keyId
                else
                    commit()
            }
            onAccepted: panel.finished()
            Keys.onEscapePressed: {
                editingId = ""
                text = shownName
                panel.finished()
            }
        }

        Text {
            objectName: "propertyType"
            x: tile.x + tile.width + Theme.space.md
            y: nameField.y + nameField.height
            width: more.x - x - Theme.space.sm
            textFormat: Text.PlainText
            text: panel.typeHint !== "" ? qsTr("%1 · %2").arg(panel.typeText).arg(panel.typeHint) : panel.typeText
            elide: Text.ElideRight
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Button {
            id: openButton

            objectName: "openComponent"
            x: Theme.space.lg
            y: tile.y + tile.height + Theme.space.md
            width: parent.width - 2 * Theme.space.lg
            visible: panel.placedRef !== ""
            small: true
            iconName: "code-xml"
            text: qsTr("Open component")
            toolTip: qsTr("See or change how it works. Every profile that uses it changes with it.")
            onClicked: panel.opened(panel.placedRef)
        }

        Button {
            id: more

            objectName: "propertyMore"
            x: parent.width - width - Theme.space.lg
            y: tile.y + (tile.height - height) / 2
            small: true
            tone: "ghost"
            iconName: "ellipsis"
            toolTip: qsTr("More")
            onClicked: moreMenu.open()

            Menu {
                id: moreMenu

                property bool renaming: false

                x: more.width - width
                y: more.height + Theme.space.xs
                onClosed: {
                    if (renaming) {
                        renaming = false
                        panel.rename()
                    } else {
                        panel.finished()
                    }
                }

                MenuItem {
                    objectName: "renameKey"
                    iconName: "pencil"
                    text: qsTr("Rename")
                    onTriggered: moreMenu.renaming = true
                }

                MenuItem {
                    objectName: "duplicateKey"
                    iconName: "copy"
                    text: qsTr("Duplicate")
                    shortcut: Commands.shortcutText("editor.duplicate")
                    onTriggered: panel.editor.duplicateSelection()
                }

                MenuItem {
                    objectName: "copyKey"
                    iconName: "copy"
                    text: qsTr("Copy")
                    shortcut: "Ctrl+C"
                    onTriggered: panel.editor.copySelection()
                }

                MenuItem {
                    objectName: "pasteKey"
                    iconName: "copy"
                    text: qsTr("Paste in the middle")
                    shortcut: "Ctrl+V"
                    enabled: moreMenu.visible && panel.editor !== null && panel.editor.canPaste()
                    onTriggered: panel.editor.pasteAt(0.5, 0.5)
                }

                MenuSeparator {}

                MenuItem {
                    objectName: "saveAsDefault"
                    iconName: "pin"
                    text: qsTr("Set as default for new keys")
                    enabled: panel.component !== ""
                    onTriggered: panel.editor.saveAsDefault(panel.keyId)
                }

                MenuItem {
                    objectName: "restoreDefault"
                    iconName: "rotate-ccw"
                    text: qsTr("Restore factory default")
                    enabled: moreMenu.visible && panel.component !== "" && panel.editor.hasSavedDefault(panel.keyId)
                    onTriggered: panel.editor.restoreFactoryDefault(panel.keyId)
                }

                MenuSeparator {}

                MenuItem {
                    objectName: "deleteKey"
                    iconName: "trash-2"
                    danger: true
                    text: qsTr("Delete")
                    shortcut: Commands.shortcutText("editor.delete")
                    onTriggered: panel.editor.removeSelection()
                }
            }
        }
    }

    Flickable {
        id: properties

        objectName: "propertyForm"
        anchors.fill: parent
        anchors.topMargin: header.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: groupColumn.implicitHeight

        MouseArea {
            width: properties.width
            height: Math.max(properties.height, groupColumn.implicitHeight)
            onPressed: mouse => {
                mouse.accepted = false
                panel.finished()
            }
        }

        Column {
            id: groupColumn

            width: properties.width

            Item {
                id: codeNameRow

                objectName: "codeName"
                width: groupColumn.width
                height: codeNameColumn.implicitHeight + 2 * Theme.space.lg
                visible: panel.keyId !== "" && panel.editor !== null

                Rectangle {
                    width: parent.width
                    height: Theme.size.stroke
                    color: Theme.colors.strokeSubtle
                }

                Column {
                    id: codeNameColumn

                    x: Theme.space.lg
                    y: Theme.space.lg
                    width: parent.width - 2 * Theme.space.lg
                    spacing: Theme.space.xs

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: qsTr("Code name")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        font.weight: Theme.type.label.weight
                    }

                    TextField {
                        id: codeNameField

                        objectName: "propertyCodeName"
                        width: parent.width
                        dense: true
                        maximumLength: 64
                        text: panel.keyId
                        placeholderText: qsTr("The logic calls it controls.%1").arg(panel.keyId)
                        Accessible.name: qsTr("Code name")
                        onEditingFinished: {
                            if (panel.editor === null || text === panel.keyId)
                                return
                            const problem = panel.editor.codeNameProblem(panel.keyId, text)
                            errorText = problem
                            if (problem === "")
                                panel.editor.renameCode(panel.keyId, text)
                        }
                        onTextEdited: errorText = ""

                        Connections {
                            target: panel

                            function onKeyIdChanged() {
                                codeNameField.text = panel.keyId
                                codeNameField.errorText = ""
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        text: qsTr("Shown in the logic as controls.%1. It follows the key until you change it here.").arg(panel.keyId)
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }
            }

            Repeater {
                id: rows

                model: propertyForm

                delegate: KeyFieldRow {
                    width: groupColumn.width
                    editor: panel.editor
                    keyId: panel.keyId
                }
            }

            Item {
                objectName: "onAtStart"
                width: groupColumn.width
                height: Math.max(startSwitch.implicitHeight, startHeading.implicitHeight) + 2 * Theme.space.lg

                Rectangle {
                    visible: rows.count > 0
                    width: parent.width
                    height: Theme.size.stroke
                    color: Theme.colors.strokeSubtle
                }

                Row {
                    id: startHeading

                    x: Theme.space.lg
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.xs

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("On at start")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        font.weight: Theme.type.label.weight
                    }

                    InfoButton {
                        anchors.verticalCenter: parent.verticalCenter
                        title: qsTr("On at start")
                        info: qsTr("When this is off, the component does nothing after the profile loads until logic turns it on.")
                    }
                }

                Switch {
                    id: startSwitch

                    objectName: "onAtStartSwitch"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.lg
                    anchors.verticalCenter: parent.verticalCenter
                    checked: panel.onAtStart
                    Accessible.name: qsTr("On at start")
                    onToggled: {
                        if (panel.editor !== null && panel.keyId !== "")
                            panel.editor.setValue(panel.keyId, "enabled", checked)
                        startSwitch.checked = Qt.binding(() => panel.onAtStart)
                    }
                }
            }

            Repeater {
                model: conditionForm

                delegate: KeyFieldRow {
                    width: groupColumn.width
                    editor: panel.editor
                    keyId: panel.keyId
                }
            }

            Group {
                id: wheelGroup

                readonly property bool shown: panel.row !== null && panel.row.type === "actionWheel"
                readonly property var cells: shown && panel.editor !== null && panel.editor.revision >= 0
                                             ? panel.editor.wheelCells(panel.keyId) : []
                readonly property var targets: shown && panel.editor !== null && panel.editor.revision >= 0
                                               ? panel.editor.wheelTargets(panel.keyId) : []
                readonly property var actions: ["none", "control", "tap", "logic"]

                objectName: "wheelCells"
                visible: shown
                title: qsTr("Cells")
                caption: qsTr("%1 cells").arg(cells.length)

                Column {
                    width: parent.width
                    spacing: Theme.space.sm

                    Slider {
                        objectName: "wheelCellCount"
                        width: parent.width
                        from: 2
                        to: 12
                        stepSize: 1
                        value: wheelGroup.cells.length
                        Accessible.name: qsTr("Cell count")
                        onMoved: {
                            if (panel.editor !== null)
                                panel.editor.setWheelCellCount(panel.keyId, Math.round(value))
                        }
                    }

                    Repeater {
                        model: wheelGroup.cells

                        delegate: Column {
                            id: cellRow

                            required property var modelData
                            required property int index

                            objectName: "wheelCell"
                            width: parent.width
                            spacing: Theme.space.xs

                            Row {
                                width: parent.width
                                spacing: Theme.space.xs

                                Text {
                                    width: Theme.space.xxl
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: (cellRow.index + 1).toString()
                                    color: Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.caption.size
                                }

                                TextField {
                                    objectName: "wheelCellName"
                                    width: (parent.width - Theme.space.xxl - 2 * Theme.space.xs) / 2
                                    dense: true
                                    showsMessage: false
                                    text: cellRow.modelData.name
                                    placeholderText: qsTr("Name")
                                    Accessible.name: qsTr("Name of cell %1").arg(cellRow.index + 1)
                                    onEditingFinished: {
                                        if (panel.editor !== null && text !== cellRow.modelData.name)
                                            panel.editor.setWheelCell(panel.keyId, cellRow.index, "name", text)
                                    }
                                }

                                Select {
                                    objectName: "wheelCellAction"
                                    width: (parent.width - Theme.space.xxl - 2 * Theme.space.xs) / 2
                                    model: [qsTr("Empty"), qsTr("Press a component"), qsTr("Tap a spot"), qsTr("Hand to logic")]
                                    currentIndex: wheelGroup.actions.indexOf(cellRow.modelData.action)
                                    Accessible.name: qsTr("What cell %1 does").arg(cellRow.index + 1)
                                    onActivated: index => {
                                        if (panel.editor !== null)
                                            panel.editor.setWheelCell(panel.keyId, cellRow.index, "action",
                                                                      wheelGroup.actions[index])
                                    }
                                }
                            }

                            Select {
                                objectName: "wheelCellControl"
                                visible: cellRow.modelData.action === "control"
                                x: Theme.space.xxl + Theme.space.xs
                                width: parent.width - x
                                model: wheelGroup.targets.map(target => target.name)
                                currentIndex: wheelGroup.targets.findIndex(target => target.id === cellRow.modelData.control)
                                displayText: currentIndex < 0 ? qsTr("Choose a component") : currentText
                                Accessible.name: qsTr("Component for cell %1").arg(cellRow.index + 1)
                                onActivated: index => {
                                    if (panel.editor !== null)
                                        panel.editor.setWheelCell(panel.keyId, cellRow.index, "control",
                                                                  wheelGroup.targets[index].id)
                                }
                            }

                            Text {
                                visible: cellRow.modelData.action === "tap"
                                x: Theme.space.xxl + Theme.space.xs
                                width: parent.width - x
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                                text: qsTr("Drag dot %1 on the picture").arg(cellRow.index + 1)
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }
                        }
                    }
                }
            }

            Group {
                id: behaviorGroup

                readonly property var params: panel.row !== null && panel.row.type === "behavior" && panel.editor !== null
                                              && panel.editor.revision >= 0 && ExtensionHost.nativeBehaviors.length >= 0
                                              ? panel.editor.behaviorParams(panel.keyId) : []

                objectName: "behaviorParams"
                visible: params.length > 0
                title: qsTr("Parameters")

                DeclaredParams {
                    width: parent.width
                    editor: panel.editor
                    params: behaviorGroup.params
                    onEdited: (key, json) => panel.editor.setModuleParam(panel.keyId, -1, key, json)
                }
            }

            Group {
                id: addOns

                readonly property bool watching: panel.editor !== null && panel.keyId !== ""
                readonly property var attached: watching && panel.editor.revision >= 0
                                                && ExtensionHost.nativeAttachments.length >= 0
                                                && ExtensionHost.addOnsRevision >= 0
                                                ? panel.editor.attachmentsOf(panel.keyId) : []
                readonly property var offers: watching && panel.editor.revision >= 0
                                              && ExtensionHost.nativeAttachments.length >= 0
                                              && ExtensionHost.addOnsRevision >= 0
                                              ? panel.editor.attachable(panel.keyId) : []
                readonly property var blank: ({ title: "", extension: "", module: "", enabled: false, available: false,
                                                fits: false, global: false, place: -1, params: [] })
                readonly property int own: attached.filter(item => item.global !== true).length
                readonly property int most: 8

                objectName: "addOns"
                title: qsTr("Add-ons")
                caption: attached.length > 1 ? qsTr("Applied from top to bottom") : ""

                Column {
                    width: parent.width
                    spacing: Theme.space.md

                    Text {
                        objectName: "addOnsEmpty"
                        width: parent.width
                        visible: addOns.attached.length === 0
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: addOns.offers.length > 0
                              ? qsTr("Add-ons from extensions change what this component gets from the keys or how it touches the screen.")
                              : qsTr("No turned-on extension offers add-ons for this kind of component. Turn one on on the Extensions page.")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }

                    Repeater {
                        model: addOns.attached.length

                        delegate: Column {
                            id: addOn

                            required property int index
                            readonly property var modelData: index < addOns.attached.length ? addOns.attached[index]
                                                                                             : addOns.blank
                            readonly property bool fromGlobal: modelData.global === true
                            readonly property int place: modelData.place ?? -1
                            readonly property string note: addOn.fromGlobal
                                                           ? qsTr("Global · change it in this extension's settings on the Extensions page")
                                                           : !modelData.available
                                                           ? qsTr("%1 is not installed or is off").arg(modelData.extension)
                                                           : !modelData.fits ? qsTr("Does not fit this kind of component")
                                                                             : modelData.extension

                            objectName: "addOn"
                            width: parent.width
                            spacing: Theme.space.sm

                            Item {
                                width: parent.width
                                height: Math.max(addOnSwitch.implicitHeight, addOnText.implicitHeight)

                                Column {
                                    id: addOnText

                                    anchors.left: parent.left
                                    anchors.right: addOnSwitch.left
                                    anchors.rightMargin: Theme.space.sm
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        objectName: "addOnTitle"
                                        width: parent.width
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                        text: addOn.modelData.title
                                        color: addOn.modelData.available ? Theme.colors.textPrimary : Theme.colors.textTertiary
                                        font.pixelSize: Theme.type.label.size
                                        font.weight: Theme.type.label.weight
                                    }

                                    Text {
                                        objectName: "addOnNote"
                                        width: parent.width
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                        text: addOn.note
                                        color: addOn.modelData.available && addOn.modelData.fits ? Theme.colors.textTertiary
                                                                                                 : Theme.colors.warningText
                                        font.pixelSize: Theme.type.caption.size
                                    }
                                }

                                Switch {
                                    id: addOnSwitch

                                    objectName: "addOnSwitch"
                                    anchors.right: addOnMore.visible ? addOnMore.left : parent.right
                                    anchors.rightMargin: addOnMore.visible ? Theme.space.xs : 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    checked: addOn.modelData.enabled
                                    enabled: addOn.modelData.available
                                    Accessible.name: qsTr("Use %1").arg(addOn.modelData.title)
                                    onToggled: {
                                        if (panel.editor !== null) {
                                            if (addOn.fromGlobal)
                                                panel.editor.skipGlobal(panel.keyId, addOn.modelData.module, !checked)
                                            else
                                                panel.editor.enableAttachment(panel.keyId, addOn.place, checked)
                                        }
                                        addOnSwitch.checked = Qt.binding(() => addOn.modelData.enabled)
                                    }
                                }

                                Button {
                                    id: addOnMore

                                    objectName: "addOnMore"
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: !addOn.fromGlobal
                                    small: true
                                    tone: "ghost"
                                    iconName: "ellipsis"
                                    toolTip: qsTr("More")
                                    onClicked: addOnMenu.open()

                                    Menu {
                                        id: addOnMenu

                                        x: addOnMore.width - width
                                        y: addOnMore.height + Theme.space.xs

                                        MenuItem {
                                            objectName: "addOnUp"
                                            text: qsTr("Move up")
                                            enabled: addOn.place > 0
                                            onTriggered: panel.editor.moveAttachment(panel.keyId, addOn.place, -1)
                                        }

                                        MenuItem {
                                            objectName: "addOnDown"
                                            text: qsTr("Move down")
                                            enabled: addOn.place >= 0 && addOn.place < addOns.own - 1
                                            onTriggered: panel.editor.moveAttachment(panel.keyId, addOn.place, 1)
                                        }

                                        MenuSeparator {}

                                        MenuItem {
                                            objectName: "addOnRemove"
                                            iconName: "trash-2"
                                            danger: true
                                            text: qsTr("Remove")
                                            onTriggered: panel.editor.removeAttachment(panel.keyId, addOn.place)
                                        }
                                    }
                                }
                            }

                            DeclaredParams {
                                width: parent.width
                                visible: !addOn.fromGlobal && addOn.modelData.available && addOn.modelData.enabled
                                         && params.length > 0
                                editor: panel.editor
                                params: addOn.modelData.params ?? []
                                onEdited: (key, json) => panel.editor.setModuleParam(panel.keyId, addOn.place, key, json)
                            }
                        }
                    }

                    Button {
                        id: addAddOn

                        objectName: "addAddOn"
                        width: parent.width
                        iconName: "plus"
                        text: qsTr("Add")
                        enabled: addOns.offers.length > 0 && addOns.own < addOns.most
                        toolTip: addOns.own >= addOns.most ? qsTr("A component takes at most %1 add-ons").arg(addOns.most)
                                                           : ""
                        onClicked: offerMenu.open()

                        Menu {
                            id: offerMenu

                            objectName: "addOnOffers"
                            y: addAddOn.height + Theme.space.xs

                            Instantiator {
                                model: addOns.offers

                                delegate: MenuItem {
                                    required property var modelData

                                    objectName: "addOnOffer"
                                    text: modelData.title
                                    shortcut: modelData.extension
                                    onTriggered: panel.editor.addAttachment(panel.keyId, modelData.module)
                                }

                                onObjectAdded: (index, object) => offerMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => offerMenu.removeItem(object)
                            }
                        }
                    }
                }
            }

            Group {
                objectName: "logicMembers"
                title: qsTr("Usable in logic")
                caption: qsTr("Red: events · Blue: functions")

                Flow {
                    id: memberFlow

                    width: parent.width
                    spacing: Theme.space.xs

                    Repeater {
                        model: panel.members

                        delegate: Rectangle {
                            id: chip

                            required property var modelData

                            objectName: modelData.event ? "logicEvent" : "logicFunction"
                            width: chipText.implicitWidth + 2 * Theme.space.sm
                            height: Theme.size.pill
                            radius: height / 2
                            color: modelData.event ? Theme.colors.dangerSubtle : Theme.colors.infoSubtle

                            Text {
                                id: chipText

                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: chip.modelData.text
                                color: chip.modelData.event ? Theme.colors.dangerText : Theme.colors.infoText
                                font.pixelSize: Theme.type.caption.size
                            }

                            HoverHandler {
                                id: chipHover
                            }

                            Tooltip {
                                text: "controls." + panel.keyId + "." + chip.modelData.name
                                visible: chipHover.hovered
                            }
                        }
                    }
                }

                Button {
                    id: useInLogic

                    objectName: "useInLogic"
                    anchors.top: memberFlow.bottom
                    anchors.topMargin: Theme.space.md
                    width: parent.width
                    iconName: "workflow"
                    text: qsTr("Use it in logic")
                    enabled: panel.editor !== null && panel.editor.logicBlocked === "" && panel.keyId !== ""
                    toolTip: panel.editor !== null ? panel.editor.logicBlocked : ""
                    onClicked: panel.referenced(panel.keyId)
                }

                Row {
                    id: logicKeyNote

                    objectName: "logicKeyNote"
                    anchors.top: useInLogic.bottom
                    anchors.topMargin: Theme.space.sm
                    width: parent.width
                    visible: panel.row !== null && panel.row.logicKey === true
                    height: visible ? implicitHeight : 0
                    spacing: Theme.space.xs

                    Icon {
                        name: "workflow"
                        size: Theme.size.iconSmall
                        color: Theme.colors.accentText
                    }

                    Text {
                        width: logicKeyNote.width - Theme.size.iconSmall - logicKeyNote.spacing
                        textFormat: Text.PlainText
                        text: qsTr("Pressing %1 only runs the logic: the logic listens to this key, so the component no "
                                   + "longer presses the screen by itself.").arg(panel.row !== null ? panel.row.keys : "")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
