pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Window {
    id: root

    required property VisionWorkbench bench
    readonly property string helpTopic: "scripts"
    property string tool: "select"
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property var drawing: []
    property rect band: Qt.rect(0, 0, 0, 0)
    property bool banding: false
    property bool resultsOpen: false
    readonly property var zoomSteps: [0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4]
    readonly property size picture: root.bench.pictureSize
    readonly property bool hasPicture: root.picture.width > 0 && root.picture.height > 0
    readonly property var listKinds: ["template", "color", "region", "point", "path"]
    readonly property var groupedResources: {
        const rows = []
        for (const kind of root.listKinds) {
            const items = root.listOf(kind)
            if (items.length === 0)
                continue
            rows.push({ header: true, kind: kind, count: items.length })
            for (const item of items)
                rows.push(item)
        }
        return rows
    }

    function listOf(kind: string): var {
        switch (kind) {
        case "template":
            return root.bench.templates
        case "region":
            return root.bench.regions
        case "point":
            return root.bench.points
        case "path":
            return root.bench.paths
        }
        return root.bench.colors
    }

    function chooseTool(name: string) {
        root.drawing = []
        root.banding = false
        root.tool = name
        if ((name === "template" || name === "color") && !root.bench.frozen)
            root.bench.setFrozen(true)
    }

    function toggleTool(name: string) {
        root.chooseTool(name === root.tool ? "select" : name)
    }

    function zoomTo(value: real) {
        root.zoom = Math.max(root.zoomSteps[0], Math.min(value, root.zoomSteps[root.zoomSteps.length - 1]))
        if (root.zoom <= 1) {
            root.panX = 0
            root.panY = 0
        }
    }

    function zoomStep(direction: int) {
        const steps = root.zoomSteps
        if (direction > 0) {
            for (let i = 0; i < steps.length; ++i) {
                if (steps[i] > root.zoom + 0.001) {
                    root.zoomTo(steps[i])
                    return
                }
            }
            return
        }
        for (let i = steps.length - 1; i >= 0; --i) {
            if (steps[i] < root.zoom - 0.001) {
                root.zoomTo(steps[i])
                return
            }
        }
    }

    function finishPath() {
        let points = root.drawing
        while (points.length >= 2) {
            const last = points[points.length - 1]
            const before = points[points.length - 2]
            if (Math.abs((last.x - before.x) * view.width) >= Theme.space.xs
                    || Math.abs((last.y - before.y) * view.height) >= Theme.space.xs)
                break
            points = points.slice(0, points.length - 1)
        }
        root.drawing = []
        if (points.length >= 2) {
            root.bench.addPath(points)
            root.chooseTool("select")
        }
    }

    function cancelDrawing(): bool {
        if (root.drawing.length === 0 && !root.banding)
            return false
        root.drawing = []
        root.banding = false
        return true
    }

    function pickFinished() {
        if (root.bench.requesting)
            return
        root.chooseTool("select")
    }

    function requestTool(): string {
        const items = root.bench.requestItems
        const index = root.bench.requestCurrent
        if (index < 0 || index >= items.length)
            return ""
        return items[index].kind ?? ""
    }

    function summaryText(): string {
        const result = root.bench.result
        if ((result.problem ?? "") !== "")
            return result.problem
        if (result.count === undefined)
            return ""
        const ms = Number(result.ms ?? 0).toFixed(1)
        if (result.count > 0)
            return qsTr("%1: found %n place(s) in %2 ms", "", result.count).arg(result.name).arg(ms)
        if (result.nearest !== undefined)
            return qsTr("%1: not found in %2 ms; the closest place scores %3").arg(result.name).arg(ms)
                   .arg(Number(result.nearest).toFixed(2))
        return qsTr("%1: not found in %2 ms").arg(result.name).arg(ms)
    }

    function detailOf(item: var): string {
        switch (item.kind) {
        case "template":
            return item.match === "color"
                    ? qsTr("%1 × %2 · threshold %3 · color").arg(item.width).arg(item.height).arg(Number(item.threshold).toFixed(2))
                    : qsTr("%1 × %2 · threshold %3 · grey").arg(item.width).arg(item.height).arg(Number(item.threshold).toFixed(2))
        case "region":
            return qsTr("x %1 y %2 · %3 × %4").arg(Number(item.x).toFixed(2)).arg(Number(item.y).toFixed(2))
                    .arg(Number(item.width).toFixed(2)).arg(Number(item.height).toFixed(2))
        case "point":
            return qsTr("x %1 y %2").arg(Number(item.x).toFixed(3)).arg(Number(item.y).toFixed(3))
        case "path":
            return qsTr("%n point(s) · %1 ms", "", item.points.length).arg(Math.round(item.durationMs))
        }
        return qsTr("%1 · tolerance %2").arg(item.color).arg(Number(item.tolerance).toFixed(2))
    }

    function iconOf(kind: string): string {
        switch (kind) {
        case "template":
            return "square"
        case "region":
            return "expand"
        case "point":
            return "crosshair"
        case "path":
            return "swipe-path"
        }
        return "palette"
    }

    function kindText(kind: string): string {
        switch (kind) {
        case "template":
            return qsTr("Template")
        case "region":
            return qsTr("Region")
        case "point":
            return qsTr("Point")
        case "path":
            return qsTr("Path")
        }
        return qsTr("Color")
    }

    function groupText(kind: string): string {
        switch (kind) {
        case "template":
            return qsTr("Templates")
        case "region":
            return qsTr("Regions")
        case "point":
            return qsTr("Points")
        case "path":
            return qsTr("Paths")
        }
        return qsTr("Colors")
    }

    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 600
    visible: false
    title: root.bench.draft ? qsTr("%1 — Vision resources (AI draft)").arg(root.bench.profileName)
                            : qsTr("%1 — Vision resources").arg(root.bench.profileName)
    color: Theme.colors.bgBase

    component Tag: Rectangle {
        id: tag

        required property string label
        property bool strong: false

        width: tagText.implicitWidth + 2 * Theme.space.xs
        height: tagText.implicitHeight + 2 * Theme.space.xxs
        radius: Theme.radius.xs
        color: tag.strong ? Theme.colors.accent : Theme.colors.keyHintFill

        Text {
            id: tagText

            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: tag.label
            color: tag.strong ? Theme.colors.textOnAccent : Theme.colors.keyHintText
            font.pixelSize: Theme.type.caption.size
        }
    }

    component Field: Column {
        id: field

        required property string title
        default property alias content: holder.data

        spacing: Theme.space.xs + Theme.space.xxs

        Text {
            width: field.width
            textFormat: Text.PlainText
            text: field.title
            elide: Text.ElideRight
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: Theme.type.label.weight
        }

        Item {
            id: holder

            width: field.width
            height: holder.childrenRect.height
        }
    }

    component RatioField: NumberField {
        width: Theme.size.numberFieldWide
        numbers: NumberScale {
            minimum: 0
            maximum: 1
            step: 0.01
            decimals: 4
        }
    }

    Connections {
        target: root.bench

        function onNotice(text: string, tone: string) {
            toast.show(text, tone)
        }

        function onResultsChanged() {
            root.resultsOpen = root.bench.result.count !== undefined || (root.bench.result.problem ?? "") !== ""
        }

        function onPickChanged() {
            const kind = root.bench.pick.kind ?? ""
            if (kind !== "")
                root.chooseTool(kind === "image" ? "template" : kind)
        }

        function onRequestCurrentChanged() {
            const kind = root.requestTool()
            if (kind !== "")
                root.chooseTool(kind)
        }

        function onRenamingChanged() {
            if ((root.bench.renaming.name ?? "") !== "")
                Qt.callLater(renameDialog.ask)
        }
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: !(root.activeFocusItem instanceof TextInput)
        onActivated: root.bench.undo()
    }

    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: !(root.activeFocusItem instanceof TextInput)
        onActivated: root.bench.redo()
    }

    Shortcut {
        sequences: [StandardKey.Delete]
        enabled: !(root.activeFocusItem instanceof TextInput) && root.bench.selectedKind !== ""
        onActivated: root.bench.remove(root.bench.selectedKind, root.bench.selectedName)
    }

    Shortcut {
        sequence: "Escape"
        enabled: !(root.activeFocusItem instanceof TextInput)
        onActivated: {
            if (root.cancelDrawing())
                return
            if ((root.bench.pick.kind ?? "") !== "")
                root.bench.cancelPick()
            else if (root.tool !== "select")
                root.chooseTool("select")
            else
                root.bench.select("", "")
        }
    }

    WindowChrome {
        id: chrome

        window: root
        titleBar: titleBar
        minimizeButton: titleBar.minimizeButton
        maximizeButton: titleBar.maximizeButton
        closeButton: titleBar.closeButton
        darkMode: Theme.dark
    }

    TitleBar {
        id: titleBar

        window: root
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    Item {
        id: canvas

        readonly property real margin: Theme.space.x3l
        readonly property real fit: root.hasPicture
                                    ? Math.min(Math.max(1, canvas.width - 2 * canvas.margin) / root.picture.width,
                                               Math.max(1, canvas.height - 2 * canvas.margin) / root.picture.height)
                                    : 1
        readonly property real scale: canvas.fit * root.zoom

        objectName: "canvas"
        x: workbench.x + workbench.canvasRect.x
        y: workbench.y + workbench.canvasRect.y
        width: workbench.canvasRect.width
        height: workbench.canvasRect.height
        clip: true

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: event => root.zoomStep(event.angleDelta.y > 0 ? 1 : -1)
        }

        DragHandler {
            property real startX: 0
            property real startY: 0

            target: null
            acceptedButtons: Qt.MiddleButton | Qt.RightButton
            onActiveChanged: {
                if (active) {
                    startX = root.panX
                    startY = root.panY
                }
            }
            onTranslationChanged: {
                if (active) {
                    root.panX = startX + translation.x
                    root.panY = startY + translation.y
                }
            }
        }

        Item {
            id: view

            objectName: "picture"
            width: root.hasPicture ? root.picture.width * canvas.scale : Math.max(1, canvas.width - 2 * canvas.margin)
            height: root.hasPicture ? root.picture.height * canvas.scale : Math.max(1, canvas.height - 2 * canvas.margin)
            x: (canvas.width - width) / 2 + root.panX
            y: (canvas.height - height) / 2 + root.panY

            Item {
                id: liveHost

                objectName: "livePicture"
                anchors.fill: parent
                visible: !root.bench.frozen
                Component.onCompleted: root.bench.attachVideo(liveHost)
            }

            Image {
                objectName: "frozenPicture"
                anchors.fill: parent
                visible: root.bench.frozen
                source: root.bench.frozen && root.bench.frozenKey !== "" ? "image://vision/" + root.bench.frozenKey : ""
                cache: false
                smooth: canvas.scale < 1
                fillMode: Image.Stretch
            }

            MouseArea {
                id: drawArea

                property point origin
                property bool added: false

                objectName: "drawArea"
                anchors.fill: parent
                enabled: root.hasPicture && root.tool !== "path"
                cursorShape: root.tool === "select" ? Qt.ArrowCursor : Qt.CrossCursor
                onPressed: mouse => {
                    drawArea.added = false
                    if (root.tool !== "template" && root.tool !== "region")
                        return
                    drawArea.origin = Qt.point(mouse.x, mouse.y)
                    root.band = Qt.rect(mouse.x, mouse.y, 0, 0)
                    root.banding = true
                }
                onPositionChanged: mouse => {
                    if (!root.banding)
                        return
                    const x = Math.max(0, Math.min(mouse.x, view.width))
                    const y = Math.max(0, Math.min(mouse.y, view.height))
                    root.band = Qt.rect(Math.min(drawArea.origin.x, x), Math.min(drawArea.origin.y, y),
                                        Math.abs(x - drawArea.origin.x), Math.abs(y - drawArea.origin.y))
                }
                onReleased: {
                    if (!root.banding)
                        return
                    root.banding = false
                    const box = root.band
                    if (box.width < Theme.space.xs || box.height < Theme.space.xs)
                        return
                    if (root.tool === "template")
                        root.bench.addTemplate(box.x / view.width, box.y / view.height, box.width / view.width,
                                               box.height / view.height)
                    else
                        root.bench.addRegion(box.x / view.width, box.y / view.height, box.width / view.width,
                                             box.height / view.height)
                    drawArea.added = true
                    root.pickFinished()
                }
                onClicked: mouse => {
                    if (drawArea.added) {
                        drawArea.added = false
                    } else if (root.tool === "select") {
                        root.bench.select("", "")
                    } else if (root.tool === "point") {
                        root.bench.addPoint(mouse.x / view.width, mouse.y / view.height)
                        root.pickFinished()
                    } else if (root.tool === "color") {
                        root.bench.pickColor(mouse.x / view.width, mouse.y / view.height)
                        root.pickFinished()
                    }
                }
            }

            Item {
                anchors.fill: parent
                enabled: root.hasPicture && root.tool === "path"

                TapHandler {
                    objectName: "pathTaps"
                    onTapped: eventPoint => {
                        root.drawing = root.drawing.concat([{ x: Math.max(0, Math.min(1, eventPoint.position.x / view.width)),
                                                              y: Math.max(0, Math.min(1, eventPoint.position.y / view.height)) }])
                    }
                    onDoubleTapped: root.finishPath()
                }
            }

            Repeater {
                model: root.bench.regions

                delegate: Item {
                    id: regionBox

                    required property var modelData
                    readonly property bool chosen: root.bench.selectedKind === "region"
                                                   && root.bench.selectedName === modelData.name
                    property real shiftX: 0
                    property real shiftY: 0
                    property real growX: 0
                    property real growY: 0

                    function commit() {
                        root.bench.setArea(regionBox.modelData.name, regionBox.x / view.width, regionBox.y / view.height,
                                           regionBox.width / view.width, regionBox.height / view.height)
                        regionBox.shiftX = 0
                        regionBox.shiftY = 0
                        regionBox.growX = 0
                        regionBox.growY = 0
                    }

                    objectName: "regionBox"
                    x: regionBox.modelData.x * view.width + regionBox.shiftX
                    y: regionBox.modelData.y * view.height + regionBox.shiftY
                    width: Math.max(Theme.space.sm, regionBox.modelData.width * view.width + regionBox.growX)
                    height: Math.max(Theme.space.sm, regionBox.modelData.height * view.height + regionBox.growY)

                    Rectangle {
                        anchors.fill: parent
                        color: regionBox.chosen ? Theme.colors.accentSubtle : Theme.colors.infoSubtle
                        border.width: Theme.size.focusRing
                        border.color: regionBox.chosen ? Theme.colors.accent : Theme.colors.info
                    }

                    Tag {
                        x: Theme.space.xs
                        y: Theme.space.xs
                        label: qsTr("Region · %1").arg(regionBox.modelData.name)
                        strong: regionBox.chosen
                    }

                    MouseArea {
                        property point start

                        anchors.fill: parent
                        enabled: root.tool === "select"
                        cursorShape: Qt.SizeAllCursor
                        onPressed: mouse => {
                            start = mapToItem(view, mouse.x, mouse.y)
                            root.bench.select("region", regionBox.modelData.name)
                        }
                        onPositionChanged: mouse => {
                            const at = mapToItem(view, mouse.x, mouse.y)
                            const left = regionBox.modelData.x * view.width
                            const top = regionBox.modelData.y * view.height
                            regionBox.shiftX = Math.max(-left, Math.min(at.x - start.x, view.width - left - regionBox.width))
                            regionBox.shiftY = Math.max(-top, Math.min(at.y - start.y, view.height - top - regionBox.height))
                        }
                        onReleased: {
                            if (regionBox.shiftX !== 0 || regionBox.shiftY !== 0)
                                regionBox.commit()
                        }
                    }

                    Rectangle {
                        objectName: "regionHandle"
                        visible: regionBox.chosen && root.tool === "select"
                        x: parent.width - width / 2
                        y: parent.height - height / 2
                        width: Theme.space.md
                        height: Theme.space.md
                        radius: width / 2
                        color: Theme.colors.knob
                        border.width: Theme.size.focusRing
                        border.color: Theme.colors.accent

                        MouseArea {
                            property point start

                            anchors.fill: parent
                            anchors.margins: -Theme.space.xs
                            cursorShape: Qt.SizeFDiagCursor
                            onPressed: mouse => start = mapToItem(view, mouse.x, mouse.y)
                            onPositionChanged: mouse => {
                                const at = mapToItem(view, mouse.x, mouse.y)
                                const right = (regionBox.modelData.x + regionBox.modelData.width) * view.width
                                const bottom = (regionBox.modelData.y + regionBox.modelData.height) * view.height
                                regionBox.growX = Math.min(at.x - start.x, view.width - right)
                                regionBox.growY = Math.min(at.y - start.y, view.height - bottom)
                            }
                            onReleased: regionBox.commit()
                        }
                    }
                }
            }

            Repeater {
                model: root.bench.templates

                delegate: Rectangle {
                    id: cutBox

                    required property var modelData
                    readonly property bool chosen: root.bench.selectedKind === "template"
                                                   && root.bench.selectedName === modelData.name

                    objectName: "templateBox"
                    visible: cutBox.modelData.cut !== undefined
                    x: cutBox.visible ? cutBox.modelData.cut.x * view.width : 0
                    y: cutBox.visible ? cutBox.modelData.cut.y * view.height : 0
                    width: cutBox.visible ? cutBox.modelData.cut.width * view.width : 0
                    height: cutBox.visible ? cutBox.modelData.cut.height * view.height : 0
                    color: "transparent"
                    border.width: Theme.size.focusRing
                    border.color: cutBox.chosen ? Theme.colors.accent : Theme.colors.accentLine

                    Tag {
                        y: -height - Theme.space.xxs
                        label: qsTr("Template · %1").arg(cutBox.modelData.name)
                        strong: cutBox.chosen
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: root.tool === "select"
                        onClicked: root.bench.select("template", cutBox.modelData.name)
                    }
                }
            }

            Repeater {
                model: root.bench.paths

                delegate: Item {
                    id: route

                    required property var modelData
                    readonly property bool chosen: root.bench.selectedKind === "path"
                                                   && root.bench.selectedName === modelData.name
                    property real shiftX: 0
                    property real shiftY: 0

                    objectName: "pathLine"
                    x: route.shiftX
                    y: route.shiftY
                    width: view.width
                    height: view.height

                    Shape {
                        anchors.fill: parent

                        ShapePath {
                            strokeColor: route.chosen ? Theme.colors.accent : Theme.colors.keyHintText
                            strokeWidth: Theme.size.focusRing
                            strokeStyle: ShapePath.DashLine
                            fillColor: "transparent"

                            PathPolyline {
                                path: route.modelData.points.map(point => Qt.point(point.x * view.width, point.y * view.height))
                            }
                        }
                    }

                    Tag {
                        x: route.modelData.points[0].x * view.width + Theme.space.sm
                        y: route.modelData.points[0].y * view.height + Theme.space.sm
                        label: qsTr("Path · %1").arg(route.modelData.name)
                        strong: route.chosen
                    }

                    Repeater {
                        model: route.modelData.points

                        delegate: Rectangle {
                            id: knot

                            required property var modelData

                            x: knot.modelData.x * view.width - width / 2
                            y: knot.modelData.y * view.height - height / 2
                            width: Theme.space.md
                            height: Theme.space.md
                            radius: width / 2
                            color: route.chosen ? Theme.colors.accent : Theme.colors.keyHintText
                            border.width: Theme.size.stroke
                            border.color: Theme.colors.keyHintFill

                            MouseArea {
                                property point start

                                anchors.fill: parent
                                anchors.margins: -Theme.space.xs
                                enabled: root.tool === "select"
                                cursorShape: Qt.SizeAllCursor
                                onPressed: mouse => {
                                    start = mapToItem(view, mouse.x, mouse.y)
                                    root.bench.select("path", route.modelData.name)
                                }
                                onPositionChanged: mouse => {
                                    const at = mapToItem(view, mouse.x, mouse.y)
                                    route.shiftX = at.x - start.x
                                    route.shiftY = at.y - start.y
                                }
                                onReleased: {
                                    const dx = route.shiftX / view.width
                                    const dy = route.shiftY / view.height
                                    route.shiftX = 0
                                    route.shiftY = 0
                                    if (dx !== 0 || dy !== 0)
                                        root.bench.movePath(route.modelData.name, dx, dy)
                                }
                            }
                        }
                    }
                }
            }

            Repeater {
                model: root.bench.points

                delegate: Item {
                    id: pin

                    required property var modelData
                    readonly property bool chosen: root.bench.selectedKind === "point"
                                                   && root.bench.selectedName === modelData.name
                    property real shiftX: 0
                    property real shiftY: 0

                    objectName: "pointPin"
                    x: pin.modelData.x * view.width - width / 2 + pin.shiftX
                    y: pin.modelData.y * view.height - height / 2 + pin.shiftY
                    width: Theme.size.crosshair
                    height: Theme.size.crosshair

                    Icon {
                        anchors.centerIn: parent
                        name: "crosshair"
                        size: Theme.size.crosshair
                        color: pin.chosen ? Theme.colors.accent : Theme.colors.keyHintText
                    }

                    Tag {
                        x: pin.width + Theme.space.xxs
                        y: (pin.height - height) / 2
                        label: qsTr("Point · %1").arg(pin.modelData.name)
                        strong: pin.chosen
                    }

                    MouseArea {
                        property point start

                        anchors.fill: parent
                        enabled: root.tool === "select"
                        cursorShape: Qt.SizeAllCursor
                        onPressed: mouse => {
                            start = mapToItem(view, mouse.x, mouse.y)
                            root.bench.select("point", pin.modelData.name)
                        }
                        onPositionChanged: mouse => {
                            const at = mapToItem(view, mouse.x, mouse.y)
                            pin.shiftX = at.x - start.x
                            pin.shiftY = at.y - start.y
                        }
                        onReleased: {
                            const x = (pin.modelData.x * view.width + pin.shiftX) / view.width
                            const y = (pin.modelData.y * view.height + pin.shiftY) / view.height
                            const moved = pin.shiftX !== 0 || pin.shiftY !== 0
                            pin.shiftX = 0
                            pin.shiftY = 0
                            if (moved)
                                root.bench.movePoint(pin.modelData.name, x, y)
                        }
                    }
                }
            }

            Repeater {
                model: root.bench.results

                delegate: Rectangle {
                    id: hit

                    required property var modelData
                    required property int index

                    objectName: "resultBox"
                    x: hit.modelData.x * view.width
                    y: hit.modelData.y * view.height
                    width: Math.max(Theme.size.focusRing, hit.modelData.width * view.width)
                    height: Math.max(Theme.size.focusRing, hit.modelData.height * view.height)
                    color: "transparent"
                    border.width: Theme.size.focusRing
                    border.color: Theme.colors.success

                    Rectangle {
                        x: 0
                        y: hit.height + Theme.space.xxs
                        width: scoreText.implicitWidth + 2 * Theme.space.xs
                        height: scoreText.implicitHeight + 2 * Theme.space.xxs
                        radius: Theme.radius.xs
                        color: Theme.colors.success

                        Text {
                            id: scoreText

                            anchors.centerIn: parent
                            textFormat: Text.PlainText
                            text: qsTr("#%1 %2").arg(hit.index + 1).arg(Number(hit.modelData.score).toFixed(2))
                            color: Theme.colors.textOnAccent
                            font.pixelSize: Theme.type.caption.size
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            Shape {
                objectName: "pathDraft"
                anchors.fill: parent
                visible: root.drawing.length > 0

                ShapePath {
                    strokeColor: Theme.colors.accent
                    strokeWidth: Theme.size.focusRing
                    fillColor: "transparent"

                    PathPolyline {
                        path: root.drawing.map(point => Qt.point(point.x * view.width, point.y * view.height))
                    }
                }
            }

            Repeater {
                model: root.drawing

                delegate: Rectangle {
                    id: draftKnot

                    required property var modelData

                    x: draftKnot.modelData.x * view.width - width / 2
                    y: draftKnot.modelData.y * view.height - height / 2
                    width: Theme.space.sm
                    height: Theme.space.sm
                    radius: width / 2
                    color: Theme.colors.accent
                }
            }

            Rectangle {
                objectName: "band"
                visible: root.banding
                x: root.band.x
                y: root.band.y
                width: root.band.width
                height: root.band.height
                color: Theme.colors.accentSubtle
                border.width: Theme.size.focusRing
                border.color: Theme.colors.accent
            }
        }

        Column {
            objectName: "noPicture"
            anchors.centerIn: parent
            width: Math.min(canvas.width - 2 * canvas.margin, Theme.size.infoBubble)
            visible: !root.hasPicture
            spacing: Theme.space.sm

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "monitor-smartphone"
                size: Theme.size.iconHero
                color: Theme.colors.textTertiary
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: root.bench.phones.length === 0
                      ? qsTr("No phone is mirroring. Start mirroring a phone to see its picture here.")
                      : qsTr("Waiting for the phone's picture…")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }
        }

        MouseArea {
            anchors.fill: pickBanner
            visible: pickBanner.visible
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
        }

        InlineAlert {
            id: pickBanner

            objectName: "pickBanner"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Theme.space.md
            width: Math.min(parent.width - 2 * Theme.space.xl, Theme.size.dialogWidth + Theme.size.sideNav)
            visible: (root.bench.pick.kind ?? "") !== ""
            tone: "info"
            title: root.bench.pick.prompt ?? ""
            detail: qsTr("What you pick is kept as a resource with a new name. Press Esc to stop picking")
            actionText: qsTr("Stop picking")
            onActionTriggered: root.bench.cancelPick()
        }
    }

    Workbench {
        id: workbench

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        bottomOpen: root.resultsOpen

        barContent: [
            Row {
                id: titleRow

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.md

                Text {
                    objectName: "benchName"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, Theme.size.fieldWidth)
                    textFormat: Text.PlainText
                    text: root.bench.profileName
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.DemiBold
                }

                Text {
                    objectName: "saveState"
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: root.bench.saved ? qsTr("Saved") : qsTr("Not saved")
                    color: root.bench.saved ? Theme.colors.textTertiary : Theme.colors.warningText
                    font.pixelSize: Theme.type.caption.size

                    Tooltip {
                        text: root.bench.problem
                        visible: root.bench.problem !== "" && saveHover.hovered
                    }

                    HoverHandler {
                        id: saveHover
                    }
                }
            },
            SegmentedControl {
                id: modeSwitch

                objectName: "modeSwitch"
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(titleRow.width + Theme.space.lg,
                            Math.min((parent.width - width) / 2, actions.x - width - Theme.space.lg))
                model: [{ text: qsTr("Live"), icon: "play" }, { text: qsTr("Frozen"), icon: "pause" }]
                currentIndex: root.bench.frozen ? 1 : 0
                enabled: !root.bench.busy
                Accessible.name: qsTr("Live or frozen picture")
                onActivated: index => {
                    root.bench.setFrozen(index === 1)
                    modeSwitch.currentIndex = Qt.binding(() => root.bench.frozen ? 1 : 0)
                }
            },
            Row {
                id: actions

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xs

                ToolbarButton {
                    objectName: "undo"
                    iconName: "undo-2"
                    text: qsTr("Undo")
                    shortcut: "Ctrl+Z"
                    enabled: root.bench.canUndo
                    onClicked: root.bench.undo()
                }

                ToolbarButton {
                    objectName: "redo"
                    iconName: "redo-2"
                    text: qsTr("Redo")
                    shortcut: "Ctrl+Y"
                    enabled: root.bench.canRedo
                    onClicked: root.bench.redo()
                }

                Item {
                    width: Theme.space.xs
                    height: 1
                }

                Select {
                    id: phoneSelect

                    function chosen(): int {
                        return root.bench.phones.findIndex(phone => phone.serial === root.bench.phone)
                    }

                    objectName: "phoneSelect"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.size.fieldWidth
                    enabled: root.bench.phones.length > 0
                    model: root.bench.phones.map(phone => phone.name)
                    currentIndex: phoneSelect.chosen()
                    displayText: root.bench.phones.length === 0 ? qsTr("No phone mirroring") : currentText
                    Accessible.name: qsTr("Phone")
                    onActivated: index => {
                        root.bench.setPhone(root.bench.phones[index].serial)
                        phoneSelect.currentIndex = Qt.binding(() => phoneSelect.chosen())
                    }
                }

                Button {
                    objectName: "testFind"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    tone: "primary"
                    iconName: "search"
                    text: qsTr("Test find")
                    enabled: (root.bench.selectedKind === "template" || root.bench.selectedKind === "color")
                             && !root.bench.busy
                    toolTip: qsTr("Find the selected template or color in the picture")
                    onClicked: root.bench.testFind()
                }
            }
        ]

        toolsContent: [
            ToolbarButton {
                objectName: "toolTemplate"
                iconName: "square"
                text: qsTr("Template: drag a box on the frozen picture")
                selected: root.tool === "template"
                onClicked: root.toggleTool("template")
            },
            ToolbarButton {
                objectName: "toolRegion"
                iconName: "expand"
                text: qsTr("Region: drag a box")
                selected: root.tool === "region"
                onClicked: root.toggleTool("region")
            },
            ToolbarButton {
                objectName: "toolPoint"
                iconName: "crosshair"
                text: qsTr("Point: click")
                selected: root.tool === "point"
                onClicked: root.toggleTool("point")
            },
            ToolbarButton {
                objectName: "toolPath"
                iconName: "swipe-path"
                text: qsTr("Path: click the points, double-click to finish")
                selected: root.tool === "path"
                onClicked: root.toggleTool("path")
            },
            ToolbarButton {
                objectName: "toolColor"
                iconName: "palette"
                text: qsTr("Pick a color: click the frozen picture")
                selected: root.tool === "color"
                onClicked: root.toggleTool("color")
            },
            ToolbarSeparator {},
            ToolbarButton {
                objectName: "zoomOut"
                iconName: "minus"
                text: qsTr("Zoom out")
                enabled: root.zoom > root.zoomSteps[0]
                onClicked: root.zoomStep(-1)
            },
            Button {
                id: zoomLevel

                objectName: "zoomLevel"
                anchors.verticalCenter: parent.verticalCenter
                width: zoomMetrics.advanceWidth + leftPadding + rightPadding
                small: true
                tone: "ghost"
                text: qsTr("%1%").arg(Math.round(root.zoom * 100))
                toolTip: qsTr("Fit the picture")
                Accessible.name: qsTr("Zoom %1%, fit the picture").arg(Math.round(root.zoom * 100))
                onClicked: root.zoomTo(1)

                TextMetrics {
                    id: zoomMetrics

                    font: zoomLevel.font
                    text: qsTr("%1%").arg(400)
                }
            },
            ToolbarButton {
                objectName: "zoomIn"
                iconName: "plus"
                text: qsTr("Zoom in")
                enabled: root.zoom < root.zoomSteps[root.zoomSteps.length - 1]
                onClicked: root.zoomStep(1)
            }
        ]

        sidebarContent: [
            Item {
                id: browse

                objectName: "browse"
                anchors.fill: parent
                visible: !root.bench.requesting && root.bench.selectedKind === ""

                ListView {
                    id: resourceList

                    objectName: "resourceList"
                    anchors.fill: parent
                    anchors.topMargin: Theme.space.xs
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.groupedResources

                    delegate: Rectangle {
                        id: row

                        required property var modelData
                        readonly property bool header: row.modelData.header === true

                        objectName: row.header ? "resourceGroup" : "resourceRow"
                        width: ListView.view.width
                        height: row.header ? Theme.size.tab : Theme.size.listItem + Theme.space.md
                        color: !row.header && rowHover.hovered ? Theme.colors.stateHover : "transparent"

                        Accessible.role: row.header ? Accessible.Heading : Accessible.ListItem
                        Accessible.name: row.header ? root.groupText(row.modelData.kind) + " · " + row.modelData.count
                                                    : row.modelData.name
                        Accessible.onPressAction: {
                            if (!row.header)
                                root.bench.select(row.modelData.kind, row.modelData.name)
                        }

                        Text {
                            objectName: "resourceGroupTitle"
                            x: Theme.space.lg
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Theme.space.xs
                            visible: row.header
                            textFormat: Text.PlainText
                            text: row.header ? root.groupText(row.modelData.kind) + " · " + row.modelData.count : ""
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                            font.weight: Font.DemiBold
                        }

                        HoverHandler {
                            id: rowHover

                            enabled: !row.header
                        }

                        TapHandler {
                            enabled: !row.header
                            onTapped: root.bench.select(row.modelData.kind, row.modelData.name)
                        }

                        Rectangle {
                            id: thumb

                            x: Theme.space.lg
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !row.header
                            width: Theme.size.tileLarge + Theme.space.md
                            height: Theme.size.tile
                            radius: Theme.radius.sm
                            color: Theme.colors.bgSunken
                            clip: true

                            Image {
                                anchors.fill: parent
                                anchors.margins: Theme.space.xxs
                                visible: row.modelData.kind === "template" && (row.modelData.picture ?? "") !== ""
                                source: visible ? "image://vision/" + row.modelData.picture : ""
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                            }

                            Rectangle {
                                objectName: "swatch"
                                anchors.fill: parent
                                anchors.margins: Theme.space.xs
                                visible: !row.header && row.modelData.kind === "color"
                                radius: Theme.radius.xs
                                color: !row.header && row.modelData.kind === "color" ? row.modelData.color : "transparent"
                            }

                            Icon {
                                anchors.centerIn: parent
                                visible: row.modelData.kind !== "template" && row.modelData.kind !== "color"
                                name: root.iconOf(row.modelData.kind)
                                size: Theme.size.iconSmall
                                color: Theme.colors.textSecondary
                            }
                        }

                        Column {
                            anchors.left: thumb.right
                            anchors.leftMargin: Theme.space.md
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space.lg
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !row.header

                            Text {
                                width: parent.width
                                textFormat: Text.PlainText
                                text: row.header ? "" : row.modelData.name
                                elide: Text.ElideRight
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                                font.weight: Theme.type.label.weight
                            }

                            Text {
                                width: parent.width
                                textFormat: Text.PlainText
                                text: row.header ? "" : root.detailOf(row.modelData)
                                elide: Text.ElideRight
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }
                        }
                    }
                }

                Text {
                    objectName: "emptyList"
                    x: Theme.space.lg
                    y: Theme.space.md
                    width: parent.width - 2 * Theme.space.lg
                    visible: resourceList.count === 0
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    text: qsTr("No resources yet. Freeze the picture, then use the tools below it to cut a template, draw a region, a point or a path, or pick a color.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.label.size
                }
            },
            Item {
                id: properties

                readonly property var item: root.bench.selected
                readonly property string kind: root.bench.selectedKind
                readonly property string name: root.bench.selectedName
                readonly property var regionNames: root.bench.regions.map(region => region.name)
                readonly property bool tested: root.bench.result.kind === properties.kind
                                               && root.bench.result.name === properties.name

                function rename() {
                    nameField.forceActiveFocus(Qt.ShortcutFocusReason)
                    nameField.selectAll()
                }

                objectName: "properties"
                anchors.fill: parent
                visible: !root.bench.requesting && properties.kind !== ""

                Button {
                    id: back

                    objectName: "backToList"
                    x: Theme.space.sm
                    y: Theme.space.sm
                    small: true
                    tone: "ghost"
                    iconName: "arrow-left"
                    text: qsTr("All resources")
                    onClicked: root.bench.select("", "")
                }

                Rectangle {
                    id: headerThumb

                    x: Theme.space.lg
                    y: back.y + back.height + Theme.space.md
                    width: Theme.size.tileLarge
                    height: Theme.size.tileLarge
                    radius: Theme.radius.md
                    color: Theme.colors.bgSunken
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: Theme.space.xxs
                        visible: properties.kind === "template" && (properties.item.picture ?? "") !== ""
                        source: visible ? "image://vision/" + properties.item.picture : ""
                        fillMode: Image.PreserveAspectFit
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Theme.space.xs
                        visible: properties.kind === "color"
                        radius: Theme.radius.xs
                        color: properties.kind === "color" ? (properties.item.color ?? "transparent") : "transparent"
                    }

                    Icon {
                        anchors.centerIn: parent
                        visible: properties.kind !== "template" && properties.kind !== "color"
                        name: root.iconOf(properties.kind)
                        size: Theme.size.icon
                        color: Theme.colors.textSecondary
                    }
                }

                TextField {
                    id: nameField

                    function commit() {
                        const problem = root.bench.rename(properties.kind, properties.name, nameField.text)
                        if (problem !== "")
                            toast.show(problem, "bad")
                        nameField.text = Qt.binding(() => properties.name)
                    }

                    objectName: "propertyName"
                    x: headerThumb.x + headerThumb.width + Theme.space.sm
                    y: headerThumb.y - Theme.space.xxs
                    width: more.x - x - Theme.space.sm
                    height: Theme.size.controlSmall
                    dense: true
                    bare: true
                    maximumLength: 64
                    text: properties.name
                    font.pixelSize: Theme.type.heading.size
                    font.weight: Theme.type.heading.weight
                    Accessible.name: qsTr("Name")
                    onEditingFinished: nameField.commit()
                    Keys.onEscapePressed: {
                        nameField.text = Qt.binding(() => properties.name)
                        nameField.focus = false
                    }
                }

                Text {
                    objectName: "propertyKind"
                    x: nameField.x + Theme.space.sm
                    y: nameField.y + nameField.height
                    width: nameField.width
                    textFormat: Text.PlainText
                    text: root.kindText(properties.kind)
                    elide: Text.ElideRight
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                Button {
                    id: more

                    objectName: "propertyMore"
                    x: parent.width - width - Theme.space.lg
                    y: headerThumb.y + (headerThumb.height - height) / 2
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
                            if (moreMenu.renaming) {
                                moreMenu.renaming = false
                                properties.rename()
                            }
                        }

                        MenuItem {
                            objectName: "renameResource"
                            iconName: "pencil"
                            text: qsTr("Rename")
                            onTriggered: moreMenu.renaming = true
                        }

                        MenuSeparator {}

                        MenuItem {
                            objectName: "deleteResource"
                            iconName: "trash-2"
                            danger: true
                            text: qsTr("Delete")
                            shortcut: qsTr("Del")
                            onTriggered: root.bench.remove(properties.kind, properties.name)
                        }
                    }
                }

                Rectangle {
                    id: headerLine

                    y: headerThumb.y + headerThumb.height + Theme.space.lg
                    width: parent.width
                    height: Theme.size.stroke
                    color: Theme.colors.strokeSubtle
                }

                Flickable {
                    id: form

                    objectName: "propertyForm"
                    anchors.fill: parent
                    anchors.topMargin: headerLine.y + headerLine.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentHeight: fields.implicitHeight + 2 * Theme.space.lg

                    Column {
                        id: fields

                        x: Theme.space.lg
                        y: Theme.space.lg
                        width: form.width - 2 * Theme.space.lg
                        spacing: Theme.space.lg

                        Field {
                            objectName: "thresholdField"
                            width: fields.width
                            visible: properties.kind === "template"
                            title: qsTr("Similarity threshold")

                            Item {
                                width: parent.width
                                height: Theme.size.control

                                Slider {
                                    id: thresholdSlider

                                    function store() {
                                        root.bench.setValue("template", properties.name, "threshold",
                                                            Math.round(thresholdSlider.value * 100) / 100)
                                        thresholdSlider.value = Qt.binding(() => properties.item.threshold ?? 0.8)
                                    }

                                    objectName: "thresholdSlider"
                                    width: parent.width - thresholdNumber.width - Theme.space.md
                                    height: parent.height
                                    from: 0
                                    to: 1
                                    stepSize: 0.01
                                    showValue: false
                                    value: properties.item.threshold ?? 0.8
                                    Accessible.name: qsTr("Similarity threshold")
                                    onPressedChanged: {
                                        if (!pressed)
                                            thresholdSlider.store()
                                    }
                                    onMoved: {
                                        if (!pressed)
                                            thresholdSlider.store()
                                    }
                                }

                                NumberField {
                                    id: thresholdNumber

                                    objectName: "thresholdNumber"
                                    x: parent.width - width
                                    numbers: NumberScale {
                                        minimum: 0
                                        maximum: 1
                                        step: 0.01
                                        decimals: 2
                                    }
                                    value: thresholdSlider.pressed ? thresholdSlider.value : (properties.item.threshold ?? 0.8)
                                    showsMessage: false
                                    Accessible.name: qsTr("Similarity threshold")
                                    onSubmitted: value => root.bench.setValue("template", properties.name, "threshold", value)
                                }
                            }
                        }

                        Field {
                            width: fields.width
                            visible: properties.kind === "template"
                            title: qsTr("Match by")

                            SegmentedControl {
                                id: matchSwitch

                                objectName: "matchSwitch"
                                model: [qsTr("Color", "match mode"), qsTr("Grey", "match mode")]
                                currentIndex: properties.item.match === "grey" ? 1 : 0
                                Accessible.name: qsTr("Match by")
                                onActivated: index => {
                                    root.bench.setValue("template", properties.name, "match", index === 0 ? "color" : "grey")
                                    matchSwitch.currentIndex = Qt.binding(() => properties.item.match === "grey" ? 1 : 0)
                                }
                            }
                        }

                        Field {
                            width: fields.width
                            visible: properties.kind === "template" || properties.kind === "color"
                            title: qsTr("Only in region")

                            Select {
                                id: regionChoice

                                function chosen(): int {
                                    return properties.regionNames.indexOf(properties.item.region ?? "") + 1
                                }

                                objectName: "regionChoice"
                                width: parent.width
                                model: [qsTr("Whole picture")].concat(properties.regionNames)
                                currentIndex: regionChoice.chosen()
                                Accessible.name: qsTr("Only in region")
                                onActivated: index => {
                                    root.bench.setValue(properties.kind, properties.name, "region",
                                                        index === 0 ? "" : properties.regionNames[index - 1])
                                    regionChoice.currentIndex = Qt.binding(() => regionChoice.chosen())
                                }
                            }
                        }

                        Text {
                            width: fields.width
                            visible: properties.kind === "template"
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            text: (properties.item.sourceWidth ?? 0) > 0
                                  ? qsTr("%1 × %2 pixels, cut from a %3 × %4 picture; it is scaled when the picture size differs")
                                    .arg(properties.item.width ?? 0).arg(properties.item.height ?? 0)
                                    .arg(properties.item.sourceWidth).arg(properties.item.sourceHeight)
                                  : qsTr("%1 × %2 pixels").arg(properties.item.width ?? 0).arg(properties.item.height ?? 0)
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                        }

                        Field {
                            width: fields.width
                            visible: properties.kind === "color"
                            title: qsTr("Color")

                            Row {
                                spacing: Theme.space.sm

                                Rectangle {
                                    objectName: "colorSwatch"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.size.control
                                    height: Theme.size.control
                                    radius: Theme.radius.sm
                                    color: properties.kind === "color" ? (properties.item.color ?? "transparent") : "transparent"
                                    border.width: Theme.size.stroke
                                    border.color: Theme.colors.strokeDefault
                                }

                                TextField {
                                    id: hexField

                                    objectName: "colorHex"
                                    width: Theme.size.numberFieldWide
                                    mono: true
                                    maximumLength: 7
                                    text: properties.item.color ?? ""
                                    Accessible.name: qsTr("Color")
                                    onEditingFinished: {
                                        if (!root.bench.setValue("color", properties.name, "color", hexField.text))
                                            toast.show(qsTr("Write the color as #RRGGBB"), "bad")
                                        hexField.text = Qt.binding(() => properties.item.color ?? "")
                                    }
                                }
                            }
                        }

                        Field {
                            width: fields.width
                            visible: properties.kind === "color"
                            title: qsTr("Tolerance")

                            Item {
                                width: parent.width
                                height: Theme.size.control

                                Slider {
                                    id: toleranceSlider

                                    function store() {
                                        root.bench.setValue("color", properties.name, "tolerance",
                                                            Math.round(toleranceSlider.value * 100) / 100)
                                        toleranceSlider.value = Qt.binding(() => properties.item.tolerance ?? 0.1)
                                    }

                                    objectName: "toleranceSlider"
                                    width: parent.width - toleranceNumber.width - Theme.space.md
                                    height: parent.height
                                    from: 0
                                    to: 1
                                    stepSize: 0.01
                                    showValue: false
                                    value: properties.item.tolerance ?? 0.1
                                    Accessible.name: qsTr("Tolerance")
                                    onPressedChanged: {
                                        if (!pressed)
                                            toleranceSlider.store()
                                    }
                                    onMoved: {
                                        if (!pressed)
                                            toleranceSlider.store()
                                    }
                                }

                                NumberField {
                                    id: toleranceNumber

                                    objectName: "toleranceNumber"
                                    x: parent.width - width
                                    numbers: NumberScale {
                                        minimum: 0
                                        maximum: 1
                                        step: 0.01
                                        decimals: 2
                                    }
                                    value: toleranceSlider.pressed ? toleranceSlider.value : (properties.item.tolerance ?? 0.1)
                                    showsMessage: false
                                    Accessible.name: qsTr("Tolerance")
                                    onSubmitted: value => root.bench.setValue("color", properties.name, "tolerance", value)
                                }
                            }
                        }

                        Field {
                            width: fields.width
                            visible: properties.kind === "region"
                            title: qsTr("Left and top")

                            Row {
                                spacing: Theme.space.sm

                                RatioField {
                                    objectName: "regionX"
                                    prefix: "x"
                                    value: properties.item.x ?? 0
                                    Accessible.name: qsTr("Left")
                                    onSubmitted: value => root.bench.setValue("region", properties.name, "x", value)
                                }

                                RatioField {
                                    objectName: "regionY"
                                    prefix: "y"
                                    value: properties.item.y ?? 0
                                    Accessible.name: qsTr("Top")
                                    onSubmitted: value => root.bench.setValue("region", properties.name, "y", value)
                                }
                            }
                        }

                        Field {
                            width: fields.width
                            visible: properties.kind === "region"
                            title: qsTr("Width and height")

                            Row {
                                spacing: Theme.space.sm

                                RatioField {
                                    objectName: "regionWidth"
                                    prefix: "w"
                                    value: properties.item.width ?? 0
                                    Accessible.name: qsTr("Width")
                                    onSubmitted: value => root.bench.setValue("region", properties.name, "width", value)
                                }

                                RatioField {
                                    objectName: "regionHeight"
                                    prefix: "h"
                                    value: properties.item.height ?? 0
                                    Accessible.name: qsTr("Height")
                                    onSubmitted: value => root.bench.setValue("region", properties.name, "height", value)
                                }
                            }
                        }

                        Field {
                            width: fields.width
                            visible: properties.kind === "point"
                            title: qsTr("Position")

                            Row {
                                spacing: Theme.space.sm

                                RatioField {
                                    objectName: "pointX"
                                    prefix: "x"
                                    value: properties.item.x ?? 0
                                    Accessible.name: qsTr("Left")
                                    onSubmitted: value => root.bench.setValue("point", properties.name, "x", value)
                                }

                                RatioField {
                                    objectName: "pointY"
                                    prefix: "y"
                                    value: properties.item.y ?? 0
                                    Accessible.name: qsTr("Top")
                                    onSubmitted: value => root.bench.setValue("point", properties.name, "y", value)
                                }
                            }
                        }

                        Field {
                            width: fields.width
                            visible: properties.kind === "path"
                            title: qsTr("Swipe duration")

                            NumberField {
                                objectName: "pathDuration"
                                width: Theme.size.numberFieldWide
                                unit: qsTr("ms")
                                numbers: NumberScale {
                                    minimum: 1
                                    maximum: 60000
                                    step: 50
                                }
                                value: properties.item.durationMs ?? 300
                                Accessible.name: qsTr("Swipe duration")
                                onSubmitted: value => root.bench.setValue("path", properties.name, "durationMs", value)
                            }
                        }

                        Field {
                            width: fields.width
                            visible: properties.kind === "path"
                            title: qsTr("Points", "path")

                            Column {
                                width: parent.width
                                spacing: Theme.space.xs

                                Repeater {
                                    model: properties.kind === "path" ? properties.item.points ?? [] : []

                                    delegate: Row {
                                        id: pointRow

                                        required property var modelData
                                        required property int index

                                        function store(axis: string, value: real) {
                                            const points = properties.item.points.map(point => ({ x: point.x, y: point.y }))
                                            points[pointRow.index][axis] = value
                                            root.bench.setValue("path", properties.name, "points", points)
                                        }

                                        objectName: "pathPoint"
                                        spacing: Theme.space.sm

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Theme.space.x3l
                                            textFormat: Text.PlainText
                                            text: qsTr("#%1").arg(pointRow.index + 1)
                                            color: Theme.colors.textTertiary
                                            font.pixelSize: Theme.type.caption.size
                                        }

                                        RatioField {
                                            prefix: "x"
                                            value: pointRow.modelData.x
                                            Accessible.name: qsTr("Left of point %1").arg(pointRow.index + 1)
                                            onSubmitted: value => pointRow.store("x", value)
                                        }

                                        RatioField {
                                            prefix: "y"
                                            value: pointRow.modelData.y
                                            Accessible.name: qsTr("Top of point %1").arg(pointRow.index + 1)
                                            onSubmitted: value => pointRow.store("y", value)
                                        }
                                    }
                                }
                            }
                        }

                        InlineAlert {
                            objectName: "testResult"
                            width: fields.width
                            visible: properties.tested
                            tone: (root.bench.result.problem ?? "") !== "" ? "bad"
                                                                          : (root.bench.result.count ?? 0) > 0 ? "info" : "warn"
                            title: root.summaryText()
                            detail: root.bench.results.length > 0
                                    ? qsTr("(%1, %2) · similarity %3").arg(root.bench.results[0].centerX)
                                      .arg(root.bench.results[0].centerY).arg(Number(root.bench.results[0].score).toFixed(2))
                                    : ""
                        }
                    }
                }
            },
            Item {
                id: materialsPanel

                objectName: "materialsPanel"
                anchors.fill: parent
                visible: root.bench.requesting

                MaterialsList {
                    anchors.fill: parent
                    bench: root.bench
                }
            }
        ]

        bottomContent: [
            Item {
                objectName: "testResults"
                anchors.fill: parent

                Text {
                    id: resultsTitle

                    x: Theme.space.lg
                    y: Theme.space.md
                    textFormat: Text.PlainText
                    text: qsTr("Test results")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                }

                Text {
                    objectName: "resultSummary"
                    anchors.left: resultsTitle.right
                    anchors.leftMargin: Theme.space.md
                    anchors.right: closeResults.left
                    anchors.rightMargin: Theme.space.md
                    anchors.baseline: resultsTitle.baseline
                    textFormat: Text.PlainText
                    text: root.summaryText()
                    elide: Text.ElideRight
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }

                ToolbarButton {
                    id: closeResults

                    objectName: "closeResults"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.sm
                    y: Theme.space.xxs
                    compact: true
                    iconName: "x"
                    text: qsTr("Close")
                    onClicked: root.resultsOpen = false
                }

                ListView {
                    objectName: "resultList"
                    anchors.fill: parent
                    anchors.topMargin: resultsTitle.y + resultsTitle.height + Theme.space.sm
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.bench.results

                    delegate: Item {
                        id: resultRow

                        required property var modelData
                        required property int index

                        objectName: "resultRow"
                        width: ListView.view.width
                        height: Theme.size.listItem

                        Text {
                            id: resultIndex

                            x: Theme.space.lg
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.space.x3l
                            textFormat: Text.PlainText
                            text: qsTr("#%1").arg(resultRow.index + 1)
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                        }

                        Text {
                            anchors.left: resultIndex.right
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: qsTr("(%1, %2) · similarity %3").arg(resultRow.modelData.centerX)
                                  .arg(resultRow.modelData.centerY).arg(Number(resultRow.modelData.score).toFixed(2))
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                        }
                    }
                }
            }
        ]
    }

    Dialog {
        id: renameDialog

        property var renaming: ({})
        readonly property var places: renameDialog.renaming.places ?? []
        readonly property var unsaved: renameDialog.renaming.unsaved ?? []

        function ask() {
            if ((root.bench.renaming.name ?? "") === "")
                return
            renameDialog.renaming = root.bench.renaming
            renameDialog.open()
        }

        function finish(alsoLogic: bool) {
            const problem = root.bench.finishRename(alsoLogic)
            if (problem !== "")
                toast.show(problem, "bad")
        }

        objectName: "renameReferences"
        width: parent ? Math.min(Theme.size.palette, parent.width - 2 * Theme.space.lg) : Theme.size.palette
        title: qsTr("These places use %1").arg(renameDialog.renaming.name ?? "")
        acceptText: qsTr("Change them too")
        alternativeText: qsTr("Only rename")
        onAccepted: renameDialog.finish(true)
        onAlternativeChosen: renameDialog.finish(false)
        onRejected: root.bench.cancelRename()

        Column {
            width: parent.width
            spacing: Theme.space.md

            Text {
                objectName: "renameNote"
                width: parent.width
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("Renaming to %1. If you only rename, these places no longer find it.")
                      .arg(renameDialog.renaming.wanted ?? "")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }

            ListView {
                objectName: "renamePlaces"
                width: parent.width
                height: Math.min(contentHeight, Theme.size.paletteListMax)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: renameDialog.places

                delegate: Item {
                    id: placeRow

                    required property var modelData

                    objectName: "renamePlace"
                    width: ListView.view.width
                    height: Theme.size.listItem

                    Text {
                        id: placeName

                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width / 3)
                        textFormat: Text.PlainText
                        text: placeRow.modelData.place
                        elide: Text.ElideMiddle
                        color: placeRow.modelData.unsaved === true ? Theme.colors.warningText : Theme.colors.textTertiary
                        font.family: Theme.type.monoFamily
                        font.pixelSize: Theme.type.caption.size
                    }

                    Text {
                        anchors.left: placeName.right
                        anchors.leftMargin: Theme.space.md
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: placeRow.modelData.text
                        elide: Text.ElideRight
                        color: Theme.colors.textPrimary
                        font.family: Theme.type.monoFamily
                        font.pixelSize: Theme.type.mono.size
                    }
                }
            }

            Text {
                objectName: "renameMore"
                width: parent.width
                visible: (renameDialog.renaming.more ?? 0) > 0
                textFormat: Text.PlainText
                text: qsTr("and %n more", "", renameDialog.renaming.more ?? 0)
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            InlineAlert {
                objectName: "renameUnsaved"
                width: parent.width
                visible: renameDialog.unsaved.length > 0
                tone: "warn"
                title: qsTr("%1 has changes that are not saved in the logic editor, so it is left out", "",
                            renameDialog.unsaved.length).arg(renameDialog.unsaved.join(qsTr(", ", "list separator")))
            }

            InlineAlert {
                objectName: "renameShared"
                width: parent.width
                visible: renameDialog.renaming.shared === true
                tone: "warn"
                title: qsTr("Another kind of resource is also named %1, so some of these places may mean that one")
                       .arg(renameDialog.renaming.name ?? "")
            }
        }
    }

    Toast {
        id: toast
    }

    AiCorner {
        objectName: "visionAiCorner"
        window: "vision"
        bubbleBottom: root.bench.requesting ? Theme.size.control + 2 * Theme.space.lg : Theme.space.lg
        context: ({
            serial: root.bench.phone,
            profile: root.bench.profileId,
            profileName: root.bench.profileName,
            selection: root.bench.selectedKind !== "" ? root.bench.selectedKind + " " + root.bench.selectedName : ""
        })
    }

    Component.onCompleted: chrome.attach()
}
