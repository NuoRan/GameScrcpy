pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

Item {
    id: sidebar

    property var sections: []
    property var model: null
    property string title: qsTr("My blueprint")
    property string iconName: "workflow"
    property string tone: ""
    property var collapsed: ({})
    property var expanded: ({})
    readonly property string dragKey: "application/x-gamescrcpy-blueprint"

    signal itemActivated(var item)
    signal addClicked(string section)

    function toggleSection(key: string) {
        const next = Object.assign({}, collapsed)
        next[key] = !(collapsed[key] === true)
        collapsed = next
    }

    function toggleItem(key: string) {
        const next = Object.assign({}, expanded)
        next[key] = !(expanded[key] === true)
        expanded = next
    }

    function typeNameList(): var {
        const list = []
        for (const section of sections ?? []) {
            if (section.key !== "structs" && section.key !== "enums" && section.key !== "classes")
                continue
            for (const item of section.items ?? [])
                list.push(String(item.title ?? ""))
        }
        return list
    }

    function activate(payload: var) {
        const key = String(payload.item ?? "")
        if (key.startsWith("def:")) {
            typeEditor.showType(key.substring(4))
            return
        }
        sidebar.itemActivated(payload)
    }

    function addTo(key: string) {
        const kind = key === "structs" ? "struct" : key === "enums" ? "enum" : key === "classes" ? "class" : ""
        if (kind === "" || model === null) {
            sidebar.addClicked(key)
            return
        }
        const made = model.addType(kind, "")
        const problem = String(made.problem ?? "")
        if (problem !== "") {
            sidebar.showTip(problem, Qt.point(sidebar.width / 2, Theme.size.bpSection * 2))
            return
        }
        const madeKey = String(made.key ?? "")
        if (madeKey.startsWith("def:"))
            typeEditor.showType(madeKey.substring(4))
    }

    function startDrag(payload: var, scene: point) {
        ghost.payload = { section: payload.section, item: payload.item, member: payload.member,
                          mode: payload.mode ?? "" }
        ghost.label = payload.title ?? ""
        ghost.iconName = payload.icon ?? ""
        moveDrag(scene)
        ghost.Drag.active = true
    }

    function moveDrag(scene: point) {
        const layer = ghost.parent
        const at = layer ? layer.mapFromItem(null, scene.x, scene.y) : scene
        ghost.x = at.x - ghost.Drag.hotSpot.x
        ghost.y = at.y - ghost.Drag.hotSpot.y
    }

    function endDrag(drop: bool) {
        if (!ghost.Drag.active)
            return
        if (drop)
            ghost.Drag.drop()
        else
            ghost.Drag.cancel()
        ghost.Drag.active = false
    }

    function showTip(text: string, at: point) {
        tip.text = text
        tip.x = at.x - tip.width / 2
        tip.y = at.y - tip.height - Theme.space.xs
        tip.visible = true
    }

    function hideTip() {
        tip.visible = false
    }

    objectName: "bpSidebar"
    implicitWidth: Theme.size.workbenchLeft
    implicitHeight: Theme.size.workbenchSidebar

    Accessible.role: Accessible.Tree
    Accessible.name: title

    component Grip: MouseArea {
        id: grip

        required property var payload
        property point origin
        property bool dragging: false
        property int modifiers: Qt.NoModifier

        signal dragStarted(var payload, point scene)
        signal dragMoved(point scene)
        signal dragEnded(bool drop)
        signal activated(var payload)

        hoverEnabled: true
        preventStealing: true
        cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: mouse => {
            grip.origin = Qt.point(mouse.x, mouse.y)
            grip.modifiers = mouse.modifiers
            grip.dragging = false
        }
        onPositionChanged: mouse => {
            if (!grip.pressed)
                return
            const scene = grip.mapToItem(null, mouse.x, mouse.y)
            if (!grip.dragging) {
                if (Math.abs(mouse.x - grip.origin.x) + Math.abs(mouse.y - grip.origin.y)
                        < Application.styleHints.startDragDistance)
                    return
                grip.dragging = true
                const kept = Object.assign({}, grip.payload)
                const mods = mouse.modifiers !== Qt.NoModifier ? mouse.modifiers : grip.modifiers
                kept.mode = (mods & Qt.ControlModifier) !== 0 ? "get" : (mods & Qt.AltModifier) !== 0 ? "set" : ""
                grip.dragStarted(kept, scene)
                return
            }
            grip.dragMoved(scene)
        }
        onReleased: {
            if (grip.dragging) {
                grip.dragging = false
                grip.dragEnded(true)
            } else {
                grip.activated({ section: grip.payload.section, item: grip.payload.item,
                                 member: grip.payload.member })
            }
        }
        onCanceled: {
            if (grip.dragging) {
                grip.dragging = false
                grip.dragEnded(false)
            }
        }
    }

    Row {
        id: header

        x: Theme.space.md
        y: Theme.space.sm + Theme.space.xxs
        height: Theme.size.bpItem
        spacing: Theme.space.sm

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: sidebar.iconName
            size: Theme.size.iconSmall
            color: sidebar.tone !== "" ? BlueprintLook.ink(sidebar.tone) : Theme.colors.accentText
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, sidebar.width - 2 * Theme.space.md - Theme.size.iconSmall - parent.spacing)
            textFormat: Text.PlainText
            text: sidebar.title
            elide: Text.ElideRight
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: Font.DemiBold
        }
    }

    Flickable {
        id: scroller

        y: header.y + header.height + Theme.space.xs
        width: parent.width
        height: parent.height - y
        contentHeight: column.implicitHeight + Theme.space.md
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column

            x: Theme.space.sm
            width: scroller.width - 2 * x

            Repeater {
                model: sidebar.sections ?? []

                delegate: Column {
                    id: section

                    required property var modelData
                    readonly property string key: modelData.key ?? ""
                    readonly property bool closed: sidebar.collapsed[key] === true

                    width: column.width

                    Item {
                        objectName: "bpSection_" + section.key
                        width: parent.width
                        height: Theme.size.bpSection

                        Accessible.role: Accessible.Button
                        Accessible.name: section.modelData.title ?? ""

                        MouseArea {
                            id: sectionArea

                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sidebar.toggleSection(section.key)
                        }

                        Row {
                            x: Theme.space.xs
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Theme.space.xs
                            spacing: Theme.space.xs

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron-down"
                                size: Theme.size.iconSmall - Theme.space.xs - Theme.space.xxs
                                rotation: section.closed ? -90 : 0
                                color: sectionArea.containsMouse ? Theme.colors.textSecondary : Theme.colors.textTertiary
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: section.modelData.title ?? ""
                                color: sectionArea.containsMouse ? Theme.colors.textSecondary : Theme.colors.textTertiary
                                font.pixelSize: Theme.type.micro.size
                                font.weight: Theme.type.micro.weight
                            }
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space.sm
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Theme.space.xs
                            visible: section.modelData.addable !== true && text !== ""
                            textFormat: Text.PlainText
                            text: section.modelData.count ?? ""
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.micro.size
                        }

                        T.AbstractButton {
                            id: add

                            objectName: "bpAdd_" + section.key
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space.xs
                            anchors.bottom: parent.bottom
                            visible: section.modelData.addable === true
                            width: Theme.size.bpChip
                            height: Theme.size.bpChip
                            hoverEnabled: true
                            focusPolicy: Qt.TabFocus
                            Accessible.name: qsTr("Add to “%1”").arg(section.modelData.title ?? "")
                            onClicked: sidebar.addTo(section.key)
                            onHoveredChanged: {
                                if (add.hovered)
                                    sidebar.showTip(add.Accessible.name, add.mapToItem(sidebar, add.width / 2, 0))
                                else
                                    sidebar.hideTip()
                            }

                            contentItem: Item {
                                Icon {
                                    anchors.centerIn: parent
                                    name: "plus"
                                    size: Theme.size.iconSmall - Theme.space.xxs
                                    color: add.hovered ? Theme.colors.textPrimary : Theme.colors.textTertiary
                                }
                            }

                            background: StateLayer {
                                radius: Theme.radius.xs
                                hovered: add.hovered
                                pressed: add.down
                            }
                        }
                    }

                    Repeater {
                        model: section.closed ? [] : section.modelData.items ?? []

                        delegate: Column {
                            id: entry

                            required property var modelData
                            required property int index
                            readonly property string key: modelData.key ?? ""
                            readonly property var members: modelData.members ?? []
                            readonly property bool open: sidebar.expanded[section.key + "/" + key] === true
                            readonly property string category: modelData.category ?? ""
                            readonly property string description: modelData.description ?? ""
                            readonly property bool categoryHead: category !== "" && (index === 0
                                || (((section.modelData.items[index - 1] ?? ({})).category ?? "") !== category))

                            width: section.width

                            Text {
                                objectName: "bpCategory_" + section.key + "_" + entry.key
                                visible: entry.categoryHead
                                width: parent.width
                                topPadding: Theme.space.sm
                                bottomPadding: Theme.space.xxs
                                leftPadding: Theme.space.xs + Theme.space.xxs
                                textFormat: Text.PlainText
                                text: entry.category
                                elide: Text.ElideRight
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.micro.size
                                font.weight: Theme.type.micro.weight
                            }

                            Item {
                                objectName: "bpItem_" + section.key + "_" + entry.key
                                width: parent.width
                                height: Theme.size.bpItem

                                Accessible.role: Accessible.TreeItem
                                Accessible.name: entry.modelData.title ?? ""

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.radius.sm
                                    color: itemGrip.containsMouse ? Theme.colors.stateHover : "transparent"
                                }

                                Grip {
                                    id: itemGrip

                                    anchors.fill: parent
                                    payload: ({ section: section.key, item: entry.key, member: "",
                                                title: entry.modelData.title ?? "", icon: entry.modelData.icon ?? "" })
                                    onDragStarted: (payload, scene) => sidebar.startDrag(payload, scene)
                                    onDragMoved: scene => sidebar.moveDrag(scene)
                                    onDragEnded: drop => sidebar.endDrag(drop)
                                    onActivated: payload => sidebar.activate(payload)
                                    onContainsMouseChanged: {
                                        if (itemGrip.containsMouse && entry.description !== "")
                                            sidebar.showTip(entry.description, itemGrip.mapToItem(sidebar, itemGrip.width / 2, 0))
                                        else
                                            sidebar.hideTip()
                                    }
                                }

                                Row {
                                    x: Theme.space.xs + Theme.space.xxs
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - x - Theme.space.sm
                                    spacing: Theme.space.xs + Theme.space.xxs

                                    Item {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.size.iconSmall - Theme.space.xs
                                        height: width

                                        Icon {
                                            anchors.centerIn: parent
                                            visible: entry.members.length > 0
                                            name: "chevron-right"
                                            size: parent.width
                                            rotation: entry.open ? 90 : 0
                                            color: Theme.colors.textTertiary
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -Theme.space.xs
                                            enabled: entry.members.length > 0
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: sidebar.toggleItem(section.key + "/" + entry.key)
                                        }
                                    }

                                    Item {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.size.iconSmall
                                        height: width
                                        visible: (entry.modelData.icon ?? "") !== "" || (entry.modelData.dot ?? "") !== ""

                                        Icon {
                                            anchors.centerIn: parent
                                            visible: (entry.modelData.dot ?? "") === ""
                                            name: entry.modelData.icon ?? ""
                                            size: Theme.size.iconSmall - Theme.space.xxs
                                            color: Theme.colors.textTertiary
                                        }

                                        Rectangle {
                                            anchors.centerIn: parent
                                            readonly property string kind: entry.modelData.dot ?? ""
                                            visible: kind !== ""
                                            width: Theme.size.bpPin
                                            height: width
                                            radius: kind === "event" ? Theme.radius.xs / 2 : width / 2
                                            color: kind === "global" ? Theme.colors.bpCustomInk
                                                 : kind === "event" ? Theme.colors.bpEventInk
                                                 : kind === "variable" ? Theme.colors.accent
                                                 : kind === "local" ? Theme.colors.textTertiary
                                                 : Theme.colors.success
                                        }
                                    }

                                    Text {
                                        id: itemTitle

                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Math.min(implicitWidth, parent.width - x - (itemBadge.visible
                                                        ? itemBadge.implicitWidth + parent.spacing : 0))
                                        textFormat: Text.PlainText
                                        text: entry.modelData.title ?? ""
                                        elide: Text.ElideRight
                                        color: itemGrip.containsMouse ? Theme.colors.textPrimary : Theme.colors.textSecondary
                                        font.pixelSize: Theme.type.caption.size
                                    }
                                }

                                Text {
                                    id: itemBadge

                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.space.sm
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: text !== ""
                                    textFormat: Text.PlainText
                                    text: entry.modelData.badge ?? ""
                                    color: Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.micro.size
                                }
                            }

                            Repeater {
                                model: entry.open ? entry.members : []

                                delegate: Item {
                                    id: member

                                    required property var modelData
                                    readonly property string kind: modelData.kind ?? ""

                                    objectName: "bpMember_" + section.key + "_" + entry.key + "_" + (modelData.key ?? "")
                                    width: entry.width
                                    height: Theme.size.bpMember

                                    Accessible.role: Accessible.TreeItem
                                    Accessible.name: modelData.title ?? ""

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: Theme.radius.sm
                                        color: memberGrip.containsMouse ? Theme.colors.stateHover : "transparent"
                                    }

                                    Grip {
                                        id: memberGrip

                                        anchors.fill: parent
                                        payload: ({ section: section.key, item: entry.key, member: member.modelData.key ?? "",
                                                    title: member.modelData.title ?? "", icon: "" })
                                        onDragStarted: (payload, scene) => sidebar.startDrag(payload, scene)
                                        onDragMoved: scene => sidebar.moveDrag(scene)
                                        onDragEnded: drop => sidebar.endDrag(drop)
                                        onActivated: payload => sidebar.activate(payload)
                                    }

                                    Rectangle {
                                        x: Theme.space.x4l - Theme.space.sm
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.size.bpMemberMark
                                        height: width
                                        radius: member.kind === "state" || member.kind === "function" ? width / 2
                                                                                                        : Theme.radius.xs / 2
                                        color: {
                                            switch (member.kind) {
                                            case "state":
                                                return Theme.colors.success
                                            case "event":
                                                return Theme.colors.bpEventInk
                                            case "function":
                                                return Theme.colors.bpActionInk
                                            default:
                                                return Theme.colors.textSecondary
                                            }
                                        }
                                    }

                                    Text {
                                        x: Theme.space.x4l + Theme.space.xs
                                        width: parent.width - x - memberKind.implicitWidth - 2 * Theme.space.sm
                                        anchors.verticalCenter: parent.verticalCenter
                                        textFormat: Text.PlainText
                                        text: member.modelData.title ?? ""
                                        elide: Text.ElideRight
                                        color: memberGrip.containsMouse ? Theme.colors.textPrimary : Theme.colors.textSecondary
                                        font.pixelSize: Theme.type.caption.size
                                    }

                                    Text {
                                        id: memberKind

                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.space.sm
                                        anchors.verticalCenter: parent.verticalCenter
                                        textFormat: Text.PlainText
                                        text: (member.modelData.badge ?? "") !== "" ? member.modelData.badge
                                                                                    : BlueprintLook.memberKindName(member.kind)
                                        color: Theme.colors.textTertiary
                                        font.pixelSize: Theme.type.micro.size
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Tooltip {
        id: tip

        parent: sidebar
        visible: false
    }

    TypeEditor {
        id: typeEditor

        model: sidebar.model
        typeNames: sidebar.typeNameList()
    }

    Rectangle {
        id: ghost

        property var payload: null
        property string label: ""
        property string iconName: ""

        parent: T.Overlay.overlay ?? sidebar
        visible: Drag.active
        z: 1
        width: ghostRow.implicitWidth + 2 * Theme.space.md
        height: Theme.size.chip
        radius: Theme.radius.md
        color: Theme.colors.bgElevated
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accent

        Drag.keys: [sidebar.dragKey]
        Drag.hotSpot.x: Theme.space.md
        Drag.hotSpot.y: height / 2

        Row {
            id: ghostRow

            anchors.centerIn: parent
            spacing: Theme.space.sm

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: ghost.iconName !== ""
                name: ghost.iconName
                size: Theme.size.iconSmall
                color: Theme.colors.textSecondary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: ghost.label
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
            }
        }
    }
}
