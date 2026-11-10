pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

T.Popup {
    id: picker

    property var catalog: null
    property string fromNode: ""
    property string fromPin: ""
    property string fromLabel: ""
    property real canvasX: 0
    property real canvasY: 0
    property var items: []
    property string openFolder: ""
    readonly property bool folding: search.text === "" && items.some(item => (item.control ?? "") !== "")
                                    && new Set(items.map(item => item.control ?? "").filter(control => control !== "")).size > 1
    property int current: -1
    readonly property var rows: picker.buildRows()

    signal chosen(string id, real x, real y, string fromNode, string fromPin)

    onOpenFolderChanged: picker.resetCurrent()

    function show(x: real, y: real, atX: real, atY: real, node: string, pin: string, label: string) {
        picker.x = x
        picker.y = y
        canvasX = atX
        canvasY = atY
        fromNode = node
        fromPin = pin
        fromLabel = label
        search.text = ""
        openFolder = ""
        refresh()
        open()
        search.forceActiveFocus(Qt.PopupFocusReason)
    }

    function buildRows(): var {
        const made = []
        if (openFolder !== "" && search.text === "") {
            let title = ""
            for (const item of items) {
                if ((item.control ?? "") === openFolder) {
                    title = (item.controlTitle ?? "") !== "" ? item.controlTitle : (item.group ?? "")
                    break
                }
            }
            made.push({ kind: "back", control: openFolder, title: title })
            for (let index = 0; index < items.length; ++index) {
                if ((items[index].control ?? "") === openFolder)
                    made.push({ kind: "item", item: items[index], index: index })
            }
            return made
        }
        if (folding) {
            const counts = ({})
            for (const item of items) {
                const control = item.control ?? ""
                if (control !== "")
                    counts[control] = (counts[control] ?? 0) + 1
            }
            const placed = ({})
            let grouped = false
            let group = null
            for (let index = 0; index < items.length; ++index) {
                const item = items[index]
                const control = item.control ?? ""
                if (control !== "") {
                    if (!grouped) {
                        grouped = true
                        made.push({ kind: "heading", title: qsTr("Components") })
                    }
                    if (placed[control] !== true) {
                        placed[control] = true
                        made.push({ kind: "folder", control: control,
                                    title: (item.controlTitle ?? "") !== "" ? item.controlTitle : (item.group ?? ""),
                                    count: counts[control], icon: item.icon ?? "" })
                    }
                    continue
                }
                const name = item.group ?? ""
                if (name !== group) {
                    group = name
                    if (name !== "")
                        made.push({ kind: "heading", title: name })
                }
                made.push({ kind: "item", item: item, index: index })
            }
            return made
        }
        let group = null
        for (let index = 0; index < items.length; ++index) {
            const item = items[index]
            const name = item.group ?? ""
            if (name !== group) {
                group = name
                if (name !== "")
                    made.push({ kind: "heading", title: name })
            }
            made.push({ kind: "item", item: item, index: index })
        }
        return made
    }

    function resetCurrent() {
        const all = picker.buildRows()
        for (let index = 0; index < all.length; ++index) {
            if (all[index].kind !== "heading") {
                current = index
                return
            }
        }
        current = -1
    }

    function isFolder(index: int): bool {
        return index >= 0 && index < picker.rows.length && picker.rows[index].kind === "folder"
    }

    function back(): bool {
        if (openFolder === "" || search.text !== "")
            return false
        const control = openFolder
        openFolder = ""
        const all = picker.buildRows()
        for (let index = 0; index < all.length; ++index) {
            if (all[index].kind === "folder" && all[index].control === control) {
                current = index
                list.positionViewAtIndex(current, ListView.Contain)
                break
            }
        }
        return true
    }

    function refresh() {
        const ask = picker.catalog
        const found = typeof ask === "function" ? ask(fromNode, fromPin, search.text) : []
        const entries = []
        const count = found !== null && found !== undefined && typeof found.length === "number" ? found.length : 0
        for (let index = 0; index < count; ++index)
            entries.push(found[index])
        items = entries
        picker.resetCurrent()
        list.positionViewAtBeginning()
    }

    function step(direction: int) {
        const all = picker.buildRows()
        const selectable = []
        for (let index = 0; index < all.length; ++index) {
            if (all[index].kind !== "heading")
                selectable.push(index)
        }
        if (selectable.length === 0) {
            current = -1
            return
        }
        let at = selectable.indexOf(current)
        if (at < 0)
            at = direction > 0 ? -1 : 0
        current = selectable[(at + direction + selectable.length) % selectable.length]
        list.positionViewAtIndex(current, ListView.Contain)
    }

    function choose(index: int) {
        if (index < 0 || index >= picker.rows.length)
            return
        const entry = picker.rows[index]
        if (entry.kind === "folder") {
            openFolder = entry.control
            return
        }
        if (entry.kind === "back") {
            picker.back()
            return
        }
        if (entry.kind !== "item")
            return
        const id = entry.item.id ?? ""
        const node = fromNode
        const pin = fromPin
        close()
        picker.chosen(id, canvasX, canvasY, node, pin)
    }

    function hover(row: Item, title: string, detail: string) {
        tip.row = row
        tip.text = title
        tip.detail = detail
    }

    onClosed: picker.hover(null, "", "")

    objectName: "bpPicker"
    width: Theme.size.bpPicker
    implicitHeight: column.implicitHeight + topPadding + bottomPadding
    padding: Theme.space.xs + Theme.space.xxs
    margins: Theme.space.sm
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

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

    contentItem: Column {
        id: column

        spacing: Theme.space.xs

        Text {
            objectName: "bpPickerHeader"
            width: column.width
            visible: picker.fromPin !== ""
            leftPadding: Theme.space.xs + Theme.space.xxs
            textFormat: Text.PlainText
            text: qsTr("From “%1” you can connect:").arg(picker.fromLabel)
            elide: Text.ElideRight
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        SearchField {
            id: search

            objectName: "bpPickerSearch"
            width: column.width
            placeholderText: qsTr("Search blocks")
            onTextChanged: {
                picker.openFolder = ""
                Qt.callLater(picker.refresh)
            }
            Keys.onUpPressed: picker.step(-1)
            Keys.onDownPressed: picker.step(1)
            Keys.onReturnPressed: picker.choose(picker.current)
            Keys.onEnterPressed: picker.choose(picker.current)
            Keys.onRightPressed: event => {
                if (picker.isFolder(picker.current))
                    picker.choose(picker.current)
                else
                    event.accepted = false
            }
            Keys.onLeftPressed: event => {
                if (!picker.back())
                    event.accepted = false
            }
            Keys.onPressed: event => {
                if (event.key !== Qt.Key_Backspace)
                    return
                if (picker.back())
                    event.accepted = true
            }
        }

        ListView {
            id: list

            objectName: "bpPickerList"
            width: column.width
            height: Math.min(Theme.size.bpPickerList, contentHeight)
            visible: picker.items.length > 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: picker.rows

            delegate: Item {
                id: row

                required property int index
                required property var modelData
                readonly property string kind: modelData.kind ?? "item"
                readonly property bool heading: kind === "heading"
                readonly property var item: modelData.item ?? ({})
                readonly property bool current: !heading && row.index === picker.current
                readonly property string title: kind === "item" ? (row.item.title ?? "") : (row.modelData.title ?? "")
                readonly property string description: kind === "item" ? (row.item.description ?? "") : ""

                width: list.width
                height: heading ? Theme.size.bpSection : Theme.size.menuItem
                objectName: kind === "item" ? "bpPickerItem_" + (row.item.id ?? "")
                            : kind === "folder" ? "bpPickerFolder_" + row.modelData.control
                            : kind === "back" ? "bpPickerBack" : ""

                Accessible.role: heading ? Accessible.Heading : Accessible.ListItem
                Accessible.name: row.title

                Text {
                    visible: row.heading
                    x: Theme.space.sm
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.space.xxs
                    textFormat: Text.PlainText
                    text: row.modelData.title ?? ""
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.micro.size
                    font.weight: Theme.type.micro.weight
                }

                Rectangle {
                    visible: !row.heading
                    anchors.fill: parent
                    radius: Theme.radius.sm
                    color: row.current ? (picker.activeFocus ? Theme.colors.accentSubtle : Theme.colors.statePress)
                                       : area.containsMouse ? Theme.colors.stateHover : "transparent"
                }

                Row {
                    id: itemRow

                    visible: row.kind === "item"
                    x: Theme.space.sm
                    y: Math.round((row.height - height) / 2)
                    width: parent.width - 2 * x
                    spacing: Theme.space.sm

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.size.bpMemberMark
                        height: width
                        radius: Theme.radius.xs / 2
                        color: BlueprintLook.ink(row.item.tone ?? "")
                    }

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: (row.item.icon ?? "") !== ""
                        name: row.item.icon ?? ""
                        size: Theme.size.iconSmall
                        color: BlueprintLook.ink(row.item.tone ?? "")
                    }

                    Text {
                        id: titleText

                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width - x - (subtitle.visible ? Theme.space.lg : 0))
                        textFormat: Text.PlainText
                        text: row.item.title ?? ""
                        elide: Text.ElideRight
                        color: row.current ? Theme.colors.textPrimary : Theme.colors.textSecondary
                        font.pixelSize: Theme.type.label.size
                    }

                    Text {
                        id: subtitle

                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, parent.width - titleText.x - titleText.width - parent.spacing)
                        visible: text !== ""
                        horizontalAlignment: Text.AlignRight
                        textFormat: Text.PlainText
                        text: row.item.subtitle ?? ""
                        elide: Text.ElideRight
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }

                Row {
                    id: folderRow

                    visible: row.kind === "folder"
                    x: Theme.space.sm
                    width: parent.width - 2 * x
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.sm

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.size.bpMemberMark
                        height: width
                        radius: Theme.radius.xs / 2
                        color: BlueprintLook.ink("custom")
                    }

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: (row.modelData.icon ?? "") !== ""
                        name: row.modelData.icon ?? ""
                        size: Theme.size.iconSmall
                        color: BlueprintLook.ink("custom")
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, folderRow.width - x - folderTail.width - Theme.space.sm)
                        textFormat: Text.PlainText
                        text: row.modelData.title ?? ""
                        elide: Text.ElideRight
                        color: row.current ? Theme.colors.textPrimary : Theme.colors.textSecondary
                        font.pixelSize: Theme.type.label.size
                    }
                }

                Row {
                    id: folderTail

                    visible: row.kind === "folder"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.xs

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: String(row.modelData.count ?? 0)
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-right"
                        size: Theme.size.iconSmall
                        color: Theme.colors.textTertiary
                    }
                }

                Row {
                    visible: row.kind === "back"
                    x: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.xs

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-right"
                        rotation: 180
                        size: Theme.size.iconSmall
                        color: Theme.colors.textSecondary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: row.modelData.title ?? ""
                        elide: Text.ElideRight
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.label.size
                    }
                }

                MouseArea {
                    id: area

                    anchors.fill: parent
                    enabled: !row.heading
                    hoverEnabled: true
                    onClicked: picker.choose(row.index)
                    onContainsMouseChanged: {
                        if (area.containsMouse)
                            picker.hover(row, row.title, row.description)
                        else if (tip.row === row)
                            picker.hover(null, "", "")
                    }
                }
            }
        }

        Text {
            objectName: "bpPickerEmpty"
            width: column.width
            visible: picker.items.length === 0
            topPadding: Theme.space.sm
            bottomPadding: Theme.space.sm
            leftPadding: Theme.space.sm
            textFormat: Text.PlainText
            text: search.text !== "" ? qsTr("No blocks match “%1”").arg(search.text)
                                     : picker.fromPin !== "" ? qsTr("Nothing connects here")
                                                             : qsTr("No blocks to add here")
            wrapMode: Text.Wrap
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Tooltip {
            id: tip

            property Item row: null

            objectName: "bpPickerTip"
            parent: tip.row
            x: tip.row !== null ? tip.row.width + Theme.space.sm : 0
            y: 0
            visible: tip.row !== null && tip.detail !== ""
        }
    }
}
