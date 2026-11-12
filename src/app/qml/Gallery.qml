pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Blueprint

Window {
    id: root

    readonly property list<string> accents: Object.keys(Theme.accentChoices)
    property int page: 0

    width: 1180
    height: 820
    visible: true
    title: "GameScrcpy component gallery"
    color: Theme.colors.bgBase

    component Section: Rectangle {
        id: section

        required property string title
        default property alias content: body.data

        width: column.width
        implicitHeight: body.y + body.implicitHeight + Theme.space.xl
        radius: Theme.radius.xl
        color: Theme.colors.bgRaised
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle

        Text {
            id: heading

            x: Theme.space.xl
            y: Theme.space.lg
            textFormat: Text.PlainText
            text: section.title
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.micro.size
            font.weight: Theme.type.micro.weight
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
        }

        Column {
            id: body

            x: Theme.space.xl
            y: heading.y + heading.height + Theme.space.md
            width: section.width - 2 * x
            spacing: Theme.space.md
        }
    }

    component Caption: Text {
        textFormat: Text.PlainText
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 2 * Theme.space.xxl
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column

            x: Theme.space.xxl
            y: Theme.space.xxl
            width: root.width - 2 * x
            spacing: Theme.space.lg

            Row {
                spacing: Theme.space.lg

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: "Components"
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.title.size
                    font.weight: Theme.type.title.weight
                }

                SegmentedControl {
                    anchors.verticalCenter: parent.verticalCenter
                    model: ["Basics", "Inputs", "Surfaces", "Blueprint"]
                    currentIndex: root.page
                    onActivated: index => root.page = index
                }

                SegmentedControl {
                    anchors.verticalCenter: parent.verticalCenter
                    model: ["Dark", "Light"]
                    currentIndex: Theme.dark ? 0 : 1
                    onActivated: index => Theme.dark = index === 0
                }

                SegmentedControl {
                    anchors.verticalCenter: parent.verticalCenter
                    model: root.accents
                    currentIndex: root.accents.indexOf(Theme.accentName)
                    onActivated: index => Theme.accentName = root.accents[index]
                }

                Switch {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Reduced motion"
                    checked: Theme.reducedMotion
                    onToggled: Theme.reducedMotion = checked
                }
            }

            Section {
                title: "Buttons"
                visible: root.page === 0

                Row {
                    spacing: Theme.space.md

                    Button {
                        tone: "primary"
                        iconName: "monitor-play"
                        text: "Start mirroring"
                    }
                    Button {
                        iconName: "link"
                        text: "Pair device"
                    }
                    Button {
                        tone: "ghost"
                        iconName: "refresh-cw"
                        text: "Refresh"
                    }
                    Button {
                        tone: "danger"
                        iconName: "power"
                        text: "Disconnect"
                    }
                    Button {
                        tone: "primary"
                        text: "Disabled"
                        enabled: false
                    }
                    Button {
                        text: "Disabled"
                        enabled: false
                    }
                }

                Row {
                    spacing: Theme.space.md

                    Button {
                        small: true
                        tone: "primary"
                        text: "Connect"
                    }
                    Button {
                        small: true
                        text: "Details"
                    }
                    Button {
                        small: true
                        tone: "ghost"
                        text: "Cancel"
                    }
                    Button {
                        small: true
                        tone: "danger"
                        iconName: "x"
                        text: "Remove"
                    }
                    Row {
                        spacing: Theme.size.stroke

                        Button {
                            small: true
                            tone: "primary"
                            joined: "start"
                            text: "Launch"
                        }
                        Button {
                            small: true
                            tone: "primary"
                            joined: "end"
                            iconName: "chevron-down"
                            toolTip: "More ways to launch"
                        }
                    }
                    Button {
                        iconName: "settings-2"
                        toolTip: "Settings"
                        shortcut: "Ctrl ,"
                    }
                    Button {
                        tone: "ghost"
                        iconName: "search"
                        toolTip: "Search"
                        shortcut: "Ctrl K"
                    }
                    Button {
                        id: more

                        tone: "ghost"
                        iconName: "ellipsis"
                        toolTip: "More"
                        onClicked: moreMenu.open()

                        Menu {
                            id: moreMenu

                            y: more.height + Theme.space.xs

                            MenuItem {
                                text: "Show window"
                                iconName: "monitor"
                                shortcut: "Ctrl Enter"
                            }
                            MenuItem {
                                text: "Launch on an independent display"
                                iconName: "monitor-smartphone"
                                description: "The phone's screen is not affected"
                            }
                            MenuItem {
                                text: "Screenshot"
                                iconName: "camera"
                                shortcut: "Ctrl S"
                            }
                            MenuItem {
                                text: "Keep the screen on"
                                checkable: true
                                checked: true
                            }
                            MenuItem {
                                text: "Unavailable"
                                iconName: "plug"
                                enabled: false
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: "Disconnect"
                                iconName: "power"
                                danger: true
                            }
                        }
                    }
                    Button {
                        tone: "primary"
                        iconName: "plus"
                        toolTip: "Add"
                    }
                    Button {
                        small: true
                        iconName: "maximize-2"
                        toolTip: "Full screen"
                        shortcut: "F11"
                    }
                }
            }

            Section {
                title: "Selection"
                visible: root.page === 0

                Row {
                    spacing: Theme.space.xxl

                    Switch {
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Switch {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: true
                    }
                    Switch {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Turn the device screen off"
                        checked: true
                    }
                    Switch {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Disabled"
                        enabled: false
                    }
                }

                Row {
                    spacing: Theme.space.xxl

                    Checkbox {
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Checkbox {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: true
                    }
                    Checkbox {
                        anchors.verticalCenter: parent.verticalCenter
                        tristate: true
                        checkState: Qt.PartiallyChecked
                        text: "Some layers"
                    }
                    Checkbox {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Remember this device"
                        checked: true
                    }
                    Checkbox {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Disabled"
                        enabled: false
                    }
                }

                Row {
                    spacing: Theme.space.lg

                    SegmentedControl {
                        model: [
                            { text: "Video", icon: "monitor" },
                            { text: "Audio", icon: "volume-2" },
                            { text: "Controls", icon: "gamepad-2" }
                        ]
                    }
                    SegmentedControl {
                        model: ["USB", "WiFi direct", "Auto"]
                        currentIndex: 1
                    }
                    SegmentedControl {
                        model: [
                            { icon: "rotate-ccw", description: "Rotate left" },
                            { icon: "rotate-cw", description: "Rotate right" },
                            { icon: "maximize-2", description: "Fit" }
                        ]
                        currentIndex: 2
                    }
                    SegmentedControl {
                        model: ["Disabled", "Choice"]
                        enabled: false
                    }
                }
            }

            Section {
                title: "Inputs"
                visible: root.page === 1

                Row {
                    spacing: Theme.space.lg

                    TextField {
                        placeholderText: "Device address"
                        mono: true
                    }
                    TextField {
                        width: Theme.size.fieldWidth / 2
                        text: "8"
                        suffix: "Mbps"
                    }
                    NumberField {
                        width: Theme.size.numberFieldWide
                        prefix: "X"
                        unit: "%"
                        value: 76
                        numbers: NumberScale {
                            decimals: 1
                        }
                    }
                    TextField {
                        text: "192.168.1.300"
                        mono: true
                        errorText: "Not an IP address"
                    }
                    TextField {
                        placeholderText: "Disabled"
                        enabled: false
                    }
                }

                Row {
                    spacing: Theme.space.lg

                    SearchField {
                        width: Theme.size.fieldWidth * 1.4
                        placeholderText: "Search settings"
                        shortcut: "Ctrl K"
                    }
                    SearchField {
                        width: Theme.size.fieldWidth
                        text: "bitrate"
                    }
                    Select {
                        model: ["1080p", "1440p", "Native"]
                        currentIndex: 2
                    }
                    Slider {
                        from: 1
                        to: 30
                        value: 8
                        valueText: Math.round(value) + " Mb"
                    }
                }

                Row {
                    spacing: Theme.space.lg

                    KeyCaptureField {
                        width: Theme.size.fieldWidth * 1.4
                    }
                    KeyCaptureField {
                        width: Theme.size.fieldWidth * 1.4
                        keys: [{ text: "Ctrl" }, { text: "Shift" }, { text: "F" }]
                    }
                    KeyCaptureField {
                        width: Theme.size.fieldWidth * 1.4
                        keys: [{ text: "Right button", mouse: true }]
                        conflictText: "Also bound to “Walk”"
                    }
                }

                Row {
                    spacing: Theme.space.x3l

                    CodeField {
                        description: "Pairing code"
                    }
                    CodeField {
                        text: "482917"
                        description: "Pairing code"
                    }
                }
            }

            Section {
                title: "Status"
                visible: root.page === 0

                Row {
                    spacing: Theme.space.md

                    StatusPill {
                        tone: "live"
                        text: "Mirroring"
                    }
                    StatusPill {
                        tone: "ok"
                        text: "Connected"
                    }
                    StatusPill {
                        tone: "warn"
                        text: "Unauthorized"
                    }
                    StatusPill {
                        tone: "bad"
                        text: "Offline"
                    }
                    StatusPill {
                        text: "Idle"
                    }
                }

                Row {
                    spacing: Theme.space.xs + Theme.space.xxs

                    Repeater {
                        model: ["W", "A", "S", "D", "Space", "Shift", "Ctrl", "Tab", "Esc", "F1"]

                        delegate: Keycap {
                            required property string modelData

                            text: modelData
                        }
                    }

                    Caption {
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: Theme.space.md
                        text: "Keycaps"
                    }
                }
            }

            Row {
                visible: root.page === 2
                spacing: Theme.space.xxl

                Rectangle {
                    width: Theme.size.navRail
                    height: surfaces.height
                    radius: Theme.radius.xl
                    color: Theme.colors.bgBase
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeSubtle

                    NavRail {
                        anchors.fill: parent
                        model: [
                            { text: "Devices", icon: "smartphone" },
                            { text: "Mapping", icon: "keyboard" },
                            { text: "Scripts", icon: "code-xml" },
                            { text: "Settings", icon: "settings-2" }
                        ]

                        NavItem {
                            text: "Help"
                            iconName: "circle-help"
                        }
                    }
                }

                Column {
                    id: surfaces

                    width: column.width - Theme.size.navRail - Theme.space.xxl
                    spacing: Theme.space.md

                    Card {
                        width: parent.width
                        padding: 0

                        Column {
                            width: parent.width

                            SettingsRow {
                                width: parent.width
                                title: "Bitrate"
                                description: "Higher looks sharper and needs a faster link."

                                Slider {
                                    from: 1
                                    to: 30
                                    value: 8
                                    valueText: Math.round(value) + " Mb"
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                divider: true
                                overridden: true
                                title: "Turn the device screen off"
                                description: "This device overrides the global setting."

                                Switch {
                                    checked: true
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                divider: true
                                title: "Resolution"

                                Select {
                                    model: ["1080p", "1440p", "Native"]
                                }
                            }
                        }
                    }

                    Card {
                        width: parent.width
                        active: true

                        Row {
                            spacing: Theme.space.md

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: "Pixel 8"
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.subtitle.size
                                font.weight: Font.DemiBold
                            }
                            StatusPill {
                                anchors.verticalCenter: parent.verticalCenter
                                tone: "live"
                                text: "Mirroring"
                            }
                            Caption {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "An active card"
                            }
                        }
                    }

                    Card {
                        width: parent.width

                        Column {
                            spacing: Theme.space.lg

                            ConnectionStepper {
                                current: 2
                            }
                            ConnectionStepper {
                                current: 3
                                failed: true
                            }
                        }
                    }

                    Row {
                        spacing: Theme.space.md

                        Button {
                            text: "Show a toast"
                            onClicked: toast.show("Screenshot saved", "ok")
                        }
                        Button {
                            text: "Toast with an action"
                            onClicked: actionToast.open()
                        }
                        Button {
                            tone: "danger"
                            text: "Open a dialog"
                            onClicked: dialog.open()
                        }
                        Button {
                            objectName: "openSheet"
                            text: "Open a settings sheet"
                            onClicked: sheet.open()
                        }
                    }
                }
            }

            Section {
                title: "Mirror window"
                visible: root.page === 2

                Rectangle {
                    width: parent.width
                    height: 260
                    radius: Theme.radius.lg
                    color: Theme.colors.bgSunken
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeSubtle

                    ToolbarRevealArea {
                        anchors.fill: parent
                        toolbar: mirrorToolbar
                        active: !lockDemo.checked
                    }

                    FloatingToolbar {
                        id: mirrorToolbar

                        anchors.horizontalCenter: parent.horizontalCenter
                        y: Theme.space.md
                        pinned: keepToolbar.checked

                        ToolbarButton {
                            iconName: "arrow-left"
                            text: "Back"
                        }
                        ToolbarButton {
                            iconName: "house"
                            text: "Home"
                        }
                        ToolbarButton {
                            iconName: "square"
                            text: "Recent apps"
                        }
                        ToolbarSeparator {}
                        ToolbarButton {
                            iconName: "camera"
                            text: "Screenshot"
                            enabled: false
                        }
                        ToolbarButton {
                            iconName: "rotate-cw"
                            text: "Rotate"
                        }
                        ToolbarButton {
                            iconName: "volume-2"
                            text: "Mute"
                            checkable: true
                        }
                        ToolbarSeparator {}
                        ToolbarButton {
                            iconName: "activity"
                            text: "Diagnostics"
                            checkable: true
                            checked: true
                        }
                        ToolbarButton {
                            iconName: "maximize-2"
                            text: "Full screen"
                            shortcut: "F11"
                            checkable: true
                        }
                        ToolbarButton {
                            iconName: "ellipsis"
                            text: "More"
                        }
                    }

                    EdgeGlow {
                        anchors.fill: parent
                        active: lockDemo.checked
                    }

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Theme.space.md
                        spacing: Theme.space.lg

                        KeyHint {
                            small: true
                            text: "W"
                        }
                        KeyHint {
                            shape: "tap"
                            text: "F"
                        }
                        KeyHint {
                            shape: "hold"
                            text: "C"
                        }
                        KeyHint {
                            shape: "longPress"
                            text: "E"
                        }
                        KeyHint {
                            shape: "turbo"
                            mouseButton: "left"
                        }
                        KeyHint {
                            shape: "camera"
                            text: "Tab"
                        }
                        KeyHint {
                            shape: "freeLook"
                            text: "Alt"
                        }
                        KeyHint {
                            shape: "aim"
                            text: "1"
                        }
                        KeyHint {
                            shape: "zoomIn"
                            small: true
                            mouseButton: "wheelUp"
                        }
                        KeyHint {
                            shape: "zoomOut"
                            small: true
                            mouseButton: "wheelDown"
                        }
                        MappingPreview {
                            markers: [
                                { type: "wasd", x: 0.17, y: 0.66, rx: 0.08, ry: 0.14 },
                                { type: "camera", x: 0.5, y: 0.5, rx: 0, ry: 0 },
                                { type: "tap", x: 0.84, y: 0.62, rx: 0, ry: 0 },
                                { type: "hold", x: 0.9, y: 0.78, rx: 0, ry: 0 },
                                { type: "skill", x: 0.74, y: 0.8, rx: 0.06, ry: 0.1 }
                            ]
                        }
                        MappingPreview {
                            portrait: true
                            markers: [
                                { type: "wasd", x: 0.3, y: 0.8, rx: 0.2, ry: 0.11 },
                                { type: "tap", x: 0.75, y: 0.85, rx: 0, ry: 0 }
                            ]
                        }
                    }

                    StatusCapsule {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: Theme.space.md
                        transport: "WiFi direct"
                        profileName: "Shooter"
                        cameraLocked: lockDemo.checked
                    }

                    StatusCapsule {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: Theme.space.md
                        framesPerSecond: 59.7
                        latencyMs: 12.3
                        transport: "USB"
                    }
                }

                Row {
                    spacing: Theme.space.lg

                    Button {
                        text: "Show the toolbar"
                        onClicked: mirrorToolbar.reveal()
                    }
                    Switch {
                        id: keepToolbar

                        anchors.verticalCenter: parent.verticalCenter
                        text: "Keep it shown"
                    }
                    Switch {
                        id: lockDemo

                        anchors.verticalCenter: parent.verticalCenter
                        text: "Camera locked"
                    }
                    Caption {
                        anchors.verticalCenter: parent.verticalCenter
                        text: mirrorToolbar.shown ? "Shown" : "Hidden: rest the pointer at the top of the frame"
                    }
                }

                Column {
                    spacing: Theme.space.sm

                    LatencyTestCard {
                        progress: ({ running: true, measured: 3, target: 10, missed: 0 })
                    }
                    LatencyTestCard {
                        progress: ({ running: false, measured: 10, target: 10, missed: 1, samples: 10,
                                     medianMs: 48.2, lowMs: 41.0, highMs: 57.4 })
                    }
                    LatencyTestCard {
                        progress: ({ running: false, measured: 1, target: 10, missed: 15 })
                    }
                }
            }

            Section {
                title: "Blueprint"
                visible: root.page === 3

                BlueprintMock {
                    id: blueprint
                }

                Row {
                    spacing: Theme.space.lg

                    SegmentedControl {
                        id: blueprintPage

                        readonly property var keys: ["", "fn:combo", "machine:12:3", "perf", "broken"]

                        anchors.verticalCenter: parent.verticalCenter
                        model: ["Event page", "Function", "Machine", "300 blocks", "Problem"]
                        currentIndex: Math.max(0, keys.indexOf(blueprint.page))
                        onActivated: index => blueprint.open(blueprintPage.keys[index])
                    }
                    Switch {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Debug data"
                        checked: blueprint.debugging
                        onToggled: checked ? blueprint.startDebugging() : blueprint.stopDebugging()
                    }
                    Switch {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Read-only"
                        checked: blueprint.readOnly
                        onToggled: blueprint.readOnly = checked
                    }
                    Button {
                        text: "Pulse"
                        onClicked: {
                            if (blueprint.page === "machine:12:3") {
                                blueprintCanvas.pulse("playing", "t2")
                            } else {
                                blueprintCanvas.pulse("every", "")
                                blueprintCanvas.pulse("find", "w12")
                                blueprintCanvas.pulse("click", "w13")
                            }
                        }
                    }
                    Button {
                        text: "Fit"
                        onClicked: blueprintCanvas.fit()
                    }
                    DebugBar {
                        anchors.verticalCenter: parent.verticalCenter
                        state: blueprint.debugState
                        pausedTitle: blueprint.pausedTitle
                        othersPaused: blueprint.othersPaused
                        onContinueClicked: blueprint.resume()
                        onStepOverClicked: toast.show("Step over", "info")
                        onStepIntoClicked: toast.show("Step into", "info")
                        onStepOutClicked: toast.show("Step out", "info")
                        onStopClicked: blueprint.stopDebugging()
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 660
                    radius: Theme.radius.lg
                    color: Theme.colors.bgBase
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeSubtle
                    clip: true

                    BlueprintSidebar {
                        id: blueprintSidebar

                        x: Theme.size.stroke
                        y: Theme.size.stroke
                        width: Theme.size.workbenchLeft
                        height: parent.height - 2 * Theme.size.stroke
                        sections: blueprint.sidebar
                        expanded: ({ "controls/control:aim": true })
                        onItemActivated: item => toast.show("Activated " + item.section + " / " + item.item, "info")
                        onAddClicked: section => toast.show("Add to " + section, "info")
                    }

                    Rectangle {
                        x: blueprintSidebar.x + blueprintSidebar.width
                        width: Theme.size.stroke
                        height: parent.height
                        color: Theme.colors.strokeSubtle
                    }

                    BlueprintPages {
                        id: blueprintPages

                        x: blueprintSidebar.x + blueprintSidebar.width + Theme.size.stroke
                        y: Theme.size.stroke
                        width: blueprintDetails.x - x - Theme.size.stroke
                        pages: blueprint.pages
                        crumbs: blueprint.crumbs
                        current: blueprint.page
                        onPageChosen: key => blueprint.open(key)
                        onCrumbChosen: key => blueprint.open(key)
                    }

                    BlueprintCanvas {
                        id: blueprintCanvas

                        x: blueprintPages.x
                        y: blueprintPages.y + blueprintPages.height
                        width: blueprintPages.width
                        height: (blueprintTimeline.visible ? blueprintTimeline.y : parent.height - Theme.size.stroke) - y
                        graph: blueprint.graph
                        selection: blueprint.selection
                        check: blueprint.check
                        catalog: blueprint.catalog
                        debugging: blueprint.debugging
                        pausedNode: blueprint.pausedNode
                        pinValues: blueprint.pinValues
                        reached: blueprint.reached
                        onSelectionEdited: keys => blueprint.selection = keys
                        onMoved: (keys, dx, dy) => blueprint.move(keys, dx, dy)
                        onConnectRequested: (from, fromPin, to, toPin) => blueprint.connectPins(from, fromPin, to, toPin)
                        onDisconnectRequested: (node, pin) => blueprint.disconnectPin(node, pin)
                        onWireRemoved: wire => blueprint.removeWire(wire)
                        onValueEdited: (node, pin, value) => blueprint.setValue(node, pin, value)
                        onAddChosen: (id, x, y, node, pin) => blueprint.add(id, x, y, node, pin)
                        onRemoveRequested: keys => blueprint.remove(keys)
                        onDuplicateRequested: keys => blueprint.duplicate(keys)
                        onCodeEdited: (node, text) => blueprint.setCode(node, text)
                        onCommentRequested: keys => blueprint.addComment(keys)
                        onCommentEdited: (key, changes) => blueprint.editComment(key, changes)
                        onRerouteRequested: (wire, x, y) => blueprint.addReroute(wire, x, y)
                        onBreakpointToggled: node => blueprint.toggleBreakpoint(node)
                        onBreakpointEnabled: (node, on) => blueprint.enableBreakpoint(node, on)
                        onStateAdded: (x, y) => blueprint.addState(x, y)
                        onTransitionRequested: (from, to) => blueprint.addTransition(from, to)
                        onTransitionLabelEdited: (wire, text) => blueprint.setTransitionLabel(wire, text)
                        onStateRenamed: (node, name) => blueprint.renameState(node, name)
                        onStartStateSet: node => blueprint.setStartState(node)
                        onArrangeRequested: toast.show("Arrange the page", "info")
                        onOpened: node => toast.show("Open " + node, "info")
                        onPickRequested: (node, pin, kind) => toast.show("Pick a " + kind + " for " + node, "info")
                        onJumpRequested: node => toast.show("Jump to the code of " + (node === "" ? "the file" : node),
                                                            "info")
                        onCopyRequested: keys => toast.show("Copied " + keys.length + " as text", "info")
                        onDropped: (item, x, y, node, pin) => toast.show("Dropped " + (item ? item.item : "")
                                                                          + (pin !== "" ? " on " + node + "." + pin : ""),
                                                                          "info")
                    }

                    DebugTimeline {
                        id: blueprintTimeline

                        x: blueprintPages.x
                        y: parent.height - height - Theme.size.stroke
                        width: blueprintPages.width
                        visible: blueprint.debugging
                        span: blueprint.timeline.span
                        playhead: blueprint.timeline.playhead
                        live: blueprint.timeline.live
                        frames: blueprint.timeline.frames
                        steps: blueprint.timeline.steps
                        lanes: blueprint.timeline.lanes
                        onSeeked: at => blueprint.seek(at)
                        onLiveClicked: blueprint.goLive()
                        onSaveClicked: toast.show("Kept the last 5 minutes", "info")
                    }

                    Rectangle {
                        x: blueprintDetails.x - Theme.size.stroke
                        width: Theme.size.stroke
                        height: parent.height
                        color: Theme.colors.strokeSubtle
                    }

                    BlueprintDetails {
                        id: blueprintDetails

                        x: parent.width - width - Theme.size.stroke
                        y: Theme.size.stroke
                        width: Theme.size.workbenchSidebar
                        height: parent.height - 2 * Theme.size.stroke
                        visible: !blueprint.debugging
                        details: blueprint.details
                        onFieldEdited: (name, value) => toast.show("Edited " + name + " = " + value, "info")
                        onJumpRequested: toast.show("Jump to the code", "info")
                    }

                    DebugMoment {
                        x: blueprintDetails.x
                        y: blueprintDetails.y
                        width: blueprintDetails.width
                        height: blueprintDetails.height
                        visible: blueprint.debugging
                        title: blueprint.moment.title
                        subtitle: blueprint.moment.subtitle
                        frame: blueprint.moment.frame
                        boxes: blueprint.moment.boxes
                        result: blueprint.moment.result
                        variables: blueprint.moment.variables
                        stack: blueprint.moment.stack
                        onStackChosen: index => blueprint.chooseFrame(index)
                    }
                }
            }
        }
    }

    Toast {
        id: toast
    }

    Toast {
        id: actionToast

        text: "Device disconnected"
        tone: "warn"
        actionText: "Reconnect"
    }

    Dialog {
        id: dialog

        title: "Forget this device?"
        acceptText: "Forget"
        danger: true

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: "Its pairing, settings and history are removed. Pair it again to reconnect."
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }
    }

    SettingsSheet {
        id: sheet

        objectName: "gallerySheet"
        title: "Touch box"
        subtitle: "Extension · 1.0.0 · changes apply at once"
        iconName: "usb"
        tone: "amber"
        statusText: "Connected"
        statusTone: "ok"
        alternativeText: "Wiring guide"

        SettingsGroup {
            title: "Connection"

            SettingsRow {
                width: parent.width
                title: "Connection mode"
                description: "Serial uses the board's USB serial port, with the least delay."

                SegmentedControl {
                    model: ["Serial port", "Wi-Fi"]
                }
            }
            SettingsRow {
                width: parent.width
                divider: true
                title: "Serial port"

                Select {
                    model: ["Automatic · now COM5", "COM5", "COM7"]
                }
                Button {
                    small: true
                    tone: "ghost"
                    iconName: "refresh-cw"
                    toolTip: "Look for serial ports again"
                }
            }
            SettingsRow {
                width: parent.width
                divider: true
                title: "Wi-Fi"
                note: "Wi-Fi not set up"

                Button {
                    small: true
                    text: "Set up Wi-Fi…"
                }
            }
        }

        SettingsGroup {
            title: "Firmware"

            SettingsRow {
                width: parent.width
                title: "Firmware version"
                note: "2.1.0 · up to date"

                Button {
                    small: true
                    tone: "ghost"
                    text: "Flash firmware…"
                }
            }
            SettingsRow {
                width: parent.width
                divider: true
                title: "Restart the board after flashing"

                Switch {
                    checked: true
                }
            }
        }
    }
}
