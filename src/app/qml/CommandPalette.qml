pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

T.Popup {
    id: commandPalette

    property Item backdrop: null
    property bool choosing: false
    property var options: []
    property string optionsGroup
    property string optionsTitle
    property string optionsTarget
    property point pointer
    property bool pointerKnown: false

    readonly property bool frosted: backdrop !== null && !Theme.reducedTransparency
    readonly property bool asksDevice: Commands.currentDevice === "" && DeviceManager.deviceCount > 1
    readonly property real room: Math.max(Theme.size.paletteRow + 2 * Theme.space.sm,
                                          Math.min(Theme.size.paletteListMax,
                                                   (parent ? parent.height : 0) - y - Theme.space.xxl
                                                   - bar.height - footer.height - 2 * Theme.size.stroke
                                                   - (deviceRow.visible ? deviceRow.height : 0)))
    readonly property rect behindRect: {
        if (!frosted || parent === null)
            return Qt.rect(0, 0, 0, 0)
        const origin = backdrop.mapFromItem(parent, x, y)
        return Qt.rect(origin.x, origin.y, width, height)
    }

    function escaped(text: string): string {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;")
    }

    function marked(title: string, ranges: var): string {
        let text = ""
        let at = 0
        for (const range of ranges) {
            const start = range[0]
            const end = start + range[1]
            text += escaped(title.slice(at, start)) + "<font color=\"" + Theme.colors.accentText + "\">"
                    + escaped(title.slice(start, end)) + "</font>"
            at = end
        }
        return text + escaped(title.slice(at))
    }

    function cell(role: int): var {
        return results.data(results.index(list.currentIndex, 0), role)
    }

    function restart() {
        choosing = false
        list.currentIndex = 0
    }

    function page(): int {
        const view = choosing ? optionList : list
        return Math.max(1, Math.floor(view.height / Theme.size.paletteRow))
    }

    function nextOption(from: int, delta: int): int {
        const last = options.length - 1
        const step = delta > 0 ? 1 : -1
        const wanted = Math.max(0, Math.min(last, from + delta))
        for (let index = wanted; index >= 0 && index <= last; index += step) {
            if (options[index].enabled)
                return index
        }
        for (let index = wanted - step; index !== from && index >= 0 && index <= last; index -= step) {
            if (options[index].enabled)
                return index
        }
        return from
    }

    function move(delta: int) {
        if (choosing)
            optionList.currentIndex = nextOption(optionList.currentIndex, delta)
        else if (results.count > 0)
            list.currentIndex = Math.max(0, Math.min(results.count - 1, list.currentIndex + delta))
    }

    function openChoices(): bool {
        const row = list.currentIndex
        if (choosing || row < 0 || row >= results.count || cell(CommandModel.KindRole) !== "group")
            return false
        options = results.choices(row)
        optionsGroup = cell(CommandModel.CommandIdRole)
        optionsTitle = cell(CommandModel.TitleRole)
        optionsTarget = cell(CommandModel.TargetNameRole)
        let first = options.findIndex(option => option.checked && option.enabled)
        if (first < 0)
            first = options.findIndex(option => option.enabled)
        choosing = true
        optionList.currentIndex = Math.max(0, first)
        return true
    }

    function closeChoices() {
        choosing = false
    }

    function activate() {
        if (choosing) {
            const option = options[optionList.currentIndex]
            if (option === undefined || !option.enabled)
                return
            results.triggerChoice(optionsGroup, option.commandId)
            close()
            return
        }
        if (list.currentIndex < 0 || list.currentIndex >= results.count || openChoices())
            return
        results.trigger(list.currentIndex)
        close()
    }

    function pick(index: int) {
        list.currentIndex = index
        activate()
    }

    function choose(index: int) {
        optionList.currentIndex = index
        activate()
    }

    function pointed(at: point) {
        const moved = pointerKnown && (at.x !== pointer.x || at.y !== pointer.y)
        pointer = at
        pointerKnown = true
        if (!moved)
            return
        const view = choosing ? optionList : list
        const local = view.contentItem.mapFromItem(null, at.x, at.y)
        const index = view.indexAt(local.x, local.y)
        if (index < 0)
            return
        if (!choosing)
            list.currentIndex = index
        else if (options[index].enabled)
            optionList.currentIndex = index
    }

    function press(event: KeyEvent) {
        const plain = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier)) === 0
        switch (event.key) {
        case Qt.Key_Up:
            move(-1)
            break
        case Qt.Key_Down:
            move(1)
            break
        case Qt.Key_PageUp:
            move(-page())
            break
        case Qt.Key_PageDown:
            move(page())
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            activate()
            break
        case Qt.Key_Escape:
            if (choosing)
                closeChoices()
            else
                close()
            break
        case Qt.Key_Tab:
            if (!choosing && (!plain || !openChoices())) {
                event.accepted = false
                return
            }
            break
        case Qt.Key_Right:
            if (choosing || !plain || input.cursorPosition < input.length || !openChoices()) {
                event.accepted = false
                return
            }
            break
        case Qt.Key_Left:
            if (!choosing || !plain) {
                event.accepted = false
                return
            }
            closeChoices()
            break
        default:
            event.accepted = false
            return
        }
        event.accepted = true
    }

    parent: T.Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: Theme.size.paletteTop
    implicitWidth: Theme.size.palette
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    padding: 0
    margins: Theme.space.lg
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    onAboutToShow: {
        input.clear()
        results.target = ""
        deviceChoice.currentIndex = -1
        pointerKnown = false
        restart()
        list.positionViewAtBeginning()
    }
    onAboutToHide: deviceChoice.popup.close()

    CommandModel {
        id: results

        query: input.text
        active: commandPalette.visible
    }

    Connections {
        target: results

        function onCountChanged() {
            if (list.currentIndex >= results.count)
                list.currentIndex = Math.max(0, results.count - 1)
        }
    }

    contentItem: Column {
        Item {
            id: bar

            width: parent.width
            height: Theme.size.paletteInput

            Icon {
                x: Theme.space.lg + Theme.space.xxs
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: Theme.size.iconLarge
                color: Theme.colors.textTertiary
            }

            T.TextField {
                id: input

                anchors.fill: parent
                leftPadding: Theme.space.lg + Theme.space.xxs + Theme.size.iconLarge + Theme.space.md
                rightPadding: Theme.space.lg + Theme.space.xxs + escapeHint.width + Theme.space.md
                topPadding: 0
                bottomPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                focus: true
                color: Theme.colors.textPrimary
                selectionColor: Theme.colors.accentLine
                selectedTextColor: Theme.colors.textPrimary
                placeholderText: qsTr("Search commands")
                placeholderTextColor: Theme.colors.textTertiary
                font.pixelSize: Theme.type.subtitle.size

                Accessible.role: Accessible.EditableText
                Accessible.name: placeholderText
                Accessible.searchEdit: true

                Keys.onPressed: event => commandPalette.press(event)
                onTextChanged: commandPalette.restart()

                Text {
                    x: input.leftPadding
                    width: input.width - input.leftPadding - input.rightPadding
                    height: input.height
                    visible: input.length === 0 && input.preeditText === ""
                    textFormat: Text.PlainText
                    text: input.placeholderText
                    font: input.font
                    color: input.placeholderTextColor
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            Keycap {
                id: escapeHint

                anchors.right: parent.right
                anchors.rightMargin: Theme.space.lg + Theme.space.xxs
                anchors.verticalCenter: parent.verticalCenter
                text: "Esc"
            }
        }

        Rectangle {
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeDefault
        }

        Item {
            id: deviceRow

            visible: commandPalette.asksDevice
            width: parent.width
            height: Theme.size.control + 2 * Theme.space.sm

            Row {
                x: Theme.space.lg + Theme.space.xxs
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.md

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: qsTr("Device")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                Select {
                    id: deviceChoice

                    anchors.verticalCenter: parent.verticalCenter
                    model: DeviceManager.sorted
                    textRole: "name"
                    valueRole: "serial"
                    focusPolicy: Qt.TabFocus
                    displayText: results.target === "" ? qsTr("Choose a device")
                                                       : Commands.device(results.target).name ?? ""
                    Accessible.name: qsTr("Device")
                    onActivated: index => {
                        results.target = deviceChoice.valueAt(index)
                        commandPalette.closeChoices()
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: {
                if (commandPalette.choosing)
                    return Math.min(optionHeader.height + commandPalette.options.length * Theme.size.paletteRow
                                    + Theme.space.sm, commandPalette.room)
                if (results.count === 0)
                    return Theme.size.paletteRow + 2 * Theme.space.lg
                return Math.min(results.count * Theme.size.paletteRow + results.sectionCount * Theme.size.paletteSection
                                + 2 * Theme.space.sm, commandPalette.room)
            }

            HoverHandler {
                onPointChanged: {
                    if (hovered)
                        commandPalette.pointed(point.scenePosition)
                }
            }

            ListView {
                id: list

                anchors.fill: parent
                visible: !commandPalette.choosing && results.count > 0
                topMargin: Theme.space.sm
                bottomMargin: Theme.space.sm
                clip: true
                model: results
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Commands")
                section.property: "section"
                section.delegate: Text {
                    required property string section

                    width: list.width
                    height: section === "" ? 0 : Theme.size.paletteSection
                    leftPadding: Theme.space.lg + Theme.space.xxs
                    verticalAlignment: Text.AlignVCenter
                    textFormat: Text.PlainText
                    text: section === "recent" ? qsTr("Recently used") : section === "other" ? qsTr("Other commands") : ""
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.micro.size
                    font.weight: Theme.type.micro.weight
                }

                delegate: T.AbstractButton {
                    id: row

                    required property var model
                    required property int index

                    width: ListView.view.width
                    implicitHeight: Theme.size.paletteRow
                    leftInset: Theme.space.sm
                    rightInset: Theme.space.sm
                    leftPadding: Theme.space.sm + Theme.space.md
                    rightPadding: Theme.space.sm + Theme.space.md
                    topPadding: 0
                    bottomPadding: 0
                    focusPolicy: Qt.NoFocus
                    text: model.title
                    Accessible.description: model.targetName
                    onClicked: commandPalette.pick(index)

                    background: Rectangle {
                        radius: Theme.radius.md
                        color: row.ListView.isCurrentItem ? Theme.colors.stateSelected : "transparent"
                    }

                    contentItem: Item {
                        Icon {
                            id: glyph

                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            name: row.model.icon
                            size: Theme.size.icon
                            color: Theme.colors.textSecondary
                        }

                        Text {
                            id: title

                            anchors.left: glyph.right
                            anchors.leftMargin: Theme.space.md
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(0, Math.min(implicitWidth, trailing.x - x - Theme.space.md))
                            text: commandPalette.marked(row.model.title, row.model.highlights)
                            textFormat: Text.StyledText
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Theme.type.label.weight
                            elide: Text.ElideRight
                        }

                        Text {
                            anchors.left: title.right
                            anchors.leftMargin: Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(0, Math.min(implicitWidth, trailing.x - x - Theme.space.md))
                            visible: text !== ""
                            textFormat: Text.PlainText
                            text: row.model.subtitle
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                            elide: Text.ElideRight
                        }

                        Row {
                            id: trailing

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.space.md

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, Theme.size.paletteTarget)
                                visible: text !== ""
                                textFormat: Text.PlainText
                                text: row.model.targetName
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                                elide: Text.ElideRight
                            }

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: row.model.shortcut.length > 0
                                spacing: Theme.space.xs

                                Repeater {
                                    model: row.model.shortcut

                                    delegate: Keycap {
                                        required property string modelData

                                        text: modelData
                                    }
                                }
                            }

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: row.model.kind === "group"
                                name: "chevron-right"
                                size: Theme.size.iconSmall
                                color: Theme.colors.textTertiary
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: !commandPalette.choosing && results.count === 0
                textFormat: Text.PlainText
                text: qsTr("No matching commands")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.body.size
            }

            Item {
                anchors.fill: parent
                visible: commandPalette.choosing

                T.AbstractButton {
                    id: optionHeader

                    width: parent.width
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    topPadding: Theme.space.sm + Theme.space.xxs
                    bottomPadding: Theme.space.xs
                    leftPadding: Theme.space.lg + Theme.space.xxs
                    rightPadding: Theme.space.lg + Theme.space.xxs
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: true
                    text: commandPalette.optionsTarget === ""
                          ? commandPalette.optionsTitle
                          : commandPalette.optionsTitle + " · " + commandPalette.optionsTarget
                    Accessible.name: qsTr("Back to all commands")
                    onClicked: commandPalette.closeChoices()

                    contentItem: Row {
                        spacing: Theme.space.xs + Theme.space.xxs

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "arrow-left"
                            size: Theme.size.iconSmall
                            color: optionHeader.hovered ? Theme.colors.textSecondary : Theme.colors.textTertiary
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: optionHeader.text
                            color: optionHeader.hovered ? Theme.colors.textSecondary : Theme.colors.textTertiary
                            font.pixelSize: Theme.type.micro.size
                            font.weight: Theme.type.micro.weight
                        }
                    }
                }

                ListView {
                    id: optionList

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: optionHeader.bottom
                    anchors.bottom: parent.bottom
                    bottomMargin: Theme.space.sm
                    clip: true
                    model: commandPalette.options
                    boundsBehavior: Flickable.StopAtBounds
                    Accessible.role: Accessible.List
                    Accessible.name: commandPalette.optionsTitle

                    delegate: T.AbstractButton {
                        id: option

                        required property var modelData
                        required property int index

                        width: ListView.view.width
                        implicitHeight: Theme.size.paletteRow
                        leftInset: Theme.space.sm
                        rightInset: Theme.space.sm
                        leftPadding: Theme.space.sm + Theme.space.md
                        rightPadding: Theme.space.sm + Theme.space.md
                        topPadding: 0
                        bottomPadding: 0
                        enabled: modelData.enabled
                        opacity: enabled ? 1 : Theme.disabledOpacity
                        focusPolicy: Qt.NoFocus
                        text: modelData.title
                        Accessible.checkable: true
                        Accessible.checked: modelData.checked
                        onClicked: commandPalette.choose(index)

                        background: Rectangle {
                            radius: Theme.radius.md
                            color: option.ListView.isCurrentItem && option.enabled ? Theme.colors.stateSelected
                                                                                   : "transparent"
                        }

                        contentItem: Item {
                            Icon {
                                id: mark

                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                name: option.modelData.checked ? "check" : ""
                                size: Theme.size.icon
                                color: Theme.colors.accentText
                            }

                            Text {
                                anchors.left: mark.right
                                anchors.right: keys.left
                                anchors.leftMargin: Theme.space.md
                                anchors.rightMargin: Theme.space.md
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: option.text
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                                font.weight: Theme.type.label.weight
                                elide: Text.ElideRight
                            }

                            Row {
                                id: keys

                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.space.xs

                                Repeater {
                                    model: option.modelData.shortcut

                                    delegate: Keycap {
                                        required property string modelData

                                        text: modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }

        Item {
            id: footer

            width: parent.width
            height: Theme.size.paletteFooter

            Row {
                x: Theme.space.lg + Theme.space.xxs
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.lg + Theme.space.xxs

                Hint {
                    keys: ["↑", "↓"]
                    label: qsTr("Select")
                }

                Hint {
                    keys: ["Enter"]
                    label: commandPalette.choosing ? qsTr("Apply") : qsTr("Run")
                }

                Hint {
                    keys: [commandPalette.choosing ? "←" : "Tab"]
                    label: commandPalette.choosing ? qsTr("Back") : qsTr("Options")
                }
            }
        }
    }

    background: Item {
        implicitWidth: Theme.size.palette

        RectangularShadow {
            anchors.fill: face
            radius: face.radius
            offset.y: Theme.shadow.ambientOffset
            blur: Theme.shadow.ambientBlur
            color: Theme.shadow.ambient
        }

        RectangularShadow {
            anchors.fill: face
            radius: face.radius
            offset.y: Theme.shadow.keyOffset
            blur: Theme.shadow.keyBlur
            color: Theme.shadow.key
        }

        Item {
            anchors.fill: parent
            visible: commandPalette.frosted

            ShaderEffectSource {
                id: behind

                anchors.fill: parent
                visible: false
                sourceItem: commandPalette.frosted ? commandPalette.backdrop : null
                sourceRect: commandPalette.behindRect
            }

            Rectangle {
                id: glassShape

                anchors.fill: parent
                radius: face.radius
                visible: false
                layer.enabled: true
            }

            MultiEffect {
                anchors.fill: parent
                source: behind
                autoPaddingEnabled: false
                blurEnabled: true
                blur: 1.0
                blurMax: Theme.shadow.backdropBlur
                maskEnabled: true
                maskSource: glassShape
            }
        }

        Rectangle {
            id: face

            anchors.fill: parent
            radius: Theme.radius.xl
            color: commandPalette.frosted ? Theme.colors.bgFrosted : Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeStrong
        }
    }

    T.Overlay.modal: Rectangle {
        color: Theme.colors.scrim

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.fast
            }
        }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
            NumberAnimation {
                property: "scale"
                from: Theme.motion.enterScale
                to: 1
                duration: Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
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

    component Hint: Row {
        id: hint

        property list<string> keys
        property string label

        spacing: Theme.space.xs + Theme.space.xxs

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.xs

            Repeater {
                model: hint.keys

                delegate: Keycap {
                    required property string modelData

                    text: modelData
                }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: hint.label
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }
    }
}
