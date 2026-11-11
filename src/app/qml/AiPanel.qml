pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Rectangle {
    id: panel

    property var context: ({})
    property bool shown: false
    property bool editorUnsaved: false
    property string pending
    readonly property AiConversation conversation: AiChats.conversation
    readonly property string profile: panel.context.profile ?? ""
    readonly property string profileName: panel.context.profileName ?? ""
    readonly property bool asking: (McpServer.confirmation.client ?? "") === "GameScrcpy AI"
    readonly property real inputMost: 6 * Theme.type.label.lineHeight
    property var openRuns: ({})
    property int waitedSeconds: 0
    readonly property var codeColors: ({
        "+": Theme.colors.successText,
        "-": Theme.colors.dangerText,
        "@": Theme.colors.infoText
    })

    function submit() {
        const words = prompt.text.trim()
        if (words === "" || panel.conversation.busy || !AiAssistant.ready)
            return
        if (AiAssistant.explanationWanted) {
            panel.pending = words
            disclosure.open()
            return
        }
        panel.deliver(words)
    }

    function toggleRun(start: int) {
        const runs = Object.assign({}, panel.openRuns)
        runs[start] = runs[start] !== true
        panel.openRuns = runs
    }

    function deliver(words: string) {
        if (!AiChats.send(words, panel.context))
            return
        prompt.clear()
        messages.follow = true
        Qt.callLater(() => messages.positionViewAtEnd())
    }

    function changes(added: int, removed: int): string {
        return "+" + added + " \u2212" + removed
    }

    function titleOf(draft: var): string {
        if (draft.kind === "profile") {
            if (draft.id === panel.profile)
                return panel.profileName
            return (ProfileLibrary.entries.find(entry => entry.id === draft.id) ?? {}).name ?? draft.id
        }
        if (draft.kind === "component")
            return qsTr("Component %1").arg(draft.id)
        if (draft.kind === "task")
            return qsTr("Task %1").arg(draft.id)
        return draft.target
    }

    function showDiff(target: string, path: string) {
        diffDialog.path = path
        diffDialog.lines = drafts.fileDiff(target, path).split("\n")
        diffDialog.open()
    }

    function lineColor(line: string): color {
        if (line.startsWith("+++") || line.startsWith("---"))
            return Theme.colors.textTertiary
        return panel.codeColors[line.charAt(0)] ?? Theme.colors.textSecondary
    }

    color: Theme.colors.bgRaised

    onShownChanged: {
        if (panel.shown && AiAssistant.ready)
            Qt.callLater(() => prompt.forceActiveFocus())
    }

    AiDrafts {
        id: drafts
    }

    Timer {
        interval: Theme.motion.tick
        repeat: true
        running: panel.conversation.waiting
        onRunningChanged: {
            if (running)
                panel.waitedSeconds = 0
        }
        onTriggered: panel.waitedSeconds += 1
    }

    Connections {
        target: panel.conversation

        function onModelReset() {
            panel.openRuns = ({})
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }

    Column {
        objectName: "aiSetup"
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.space.xl
        visible: !AiAssistant.ready && panel.conversation.count === 0
        spacing: Theme.space.md

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "bot"
            size: Theme.size.iconHero
            color: Theme.colors.textTertiary
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("Set up the AI assistant")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.subtitle.size
            font.weight: Theme.type.subtitle.weight
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: AiAssistant.providerMissing ? AiAssistant.missing
                                              : qsTr("It uses the model you set up in Settings > AI.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }

        Button {
            objectName: "aiOpenSettings"
            anchors.horizontalCenter: parent.horizontalCenter
            tone: "primary"
            iconName: "settings-2"
            text: qsTr("Open settings")
            onClicked: AiAssistant.openSettings()
        }
    }

    Column {
        objectName: "aiWelcome"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.space.xl
        y: (composer.y - height) / 2
        visible: AiAssistant.ready && panel.conversation.count === 0 && drafts.count === 0
        spacing: Theme.space.md

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "sparkles"
            size: Theme.size.iconHero
            color: Theme.colors.accentText
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: panel.profileName !== "" ? qsTr("Ask about %1").arg(panel.profileName) : qsTr("Ask the assistant")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.subtitle.size
            font.weight: Theme.type.subtitle.weight
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("It knows the help, the blocks, the settings, the shortcuts and what you have open here, and "
                       + "reads the connection log when a phone won't connect. It can place keys, write and test logic "
                       + "and blueprints, and make tasks and components. Its changes wait in a draft until you apply "
                       + "them.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }
    }

    ListView {
        id: messages

        property bool follow: true

        objectName: "aiMessages"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: setupToContinue.visible ? setupToContinue.top : composer.top
        visible: AiAssistant.ready || panel.conversation.count > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: panel.conversation
        topMargin: Theme.space.md
        bottomMargin: Theme.space.md
        spacing: 0
        onCountChanged: {
            if (messages.follow)
                Qt.callLater(() => messages.positionViewAtEnd())
        }
        onContentHeightChanged: {
            if (messages.follow)
                Qt.callLater(() => messages.positionViewAtEnd())
        }
        onMovingChanged: {
            if (!messages.moving)
                messages.follow = messages.atYEnd
        }

        delegate: Column {
            id: entry

            required property int index
            required property string kind
            required property string text
            required property var blocks
            required property string title
            required property string input
            required property string output
            required property bool running
            required property bool failed
            required property int seconds
            required property string changes
            required property var run
            property bool expanded: false
            readonly property bool grouped: entry.kind === "tool" && (entry.run.grouped ?? false)
            readonly property bool groupOpen: entry.grouped && panel.openRuns[entry.run.start] === true
            readonly property bool shown: !entry.grouped || entry.run.first || entry.groupOpen

            width: ListView.view.width
            topPadding: !entry.shown || entry.index === 0 ? 0
                        : entry.grouped && !entry.run.first ? Theme.space.xs : Theme.space.md
            spacing: Theme.space.xs

            Item {
                objectName: "aiUserMessage"
                visible: entry.kind === "user"
                width: parent.width
                height: bubble.height

                Rectangle {
                    id: bubble

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.lg
                    width: Math.min(userText.implicitWidth, parent.width - 2 * Theme.space.lg - Theme.space.xxl)
                           + 2 * Theme.space.md
                    height: userText.height + 2 * Theme.space.sm
                    radius: Theme.radius.lg
                    bottomRightRadius: Theme.radius.xs
                    color: Theme.colors.bgElevated
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeDefault

                    TextEdit {
                        id: userText

                        x: Theme.space.md
                        y: Theme.space.sm
                        width: parent.width - 2 * Theme.space.md
                        readOnly: true
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        text: entry.text
                        color: Theme.colors.textPrimary
                        selectionColor: Theme.colors.accentLine
                        selectedTextColor: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                    }
                }
            }

            Column {
                objectName: "aiAssistantMessage"
                visible: entry.kind === "assistant"
                x: Theme.space.lg
                width: parent.width - 2 * Theme.space.lg
                spacing: Theme.space.sm

                Repeater {
                    model: entry.kind === "assistant" ? entry.blocks : []

                    delegate: Item {
                        id: block

                        required property var modelData
                        readonly property bool code: block.modelData.code === true

                        width: parent ? parent.width : implicitWidth
                        height: block.code ? codeBox.height : prose.height

                        TextEdit {
                            id: prose

                            visible: !block.code
                            width: parent.width
                            readOnly: true
                            selectByMouse: true
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.Wrap
                            text: block.code ? "" : block.modelData.text
                            color: Theme.colors.textSecondary
                            selectionColor: Theme.colors.accentLine
                            selectedTextColor: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                        }

                        Rectangle {
                            id: codeBox

                            objectName: "aiCodeBlock"
                            visible: block.code
                            width: parent.width
                            height: codeText.height + copyCode.height + 2 * Theme.space.sm
                            radius: Theme.radius.md
                            color: Theme.colors.bgCode
                            border.width: Theme.size.stroke
                            border.color: Theme.colors.strokeDefault

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.space.sm
                                anchors.verticalCenter: copyCode.verticalCenter
                                textFormat: Text.PlainText
                                text: block.modelData.language ?? ""
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }

                            Button {
                                id: copyCode

                                objectName: "aiCopyCode"
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.space.xs
                                y: Theme.space.xs
                                small: true
                                tone: "ghost"
                                iconName: "copy"
                                text: qsTr("Copy")
                                onClicked: {
                                    codeText.selectAll()
                                    codeText.copy()
                                    codeText.deselect()
                                }
                            }

                            TextEdit {
                                id: codeText

                                x: Theme.space.sm
                                y: copyCode.y + copyCode.height
                                width: parent.width - 2 * Theme.space.sm
                                readOnly: true
                                selectByMouse: true
                                textFormat: TextEdit.PlainText
                                wrapMode: TextEdit.WrapAnywhere
                                text: block.code ? block.modelData.text : ""
                                color: Theme.colors.textPrimary
                                selectionColor: Theme.colors.accentLine
                                selectedTextColor: Theme.colors.textPrimary
                                font.family: Theme.type.monoFamily
                                font.pixelSize: Theme.type.caption.size
                            }
                        }
                    }
                }
            }

            Column {
                id: thinking

                property bool open: false

                objectName: "aiThinkingBlock"
                visible: entry.kind === "thinking"
                x: Theme.space.lg
                width: parent.width - 2 * Theme.space.lg
                spacing: Theme.space.xs

                Item {
                    width: parent.width
                    height: Theme.size.controlSmall

                    Accessible.role: Accessible.Button
                    Accessible.name: thinkingTitle.text
                    Accessible.onPressAction: {
                        if (!entry.running)
                            thinking.open = !thinking.open
                    }

                    Icon {
                        id: thinkingIcon

                        anchors.verticalCenter: parent.verticalCenter
                        name: "brain"
                        size: Theme.size.iconSmall
                        color: entry.running ? Theme.colors.accentText : Theme.colors.textTertiary

                        SequentialAnimation on opacity {
                            running: entry.running && thinking.visible && !Theme.motion.reduced
                            loops: Animation.Infinite
                            onRunningChanged: {
                                if (!running)
                                    thinkingIcon.opacity = 1
                            }

                            NumberAnimation {
                                to: Theme.motion.pulseLow
                                duration: Theme.motion.pulse / 2
                            }
                            NumberAnimation {
                                to: 1
                                duration: Theme.motion.pulse / 2
                            }
                        }
                    }

                    Text {
                        id: thinkingTitle

                        objectName: "aiThinkingTitle"
                        anchors.left: thinkingIcon.right
                        anchors.leftMargin: Theme.space.sm
                        anchors.right: thinkingChevron.left
                        anchors.rightMargin: Theme.space.sm
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: entry.running ? qsTr("Thinking…")
                                            : entry.seconds > 0 ? qsTr("Thought for %n second(s)", "", entry.seconds)
                                                                : qsTr("Thought")
                        elide: Text.ElideRight
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: Font.Medium
                    }

                    Icon {
                        id: thinkingChevron

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !entry.running
                        name: thinking.open ? "chevron-down" : "chevron-right"
                        size: Theme.size.iconSmall
                        color: Theme.colors.textTertiary
                    }

                    HoverHandler {
                        enabled: !entry.running
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        enabled: !entry.running
                        onTapped: thinking.open = !thinking.open
                    }
                }

                Item {
                    id: thinkingBody

                    objectName: "aiThinkingText"
                    visible: (entry.running || thinking.open) && entry.text !== ""
                    width: parent.width
                    height: entry.running ? Math.min(thinkingText.implicitHeight, 5 * Theme.type.caption.lineHeight)
                                          : thinkingText.implicitHeight
                    clip: true

                    Rectangle {
                        width: Theme.size.stroke * 2
                        height: parent.height
                        radius: width / 2
                        color: Theme.colors.strokeDefault
                    }

                    Text {
                        id: thinkingText

                        x: Theme.space.md
                        y: entry.running ? Math.min(0, thinkingBody.height - implicitHeight) : 0
                        width: parent.width - x
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        text: entry.text
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        lineHeight: Theme.type.caption.lineHeight
                        lineHeightMode: Text.FixedHeight
                    }
                }
            }

            Rectangle {
                objectName: "aiToolGroup"
                visible: entry.grouped && entry.run.first
                x: Theme.space.lg

                Accessible.role: Accessible.Button
                Accessible.name: groupTitle.text
                Accessible.onPressAction: panel.toggleRun(entry.run.start)
                width: parent.width - 2 * Theme.space.lg
                height: Theme.size.controlSmall + 2 * Theme.space.xxs
                radius: Theme.radius.md
                color: Theme.colors.bgOverlay
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeSubtle

                Spinner {
                    id: groupSpinner

                    x: Theme.space.sm + Theme.space.xxs
                    anchors.verticalCenter: parent.verticalCenter
                    spinning: entry.run.running === true
                }

                Icon {
                    x: groupSpinner.x
                    anchors.verticalCenter: parent.verticalCenter
                    visible: entry.run.running !== true
                    name: entry.run.failed === true ? "triangle-alert" : "layers"
                    size: Theme.size.iconSmall
                    color: entry.run.failed === true ? Theme.colors.dangerText : Theme.colors.textSecondary
                }

                Text {
                    id: groupTitle

                    objectName: "aiToolGroupTitle"
                    anchors.left: groupSpinner.right
                    anchors.leftMargin: Theme.space.sm
                    anchors.right: groupChevron.left
                    anchors.rightMargin: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: entry.run.running === true ? qsTr("Called %n tool(s) · %1", "", entry.run.count ?? 0)
                                                           .arg(entry.run.current ?? "")
                                                     : qsTr("Called %n tool(s)", "", entry.run.count ?? 0)
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.Medium
                }

                Icon {
                    id: groupChevron

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.sm + Theme.space.xxs
                    anchors.verticalCenter: parent.verticalCenter
                    name: entry.groupOpen ? "chevron-down" : "chevron-right"
                    size: Theme.size.iconSmall
                    color: Theme.colors.textTertiary
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: panel.toggleRun(entry.run.start)
                }
            }

            Rectangle {
                objectName: "aiToolRow"
                visible: entry.kind === "tool" && (!entry.grouped || entry.groupOpen)
                x: Theme.space.lg
                width: parent.width - 2 * Theme.space.lg
                height: toolColumn.implicitHeight + 2 * Theme.space.xxs
                radius: Theme.radius.md
                color: Theme.colors.bgOverlay
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeSubtle

                Column {
                    id: toolColumn

                    x: Theme.space.sm + Theme.space.xxs
                    y: Theme.space.xxs
                    width: parent.width - 2 * x
                    bottomPadding: entry.expanded ? Theme.space.sm : 0
                    spacing: Theme.space.xs

                    Item {
                        width: parent.width
                        height: Theme.size.controlSmall

                        Accessible.role: Accessible.Button
                        Accessible.name: toolTitle.text
                        Accessible.onPressAction: entry.expanded = !entry.expanded

                        Spinner {
                            id: toolState

                            anchors.verticalCenter: parent.verticalCenter
                            spinning: entry.running && entry.kind === "tool"
                        }

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !entry.running
                            name: entry.failed ? "triangle-alert" : "circle-check"
                            size: Theme.size.iconSmall
                            color: entry.failed ? Theme.colors.dangerText : Theme.colors.successText
                        }

                        Text {
                            id: toolTitle

                            objectName: "aiToolTitle"
                            anchors.left: toolState.right
                            anchors.leftMargin: Theme.space.sm
                            anchors.right: toolChanges.visible ? toolChanges.left : chevron.left
                            anchors.rightMargin: Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: entry.title
                            elide: Text.ElideRight
                            color: entry.failed ? Theme.colors.dangerText : Theme.colors.textPrimary
                            font.pixelSize: Theme.type.caption.size
                            font.weight: Font.Medium
                        }

                        Row {
                            id: toolChanges

                            objectName: "aiToolChanges"
                            anchors.right: chevron.left
                            anchors.rightMargin: Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            visible: entry.changes !== ""
                            spacing: Theme.space.xs

                            Repeater {
                                model: entry.changes !== "" ? entry.changes.split(" ") : []

                                delegate: Text {
                                    required property string modelData

                                    textFormat: Text.PlainText
                                    text: modelData
                                    color: modelData.startsWith("+") ? Theme.colors.successText : Theme.colors.dangerText
                                    font.family: Theme.type.monoFamily
                                    font.pixelSize: Theme.type.caption.size
                                }
                            }
                        }

                        Icon {
                            id: chevron

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            name: entry.expanded ? "chevron-down" : "chevron-right"
                            size: Theme.size.iconSmall
                            color: Theme.colors.textTertiary
                        }

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        TapHandler {
                            onTapped: entry.expanded = !entry.expanded
                        }
                    }

                    Rectangle {
                        objectName: "aiToolDetails"
                        visible: entry.expanded
                        width: parent.width
                        height: details.implicitHeight + 2 * Theme.space.sm
                        radius: Theme.radius.md
                        color: Theme.colors.bgCode
                        border.width: Theme.size.stroke
                        border.color: Theme.colors.strokeSubtle

                        Column {
                            id: details

                            x: Theme.space.sm
                            y: Theme.space.sm
                            width: parent.width - 2 * Theme.space.sm
                            spacing: Theme.space.xs

                            Text {
                                visible: entry.input !== ""
                                textFormat: Text.PlainText
                                text: qsTr("Arguments")
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.micro.size
                                font.weight: Theme.type.micro.weight
                            }

                            TextEdit {
                                visible: entry.input !== ""
                                width: parent.width
                                readOnly: true
                                selectByMouse: true
                                textFormat: TextEdit.PlainText
                                wrapMode: TextEdit.WrapAnywhere
                                text: entry.input
                                color: Theme.colors.textSecondary
                                selectionColor: Theme.colors.accentLine
                                selectedTextColor: Theme.colors.textPrimary
                                font.family: Theme.type.monoFamily
                                font.pixelSize: Theme.type.caption.size
                            }

                            Text {
                                textFormat: Text.PlainText
                                text: entry.running ? qsTr("Running…") : qsTr("Result")
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.micro.size
                                font.weight: Theme.type.micro.weight
                            }

                            TextEdit {
                                visible: entry.output !== ""
                                width: parent.width
                                readOnly: true
                                selectByMouse: true
                                textFormat: TextEdit.PlainText
                                wrapMode: TextEdit.WrapAnywhere
                                text: entry.output
                                color: entry.failed ? Theme.colors.dangerText : Theme.colors.textSecondary
                                selectionColor: Theme.colors.accentLine
                                selectedTextColor: Theme.colors.textPrimary
                                font.family: Theme.type.monoFamily
                                font.pixelSize: Theme.type.caption.size
                            }
                        }
                    }
                }
            }

            Text {
                objectName: "aiNotice"
                visible: entry.kind === "notice"
                x: Theme.space.lg
                width: parent.width - 2 * Theme.space.lg
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: entry.text
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            InlineAlert {
                objectName: "aiProblem"
                visible: entry.kind === "problem"
                x: Theme.space.lg
                width: parent.width - 2 * Theme.space.lg
                tone: "bad"
                title: entry.text
            }
        }

        footer: Column {
            id: footer

            width: messages.width
            topPadding: Theme.space.sm
            spacing: Theme.space.md

            StatusPill {
                objectName: "aiThinking"
                x: Theme.space.lg
                visible: panel.conversation.waiting
                tone: "live"
                text: panel.waitedSeconds > 0 ? qsTr("Thinking… %n s", "", panel.waitedSeconds) : qsTr("Thinking…")
            }

            Repeater {
                model: drafts.drafts

                delegate: Rectangle {
                    id: draftCard

                    required property var modelData
                    property bool open: false
                    property string problem
                    readonly property bool blocked: panel.editorUnsaved
                                                    && draftCard.modelData.target === "profile:" + panel.profile

                    objectName: "aiDraftCard"
                    x: Theme.space.lg
                    width: footer.width - 2 * Theme.space.lg
                    height: cardColumn.implicitHeight + 2 * Theme.space.md
                    radius: Theme.radius.lg
                    color: Theme.colors.bgElevated
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.accentLine

                    Column {
                        id: cardColumn

                        x: Theme.space.md
                        y: Theme.space.md
                        width: parent.width - 2 * Theme.space.md
                        spacing: Theme.space.sm

                        Row {
                            id: draftTitle

                            width: parent.width
                            spacing: Theme.space.sm

                            Icon {
                                id: draftIcon

                                anchors.verticalCenter: parent.verticalCenter
                                name: "pencil"
                                size: Theme.size.iconSmall
                                color: Theme.colors.accentText
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: draftTitle.width - draftIcon.width - draftTitle.spacing
                                textFormat: Text.PlainText
                                text: qsTr("Draft for %1").arg(panel.titleOf(draftCard.modelData))
                                elide: Text.ElideRight
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                                font.weight: Font.DemiBold
                            }
                        }

                        Text {
                            objectName: "aiDraftSummary"
                            width: parent.width
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: qsTr("%n file(s) changed", "", draftCard.modelData.files.length) + "  \u00b7  "
                                  + panel.changes(draftCard.modelData.added, draftCard.modelData.removed)
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                        }

                        Text {
                            objectName: "aiDraftConflicts"
                            visible: draftCard.modelData.conflicts > 0
                            width: parent.width
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: qsTr("%n file(s) changed on disk after the draft began; applying writes the draft's "
                                       + "version over them.", "", draftCard.modelData.conflicts)
                            color: Theme.colors.warningText
                            font.pixelSize: Theme.type.caption.size
                        }

                        Text {
                            objectName: "aiDraftBlocked"
                            visible: draftCard.blocked
                            width: parent.width
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: qsTr("Save or discard your own changes in the editor first.")
                            color: Theme.colors.warningText
                            font.pixelSize: Theme.type.caption.size
                        }

                        Text {
                            objectName: "aiDraftProblem"
                            visible: draftCard.problem !== ""
                            width: parent.width
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: draftCard.problem
                            color: Theme.colors.dangerText
                            font.pixelSize: Theme.type.caption.size
                        }

                        Column {
                            objectName: "aiDraftFiles"
                            visible: draftCard.open
                            width: parent.width

                            Repeater {
                                model: draftCard.open ? draftCard.modelData.files : []

                                delegate: Rectangle {
                                    id: fileRow

                                    required property var modelData

                                    objectName: "aiDraftFile"
                                    width: parent ? parent.width : implicitWidth
                                    height: Theme.size.controlSmall
                                    radius: Theme.radius.sm
                                    color: fileHover.hovered ? Theme.colors.stateHover : "transparent"

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: Theme.space.xs
                                        anchors.right: counts.left
                                        anchors.rightMargin: Theme.space.sm
                                        anchors.verticalCenter: parent.verticalCenter
                                        textFormat: Text.PlainText
                                        text: fileRow.modelData.path
                                        elide: Text.ElideMiddle
                                        color: fileRow.modelData.conflict ? Theme.colors.warningText
                                                                          : Theme.colors.textPrimary
                                        font.pixelSize: Theme.type.caption.size
                                    }

                                    Text {
                                        id: counts

                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.space.xs
                                        anchors.verticalCenter: parent.verticalCenter
                                        textFormat: Text.PlainText
                                        text: panel.changes(fileRow.modelData.added, fileRow.modelData.removed)
                                        color: Theme.colors.textTertiary
                                        font.pixelSize: Theme.type.caption.size
                                    }

                                    HoverHandler {
                                        id: fileHover

                                        cursorShape: Qt.PointingHandCursor
                                    }

                                    TapHandler {
                                        onTapped: panel.showDiff(draftCard.modelData.target, fileRow.modelData.path)
                                    }
                                }
                            }
                        }

                        Flow {
                            width: parent.width
                            spacing: Theme.space.xs

                            Button {
                                objectName: "aiDraftView"
                                small: true
                                tone: "ghost"
                                iconName: draftCard.open ? "chevron-down" : "eye"
                                text: draftCard.open ? qsTr("Hide changes") : qsTr("View changes")
                                onClicked: draftCard.open = !draftCard.open
                            }

                            Button {
                                objectName: "aiDraftApply"
                                small: true
                                tone: "primary"
                                iconName: "check"
                                text: qsTr("Apply")
                                enabled: !draftCard.blocked
                                onClicked: {
                                    const title = panel.titleOf(draftCard.modelData)
                                    const problem = drafts.apply(draftCard.modelData.target)
                                    if (problem === "")
                                        panel.conversation.note(qsTr("Applied the draft to %1.").arg(title))
                                    else
                                        draftCard.problem = problem
                                }
                            }

                            Button {
                                objectName: "aiDraftDiscard"
                                small: true
                                tone: "ghost"
                                iconName: "trash-2"
                                text: qsTr("Discard")
                                onClicked: {
                                    const title = panel.titleOf(draftCard.modelData)
                                    drafts.discard(draftCard.modelData.target)
                                    panel.conversation.note(qsTr("Discarded the draft for %1.").arg(title))
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    InlineAlert {
        id: setupToContinue

        objectName: "aiSetupToContinue"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space.lg
        visible: !AiAssistant.ready && panel.conversation.count > 0
        tone: AiAssistant.providerMissing ? "warn" : "info"
        title: qsTr("Set up the AI assistant to go on with this conversation")
        detail: AiAssistant.providerMissing ? AiAssistant.missing : ""
        actionText: qsTr("Open settings")
        onActionTriggered: AiAssistant.openSettings()
    }

    Column {
        id: composer

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space.lg
        visible: AiAssistant.ready
        spacing: Theme.space.sm

        Text {
            objectName: "aiAsking"
            visible: panel.asking
            width: parent.width
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("GameScrcpy is asking you to confirm in its main window.")
            color: Theme.colors.warningText
            font.pixelSize: Theme.type.caption.size
        }

        FieldFrame {
            objectName: "aiComposer"
            width: parent.width
            height: composerColumn.implicitHeight + 2 * Theme.space.sm + Theme.space.xxs
            radius: Theme.radius.lg
            focused: prompt.activeFocus
            hovered: composerHover.hovered

            HoverHandler {
                id: composerHover
            }

            TapHandler {
                onTapped: prompt.forceActiveFocus()
            }

            Column {
                id: composerColumn

                x: Theme.space.md
                y: Theme.space.sm + Theme.space.xxs
                width: parent.width - x - Theme.space.sm - Theme.space.xxs
                spacing: Theme.space.sm

                Flickable {
                    id: inputScroll

                    width: parent.width
                    height: Math.min(prompt.contentHeight, panel.inputMost)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: width
                    contentHeight: prompt.contentHeight

                    function follow(cursor: rect) {
                        if (cursor.y < inputScroll.contentY)
                            inputScroll.contentY = cursor.y
                        else if (cursor.y + cursor.height > inputScroll.contentY + inputScroll.height)
                            inputScroll.contentY = cursor.y + cursor.height - inputScroll.height
                    }

                    TextEdit {
                        id: prompt

                        objectName: "aiInput"
                        width: inputScroll.width
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        color: Theme.colors.textPrimary
                        selectionColor: Theme.colors.accentLine
                        selectedTextColor: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        Accessible.name: qsTr("Message to the AI assistant")
                        Keys.onPressed: event => {
                            const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                            if (!enter || (event.modifiers & Qt.ShiftModifier) || prompt.preeditText !== "")
                                return
                            event.accepted = true
                            panel.submit()
                        }
                        onCursorRectangleChanged: inputScroll.follow(prompt.cursorRectangle)
                    }

                    Text {
                        width: parent.width
                        visible: prompt.length === 0 && prompt.preeditText === ""
                        textFormat: Text.PlainText
                        text: qsTr("Ask about anything in GameScrcpy")
                        elide: Text.ElideRight
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.label.size
                    }
                }

                Item {
                    width: parent.width
                    height: Math.max(hint.implicitHeight, modelPicker.implicitHeight, sendButton.implicitHeight)

                    AiModelPicker {
                        id: modelPicker

                        objectName: "aiModelPicker"
                        anchors.left: parent.left
                        anchors.leftMargin: -Theme.space.sm
                        anchors.verticalCenter: parent.verticalCenter
                        conversation: panel.conversation
                        widest: parent.width - sendButton.width
                    }

                    Text {
                        id: hint

                        anchors.left: parent.left
                        anchors.right: sendButton.left
                        anchors.rightMargin: Theme.space.sm
                        anchors.verticalCenter: parent.verticalCenter
                        visible: panel.conversation.count === 0 && !modelPicker.visible
                        textFormat: Text.PlainText
                        text: qsTr("Enter sends, Shift+Enter starts a new line")
                        elide: Text.ElideRight
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }

                    Button {
                        id: sendButton

                        objectName: "aiSend"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !panel.conversation.busy
                        small: true
                        tone: "primary"
                        text: qsTr("Send")
                        toolTip: qsTr("Enter sends, Shift+Enter starts a new line")
                        enabled: prompt.text.trim() !== ""
                        onClicked: panel.submit()
                    }

                    Button {
                        objectName: "aiStop"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: panel.conversation.busy
                        small: true
                        iconName: "square"
                        text: qsTr("Stop")
                        onClicked: panel.conversation.stop()
                    }
                }
            }
        }
    }

    AiDisclosureDialog {
        id: disclosure

        onAccepted: {
            AiAssistant.acknowledgeExplanation(disclosure.remember)
            const words = panel.pending
            panel.pending = ""
            if (words !== "")
                panel.deliver(words)
        }
        onRejected: panel.pending = ""
    }

    Dialog {
        id: diffDialog

        property string path
        property var lines: []

        objectName: "aiDiffDialog"
        width: Math.min(Theme.size.workbenchCode,
                        (diffDialog.parent !== null ? diffDialog.parent.width : Theme.size.workbenchCode)
                            - 2 * Theme.space.xxl)
        title: diffDialog.path
        acceptText: qsTr("Close")
        rejectText: ""

        ListView {
            objectName: "aiDiffLines"
            width: parent.width
            height: Math.min(contentHeight, Math.max(Theme.size.workbenchBottom, panel.Window.height / 2))
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: diffDialog.lines

            delegate: Text {
                id: diffLine

                required property string modelData

                width: ListView.view.width
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                text: diffLine.modelData
                color: panel.lineColor(diffLine.modelData)
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size
            }
        }
    }

    component Spinner: Icon {
        id: spinner

        property bool spinning: false

        visible: spinner.spinning
        name: "loader-circle"
        size: Theme.size.iconSmall
        color: Theme.colors.accentText

        RotationAnimator on rotation {
            running: spinner.spinning && spinner.visible && !Theme.motion.reduced
            from: 0
            to: 360
            duration: Theme.motion.pulse
            loops: Animation.Infinite
        }
    }
}
