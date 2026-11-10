pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: editor

    required property ScriptDocument document
    property bool typing: false
    readonly property real lineHeight: edit.lineCount > 0 && edit.contentHeight > 0 ? edit.contentHeight / edit.lineCount
                                                                                      : metrics.height
    readonly property real gutterWidth: numbers.advanceWidth + 2 * Theme.space.md
    readonly property real wave: Theme.space.xxs
    readonly property int currentLine: Math.round(edit.cursorRectangle.y / editor.lineHeight) + 1
    readonly property string lineNumbers: {
        let text = "1"
        for (let line = 2; line <= edit.lineCount; ++line)
            text += "\n" + line
        return text
    }
    readonly property bool profile: editor.document.path.toLowerCase() === "profile.json"
    readonly property bool canUndo: edit.canUndo
    readonly property bool canRedo: edit.canRedo

    function undo() {
        edit.undo()
    }

    function redo() {
        edit.redo()
    }

    function jumpTo(line: int, column: int) {
        if (editor.document.image)
            return
        edit.cursorPosition = editor.document.positionOf(Math.max(line, 1), Math.max(column, 1))
        edit.forceActiveFocus()
        view.reveal(edit.cursorRectangle)
    }

    function focusText() {
        if (!editor.document.image)
            edit.forceActiveFocus()
    }

    function waveOf(length: real): string {
        let path = "M 0 " + editor.wave
        let up = true
        for (let x = editor.wave; x <= length + editor.wave; x += editor.wave) {
            path += " L " + x + " " + (up ? 0 : editor.wave)
            up = !up
        }
        return path
    }

    function refreshCompletion(asked: bool) {
        const found = editor.document.completion(edit.cursorPosition, asked)
        completion.items = found.items ?? []
        completion.current = 0
    }

    function afterTyping() {
        if (!editor.typing)
            return
        editor.typing = false
        editor.refreshCompletion(false)
    }

    function acceptCompletion() {
        const chosen = completion.chosen
        completion.items = []
        if (chosen === null)
            return
        const end = edit.cursorPosition
        editor.typing = false
        edit.remove(chosen.from, end)
        edit.insert(chosen.from, chosen.insert)
        edit.cursorPosition = chosen.from + chosen.caret
    }

    function insertText(text: string) {
        const start = edit.selectionStart
        edit.remove(edit.selectionStart, edit.selectionEnd)
        edit.insert(start, text)
        edit.cursorPosition = start + text.length
    }

    function appendBlock(text: string, caret: int) {
        if (editor.document.image || edit.readOnly)
            return
        const body = edit.text
        const lead = body.length === 0 ? "" : body.endsWith("\n") ? "\n" : "\n\n"
        const start = edit.length
        edit.insert(start, lead + text)
        edit.cursorPosition = start + lead.length + caret
        edit.forceActiveFocus()
        view.reveal(edit.cursorRectangle)
    }

    function openMenu(at: point) {
        const position = edit.positionAt(at.x, at.y)
        if (edit.selectionStart === edit.selectionEnd || position < edit.selectionStart || position > edit.selectionEnd)
            edit.cursorPosition = position
        edit.forceActiveFocus()
        codeMenu.position = position
        codeMenu.target = editor.document.referenceAt(position)
        const spot = edit.mapToItem(editor, at.x, at.y)
        codeMenu.popup(spot.x, spot.y)
    }

    FontMetrics {
        id: metrics

        font: edit.font
    }

    TextMetrics {
        id: numbers

        font: edit.font
        text: String(Math.max(99, edit.lineCount))
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.colors.bgEditor
    }

    Rectangle {
        id: diskBar

        objectName: "changedOnDisk"
        visible: editor.document.changedOnDisk
        width: parent.width
        height: visible ? Theme.size.control + 2 * Theme.space.sm : 0
        color: Theme.colors.warningSubtle

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.lg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "triangle-alert"
                size: Theme.size.iconSmall
                color: Theme.colors.warningText
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("Changed on disk")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space.lg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Button {
                objectName: "reloadFromDisk"
                small: true
                text: qsTr("Reload")
                onClicked: editor.document.reload()
            }

            Button {
                objectName: "keepMine"
                small: true
                tone: "ghost"
                text: qsTr("Keep mine")
                onClicked: editor.document.keepMine()
            }
        }
    }

    Rectangle {
        id: readOnlyBar

        objectName: "readOnlyNote"
        y: diskBar.height
        visible: editor.document.readOnly && !editor.document.image
        width: parent.width
        height: visible ? Theme.size.listItem : 0
        color: Theme.colors.bgRaised

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.lg
            anchors.right: parent.right
            anchors.rightMargin: Theme.space.lg
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            elide: Text.ElideRight
            text: editor.document.recorded ? qsTr("The code from when the debug record was made; read only")
                  : editor.document.draft ? qsTr("The AI's draft; apply the draft to make it the profile")
                  : editor.profile ? qsTr("Read only here: change the keys in the key mapping editor")
                                   : qsTr("This kind of file is read only here")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
        }
    }

    Image {
        objectName: "picture"
        visible: editor.document.image
        anchors.centerIn: parent
        width: Math.min(sourceSize.width, parent.width - 2 * Theme.space.xxl)
        height: Math.min(sourceSize.height, parent.height - 2 * Theme.space.xxl)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: false
        source: editor.document.source
    }

    Flickable {
        id: view

        function reveal(area: rect) {
            if (view.width <= 0 || view.height <= 0)
                return
            const left = edit.x + area.x
            const top = edit.y + area.y
            if (left - editor.gutterWidth - Theme.space.md < view.contentX)
                view.contentX = Math.max(0, left - editor.gutterWidth - Theme.space.md)
            else if (left + area.width + Theme.space.md > view.contentX + view.width)
                view.contentX = left + area.width + Theme.space.md - view.width
            if (top < view.contentY)
                view.contentY = Math.max(0, top - Theme.space.sm)
            else if (top + area.height > view.contentY + view.height)
                view.contentY = top + area.height + Theme.space.sm - view.height
        }

        objectName: "codeView"
        visible: !editor.document.image
        anchors.fill: parent
        anchors.topMargin: diskBar.height + readOnlyBar.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, edit.x + edit.contentWidth + Theme.space.x3l)
        contentHeight: Math.max(height, edit.contentHeight + 2 * Theme.space.sm)

        Rectangle {
            objectName: "currentLine"
            visible: edit.activeFocus
            y: edit.y + edit.cursorRectangle.y
            width: view.contentWidth
            height: editor.lineHeight
            color: Theme.colors.stateHover
        }

        TextEdit {
            id: edit

            objectName: "code"
            x: editor.gutterWidth + Theme.space.md
            y: Theme.space.sm
            width: Math.max(implicitWidth, view.width - x)
            height: Math.max(implicitHeight, view.height - 2 * Theme.space.sm)
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.NoWrap
            readOnly: editor.document.readOnly
            selectByMouse: true
            color: Theme.colors.textPrimary
            selectionColor: Theme.colors.accentLine
            selectedTextColor: Theme.colors.textPrimary
            font.family: Theme.type.monoFamily
            font.pixelSize: Theme.type.mono.size
            tabStopDistance: metrics.averageCharacterWidth * 4
            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            Accessible.name: editor.document.name

            Component.onCompleted: editor.document.attach(edit.textDocument)
            onCursorRectangleChanged: view.reveal(edit.cursorRectangle)
            onCursorPositionChanged: {
                if (!editor.typing)
                    completion.items = []
            }
            onTextChanged: {
                if (editor.typing)
                    Qt.callLater(editor.afterTyping)
            }
            onActiveFocusChanged: {
                if (!edit.activeFocus)
                    completion.items = []
            }

            Keys.onPressed: event => {
                const open = completion.items.length > 0
                if (open && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                    const step = event.key === Qt.Key_Down ? 1 : completion.items.length - 1
                    completion.current = (completion.current + step) % completion.items.length
                    event.accepted = true
                    return
                }
                if (open && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab)) {
                    editor.acceptCompletion()
                    event.accepted = true
                    return
                }
                if (open && event.key === Qt.Key_Escape) {
                    completion.items = []
                    event.accepted = true
                    return
                }
                if (edit.readOnly)
                    return
                if (event.key === Qt.Key_Space && (event.modifiers & Qt.ControlModifier)) {
                    editor.refreshCompletion(true)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    editor.insertText(editor.document.lineBreak(edit.selectionStart))
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Tab) {
                    editor.insertText("    ")
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Backtab) {
                    event.accepted = true
                    return
                }
                const typed = event.text
                editor.typing = (typed.length === 1 && /[A-Za-z0-9_$.]/.test(typed))
                                || (event.key === Qt.Key_Backspace && open)
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: (eventPoint, button) => editor.openMenu(eventPoint.position)
            }
        }

        Repeater {
            model: editor.document.marks

            delegate: Item {
                id: mark

                required property var modelData
                readonly property rect from: edit.positionToRectangle(modelData.start)
                readonly property rect to: edit.positionToRectangle(modelData.end)
                readonly property rect tail: edit.positionToRectangle(modelData.lineEnd)
                readonly property real span: Math.max(to.x - from.x, Theme.space.sm)
                readonly property color tint: modelData.severity === "error" ? Theme.colors.dangerText
                                                                             : Theme.colors.warningText

                objectName: "problemMark"
                x: edit.x
                y: edit.y

                Shape {
                    x: mark.from.x
                    y: mark.from.y + mark.from.height - editor.wave
                    width: mark.span
                    height: editor.wave

                    ShapePath {
                        strokeColor: mark.tint
                        strokeWidth: Theme.size.stroke
                        fillColor: "transparent"

                        PathSvg {
                            path: editor.waveOf(mark.span)
                        }
                    }
                }

                Text {
                    objectName: "problemNote"
                    visible: mark.modelData.message !== ""
                    x: mark.tail.x + Theme.space.xl
                    y: mark.tail.y
                    width: Math.max(0, view.contentX + view.width - edit.x - x - Theme.space.md)
                    height: mark.tail.height
                    verticalAlignment: Text.AlignVCenter
                    textFormat: Text.PlainText
                    text: mark.modelData.message
                    elide: Text.ElideRight
                    color: mark.tint
                    font.pixelSize: Theme.type.caption.size
                    font.italic: true
                }
            }
        }

        Rectangle {
            objectName: "lineNumbers"
            x: view.contentX
            width: editor.gutterWidth
            height: view.contentHeight
            color: Theme.colors.bgEditor

            Rectangle {
                visible: edit.activeFocus
                y: edit.y + edit.cursorRectangle.y
                width: parent.width
                height: editor.lineHeight
                color: Theme.colors.stateHover
            }

            Text {
                y: edit.y
                width: parent.width - Theme.space.md
                horizontalAlignment: Text.AlignRight
                textFormat: Text.PlainText
                text: editor.lineNumbers
                color: Theme.colors.textTertiary
                font: edit.font
                lineHeightMode: Text.FixedHeight
                lineHeight: editor.lineHeight
            }

            Text {
                visible: edit.activeFocus
                y: edit.y + edit.cursorRectangle.y
                width: parent.width - Theme.space.md
                horizontalAlignment: Text.AlignRight
                textFormat: Text.PlainText
                text: String(editor.currentLine)
                color: Theme.colors.textPrimary
                font: edit.font
            }
        }
    }

    Item {
        id: completion

        property var items: []
        property int current: 0
        readonly property var chosen: items.length > 0 ? items[Math.min(current, items.length - 1)] : null
        readonly property real below: view.y + edit.y + edit.cursorRectangle.y + edit.cursorRectangle.height - view.contentY
        readonly property real caretLeft: view.x + edit.x + edit.cursorRectangle.x - view.contentX

        objectName: "completion"
        visible: items.length > 0 && edit.activeFocus
        width: Math.min(Theme.size.dialogWidth, editor.width - 2 * Theme.space.sm)
        height: choices.height + (help.visible ? help.implicitHeight + Theme.space.xs : 0) + 2 * Theme.space.xs
        x: Math.max(Theme.space.sm, Math.min(caretLeft - Theme.space.sm, editor.width - width - Theme.space.sm))
        y: below + height + Theme.space.xs > editor.height ? Math.max(0, below - editor.lineHeight - height - Theme.space.xs)
                                                           : below + Theme.space.xs

        PopupSurface {
            anchors.fill: parent
        }

        ListView {
            id: choices

            x: Theme.space.xs
            y: Theme.space.xs
            width: parent.width - 2 * Theme.space.xs
            height: Math.min(count, 8) * Theme.size.menuItem
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: completion.items
            currentIndex: completion.current
            onCurrentIndexChanged: choices.positionViewAtIndex(choices.currentIndex, ListView.Contain)

            delegate: Rectangle {
                id: choice

                required property var modelData
                required property int index

                objectName: "completionItem"
                width: ListView.view.width
                height: Theme.size.menuItem
                radius: Theme.radius.sm
                color: choice.index === completion.current ? Theme.colors.statePress : "transparent"

                TapHandler {
                    onTapped: {
                        completion.current = choice.index
                        editor.acceptCompletion()
                    }
                }

                Text {
                    id: kind

                    x: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.size.iconSmall
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: {
                        switch (choice.modelData.kind) {
                        case "function":
                        case "method":
                            return "ƒ"
                        case "namespace":
                            return "{}"
                        case "control":
                            return "◆"
                        case "state":
                            return "◇"
                        default:
                            return "•"
                        }
                    }
                    color: Theme.colors.accentText
                    font.family: Theme.type.monoFamily
                    font.pixelSize: Theme.type.caption.size
                }

                Text {
                    id: label

                    anchors.left: kind.right
                    anchors.leftMargin: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: choice.modelData.label
                    color: Theme.colors.textPrimary
                    font.family: Theme.type.monoFamily
                    font.pixelSize: Theme.type.mono.size
                }

                Text {
                    anchors.left: label.right
                    anchors.leftMargin: Theme.space.sm
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: choice.modelData.detail
                    elide: Text.ElideRight
                    color: Theme.colors.textTertiary
                    font.family: Theme.type.monoFamily
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }

        Text {
            id: help

            x: Theme.space.md
            y: choices.y + choices.height + Theme.space.xs
            width: parent.width - 2 * Theme.space.md
            visible: text !== ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            text: completion.chosen !== null ? completion.chosen.documentation : ""
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
        }
    }

    Menu {
        id: codeMenu

        property int position: 0
        property var target: ({})

        objectName: "codeMenu"

        MenuItem {
            objectName: "findReferences"
            iconName: "search"
            text: qsTr("Find references")
            enabled: (codeMenu.target.name ?? "") !== ""
            onTriggered: editor.document.findReferences(codeMenu.position)
        }
    }
}
