pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import QtQuick.Dialogs
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls
import GameScrcpy.App

Item {
    id: keymapEditor

    required property string serial
    property ProfileEditor editor
    property rect picture
    property int barTop
    property bool frozen: false
    property bool snapping: true
    property bool showingNames: true
    property int ratioIndex: 0
    property real zoom: 1
    property real wheelTurn: 0
    property int browsePage: 0
    property bool noPicture: false
    property int tourBaseControls: 0
    property int tourBaseBound: 0
    readonly property int tourControls: keymapEditor.keyRows.length
    readonly property int tourBound: keymapEditor.keyRows.filter(row => row.keys !== "").length

    onLogicShownChanged: {
        if (keymapEditor.logicShown)
            Qt.callLater(tour.cancel)
    }
    readonly property var backdrop: noPicture && DeviceManager.backgroundRevision >= 0
                                    ? DeviceManager.keymapBackground(serial) : ({})
    readonly property string backdropUrl: backdrop.url !== undefined ? backdrop.url.toString() : ""

    readonly property bool trying: editor !== null && editor.trying
    readonly property bool readOnly: editor !== null && editor.preview
    readonly property bool logicShown: editor !== null && editor.logicShown
    readonly property ScriptWorkbench logicBench: editor !== null ? editor.logic as ScriptWorkbench : null
    readonly property LogicView logicView: logicLoader.item as LogicView
    readonly property string selectionText: keymapEditor.logicShown && keymapEditor.logicView !== null
                                            ? keymapEditor.logicView.selectionText
                                            : keymapEditor.keyRows.filter(row => row.selected).map(
                                                  row => row.name + (row.keys !== "" ? " (" + row.keys + ")" : "")
                                                         + " [" + row.id + "]").join(", ")
    readonly property bool unsaved: editor !== null && (editor.dirty || (logicBench !== null && logicBench.dirtyCount > 0))
    readonly property bool previewing: ratioIndex > 0 && !trying
    readonly property bool arranging: !trying && !previewing && !readOnly
    readonly property int toolsRoom: bench.toolsRoom
    readonly property rect view: Qt.rect(bench.canvasRect.x, bench.y + bench.canvasRect.y, bench.canvasRect.width,
                                         bench.canvasRect.height)
    readonly property point viewCenter: Qt.point(view.x + view.width / 2, view.y + view.height / 2)
    readonly property rect shown: Qt.rect(picture.x - view.x, picture.y - view.y, picture.width, picture.height)
    readonly property bool scrollable: picture.width > view.width + 1 || picture.height > view.height + 1
    readonly property bool portrait: picture.height > picture.width
    readonly property var ratios: {
        const stretch = picture.width > 0 && picture.height > 0
                ? 9 * Math.max(picture.width, picture.height) / Math.min(picture.width, picture.height) : 16
        const named = side => portrait ? "9:" + side : side + ":9"
        return [{ text: qsTr("Phone (%1)").arg(named(Math.round(2 * stretch) / 2)), side: stretch }]
                .concat([16, 18, 19.5, 20, 21].map(side => ({ text: named(side), side: side })))
    }
    readonly property rect stage: previewing ? fitted(ratios[ratioIndex].side, shown) : shown
    readonly property var markers: {
        if (editor === null || stage.width <= 0 || editor.revision < 0 || ComponentLibrary.mine.length < 0
                || editor.handedOver.length < 0)
            return []
        const all = previewing ? editor.previewMarkers(stage.width, stage.height)
                               : editor.markers(stage.width, stage.height)
        return trying ? all.map(marker => Object.assign({}, marker, { selected: false })) : all
    }
    readonly property var zoomSteps: [0.5, 0.75, 1, 1.25, 1.5, 2]
    readonly property var keyRows: editor !== null && editor.revision >= 0 && editor.handedOver.length >= 0
                                   ? editor.keyList() : []
    readonly property var diffRows: keymapEditor.markers.filter(marker => (marker.diff ?? "") !== "")
    readonly property int selectedCount: editor !== null ? editor.selection.length : 0
    readonly property var current: {
        const chosen = markers.filter(marker => marker.selected)
        return chosen.length === 1 ? chosen[0] : null
    }
    readonly property var placedPoints: current !== null && ((current.component ?? "") !== "" || current.modulePoints === true
                                                             || current.type === "multiPress"
                                                             || current.type === "actionWheel")
                                        ? current.points ?? [] : []
    readonly property var currentRow: selectedCount === 1 ? keyRows.find(row => row.selected) ?? null : null
    readonly property real snapDistance: Theme.space.sm
    readonly property real handleMinimum: Theme.size.keyHint / 2
    readonly property real tileGap: Theme.space.xs + Theme.space.xxs
    readonly property var library: [
        { title: qsTr("Basics"), items: [
            { kind: "tap", icon: "mouse-pointer-click", text: qsTr("Tap"), hint: qsTr("Taps quickly once"),
              tip: qsTr("Each key press taps the screen once, however long the key is held. For buttons you only touch, like jump or pick up.") },
            { kind: "hold", icon: "pointer", text: qsTr("Hold"), hint: qsTr("Held as long as the key"),
              tip: qsTr("The screen stays pressed as long as the key is held. For buttons you keep pressed, like fire, scope or crouch.") },
            { kind: "longPress", icon: "timer", text: qsTr("Long press"), hint: qsTr("Holds for a set time"),
              tip: qsTr("Each key press holds the screen for the time you set, even if the key is let go sooner. For buttons that need a long press.") },
            { kind: "turbo", icon: "tap-echo", text: qsTr("Turbo"), hint: qsTr("Taps again and again while held"),
              tip: qsTr("While the key is held, taps the screen again and again at the rate you set. For single-shot weapons or repeated pickups.") },
            { kind: "swipe", icon: "swipe-path", text: qsTr("Swipe"), hint: qsTr("Swipes along a path once"),
              tip: qsTr("Each key press swipes along the path you draw on the picture.") },
            { kind: "pinch", icon: "expand", text: qsTr("Pinch"), hint: qsTr("Zooms in or out"),
              tip: qsTr("Two keys spread or pinch two fingers, for zooming a map or a scope.") },
            { kind: "multiPress", icon: "multi-touch", text: qsTr("Multi-press"), hint: qsTr("Holds several places at once"),
              tip: qsTr("While the key is held, several places on the screen stay pressed together, like peek and fire. Drag the dots to place them.") }
        ] },
        { title: qsTr("Movement"), items: [
            { kind: "wasd", icon: "joystick-ring", text: qsTr("WASD stick"), hint: qsTr("Walk with the keyboard"),
              tip: qsTr("W A S D push the stick on the screen; the walk and sprint keys push it less or more.") },
            { kind: "rightWalk", icon: "navigation", text: qsTr("Right-click walk"), hint: qsTr("Right-click to walk there"),
              tip: qsTr("A click walks the hero toward that spot, as in MOBA games on a computer.") },
            { kind: "padStick", icon: "gamepad-2", text: qsTr("Gamepad stick"), hint: qsTr("Left stick pushes the stick"),
              tip: qsTr("A gamepad's stick pushes the stick on the screen.") }
        ] },
        { title: qsTr("Wheels"), items: [
            { kind: "dial", icon: "dial-cells", text: qsTr("Direction wheel"), hint: qsTr("Slides to a wheel cell"),
              tip: qsTr("Holding the key presses the middle of the game's weapon, item or emote wheel; move the mouse toward a cell and let go to pick it.") },
            { kind: "actionWheel", icon: "wheel-slices", text: qsTr("Action wheel"), hint: qsTr("Runs the cell you point at"),
              tip: qsTr("Holding the key opens a wheel in the middle of the picture; move the mouse toward a cell and let go to press a component, tap a spot or tell the logic.") }
        ] },
        { title: qsTr("View"), items: [
            { kind: "camera", icon: "crosshair", text: qsTr("View"), hint: qsTr("Mouse turns after locking"),
              tip: qsTr("After the lock key, moving the mouse turns the view. For shooters.") },
            { kind: "padCamera", icon: "move", text: qsTr("Gamepad view"), hint: qsTr("Right stick turns the view"),
              tip: qsTr("A gamepad's right stick turns the view.") },
            { kind: "freeLook", icon: "eye", text: qsTr("Free look"), hint: qsTr("Hold to look around"),
              tip: qsTr("While the key is held, the mouse looks around without turning the character.") },
            { kind: "tempCursor", icon: "cursor-arrow", text: qsTr("Temporary cursor"), hint: qsTr("Shows the cursor while held"),
              tip: qsTr("While the view is locked, holding the key brings out the cursor to click on the picture; letting go locks the view again.") }
        ] },
        { title: qsTr("MOBA"), items: [
            { kind: "skill", icon: "target", text: qsTr("Skill cast"), hint: qsTr("Casts toward the pointer"),
              tip: qsTr("Press to aim toward the pointer, release or click to cast; a cancel key can call it off.") }
        ] }
    ]
    readonly property var customLibrary: {
        const tileOf = entry => ({ kind: "component:" + entry.ref, ref: entry.ref,
                                   icon: entry.icon !== "" ? entry.icon : "sparkles", text: entry.name,
                                   hint: entry.description !== "" ? entry.description : qsTr("Made with logic"),
                                   tip: (entry.extensionId ?? "") !== ""
                                        ? qsTr("Comes with the extension %1. Click to copy it to your components, then place your copy. Double-click to see how it is made.").arg(entry.extension ?? "")
                                        : entry.example ? qsTr("Drag it onto the picture. Double-click to see how it is made.")
                                                        : qsTr("Drag it onto the picture. Double-click to change it."),
                                   badge: (entry.extensionId ?? "") !== "" ? "puzzle" : entry.example ? "workflow" : "",
                                   extensionId: entry.extensionId ?? "" })
        const fresh = { kind: "new", icon: "plus", text: qsTr("New component…"), hint: "",
                        tip: qsTr("Make your own component. It starts with one key that taps where you place it."),
                        badge: "" }
        return [{ title: qsTr("Example components"), note: qsTr("Made with logic, double-click to look"),
                  items: ComponentLibrary.examples.map(tileOf) },
                { title: qsTr("My components"), note: "", items: ComponentLibrary.mine.map(tileOf).concat([fresh]) }]
    }
    readonly property var extensionLibrary: [{
        title: qsTr("Extensions"),
        note: qsTr("From turned-on extensions"),
        items: ExtensionHost.nativeBehaviors.map(entry => ({
            kind: "behavior:" + entry.module, icon: "puzzle", text: entry.title,
            hint: entry.description !== "" ? entry.description : entry.extension,
            tip: qsTr("A component from %1. Drag it onto the picture, then press the key for it.").arg(entry.extension),
            badge: "" }))
    }]
    readonly property var shownLibrary: {
        const all = library.concat(customLibrary).concat(extensionLibrary).filter(group => group.items.length > 0)
        const query = componentSearch.text.trim().toLowerCase()
        if (query === "")
            return all
        return all.map(group => ({
                                     title: group.title,
                                     note: group.note ?? "",
                                     items: group.items.filter(item => item.kind !== "new"
                                                               && (item.text.toLowerCase().indexOf(query) >= 0
                                                                   || item.hint.toLowerCase().indexOf(query) >= 0
                                                                   || item.kind.toLowerCase().indexOf(query) >= 0))
                                 })).filter(group => group.items.length > 0)
    }

    signal notify(string message, string icon)

    function libraryItem(type: string): var {
        for (const group of library) {
            const found = group.items.find(item => item.kind === type)
            if (found !== undefined)
                return found
        }
        return null
    }

    function firstSentence(text: string): string {
        const cut = text.search(/[。.!?！？]/)
        return cut > 0 ? text.slice(0, cut) : text
    }

    function customOf(ref: string): var {
        return ComponentLibrary.examples.concat(ComponentLibrary.mine).find(entry => entry.ref === ref) ?? null
    }

    function rowInfo(row: var): var {
        if ((row.component ?? "") !== "") {
            const entry = customOf(row.component ?? "")
            if (entry === null)
                return { icon: "triangle-alert", text: qsTr("Missing component"),
                         hint: qsTr("This component is no longer there") }
            return { icon: entry.icon !== "" ? entry.icon : "sparkles", text: entry.name,
                     hint: entry.description !== "" ? entry.description
                                                    : entry.example ? qsTr("Example component") : qsTr("My component") }
        }
        if (row.type === "behavior") {
            const behavior = ExtensionHost.nativeBehaviors.find(each => each.module === (row.behavior ?? ""))
            if (behavior === undefined)
                return { icon: "triangle-alert", text: qsTr("Missing extension component"),
                         hint: qsTr("Its extension is not installed or is off") }
            return { icon: "puzzle", text: behavior.title,
                     hint: behavior.description !== "" ? behavior.description : behavior.extension }
        }
        const item = libraryItem(row.type)
        return item !== null ? { icon: item.icon, text: item.text, hint: firstSentence(item.tip) }
                             : { icon: "square", text: kindOfRow(row), hint: "" }
    }

    function iconOfRow(row: var): string {
        return rowInfo(row).icon
    }

    function openComponent(ref: string) {
        const problem = DeviceManager.openComponent(ref)
        if (problem !== "")
            keymapEditor.notify(problem, "triangle-alert")
    }

    function newComponent() {
        newComponentDialog.start()
    }

    function kindOfRow(row: var): string {
        switch (row.type) {
        case "tap":
            return qsTr("Tap component")
        case "hold":
            return qsTr("Hold component")
        case "longPress":
            return qsTr("Long press component")
        case "turbo":
            return qsTr("Turbo component")
        case "wasd":
            return qsTr("WASD stick component")
        case "rightWalk":
            return qsTr("Right-click walk component")
        case "padStick":
            return qsTr("Gamepad stick component")
        case "camera":
            return qsTr("View component")
        case "padCamera":
            return qsTr("Gamepad view component")
        case "freeLook":
            return qsTr("Free look component")
        case "skill":
            return qsTr("Skill cast component")
        case "swipe":
            return qsTr("Swipe component")
        case "pinch":
            return qsTr("Pinch component")
        case "dial":
            return qsTr("Direction wheel component")
        case "multiPress":
            return qsTr("Multi-press component")
        case "tempCursor":
            return qsTr("Temporary cursor component")
        case "actionWheel":
            return qsTr("Action wheel component")
        case "behavior":
            return qsTr("Extension component")
        }
        return ""
    }

    function isStick(type: string): bool {
        return type === "wasd" || type === "rightWalk" || type === "padStick"
    }

    function isRing(type: string): bool {
        return isStick(type) || type === "skill" || type === "dial"
    }

    function fitted(side: real, area: rect): rect {
        const aspect = portrait ? 9 / side : side / 9
        const width = Math.min(area.width, area.height * aspect)
        const height = width / aspect
        return Qt.rect(area.x + (area.width - width) / 2, area.y + (area.height - height) / 2, width, height)
    }

    function insideCanvas(at: point): bool {
        const place = canvas.mapToItem(viewport, at.x, at.y)
        return at.x >= 0 && at.y >= 0 && at.x <= canvas.width && at.y <= canvas.height
               && place.x >= 0 && place.y >= 0 && place.x <= viewport.width && place.y <= viewport.height
    }

    function visibleCenter(): point {
        const left = Math.max(0, -canvas.x)
        const top = Math.max(0, -canvas.y)
        const right = Math.min(canvas.width, viewport.width - canvas.x)
        const bottom = Math.min(canvas.height, viewport.height - canvas.y)
        return Qt.point((left + right) / 2, (top + bottom) / 2)
    }

    function focusCanvas() {
        if (trying)
            DeviceManager.focusPicture(serial)
        else
            canvas.forceActiveFocus()
    }

    function showView(logic: bool) {
        if (keymapEditor.editor === null || !keymapEditor.editor.showLogic(logic))
            return
        if (!logic)
            keymapEditor.focusCanvas()
    }

    function referenceInLogic(id: string) {
        if (keymapEditor.editor === null || !keymapEditor.editor.showLogic(true))
            return
        const logic = logicLoader.item as LogicView
        if (logic !== null)
            logic.reference(id)
    }

    function pasteKeys() {
        if (keymapEditor.editor === null || !keymapEditor.editor.canPaste())
            return
        const pointer = canvasMouse.containsMouse && canvas.width > 0 && canvas.height > 0
        const x = pointer ? canvasMouse.mouseX / canvas.width : 0.5
        const y = pointer ? canvasMouse.mouseY / canvas.height : 0.5
        keymapEditor.editor.pasteAt(x, y)
    }

    function setFrozen(on: bool) {
        frozen = on
        DeviceManager.setPictureFrozen(serial, on)
    }

    function zoomTo(next: real, at: point) {
        const clamped = Math.max(zoomSteps[0], Math.min(next, zoomSteps[zoomSteps.length - 1]))
        if (clamped === zoom)
            return
        zoom = clamped
        DeviceManager.zoomPicture(serial, clamped, at.x, at.y)
    }

    function zoomStep(direction: int, at: point) {
        const next = direction > 0 ? zoomSteps.find(step => step > zoom)
                                   : zoomSteps.slice().reverse().find(step => step < zoom)
        if (next !== undefined)
            zoomTo(next, at)
    }

    function copyComponent(item: var) {
        copyToMine.ask("component", item.ref, item.text)
    }

    function dropComponent(item: var, at: point) {
        if (!arranging || item.kind === "new" || (item.extensionId ?? "") !== "" || !insideCanvas(at))
            return
        editor.beginGesture()
        const added = editor.addComponent(item.kind, at.x / canvas.width, at.y / canvas.height)
        if (added.id === undefined) {
            editor.endGesture()
            return
        }
        if (added.slot !== undefined && added.slot !== "") {
            capture.beginControl(added.id, added.slot, item.text, at)
            return
        }
        editor.endGesture()
        canvas.forceActiveFocus()
    }

    onVisibleChanged: {
        if (visible) {
            canvas.forceActiveFocus()
        } else {
            capture.end()
            ghost.item = null
            saveAsDialog.close()
            restoreDialog.close()
            newComponentDialog.close()
        }
    }

    onEditorChanged: {
        frozen = false
        snapping = true
        showingNames = true
        ratioIndex = 0
        zoom = 1
        wheelTurn = 0
        if (keymapEditor.editor === null)
            Qt.callLater(tour.cancel)
    }

    onTryingChanged: {
        if (trying) {
            capture.end()
            ghost.item = null
            frozen = false
            ratioIndex = 0
            Qt.callLater(tour.cancel)
        } else {
            canvas.forceActiveFocus()
        }
    }

    NewComponentDialog {
        id: newComponentDialog

        bases: keymapEditor.library.reduce((all, group) => all.concat(group.items), [])
        onCreated: ref => keymapEditor.openComponent(ref)
    }

    ExportPackageDialog {
        id: exportPackageDialog

        onFinished: (ok, message) => keymapEditor.notify(message, ok ? "puzzle" : "triangle-alert")
    }

    CopyToMineDialog {
        id: copyToMine

        onConfirmed: (kind, source, sourceName) => {
            if (ComponentLibrary.copyToMine(source) !== "")
                keymapEditor.notify(qsTr("Copied %1 to your components").arg(sourceName), "copy")
            else
                keymapEditor.notify(qsTr("%1 could not be copied").arg(sourceName), "triangle-alert")
        }
    }

    Connections {
        target: keymapEditor.editor
        enabled: keymapEditor.editor !== null

        function onDeleteAsked(uses) {
            deleteDialog.uses = uses
            deleteDialog.open()
        }
    }

    Dialog {
        id: deleteDialog

        objectName: "deleteUsedDialog"
        property int uses: 0
        title: deleteDialog.uses === 1 ? qsTr("Logic uses it in 1 place") : qsTr("Logic uses it in %1 places").arg(deleteDialog.uses)
        acceptText: qsTr("Delete those blocks too")
        alternativeText: qsTr("Delete only the component")
        rejectText: qsTr("Cancel")
        danger: true
        onAccepted: {
            if (keymapEditor.editor !== null)
                keymapEditor.editor.deleteSelection(true)
        }
        onAlternativeChosen: {
            if (keymapEditor.editor !== null)
                keymapEditor.editor.deleteSelection(false)
        }

        Text {
            width: parent.width
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("The blocks that use it stop working. Deleting them takes the whole statements out of the logic.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }
    }

    component ComponentTile: Rectangle {
        id: tile

        required property var modelData
        readonly property bool fresh: modelData.kind === "new"
        readonly property bool copyOnly: (modelData.extensionId ?? "") !== ""
        readonly property bool hot: tileMouse.containsMouse || tileMouse.pressed || activeFocus
        readonly property color foreground: hot ? Theme.colors.textPrimary : Theme.colors.textSecondary

        function activate() {
            if (tile.fresh)
                keymapEditor.newComponent()
            else if (tile.copyOnly)
                keymapEditor.copyComponent(tile.modelData)
            else
                keymapEditor.dropComponent(tile.modelData, keymapEditor.visibleCenter())
        }

        objectName: "componentTile"
        width: (componentList.width - keymapEditor.tileGap) / 2
        height: Theme.size.componentRow
        radius: Theme.radius.lg
        color: fresh ? (hot ? Theme.colors.stateHover : "transparent") : hot ? Theme.colors.stateSelected : Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: hot ? Theme.colors.strokeStrong : fresh ? Theme.colors.strokeDefault : Theme.colors.strokeSubtle
        enabled: keymapEditor.arranging
        opacity: enabled ? 1 : Theme.disabledOpacity
        activeFocusOnTab: true

        Accessible.role: Accessible.Button
        Accessible.name: modelData.text
        Accessible.description: modelData.hint !== "" ? modelData.hint : modelData.tip
        Accessible.onPressAction: tile.activate()

        Keys.onReturnPressed: tile.activate()
        Keys.onSpacePressed: tile.activate()

        FocusRing {
            visible: tile.activeFocus
            baseRadius: tile.radius
        }

        Icon {
            id: tileIcon

            x: Theme.space.sm
            anchors.verticalCenter: parent.verticalCenter
            visible: !tile.fresh
            name: tile.modelData.icon
            size: Theme.size.iconLarge
            color: tile.foreground
        }

        Text {
            objectName: "componentName"
            x: tileIcon.x + tileIcon.width + Theme.space.sm
            width: tile.width - x - Theme.space.sm
            anchors.verticalCenter: parent.verticalCenter
            visible: !tile.fresh
            textFormat: Text.PlainText
            text: tile.modelData.text
            elide: Text.ElideRight
            color: tile.hot ? Theme.colors.textPrimary : Theme.colors.textSecondary
            font.pixelSize: Theme.type.label.size
            font.weight: Theme.type.label.weight
        }

        Row {
            anchors.centerIn: parent
            visible: tile.fresh
            spacing: Theme.space.xs

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: tile.modelData.icon
                size: Theme.size.iconSmall
                color: tile.foreground
            }

            Text {
                objectName: "newComponent"
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: tile.modelData.text
                color: tile.foreground
                font.pixelSize: Theme.type.label.size
                font.weight: Theme.type.label.weight
            }
        }

        Icon {
            objectName: "componentBadge"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.space.xs + Theme.space.xxs
            visible: (tile.modelData.badge ?? "") !== ""
            name: tile.modelData.badge ?? ""
            size: Theme.size.componentBadge
            color: Theme.toneInk("teal")
        }

        Tooltip {
            objectName: "componentTip"
            text: tile.modelData.text
            detail: (tile.modelData.tip ?? "") !== "" ? tile.modelData.tip : (tile.modelData.hint ?? "")
            visible: tileMouse.containsMouse && !tileMouse.pressed && detail !== ""
        }

        MouseArea {
            id: tileMouse

            property point pressAt
            property bool dragging: false

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: tile.fresh || tile.copyOnly ? Qt.PointingHandCursor
                                                     : dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor

            onPressed: mouse => {
                capture.end()
                pressAt = Qt.point(mouse.x, mouse.y)
                dragging = false
            }
            onPositionChanged: mouse => {
                if (!pressed || tile.fresh || tile.copyOnly)
                    return
                if (!dragging && Math.hypot(mouse.x - pressAt.x, mouse.y - pressAt.y)
                        < Application.styleHints.startDragDistance)
                    return
                dragging = true
                ghost.item = tile.modelData
                ghost.at = mapToItem(keymapEditor, mouse.x, mouse.y)
            }
            onReleased: mouse => {
                const at = mapToItem(canvas, mouse.x, mouse.y)
                const wasDragging = dragging
                dragging = false
                ghost.item = null
                if (wasDragging)
                    keymapEditor.dropComponent(tile.modelData, at)
                else if (tile.fresh && containsMouse)
                    keymapEditor.newComponent()
            }
            onCanceled: {
                dragging = false
                ghost.item = null
            }

            TapHandler {
                onSingleTapped: {
                    if (tile.copyOnly)
                        keymapEditor.copyComponent(tile.modelData)
                }
                onDoubleTapped: {
                    if ((tile.modelData.ref ?? "") !== "")
                        keymapEditor.openComponent(tile.modelData.ref)
                }
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    if ((tile.modelData.ref ?? "").startsWith("mine/") || tile.copyOnly)
                        tileMenu.popup()
                }
            }
        }

        Menu {
            id: tileMenu

            MenuItem {
                objectName: "copyComponentToMine"
                visible: tile.copyOnly
                height: visible ? implicitHeight : 0
                iconName: "copy"
                text: qsTr("Copy to my components")
                onTriggered: keymapEditor.copyComponent(tile.modelData)
            }

            MenuItem {
                objectName: "exportComponentPackage"
                visible: !tile.copyOnly
                height: visible ? implicitHeight : 0
                iconName: "puzzle"
                text: qsTr("Export as extension package…")
                onTriggered: exportPackageDialog.start("component", tile.modelData.ref, tile.modelData.text)
            }
        }
    }

    component Handle: Rectangle {
        id: handle

        property real atX
        property real atY
        property int cursor: Qt.SizeAllCursor

        signal dragged(point at)

        x: canvas.x + atX - width / 2
        y: canvas.y + atY - height / 2
        width: Theme.space.md
        height: width
        radius: width / 2
        color: Theme.colors.bgElevated
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accent

        MouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.space.xs
            cursorShape: handle.cursor
            preventStealing: true
            onPressed: keymapEditor.editor.beginGesture()
            onPositionChanged: mouse => {
                if (pressed)
                    handle.dragged(mapToItem(canvas, mouse.x, mouse.y))
            }
            onReleased: keymapEditor.editor.endGesture()
            onCanceled: keymapEditor.editor.endGesture()
        }
    }

    Item {
        id: viewport

        objectName: "viewport"
        visible: !keymapEditor.logicShown
        x: keymapEditor.view.x
        y: keymapEditor.view.y
        width: keymapEditor.view.width
        height: keymapEditor.view.height
        clip: true

        MouseArea {
            objectName: "wheelArea"
            anchors.fill: parent
            enabled: !keymapEditor.trying
            acceptedButtons: Qt.NoButton
            onWheel: wheel => {
                if (wheel.modifiers & Qt.ControlModifier) {
                    keymapEditor.wheelTurn += wheel.angleDelta.y
                    const turns = Math.trunc(keymapEditor.wheelTurn / 120)
                    if (turns !== 0) {
                        keymapEditor.wheelTurn -= 120 * turns
                        keymapEditor.zoomStep(turns, mapToItem(keymapEditor, wheel.x, wheel.y))
                    }
                    return
                }
                if (!keymapEditor.scrollable) {
                    wheel.accepted = false
                    return
                }
                const precise = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
                const across = precise ? wheel.pixelDelta.x : wheel.angleDelta.x * Theme.space.x6l / 120
                const down = precise ? wheel.pixelDelta.y : wheel.angleDelta.y * Theme.space.x6l / 120
                const sideways = (wheel.modifiers & Qt.ShiftModifier) !== 0 && across === 0
                DeviceManager.scrollPicture(keymapEditor.serial, sideways ? -down : -across, sideways ? 0 : -down)
            }
        }

        Rectangle {
            objectName: "noPictureStage"
            visible: keymapEditor.noPicture
            x: keymapEditor.shown.x
            y: keymapEditor.shown.y
            width: keymapEditor.shown.width
            height: keymapEditor.shown.height
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeStrong

            Image {
                id: backdropImage

                objectName: "keymapBackground"
                anchors.fill: parent
                anchors.margins: Theme.size.stroke
                visible: keymapEditor.backdropUrl !== ""
                source: keymapEditor.backdropUrl
                cache: false
                asynchronous: true
                fillMode: Image.Stretch
            }

            Text {
                objectName: "noBackground"
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space.xl
                visible: keymapEditor.backdropUrl === "" || backdropImage.status === Image.Error
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.PlainText
                text: qsTr("No picture. Import a screenshot of the game, or take one on the phone, to place the keys "
                           + "on it.")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }

        Rectangle {
            objectName: "canvasDim"
            visible: !keymapEditor.trying
            x: keymapEditor.shown.x
            y: keymapEditor.shown.y
            width: keymapEditor.shown.width
            height: keymapEditor.shown.height
            color: Theme.colors.canvasDim
        }

        Rectangle {
            objectName: "previewFrame"
            visible: keymapEditor.previewing
            x: keymapEditor.stage.x - border.width
            y: keymapEditor.stage.y - border.width
            width: keymapEditor.stage.width + 2 * border.width
            height: keymapEditor.stage.height + 2 * border.width
            color: "transparent"
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accent
        }

        Shape {
            id: swipePath

            readonly property var points: keymapEditor.current !== null && keymapEditor.current.path !== undefined
                                          ? keymapEditor.current.path : []
            readonly property var arrow: {
                if (points.length < 2)
                    return []
                const end = points[points.length - 1]
                const before = points[points.length - 2]
                const angle = Math.atan2(end.y - before.y, end.x - before.x)
                const back = Theme.space.md / 2 + Theme.space.xxs
                const tipX = end.x - back * Math.cos(angle)
                const tipY = end.y - back * Math.sin(angle)
                const wing = Theme.space.sm
                return [Qt.point(tipX - wing * Math.cos(angle - Math.PI / 6), tipY - wing * Math.sin(angle - Math.PI / 6)),
                        Qt.point(tipX, tipY),
                        Qt.point(tipX - wing * Math.cos(angle + Math.PI / 6), tipY - wing * Math.sin(angle + Math.PI / 6))]
            }

            objectName: "swipePath"
            visible: points.length >= 2 && !keymapEditor.trying
            x: keymapEditor.stage.x
            y: keymapEditor.stage.y

            ShapePath {
                strokeColor: Theme.colors.accent
                strokeWidth: Theme.size.focusRing
                fillColor: "transparent"

                PathPolyline {
                    path: swipePath.points.map(point => Qt.point(point.x, point.y))
                }
            }

            ShapePath {
                strokeColor: Theme.colors.accent
                strokeWidth: Theme.size.focusRing
                fillColor: "transparent"

                PathPolyline {
                    path: swipePath.arrow
                }
            }
        }

        KeyHintLayer {
            id: keyHints

            anchors.fill: parent
            hints: keymapEditor.markers
            picture: keymapEditor.stage
            shown: !keymapEditor.trying || AppSettings.values["keymap.hints"] !== false
            marks: !keymapEditor.trying
            names: keymapEditor.showingNames
            restingOpacity: keymapEditor.trying ? (AppSettings.values["keymap.hintOpacity"] ?? 60) / 100 : 1
            intro: false
        }

        Repeater {
            model: keymapEditor.markers

            delegate: Rectangle {
                required property var modelData
                readonly property real reach: Math.max(modelData.rx, modelData.ry, Theme.size.keyHint / 2)
                                              + Theme.space.xs

                objectName: "selection"
                visible: modelData.selected && !keymapEditor.trying && keymapEditor.isRing(modelData.type)
                x: keymapEditor.stage.x + modelData.x - reach
                y: keymapEditor.stage.y + modelData.y - reach
                width: 2 * reach
                height: width
                radius: width / 2
                color: "transparent"
                border.width: Theme.size.focusRing
                border.color: Theme.colors.accent
            }
        }

        Shape {
            id: cameraArea

            readonly property var marker: keymapEditor.current !== null && keymapEditor.current.type === "camera"
                                          ? keymapEditor.current : null

            objectName: "cameraArea"
            visible: marker !== null
            x: keymapEditor.stage.x + (marker !== null ? marker.x - marker.rx : 0)
            y: keymapEditor.stage.y + (marker !== null ? marker.y - marker.ry : 0)
            width: marker !== null ? 2 * marker.rx : 0
            height: marker !== null ? 2 * marker.ry : 0

            ShapePath {
                strokeColor: Theme.colors.accent
                strokeWidth: Theme.size.stroke
                strokeStyle: ShapePath.DashLine
                fillColor: "transparent"
                startX: 0
                startY: 0

                PathLine { x: cameraArea.width; y: 0 }
                PathLine { x: cameraArea.width; y: cameraArea.height }
                PathLine { x: 0; y: cameraArea.height }
                PathLine { x: 0; y: 0 }
            }
        }

        Item {
            id: canvas

            property point pressAt
            property point startCenter
            property point applied
            property string pressedId
            property bool dragging: false
            property bool banding: false
            property bool adding: false
            property real guideX: -1
            property real guideY: -1
            property rect band

            function markerAt(px, py) {
                const margin = Theme.space.xs
                let wide = null
                for (let i = keymapEditor.markers.length - 1; i >= 0; --i) {
                    const marker = keymapEditor.markers[i]
                    if (keymapEditor.isRing(marker.type)) {
                        const distance = Math.hypot(px - marker.x, py - marker.y)
                        if (distance <= Theme.size.keyHint / 2 + margin)
                            return marker
                        if (wide === null && distance <= Math.max(marker.rx, Theme.size.keyHint / 2) + margin)
                            wide = marker
                        continue
                    }
                    const frame = keyHints.frameOf(i)
                    const fx = px + keymapEditor.stage.x
                    const fy = py + keymapEditor.stage.y
                    if (fx >= frame.x - margin && fx <= frame.x + frame.width + margin
                            && fy >= frame.y - margin && fy <= frame.y + frame.height + margin)
                        return marker
                }
                return wide
            }

            function replaceKeyAt(px: real, py: real): bool {
                const key = keyHints.keyAt(px + keymapEditor.stage.x, py + keymapEditor.stage.y)
                if (key === null || key.slot === "")
                    return false
                const marker = keymapEditor.markers[key.index]
                capture.beginReplace(marker.id, key.slot, key.text, marker.name, Qt.point(px, py))
                return true
            }

            function snapped(value, others) {
                let best = -1
                for (const candidate of others) {
                    if (Math.abs(candidate - value) <= keymapEditor.snapDistance
                            && (best < 0 || Math.abs(candidate - value) < Math.abs(best - value)))
                        best = candidate
                }
                return best
            }

            function drag(px, py, free) {
                let targetX = startCenter.x + px - pressAt.x
                let targetY = startCenter.y + py - pressAt.y
                guideX = -1
                guideY = -1
                if (!free && keymapEditor.snapping) {
                    const chosen = keymapEditor.editor.selection
                    const others = keymapEditor.markers.filter(marker => !marker.selected
                                                               && chosen.indexOf(marker.parent ?? "") < 0)
                    const snapX = snapped(targetX, others.map(marker => marker.x).concat([width / 2]))
                    const snapY = snapped(targetY, others.map(marker => marker.y).concat([height / 2]))
                    if (snapX >= 0) {
                        targetX = snapX
                        guideX = snapX
                    }
                    if (snapY >= 0) {
                        targetY = snapY
                        guideY = snapY
                    }
                }
                const moveX = targetX - startCenter.x - applied.x
                const moveY = targetY - startCenter.y - applied.y
                if (moveX !== 0 || moveY !== 0) {
                    keymapEditor.editor.moveSelection(moveX / width, moveY / height)
                    applied = Qt.point(applied.x + moveX, applied.y + moveY)
                }
            }

            function finishBand() {
                const inside = keymapEditor.markers.filter(marker => marker.x >= band.x && marker.x <= band.x + band.width
                                                                 && marker.y >= band.y && marker.y <= band.y + band.height)
                                              .map(marker => marker.id)
                const chosen = adding ? Array.from(keymapEditor.editor.selection).concat(inside) : inside
                keymapEditor.editor.select(chosen)
            }

            objectName: "canvas"
            x: keymapEditor.stage.x
            y: keymapEditor.stage.y
            width: keymapEditor.stage.width
            height: keymapEditor.stage.height
            enabled: keymapEditor.arranging
            focus: true

            Keys.onPressed: event => {
                if (event.matches(StandardKey.Copy)) {
                    keymapEditor.editor.copySelection()
                    event.accepted = true
                    return
                }
                if (event.matches(StandardKey.Paste)) {
                    keymapEditor.pasteKeys()
                    event.accepted = true
                    return
                }
                const step = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
                let dx = 0
                let dy = 0
                if (event.key === Qt.Key_Left)
                    dx = -step
                else if (event.key === Qt.Key_Right)
                    dx = step
                else if (event.key === Qt.Key_Up)
                    dy = -step
                else if (event.key === Qt.Key_Down)
                    dy = step
                else if (event.key === Qt.Key_Escape) {
                    keymapEditor.editor.select([])
                    event.accepted = true
                    return
                } else {
                    return
                }
                keymapEditor.editor.moveSelection(dx / width, dy / height)
                event.accepted = true
            }

            MouseArea {
                id: canvasMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                preventStealing: true
                cursorShape: canvas.dragging ? Qt.ClosedHandCursor
                           : containsMouse && canvas.markerAt(mouseX, mouseY) !== null ? Qt.OpenHandCursor
                           : Qt.ArrowCursor

                onPressed: mouse => {
                    canvas.forceActiveFocus()
                    const hit = canvas.markerAt(mouse.x, mouse.y)
                    canvas.pressAt = Qt.point(mouse.x, mouse.y)
                    canvas.applied = Qt.point(0, 0)
                    canvas.dragging = false
                    canvas.banding = false
                    canvas.adding = (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
                    if (hit === null) {
                        canvas.pressedId = ""
                        canvas.band = Qt.rect(mouse.x, mouse.y, 0, 0)
                        return
                    }
                    if (canvas.adding) {
                        keymapEditor.editor.toggleSelected(hit.id)
                        canvas.pressedId = ""
                        canvas.band = Qt.rect(mouse.x, mouse.y, 0, 0)
                        return
                    }
                    if (!hit.selected)
                        keymapEditor.editor.select([hit.id])
                    canvas.pressedId = hit.id
                    canvas.startCenter = Qt.point(hit.x, hit.y)
                }

                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    const far = Math.hypot(mouse.x - canvas.pressAt.x, mouse.y - canvas.pressAt.y)
                                >= Application.styleHints.startDragDistance
                    if (canvas.pressedId !== "") {
                        if (!canvas.dragging && !far)
                            return
                        if (!canvas.dragging) {
                            canvas.dragging = true
                            keymapEditor.editor.beginGesture()
                        }
                        canvas.drag(mouse.x, mouse.y, (mouse.modifiers & Qt.AltModifier) !== 0)
                        return
                    }
                    if (!canvas.banding && !far)
                        return
                    canvas.banding = true
                    canvas.band = Qt.rect(Math.min(canvas.pressAt.x, mouse.x), Math.min(canvas.pressAt.y, mouse.y),
                                          Math.abs(mouse.x - canvas.pressAt.x), Math.abs(mouse.y - canvas.pressAt.y))
                }

                onReleased: {
                    if (canvas.dragging)
                        keymapEditor.editor.endGesture()
                    else if (canvas.banding)
                        canvas.finishBand()
                    else if (canvas.pressedId === "" && !canvas.adding)
                        keymapEditor.editor.select([])
                    canvas.dragging = false
                    canvas.banding = false
                    canvas.pressedId = ""
                    canvas.guideX = -1
                    canvas.guideY = -1
                }

                onCanceled: {
                    if (canvas.dragging)
                        keymapEditor.editor.endGesture()
                    canvas.dragging = false
                    canvas.banding = false
                    canvas.pressedId = ""
                    canvas.guideX = -1
                    canvas.guideY = -1
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onDoubleTapped: eventPoint => canvas.replaceKeyAt(eventPoint.position.x, eventPoint.position.y)
                }
            }

            Rectangle {
                objectName: "guideX"
                visible: canvas.guideX >= 0
                x: canvas.guideX
                width: Theme.size.stroke
                height: parent.height
                color: Theme.colors.accent
            }

            Rectangle {
                objectName: "guideY"
                visible: canvas.guideY >= 0
                y: canvas.guideY
                width: parent.width
                height: Theme.size.stroke
                color: Theme.colors.accent
            }

            Rectangle {
                objectName: "band"
                visible: canvas.banding
                x: canvas.band.x
                y: canvas.band.y
                width: canvas.band.width
                height: canvas.band.height
                color: Theme.colors.accentSubtle
                border.width: Theme.size.stroke
                border.color: Theme.colors.accent
            }
        }

        Handle {
            objectName: "radiusHandle"
            visible: keymapEditor.arranging && keymapEditor.current !== null
                     && keymapEditor.isRing(keymapEditor.current.type)
            atX: visible ? keymapEditor.current.x + Math.max(keymapEditor.current.rx, keymapEditor.handleMinimum) * Math.SQRT1_2 : 0
            atY: visible ? keymapEditor.current.y + Math.max(keymapEditor.current.ry, keymapEditor.handleMinimum) * Math.SQRT1_2 : 0
            cursor: Qt.SizeFDiagCursor
            onDragged: at => {
                const marker = keymapEditor.current
                const radius = Math.max(keymapEditor.handleMinimum, Math.hypot(at.x - marker.x, at.y - marker.y))
                keymapEditor.editor.setLength(marker.id, "radius", radius / canvas.width)
            }
        }

        Repeater {
            model: [{ side: 1, horizontal: true }, { side: -1, horizontal: true },
                    { side: 1, horizontal: false }, { side: -1, horizontal: false }]

            delegate: Handle {
                required property var modelData

                objectName: "areaHandle"
                visible: keymapEditor.arranging && keymapEditor.current !== null && keymapEditor.current.type === "camera"
                atX: visible ? keymapEditor.current.x + (modelData.horizontal ? modelData.side * keymapEditor.current.rx : 0) : 0
                atY: visible ? keymapEditor.current.y + (modelData.horizontal ? 0 : modelData.side * keymapEditor.current.ry) : 0
                cursor: modelData.horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
                onDragged: at => {
                    const marker = keymapEditor.current
                    const halfWidth = modelData.horizontal ? Math.max(keymapEditor.handleMinimum, Math.abs(at.x - marker.x))
                                                           : marker.rx
                    const halfHeight = modelData.horizontal ? marker.ry
                                                            : Math.max(keymapEditor.handleMinimum, Math.abs(at.y - marker.y))
                    keymapEditor.editor.setArea(marker.id, "size", 2 * halfWidth / canvas.width, 2 * halfHeight / canvas.height)
                }
            }
        }

        Handle {
            objectName: "castHandle"
            visible: keymapEditor.arranging && keymapEditor.current !== null && keymapEditor.current.castRx !== undefined
            atX: visible ? keymapEditor.current.castX + keymapEditor.current.castRx * Math.SQRT1_2 : 0
            atY: visible ? keymapEditor.current.castY + keymapEditor.current.castRy * Math.SQRT1_2 : 0
            cursor: Qt.SizeFDiagCursor
            onDragged: at => {
                const marker = keymapEditor.current
                const radius = Math.max(keymapEditor.handleMinimum, Math.hypot(at.x - marker.castX, at.y - marker.castY))
                keymapEditor.editor.setLength(marker.id, "castRadius", radius / canvas.width)
            }
        }

        Handle {
            objectName: "sprintHandle"
            visible: keymapEditor.arranging && keymapEditor.current !== null && keymapEditor.current.sprint !== undefined
                     && keymapEditor.current.rx > 0
            atX: visible ? keymapEditor.current.x : 0
            atY: visible ? keymapEditor.current.y - keymapEditor.current.ry * keymapEditor.current.sprint : 0
            cursor: Qt.SizeVerCursor
            onDragged: at => {
                const marker = keymapEditor.current
                const reach = Math.hypot((at.x - marker.x) / marker.rx, (at.y - marker.y) / marker.ry)
                keymapEditor.editor.setValue(marker.id, "sprintScale",
                                             Math.round(Math.min(3, Math.max(1, reach)) * 20) / 20)
            }
        }

        Handle {
            objectName: "heroHandle"
            visible: keymapEditor.arranging && keymapEditor.current !== null && keymapEditor.current.castRx !== undefined
            atX: visible ? keymapEditor.current.castX : 0
            atY: visible ? keymapEditor.current.castY : 0
            onDragged: at => keymapEditor.editor.setPoint(keymapEditor.current.id, "hero", at.x / canvas.width,
                                                         at.y / canvas.height)
        }

        Repeater {
            model: swipePath.points.length

            delegate: Handle {
                required property int index
                readonly property var point: index < swipePath.points.length ? swipePath.points[index] : null

                objectName: "pathHandle"
                visible: keymapEditor.arranging && point !== null && keymapEditor.current !== null
                         && Math.hypot(point.x - keymapEditor.current.x, point.y - keymapEditor.current.y)
                            > Theme.size.keyHint / 2
                atX: point !== null ? point.x : 0
                atY: point !== null ? point.y : 0
                onDragged: at => keymapEditor.editor.setPathPoint(keymapEditor.current.id, index, at.x / canvas.width,
                                                                 at.y / canvas.height)
            }
        }

        Repeater {
            model: keymapEditor.placedPoints.length

            delegate: Handle {
                required property int index
                readonly property var point: index < keymapEditor.placedPoints.length ? keymapEditor.placedPoints[index]
                                                                                      : null

                objectName: "pointHandle"
                visible: keymapEditor.arranging && point !== null && keymapEditor.current !== null
                atX: point !== null ? point.x : 0
                atY: point !== null ? point.y : 0
                onDragged: at => keymapEditor.editor.setPlacedPoint(keymapEditor.current.id, point.name,
                                                                   at.x / canvas.width, at.y / canvas.height)
            }
        }

        KeymapDraftLayer {
            anchors.fill: parent
            editor: keymapEditor.editor
            stage: keymapEditor.stage
            active: keymapEditor.arranging
        }
    }

    Workbench {
        id: bench

        y: keymapEditor.barTop
        width: parent.width
        height: Math.max(0, parent.height - keymapEditor.barTop)
        bodyShown: !keymapEditor.logicShown

        barContent: [
            Row {
                id: previewBar

                objectName: "draftPreviewBar"
                visible: keymapEditor.readOnly
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.sm

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "bot"
                    size: Theme.size.iconSmall
                    color: Theme.colors.accentText
                }

                Text {
                    objectName: "draftPreviewNote"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, Math.max(0, modeSwitch.x - previewBar.x - Theme.size.iconSmall
                                                               - 2 * previewBar.spacing - Theme.space.sm))
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    text: qsTr("This is %1's draft, not in effect yet")
                              .arg(keymapEditor.editor !== null ? keymapEditor.editor.previewClient : "")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.DemiBold
                }

                StatusPill {
                    objectName: "draftPreviewPill"
                    anchors.verticalCenter: parent.verticalCenter
                    tone: "warn"
                    text: qsTr("Draft")
                }
            },
            Row {
                id: previewButtons

                objectName: "draftPreviewButtons"
                visible: keymapEditor.readOnly
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xs

                Button {
                    objectName: "draftApply"
                    small: true
                    iconName: "check"
                    text: qsTr("Apply")
                    tone: "primary"
                    onClicked: DeviceManager.applyDraftPreview(keymapEditor.serial)
                }

                Button {
                    objectName: "draftDiscard"
                    small: true
                    iconName: "trash-2"
                    text: qsTr("Discard")
                    tone: "danger"
                    onClicked: DeviceManager.discardDraftPreview(keymapEditor.serial)
                }

                Button {
                    objectName: "draftClose"
                    small: true
                    iconName: "x"
                    text: qsTr("Close")
                    tone: "ghost"
                    onClicked: DeviceManager.closeDraftPreview(keymapEditor.serial)
                }
            },
            Row {
                id: titleRow

                visible: !keymapEditor.readOnly

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.md

                Text {
                    objectName: "profileName"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, Math.max(0, modeSwitch.x - Theme.space.lg - saveState.width
                                                            - (conflicts.visible ? conflicts.width + titleRow.spacing : 0)
                                                            - titleRow.spacing))
                    textFormat: Text.PlainText
                    text: keymapEditor.editor !== null ? keymapEditor.editor.profileName : ""
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.DemiBold
                }

                Text {
                    id: saveState

                    objectName: "saveState"
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: keymapEditor.unsaved ? qsTr("Not saved") : qsTr("Saved")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                StatusPill {
                    id: conflicts

                    objectName: "conflicts"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: keymapEditor.editor !== null && keymapEditor.editor.conflictCount > 0
                    tone: "bad"
                    text: qsTr("%n conflict(s)", "", keymapEditor.editor !== null ? keymapEditor.editor.conflictCount : 0)

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: {
                            keymapEditor.showView(false)
                            keymapEditor.editor.selectConflicts()
                        }
                    }
                }
            },
            SegmentedControl {
                id: modeSwitch

                objectName: "modeSwitch"
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(0, Math.min((parent.width - width) / 2, actionsRow.x - width - Theme.space.lg))
                model: [{ text: qsTr("Picture"), icon: "image" }, { text: qsTr("Logic"), icon: "workflow" }]
                currentIndex: keymapEditor.logicShown ? 1 : 0
                Accessible.name: qsTr("Edit the picture or the logic")
                onActivated: index => {
                    keymapEditor.showView(index === 1)
                    modeSwitch.currentIndex = Qt.binding(() => keymapEditor.logicShown ? 1 : 0)
                }
            },
            Row {
                id: actionsRow

                visible: !keymapEditor.readOnly

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xs

                ToolbarButton {
                    objectName: "debug"
                    visible: keymapEditor.logicShown && keymapEditor.logicBench !== null
                    iconName: "activity"
                    text: keymapEditor.logicBench !== null && keymapEditor.logicBench.debugging ? qsTr("Stop debugging")
                                                                                              : qsTr("Debug")
                    selected: keymapEditor.logicBench !== null && keymapEditor.logicBench.debugging
                    onClicked: keymapEditor.logicBench.debug(!keymapEditor.logicBench.debugging)
                }

                ToolbarButton {
                    objectName: "undo"
                    iconName: "undo-2"
                    text: qsTr("Undo")
                    shortcut: "Ctrl+Z"
                    enabled: keymapEditor.logicShown ? keymapEditor.logicView !== null && keymapEditor.logicView.canUndo
                                                     : keymapEditor.editor !== null && keymapEditor.editor.canUndo
                    onClicked: {
                        if (keymapEditor.logicShown)
                            keymapEditor.logicView.undo()
                        else
                            keymapEditor.editor.undo()
                    }
                }

                ToolbarButton {
                    objectName: "redo"
                    iconName: "redo-2"
                    text: qsTr("Redo")
                    shortcut: "Ctrl+Y"
                    enabled: keymapEditor.logicShown ? keymapEditor.logicView !== null && keymapEditor.logicView.canRedo
                                                     : keymapEditor.editor !== null && keymapEditor.editor.canRedo
                    onClicked: {
                        if (keymapEditor.logicShown)
                            keymapEditor.logicView.redo()
                        else
                            keymapEditor.editor.redo()
                    }
                }

                ToolbarButton {
                    objectName: "tryToggle"
                    iconName: "flask-conical"
                    text: keymapEditor.trying ? qsTr("Back to editing") : qsTr("Try the keys on the phone")
                    selected: keymapEditor.trying
                    enabled: keymapEditor.editor !== null
                    onClicked: keymapEditor.editor.setTrying(!keymapEditor.trying)
                }

                ToolbarButton {
                    id: moreButton

                    readonly property var entry: {
                        const mirror = DeviceManager.mirrors.find(each => each.serial === keymapEditor.serial)
                        const id = mirror !== undefined ? mirror.editingId ?? "" : ""
                        return id === "" ? null : ProfileLibrary.entries.find(each => each.id === id) ?? null
                    }
                    readonly property bool patched: entry !== null && entry.userPatch === true

                    iconName: "ellipsis"
                    text: qsTr("More")
                    onClicked: moreMenu.open()

                    Menu {
                        id: moreMenu

                        x: moreButton.width - width
                        y: moreButton.height + Theme.space.xs

                        MenuItem {
                            objectName: "saveAs"
                            iconName: "copy"
                            text: qsTr("Save as…")
                            enabled: keymapEditor.editor !== null
                            onTriggered: saveAsDialog.start(keymapEditor.editor.profileName)
                        }

                        MenuItem {
                            objectName: "restoreOriginal"
                            visible: moreButton.patched
                            height: visible ? implicitHeight : 0
                            iconName: "rotate-ccw"
                            text: qsTr("Restore original…")
                            onTriggered: restoreDialog.open()
                        }

                        MenuItem {
                            objectName: "openFolder"
                            visible: keymapEditor.logicBench !== null
                            height: visible ? implicitHeight : 0
                            iconName: "folder-open"
                            text: qsTr("Open folder")
                            onTriggered: keymapEditor.logicBench.openFolder()
                        }

                        MenuItem {
                            objectName: "openExternal"
                            visible: keymapEditor.logicBench !== null
                            height: visible ? implicitHeight : 0
                            iconName: "share"
                            text: qsTr("Open in external editor")
                            onTriggered: keymapEditor.logicBench.openExternal()
                        }

                        MenuSeparator {}

                        MenuItem {
                            objectName: "gamepadPassthrough"
                            enabled: keymapEditor.editor !== null
                            choice: true
                            chosen: keymapEditor.editor !== null && keymapEditor.editor.gamepadPassthrough
                            text: qsTr("Gamepad: hand it to the game")
                            description: chosen ? qsTr("The phone sees a gamepad; keys on the screen ignore it")
                                                : qsTr("Now the gamepad presses the keys on the screen")
                            onTriggered: keymapEditor.editor.setGamepadPassthrough(!keymapEditor.editor.gamepadPassthrough)
                        }

                        MenuSeparator {}

                        MenuItem {
                            objectName: "discard"
                            iconName: "x"
                            text: qsTr("Discard changes and exit")
                            onTriggered: DeviceManager.finishEditing(keymapEditor.serial, false)
                        }
                    }

                    SaveAsDialog {
                        id: saveAsDialog

                        serial: keymapEditor.serial
                    }

                    RestoreDialog {
                        id: restoreDialog

                        profileName: moreButton.entry !== null ? moreButton.entry.name : ""
                        extensionName: moreButton.entry !== null ? moreButton.entry.extensionName ?? "" : ""
                        onAccepted: DeviceManager.restoreEditing(keymapEditor.serial)
                    }
                }

                Item {
                    width: Theme.space.xs
                    height: 1
                }

                Button {
                    id: saveButton

                    objectName: "save"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    focusPolicy: keymapEditor.trying ? Qt.TabFocus : Qt.StrongFocus
                    text: qsTr("Save")
                    enabled: keymapEditor.unsaved
                    onClicked: DeviceManager.saveEditing(keymapEditor.serial)
                }

                Button {
                    objectName: "done"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    tone: "primary"
                    focusPolicy: keymapEditor.trying ? Qt.TabFocus : Qt.StrongFocus
                    text: qsTr("Done")
                    onClicked: DeviceManager.finishEditing(keymapEditor.serial, true)
                }
            }
        ]

        toolsShown: !keymapEditor.trying
        toolsBelow: true
        toolsContent: [
            ToolbarButton {
                objectName: "freezeFrame"
                visible: !keymapEditor.noPicture
                iconName: "pause"
                text: qsTr("Freeze the picture")
                selected: keymapEditor.frozen
                onClicked: keymapEditor.setFrozen(!keymapEditor.frozen)
            },
            ToolbarButton {
                id: backdropButton

                objectName: "backgroundPicture"
                visible: keymapEditor.noPicture
                iconName: "image"
                text: qsTr("Background picture")
                selected: keymapEditor.backdropUrl !== ""
                onClicked: backdropMenu.open()

                Menu {
                    id: backdropMenu

                    y: backdropButton.height + Theme.space.xs

                    MenuItem {
                        objectName: "importBackground"
                        iconName: "import"
                        text: qsTr("Import a screenshot…")
                        onTriggered: backdropDialog.open()
                    }

                    MenuItem {
                        objectName: "shootBackground"
                        iconName: "camera"
                        enabled: keymapEditor.backdrop.canShoot === true && keymapEditor.backdrop.shooting !== true
                        text: keymapEditor.backdrop.shooting === true ? qsTr("Taking a screenshot on the phone…")
                                                                      : qsTr("Screenshot from the phone")
                        description: keymapEditor.backdrop.canShoot === true
                                     ? qsTr("The phone asks once to allow screen recording")
                                     : qsTr("Connect GameScrcpy Companion first: scan the code in this window")
                        onTriggered: DeviceManager.shootBackground(keymapEditor.serial)
                    }

                    MenuSeparator {}

                    MenuItem {
                        objectName: "clearBackground"
                        iconName: "trash-2"
                        enabled: keymapEditor.backdropUrl !== ""
                        text: qsTr("Clear the background")
                        onTriggered: DeviceManager.clearBackground(keymapEditor.serial)
                    }
                }

                FileDialog {
                    id: backdropDialog

                    title: qsTr("Choose a screenshot of the game")
                    nameFilters: [qsTr("Pictures (*.png *.jpg *.jpeg)")]
                    onAccepted: DeviceManager.importBackground(keymapEditor.serial, backdropDialog.selectedFile)
                }
            },
            ToolbarButton {
                objectName: "snapToggle"
                iconName: "magnet"
                text: qsTr("Snap to other keys and the center")
                selected: keymapEditor.snapping
                onClicked: keymapEditor.snapping = !keymapEditor.snapping
            },
            ToolbarButton {
                objectName: "namesToggle"
                iconName: "type"
                text: qsTr("Show the names under the keys")
                selected: keymapEditor.showingNames
                onClicked: keymapEditor.showingNames = !keymapEditor.showingNames
            },
            ToolbarSeparator {},
            Select {
                id: aspectSelect

                objectName: "aspectSelect"
                anchors.verticalCenter: parent.verticalCenter
                width: ratioMetrics.advanceWidth + leftPadding + rightPadding
                model: keymapEditor.ratios
                textRole: "text"
                currentIndex: keymapEditor.ratioIndex
                Accessible.name: qsTr("Screen shape to preview")
                onActivated: index => {
                    keymapEditor.ratioIndex = index
                    aspectSelect.currentIndex = Qt.binding(() => keymapEditor.ratioIndex)
                }

                TextMetrics {
                    id: ratioMetrics

                    font: aspectSelect.font
                    text: keymapEditor.ratios.reduce((widest, ratio) => ratio.text.length > widest.length ? ratio.text
                                                                                                         : widest, "")
                }

                Tooltip {
                    objectName: "toolTip"
                    text: qsTr("Preview the keys on a screen of another shape")
                    visible: aspectSelect.hovered && !aspectSelect.popup.visible
                }
            },
            Text {
                objectName: "previewNote"
                anchors.verticalCenter: parent.verticalCenter
                visible: keymapEditor.previewing
                leftPadding: Theme.space.xs
                rightPadding: Theme.space.xs
                textFormat: Text.PlainText
                text: qsTr("Preview only")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            },
            ToolbarSeparator {},
            ToolbarButton {
                objectName: "zoomOut"
                iconName: "minus"
                text: qsTr("Zoom out")
                enabled: keymapEditor.zoom > keymapEditor.zoomSteps[0]
                onClicked: keymapEditor.zoomStep(-1, keymapEditor.viewCenter)
            },
            Button {
                id: zoomLevel

                objectName: "zoomLevel"
                anchors.verticalCenter: parent.verticalCenter
                width: zoomMetrics.advanceWidth + leftPadding + rightPadding
                small: true
                tone: "ghost"
                text: qsTr("%1%").arg(Math.round(keymapEditor.zoom * 100))
                toolTip: qsTr("Fit the picture")
                Accessible.name: qsTr("Zoom %1%, fit the picture").arg(Math.round(keymapEditor.zoom * 100))
                onClicked: keymapEditor.zoomTo(1, keymapEditor.viewCenter)

                TextMetrics {
                    id: zoomMetrics

                    font: zoomLevel.font
                    text: qsTr("%1%").arg(200)
                }
            },
            ToolbarButton {
                objectName: "zoomIn"
                iconName: "plus"
                text: qsTr("Zoom in")
                enabled: keymapEditor.zoom < keymapEditor.zoomSteps[keymapEditor.zoomSteps.length - 1]
                onClicked: keymapEditor.zoomStep(1, keymapEditor.viewCenter)
            }
        ]

        sidebarContent: [
            Item {
                id: sideStrip

                objectName: "sideStrip"
                width: parent.width
                height: tabsLine.y + tabsLine.height

                Tabs {
                    id: sideTabs

                    readonly property var pages: keymapEditor.readOnly
                                                 ? [qsTr("Changes")]
                                                 : keymapEditor.selectedCount === 0
                                                 ? [qsTr("Library"), { text: qsTr("Placed"), count: keymapEditor.keyRows.length }]
                                                 : [keymapEditor.selectedCount === 1 ? qsTr("Properties") : qsTr("Arrange")]
                    readonly property int page: keymapEditor.readOnly ? 0
                                                : keymapEditor.selectedCount === 0 ? keymapEditor.browsePage : 0

                    objectName: "sideTabs"
                    x: Theme.space.lg
                    y: Theme.space.xs
                    model: sideTabs.pages
                    currentIndex: sideTabs.page
                    onActivated: index => {
                        if (keymapEditor.selectedCount === 0)
                            keymapEditor.browsePage = index
                        sideTabs.currentIndex = Qt.binding(() => sideTabs.page)
                    }
                }

                Rectangle {
                    id: tabsLine

                    y: sideTabs.y + sideTabs.height
                    width: parent.width
                    height: Theme.size.stroke
                    color: Theme.colors.strokeSubtle
                }
            },
            Flickable {
                objectName: "draftChanges"
                anchors.fill: parent
                anchors.topMargin: sideStrip.height
                visible: keymapEditor.readOnly
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: width
                contentHeight: changesColumn.implicitHeight + Theme.space.md + Theme.space.lg

                Column {
                    id: changesColumn

                    x: Theme.space.lg
                    y: Theme.space.md
                    width: parent.width - 2 * Theme.space.lg
                    spacing: Theme.space.xs

                    Text {
                        objectName: "draftSummary"
                        width: parent.width
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        text: {
                            let added = 0
                            let changed = 0
                            let removed = 0
                            for (const marker of keymapEditor.diffRows) {
                                if (marker.diff === "added")
                                    ++added
                                else if (marker.diff === "changed")
                                    ++changed
                                else if (marker.diff === "removed")
                                    ++removed
                            }
                            return qsTr("%1 added · %2 changed · %3 removed").arg(added).arg(changed).arg(removed)
                        }
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: keymapEditor.diffRows

                        delegate: Row {
                            id: changedRow

                            required property var modelData

                            width: changesColumn.width
                            height: Theme.size.listItem
                            spacing: Theme.space.sm

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.size.pillDot + Theme.size.focusRing
                                height: width
                                radius: width / 2
                                color: changedRow.modelData.diff === "added" ? Theme.colors.success
                                     : changedRow.modelData.diff === "changed" ? Theme.colors.warning
                                     : Theme.colors.danger
                            }

                            Text {
                                width: parent.width - parent.spacing - Theme.size.pillDot - Theme.size.focusRing
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: (changedRow.modelData.name ?? "") !== "" ? changedRow.modelData.name
                                                                               : changedRow.modelData.id
                                elide: Text.ElideRight
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                            }
                        }
                    }

                    Text {
                        objectName: "draftSame"
                        width: parent.width
                        visible: keymapEditor.diffRows.length === 0
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        text: qsTr("This draft has the same keys as the profile in use.")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.label.size
                    }
                }
            },
            Item {
                objectName: "browse"
                anchors.fill: parent
                anchors.topMargin: sideStrip.height
                visible: keymapEditor.selectedCount === 0 && !keymapEditor.readOnly

                Flickable {
                    id: libraryView

                    objectName: "library"
                    anchors.fill: parent
                    visible: keymapEditor.browsePage === 0
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentHeight: componentList.implicitHeight + Theme.space.md + Theme.space.lg

                    Column {
                        id: componentList

                        x: Theme.space.lg
                        y: Theme.space.md
                        width: libraryView.width - 2 * Theme.space.lg
                        spacing: Theme.space.xs

                        SearchField {
                            id: componentSearch

                            objectName: "componentSearch"
                            width: parent.width
                            placeholderText: qsTr("Search components")
                        }

                        Repeater {
                            model: keymapEditor.shownLibrary

                            delegate: Column {
                                id: group

                                required property var modelData

                                width: componentList.width
                                spacing: keymapEditor.tileGap

                                Item {
                                    width: parent.width
                                    height: groupTitle.implicitHeight

                                    Text {
                                        id: groupTitle

                                        topPadding: Theme.space.sm
                                        textFormat: Text.PlainText
                                        text: group.modelData.title
                                        color: Theme.colors.textTertiary
                                        font.pixelSize: Theme.type.micro.size
                                        font.weight: Theme.type.micro.weight
                                    }

                                    Text {
                                        objectName: "groupNote"
                                        anchors.right: parent.right
                                        anchors.baseline: groupTitle.baseline
                                        width: Math.max(0, parent.width - groupTitle.implicitWidth - Theme.space.sm)
                                        visible: (group.modelData.note ?? "") !== ""
                                        horizontalAlignment: Text.AlignRight
                                        textFormat: Text.PlainText
                                        text: group.modelData.note ?? ""
                                        elide: Text.ElideRight
                                        color: Theme.colors.textTertiary
                                        font.pixelSize: Theme.type.micro.size
                                    }
                                }

                                Grid {
                                    columns: 2
                                    columnSpacing: keymapEditor.tileGap
                                    rowSpacing: keymapEditor.tileGap

                                    Repeater {
                                        model: group.modelData.items

                                        delegate: ComponentTile {}
                                    }
                                }
                            }
                        }

                        Text {
                            objectName: "noComponent"
                            topPadding: Theme.space.md
                            width: parent.width
                            visible: keymapEditor.shownLibrary.length === 0
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: qsTr("No component matches “%1”").arg(componentSearch.text.trim())
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.label.size
                        }
                    }
                }

                Column {
                    objectName: "keyList"
                    anchors.fill: parent
                    anchors.topMargin: Theme.space.sm
                    visible: keymapEditor.browsePage === 1

                    Text {
                        x: Theme.space.lg
                        width: parent.width - 2 * Theme.space.lg
                        visible: keymapEditor.keyRows.length === 0
                        topPadding: Theme.space.sm
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        text: qsTr("Nothing placed yet. Drag a component from the library onto the picture.")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.label.size
                    }

                    Repeater {
                        model: keymapEditor.keyRows

                        delegate: Rectangle {
                            id: keyRow

                            required property var modelData

                            objectName: "keyRow"
                            width: parent.width
                            height: Theme.size.listItem
                            color: modelData.selected ? Theme.colors.stateSelected
                                 : rowHover.hovered ? Theme.colors.stateHover : "transparent"

                            HoverHandler {
                                id: rowHover
                            }

                            TapHandler {
                                onTapped: keymapEditor.editor.select([keyRow.modelData.id])
                            }

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                x: Theme.space.xs
                                visible: keyRow.modelData.conflict
                                width: Theme.size.pillDot
                                height: width
                                radius: width / 2
                                color: keyHints.conflictColor(keyRow.modelData.conflictGroup ?? 0)
                            }

                            Icon {
                                id: rowIcon

                                anchors.verticalCenter: parent.verticalCenter
                                x: Theme.space.lg + (keyRow.modelData.depth ?? 0) * (Theme.size.iconSmall + Theme.space.sm)
                                name: keymapEditor.iconOfRow(keyRow.modelData)
                                size: Theme.size.iconSmall
                                color: Theme.colors.textSecondary
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                x: rowIcon.x + rowIcon.width + Theme.space.sm
                                width: parent.width / 2 - x
                                textFormat: Text.PlainText
                                text: keyRow.modelData.name
                                elide: Text.ElideRight
                                color: keyRow.modelData.enabled ? Theme.colors.textPrimary : Theme.colors.textTertiary
                                font.pixelSize: Theme.type.label.size
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.space.lg
                                width: parent.width / 2 - Theme.space.lg
                                horizontalAlignment: Text.AlignRight
                                textFormat: Text.PlainText
                                text: keyRow.modelData.keys !== "" ? keyRow.modelData.keys : qsTr("Not bound")
                                elide: Text.ElideLeft
                                color: keyRow.modelData.keys !== "" ? Theme.colors.textSecondary : Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }
                        }
                    }
                }
            },
            KeyProperties {
                objectName: "properties"
                anchors.fill: parent
                anchors.topMargin: sideStrip.height
                visible: keymapEditor.selectedCount === 1 && !keymapEditor.readOnly
                editor: keymapEditor.editor
                row: keymapEditor.currentRow
                iconName: row !== null ? keymapEditor.iconOfRow(row) : ""
                typeText: row !== null ? keymapEditor.rowInfo(row).text : ""
                typeHint: row !== null ? keymapEditor.rowInfo(row).hint : ""
                onFinished: keymapEditor.focusCanvas()
                onReferenced: id => keymapEditor.referenceInLogic(id)
                onOpened: ref => keymapEditor.openComponent(ref)
            },
            Column {
                objectName: "arrange"
                anchors.fill: parent
                anchors.margins: Theme.space.lg
                anchors.topMargin: sideStrip.height + Theme.space.lg
                visible: keymapEditor.selectedCount >= 2 && !keymapEditor.readOnly
                spacing: Theme.space.md

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("%n key(s) selected", "", keymapEditor.selectedCount)
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.DemiBold
                }

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Align")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                }

                Row {
                    spacing: Theme.space.xxs

                    Repeater {
                        model: [
                            { edge: "left", icon: "align-start-vertical", text: qsTr("Align left") },
                            { edge: "centerX", icon: "align-center-vertical", text: qsTr("Align centers across") },
                            { edge: "right", icon: "align-end-vertical", text: qsTr("Align right") },
                            { edge: "top", icon: "align-start-horizontal", text: qsTr("Align top") },
                            { edge: "centerY", icon: "align-center-horizontal", text: qsTr("Align centers down") },
                            { edge: "bottom", icon: "align-end-horizontal", text: qsTr("Align bottom") }
                        ]

                        delegate: ToolbarButton {
                            required property var modelData

                            iconName: modelData.icon
                            text: modelData.text
                            onClicked: keymapEditor.editor.alignSelection(modelData.edge)
                        }
                    }
                }

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Distribute")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                }

                Row {
                    spacing: Theme.space.xxs

                    ToolbarButton {
                        iconName: "align-horizontal-distribute-center"
                        text: qsTr("Space evenly across")
                        enabled: keymapEditor.selectedCount >= 3
                        onClicked: keymapEditor.editor.distributeSelection(true)
                    }

                    ToolbarButton {
                        iconName: "align-vertical-distribute-center"
                        text: qsTr("Space evenly down")
                        enabled: keymapEditor.selectedCount >= 3
                        onClicked: keymapEditor.editor.distributeSelection(false)
                    }
                }
            }
        ]
    }

    Loader {
        id: logicLoader

        objectName: "logicLoader"
        y: bench.y + Theme.size.workbenchBar
        width: parent.width
        height: Math.max(0, parent.height - y)
        active: keymapEditor.logicBench !== null
        visible: keymapEditor.logicShown

        sourceComponent: LogicView {
            objectName: "logicView"
            bench: keymapEditor.logicBench
            preview: keymapEditor.readOnly ? keymapEditor.editor : null
        }
    }

    Connections {
        target: keymapEditor.editor

        function onLogicActionWanted(action: string) {
            if (keymapEditor.logicView === null)
                return
            if (action === "undo")
                keymapEditor.logicView.undo()
            else if (action === "redo")
                keymapEditor.logicView.redo()
        }
    }

    Item {
        id: capture

        property string target
        property string slot
        property string targetLabel
        property string replacing
        property bool swapping: false
        property point at
        property int pendingScanCode: 0
        readonly property bool active: target !== ""
        readonly property string label: targetLabel

        function beginControl(id: string, path: string, name: string, place: point) {
            end()
            target = id
            slot = path
            targetLabel = name
            at = Qt.point(place.x + canvas.x, place.y + canvas.y)
            forceActiveFocus()
        }

        function beginReplace(id: string, keySlot: string, keyText: string, name: string, place: point) {
            end()
            target = id
            replacing = keySlot
            swapping = keyText !== "?"
            targetLabel = swapping ? keyText : name
            at = Qt.point(place.x + canvas.x, place.y + canvas.y)
            forceActiveFocus()
        }

        function clear() {
            target = ""
            slot = ""
            targetLabel = ""
            replacing = ""
            swapping = false
            pendingScanCode = 0
        }

        function end() {
            if (!active)
                return
            const control = target !== "" && replacing === ""
            clear()
            if (control && keymapEditor.editor !== null)
                keymapEditor.editor.endGesture()
            keymapEditor.focusCanvas()
        }

        function finish(name: string) {
            if (name === "" || keymapEditor.editor === null)
                return
            if (replacing !== "") {
                keymapEditor.editor.rebind(target, replacing, name)
                end()
                return
            }
            keymapEditor.editor.bindInput(target, slot, name)
            end()
        }

        objectName: "capture"
        x: keymapEditor.view.x
        y: keymapEditor.view.y
        width: keymapEditor.view.width
        height: keymapEditor.view.height
        visible: active

        Keys.onShortcutOverride: event => event.accepted = true
        Keys.onPressed: event => {
            event.accepted = true
            if (event.isAutoRepeat)
                return
            if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift || event.key === Qt.Key_Alt
                    || event.key === Qt.Key_Meta) {
                pendingScanCode = event.nativeScanCode
                return
            }
            pendingScanCode = 0
            finish(keymapEditor.editor.inputOfKey(event.nativeScanCode, event.modifiers))
        }
        Keys.onReleased: event => {
            event.accepted = true
            if (event.isAutoRepeat || pendingScanCode === 0 || event.nativeScanCode !== pendingScanCode)
                return
            const scanCode = pendingScanCode
            pendingScanCode = 0
            finish(keymapEditor.editor.inputOfKey(scanCode, 0))
        }
        onActiveFocusChanged: {
            if (!activeFocus && active)
                end()
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: mouse => capture.finish(keymapEditor.editor.inputOfMouse(mouse.button, mouse.modifiers))
            onWheel: wheel => capture.finish(keymapEditor.editor.inputOfWheel(wheel.angleDelta.x, wheel.angleDelta.y,
                                                                               wheel.modifiers))
        }

        Rectangle {
            readonly property var marker: (keymapEditor.markers ?? []).find(each => each.id === capture.target) ?? null
            readonly property real reach: Math.max(marker !== null ? Math.max(marker.rx ?? 0, marker.ry ?? 0) : 0,
                                                   Theme.size.keyHint / 2) + Theme.space.sm

            objectName: "captureRing"
            x: capture.at.x - reach
            y: capture.at.y - reach
            width: 2 * reach
            height: width
            radius: width / 2
            color: "transparent"
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accent
            opacity: Theme.reducedMotion ? 1 : Theme.disabledOpacity

            SequentialAnimation on opacity {
                running: capture.active && !Theme.reducedMotion
                loops: Animation.Infinite

                NumberAnimation {
                    from: Theme.disabledOpacity
                    to: 1
                    duration: Theme.motion.pulse / 2
                    easing.type: Easing.InOutSine
                }

                NumberAnimation {
                    from: 1
                    to: Theme.disabledOpacity
                    duration: Theme.motion.pulse / 2
                    easing.type: Easing.InOutSine
                }
            }
        }

        Connections {
            target: HardwareAccess
            enabled: capture.active

            function onInputSeen(name: string) {
                capture.finish(name)
            }
        }

        Rectangle {
            objectName: "capturePrompt"
            x: Math.max(0, Math.min(capture.at.x - width / 2, capture.width - width))
            y: Math.max(0, Math.min(capture.at.y + Theme.size.keyHint / 2 + Theme.space.md, capture.height - height))
            width: Math.min(promptRow.implicitWidth + Theme.space.md + Theme.space.xs, capture.width)
            height: Theme.size.capsule
            radius: height / 2
            color: Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: Theme.colors.accent

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                onWheel: wheel => wheel.accepted = true
            }

            Row {
                id: promptRow

                x: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.sm

                Icon {
                    id: promptIcon

                    anchors.verticalCenter: parent.verticalCenter
                    name: "keyboard"
                    size: Theme.size.iconSmall
                    color: Theme.colors.accent
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, capture.width - Theme.space.md - Theme.space.xs - promptIcon.width
                                    - promptHint.implicitWidth - promptSkip.implicitWidth - 3 * promptRow.spacing)
                    textFormat: Text.PlainText
                    text: capture.swapping ? qsTr("Press a key to replace “%1”").arg(capture.label)
                                           : qsTr("Press a key for “%1”").arg(capture.label)
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                }

                Text {
                    id: promptHint

                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: qsTr("Mouse buttons and the wheel work too")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                Button {
                    id: promptSkip

                    objectName: "captureSkip"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    tone: "ghost"
                    focusPolicy: Qt.NoFocus
                    text: capture.replacing !== "" ? qsTr("Cancel") : qsTr("Skip")
                    onClicked: capture.end()
                }
            }
        }
    }

    Rectangle {
        id: ghost

        property var item: null
        property point at

        objectName: "dragGhost"
        visible: item !== null
        x: at.x - width / 2
        y: at.y - height / 2
        width: Theme.size.componentTile
        height: width
        radius: Theme.radius.lg
        color: Theme.colors.bgElevated
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accent

        Column {
            anchors.centerIn: parent
            spacing: keymapEditor.tileGap

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: ghost.item !== null ? ghost.item.icon : ""
                size: Theme.size.iconLarge
                color: Theme.colors.textPrimary
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                textFormat: Text.PlainText
                text: ghost.item !== null ? ghost.item.text : ""
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.caption.size
            }
        }
    }

    TourOverlay {
        id: tour

        anchors.fill: parent
        objectName: "keymapTour"
        tourKey: "keymap"
        settings: AppSettings
        startWhen: keymapEditor.editor !== null && !keymapEditor.logicShown && !keymapEditor.trying
                   && !AppSettings.toursQuiet
        version: keymapEditor.tourControls
        steps: [
            {
                centered: true,
                title: qsTr("Editing the key mapping"),
                text: qsTr("Here you set up the keys for a game: drag components onto the phone picture and give "
                           + "each one a key. Every change can be undone, and Save writes them into the profile.")
            },
            {
                target: libraryView,
                title: qsTr("The component library"),
                text: qsTr("Tap, hold, turbo, swipe, the WASD wheel, the camera, skills… each component presses the "
                           + "screen its own way; rest the mouse on one to see what it does. Try it: drag one onto "
                           + "the picture.")
            },
            {
                target: canvas,
                title: qsTr("Place it and give it a key"),
                text: qsTr("Drag the component onto the game's button, then click it and press the key you want "
                           + "to use for it.")
            },
            {
                target: function() { return tour.findItem(keymapEditor, "properties") },
                optional: true,
                title: qsTr("Properties"),
                text: qsTr("The selected component's settings: its key, its name and how it feels, such as how fast "
                           + "a turbo taps or how far a swipe goes. Use it in logic is here too.")
            },
            {
                target: sideTabs,
                title: qsTr("Placed"),
                text: qsTr("Placed lists every component on the picture with its key, and marks keys that clash.")
            },
            {
                target: modeSwitch,
                title: qsTr("Picture and logic"),
                text: qsTr("Logic connects keys to a blueprint: a press can run a series of actions, check "
                           + "conditions or look for pictures on the screen. The components' own events are there "
                           + "too.")
            },
            {
                target: function() { return tour.findItem(keymapEditor, "tryToggle") },
                title: qsTr("Try the keys"),
                text: qsTr("Try these keys on the phone without leaving the editor; click again to go back to "
                           + "editing.")
            },
            {
                target: function() { return tour.findItem(keymapEditor, "undo") },
                title: qsTr("Undo and redo"),
                text: qsTr("Every change can be taken back, and brought back again."),
                keys: ["Ctrl+Z", "Ctrl+Y"]
            },
            {
                target: function() { return tour.findItem(keymapEditor, "freezeFrame") },
                optional: true,
                title: qsTr("Freeze the picture"),
                text: qsTr("When the game keeps moving, freeze the picture to line the components up with its "
                           + "buttons.")
            },
            {
                target: moreButton,
                title: qsTr("More"),
                text: qsTr("Save as, restoring the original profile, handing a gamepad to the game and discarding "
                           + "the changes are in here.")
            },
            {
                target: saveButton,
                title: qsTr("Save"),
                text: qsTr("Save writes the changes into the profile; Done saves and goes back to mirroring.")
            },
            {
                centered: true,
                title: qsTr("Ready to play"),
                text: qsTr("With the keys in place, click Done and play. For combos and smarter keys, switch to "
                           + "Logic. Help can replay this tour.")
            }
        ]
        advance: {
            switch (tour.step) {
            case 1:
                return keymapEditor.tourControls > keymapEditor.tourBaseControls
            case 2:
                return keymapEditor.tourBound > keymapEditor.tourBaseBound
            case 10:
                return keymapEditor.tourControls > keymapEditor.tourBaseControls
                       && keymapEditor.editor !== null && !keymapEditor.editor.dirty
            }
            return false
        }
        onStarted: {
            keymapEditor.tourBaseControls = keymapEditor.tourControls
            keymapEditor.tourBaseBound = keymapEditor.tourBound
        }

        function startTour() {
            tour.restart()
        }
    }
}
