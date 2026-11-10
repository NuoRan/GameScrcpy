pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: tour

    property string tourKey: ""
    property var settings: null
    property var steps: []
    property bool startWhen: false
    property bool advance: false
    property int version: 0
    property int step: -1
    property bool running: false
    property int stepVersion: 0
    property bool pendingNext: false
    property int turnsLeft: 0
    property bool offering: false
    property int lostTicks: 0
    property bool manualArrival: false
    property bool held: false
    property bool placedOnce: false
    property var cornerItem: null
    readonly property int watchTicks: 10

    readonly property Item targetItem: targetValue
    readonly property var current: tour.running && tour.step >= 0 && tour.step < tour.stepCount
                                     ? tour.steps[tour.step] : null
    readonly property int stepCount: (tour.steps ?? []).length
    readonly property bool centered: tour.current !== null && tour.current.centered === true
    readonly property bool missing: tour.targetItem === null
    readonly property bool last: tour.step === tour.stepCount - 1
    readonly property var keys: {
        const value = tour.current !== null ? tour.current.keys : undefined
        if (value === undefined || value === null || value === "")
            return []
        return Array.isArray(value) ? value : [value]
    }
    readonly property bool shown: tour.running && tour.visible
    readonly property bool quiet: tour.settings !== null && tour.settings.toursQuiet === true
    readonly property bool hostShown: tour.parent !== null && tour.parent.visible && tour.width > 0
                                      && tour.height > 0
    readonly property rect hole: {
        const item = tour.targetItem
        if (item === null)
            return Qt.rect(0, 0, 0, 0)
        const spot = item.parent !== null ? item.parent.mapToItem(tour, item.x, item.y) : item.mapToItem(tour, 0, 0)
        return Qt.rect(Math.round(spot.x), Math.round(spot.y), Math.round(item.width), Math.round(item.height))
    }
    readonly property real bubbleWidth: Math.max(Theme.space.x6l,
                                                 Math.min(Theme.size.dialogWidth, tour.width - 2 * Theme.space.lg))
    readonly property real cardHeight: bubble.height
    readonly property point cardSpot: {
        const margin = Theme.space.lg
        const gap = Theme.space.md
        const clampX = x => Math.max(margin, Math.min(x, tour.width - tour.bubbleWidth - margin))
        const clampY = y => Math.max(margin, Math.min(y, tour.height - tour.cardHeight - margin))
        if (tour.missing)
            return Qt.point(clampX((tour.width - tour.bubbleWidth) / 2), clampY((tour.height - tour.cardHeight) / 2))
        const spot = tour.hole
        const middleX = clampX(spot.x + spot.width / 2 - tour.bubbleWidth / 2)
        if (spot.y + spot.height + gap + tour.cardHeight + margin <= tour.height)
            return Qt.point(middleX, spot.y + spot.height + gap)
        if (spot.y - gap - tour.cardHeight >= margin)
            return Qt.point(middleX, spot.y - gap - tour.cardHeight)
        const middleY = clampY(spot.y + spot.height / 2 - tour.cardHeight / 2)
        if (spot.x + spot.width + gap + tour.bubbleWidth + margin <= tour.width)
            return Qt.point(spot.x + spot.width + gap, middleY)
        if (spot.x - gap - tour.bubbleWidth >= margin)
            return Qt.point(spot.x - gap - tour.bubbleWidth, middleY)
        return Qt.point(clampX(spot.x + spot.width - tour.bubbleWidth - gap),
                        clampY(spot.y + spot.height - tour.cardHeight - gap))
    }
    property Item targetValue: null

    signal started()
    signal ended(bool completed)

    visible: tour.running && !tour.quiet
    z: 10000

    function seen(): bool {
        if (tour.tourKey === "")
            return false
        if (tour.settings === null)
            return false
        return tour.settings.tourSeen(tour.tourKey)
    }

    function wanted(): bool {
        return !tour.running && tour.startWhen && tour.steps.length > 0 && !tour.quiet && tour.hostShown
               && !tour.seen()
    }

    function offer() {
        if (tour.offering || !tour.wanted())
            return
        tour.offering = true
        Qt.callLater(tour.startOffered)
    }

    function startOffered() {
        if (!tour.offering)
            return
        tour.offering = false
        if (tour.wanted())
            tour.start()
    }

    function start() {
        if (tour.running || tour.steps.length === 0)
            return
        tour.running = true
        tour.lostTicks = 0
        tour.pendingNext = false
        tour.manualArrival = false
        tour.held = false
        tour.placedOnce = false
        tour.holdCorner(true)
        tour.started()
        tour.step = 0
        Qt.callLater(tour.settle)
    }

    function settle() {
        tour.placedOnce = tour.running
    }

    function settleHeld() {
        if (tour.held && !tour.advance)
            tour.held = false
    }

    function findCorner(root: Item): Item {
        if (root === null)
            return null
        if (root.objectName === "aiCorner" || root.objectName.endsWith("AiCorner"))
            return root
        const kids = root.children
        for (let index = 0; index < kids.length; ++index) {
            const found = tour.findCorner(kids[index])
            if (found !== null)
                return found
        }
        return null
    }

    function holdCorner(on: bool) {
        if (on) {
            const root = tour.Window.contentItem
            tour.cornerItem = root !== null ? tour.findCorner(root) : null
            if (tour.cornerItem !== null && tour.cornerItem.tours !== undefined)
                tour.cornerItem.tours += 1
            else
                tour.cornerItem = null
            return
        }
        if (tour.cornerItem !== null) {
            tour.cornerItem.tours = Math.max(0, tour.cornerItem.tours - 1)
            tour.cornerItem = null
        }
    }

    function available(index: int): bool {
        const entry = tour.steps[index]
        if (entry === null || entry === undefined || entry.optional !== true)
            return true
        const candidate = typeof entry.target === "function" ? entry.target() : entry.target
        return candidate !== null && candidate !== undefined && typeof candidate === "object" && tour.shownIn(candidate)
    }

    function next() {
        if (!tour.running)
            return
        let index = tour.step + 1
        while (index < tour.steps.length && !tour.available(index))
            ++index
        if (index >= tour.steps.length) {
            tour.stop(true)
            return
        }
        tour.lostTicks = 0
        tour.step = index
    }

    function forward() {
        if (!tour.running)
            return
        tour.pendingNext = false
        tour.manualArrival = true
        tour.next()
        tour.manualArrival = false
    }

    function previous() {
        if (!tour.running || tour.step <= 0)
            return
        let index = tour.step - 1
        while (index > 0 && !tour.available(index))
            --index
        if (!tour.available(index))
            return
        tour.pendingNext = false
        tour.manualArrival = true
        tour.lostTicks = 0
        tour.step = index
        tour.manualArrival = false
    }

    function skip() {
        tour.stop(false)
    }

    function restart() {
        if (tour.steps.length === 0)
            return
        tour.cancel()
        tour.turnsLeft = 10
        Qt.callLater(tour.startWhenReady)
    }

    function startWhenReady() {
        if (tour.turnsLeft <= 0)
            return
        if (tour.width <= 0 || tour.height <= 0) {
            tour.turnsLeft -= 1
            Qt.callLater(tour.startWhenReady)
            return
        }
        tour.turnsLeft = 0
        tour.start()
    }

    function cancel() {
        if (!tour.running)
            return
        if (tour.pendingNext) {
            tour.pendingNext = false
            tour.next()
            if (!tour.running)
                return
        }
        tour.running = false
        tour.pendingNext = false
        tour.held = false
        tour.holdCorner(false)
        tour.step = -1
        tour.targetValue = null
        tour.ended(false)
    }

    function stop(completed: bool) {
        if (!tour.running)
            return
        tour.running = false
        tour.pendingNext = false
        tour.held = false
        tour.holdCorner(false)
        tour.step = -1
        tour.targetValue = null
        if (tour.tourKey !== "" && tour.settings !== null)
            tour.settings.markTourSeen(tour.tourKey)
        tour.ended(completed)
    }

    function shownIn(item: Item): bool {
        if (item === null)
            return false
        let node = item
        while (node !== null && node !== undefined) {
            if (!node.visible || node.width <= 0 || node.height <= 0)
                return false
            node = node.parent
        }
        return true
    }

    function findItem(root: Item, name: string): Item {
        if (root === null)
            return null
        if (root.objectName === name)
            return root
        const kids = root.children
        for (let index = 0; index < kids.length; ++index) {
            const found = tour.findItem(kids[index], name)
            if (found !== null)
                return found
        }
        return null
    }

    function findTarget(): Item {
        if (!tour.running || tour.step < 0 || tour.step >= (tour.steps ?? []).length)
            return null
        const entry = tour.steps[tour.step]
        if (entry === null || entry === undefined || entry.centered === true)
            return null
        const candidate = typeof entry.target === "function" ? entry.target() : entry.target
        if (candidate === null || candidate === undefined || typeof candidate !== "object")
            return null
        if (!tour.shownIn(candidate))
            return null
        return candidate
    }

    function place() {
        if (!tour.running)
            return
        const found = tour.findTarget()
        tour.targetValue = found
        if (found !== null)
            tour.lostTicks = 0
    }

    function watch() {
        if (tour.centered)
            return
        if (tour.targetItem !== null && tour.shownIn(tour.targetItem))
            return
        tour.place()
        if (tour.targetItem !== null)
            return
        tour.lostTicks += 1
        if (tour.lostTicks >= tour.watchTicks)
            tour.next()
    }

    function checkAdvance() {
        if (!tour.running || !tour.advance || tour.pendingNext)
            return
        tour.pendingNext = true
        Qt.callLater(tour.runPending)
    }

    function runPending() {
        if (!tour.pendingNext)
            return
        tour.pendingNext = false
        if (tour.running && tour.advance && !tour.held)
            tour.next()
    }

    onStartWhenChanged: tour.offer()
    onStepChanged: {
        tour.stepVersion = tour.version
        tour.held = tour.manualArrival
        if (tour.held)
            Qt.callLater(tour.settleHeld)
        tour.place()
        if (tour.running)
            cardIn.restart()
        if (!tour.held)
            tour.checkAdvance()
    }
    onVersionChanged: {
        if (tour.version === tour.stepVersion)
            return
        tour.stepVersion = tour.version
        tour.place()
    }
    onAdvanceChanged: {
        if (!tour.advance) {
            tour.held = false
            return
        }
        if (!tour.held)
            tour.checkAdvance()
    }
    onHostShownChanged: {
        if (tour.hostShown)
            tour.offer()
        else
            tour.cancel()
    }
    Component.onCompleted: tour.offer()

    Shortcut {
        sequence: "Escape"
        enabled: tour.shown
        onActivated: tour.skip()
    }

    Timer {
        interval: Theme.motion.tick / tour.watchTicks
        repeat: true
        running: tour.running && tour.hostShown && !tour.quiet
        onTriggered: tour.watch()
    }

    ParallelAnimation {
        id: cardIn

        NumberAnimation {
            target: bubble
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.reducedMotion ? Theme.motion.fade : Theme.motion.medium
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: slider
            property: "y"
            from: Theme.reducedMotion ? 0 : Theme.space.sm
            to: 0
            duration: Theme.motion.medium
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.emphasized
        }
    }

    component Shade: Rectangle {
        color: Theme.colors.scrim

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            onWheel: wheel => wheel.accepted = true
        }

        Behavior on x {
            enabled: !Theme.reducedMotion
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        Behavior on y {
            enabled: !Theme.reducedMotion
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        Behavior on width {
            enabled: !Theme.reducedMotion
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        Behavior on height {
            enabled: !Theme.reducedMotion
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
    }

    Shade {
        id: shadeTop

        x: 0
        y: 0
        width: tour.width
        height: tour.hole.y
    }

    Shade {
        id: shadeBottom

        x: 0
        y: tour.hole.y + tour.hole.height
        width: tour.width
        height: Math.max(0, tour.height - tour.hole.y - tour.hole.height)
    }

    Shade {
        id: shadeLeft

        x: 0
        y: tour.hole.y
        width: tour.hole.x
        height: tour.hole.height
    }

    Shade {
        id: shadeRight

        x: tour.hole.x + tour.hole.width
        y: tour.hole.y
        width: Math.max(0, tour.width - tour.hole.x - tour.hole.width)
        height: tour.hole.height
    }

    Rectangle {
        id: glow

        property real breath: 0

        visible: !tour.missing && !Theme.reducedMotion
        x: ring.x - glow.breath * Theme.space.md
        y: ring.y - glow.breath * Theme.space.md
        width: ring.width + 2 * glow.breath * Theme.space.md
        height: ring.height + 2 * glow.breath * Theme.space.md
        radius: ring.radius + glow.breath * Theme.space.md
        color: "transparent"
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accentEdge
        opacity: 1 - glow.breath

        SequentialAnimation on breath {
            running: tour.shown && !tour.missing && !Theme.reducedMotion
            loops: Animation.Infinite

            NumberAnimation {
                from: 0
                to: 1
                duration: Theme.motion.pulse
                easing.type: Easing.OutCubic
            }
            PauseAnimation {
                duration: Theme.motion.pulse / 2
            }
        }
    }

    Rectangle {
        id: ring

        visible: !tour.missing
        x: tour.hole.x - Theme.size.stroke
        y: tour.hole.y - Theme.size.stroke
        width: tour.hole.width + 2 * Theme.size.stroke
        height: tour.hole.height + 2 * Theme.size.stroke
        radius: Theme.radius.md
        color: "transparent"
        border.width: Theme.size.focusRing
        border.color: Theme.colors.accentEdge

        Behavior on x {
            enabled: !Theme.reducedMotion
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        Behavior on y {
            enabled: !Theme.reducedMotion
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        Behavior on width {
            enabled: !Theme.reducedMotion
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        Behavior on height {
            enabled: !Theme.reducedMotion
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
    }

    Item {
        id: bubble

        objectName: "tourCard"
        x: tour.cardSpot.x
        y: tour.cardSpot.y
        width: tour.bubbleWidth
        height: content.implicitHeight + 2 * Theme.space.xl
        transform: Translate {
            id: slider
        }

        Accessible.role: Accessible.Dialog
        Accessible.name: title.text !== "" ? title.text : sentence.text

        Behavior on x {
            enabled: !Theme.reducedMotion && tour.placedOnce
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }
        Behavior on y {
            enabled: !Theme.reducedMotion && tour.placedOnce
            NumberAnimation {
                duration: Theme.motion.medium
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.emphasized
            }
        }

        PopupSurface {
            anchors.fill: parent
            radius: Theme.radius.lg
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: wheel => wheel.accepted = true
        }

        Column {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space.xl
            anchors.rightMargin: Theme.space.xl
            spacing: Theme.space.sm

            Text {
                objectName: "counter"
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("Step %1 of %2").arg(tour.step + 1).arg(tour.stepCount)
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                font.weight: Font.DemiBold
            }

            Text {
                id: title

                objectName: "title"
                width: parent.width
                visible: text !== ""
                textFormat: Text.PlainText
                text: tour.current !== null ? (tour.current.title ?? "") : ""
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.heading.size
                font.weight: Theme.type.heading.weight
                wrapMode: Text.Wrap
            }

            Text {
                id: sentence

                objectName: "sentence"
                width: parent.width
                textFormat: Text.PlainText
                text: tour.current !== null ? (tour.current.text ?? "") : ""
                color: title.visible ? Theme.colors.textSecondary : Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
                lineHeight: Theme.type.body.lineHeight
                lineHeightMode: Text.FixedHeight
                wrapMode: Text.Wrap
            }

            Flow {
                objectName: "keys"
                width: parent.width
                visible: tour.keys.length > 0
                spacing: Theme.space.xs

                Text {
                    height: Theme.size.keycap
                    verticalAlignment: Text.AlignVCenter
                    textFormat: Text.PlainText
                    text: qsTr("Shortcut")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                Repeater {
                    model: tour.keys

                    Keycap {
                        required property var modelData

                        text: String(modelData)
                    }
                }
            }

            Item {
                width: parent.width
                height: Math.max(dots.visible ? dots.height : 0, buttons.height)

                Row {
                    id: dots

                    objectName: "dots"
                    visible: dots.implicitWidth + buttons.implicitWidth + Theme.space.md <= parent.width
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.xs

                    Repeater {
                        model: tour.stepCount

                        Rectangle {
                            required property int index

                            width: index === tour.step ? Theme.space.lg : Theme.space.xs + Theme.space.xxs
                            height: Theme.space.xs + Theme.space.xxs
                            radius: height / 2
                            color: index === tour.step ? Theme.colors.accent
                                 : index < tour.step ? Theme.colors.accentLine
                                                     : Theme.colors.strokeStrong

                            Behavior on width {
                                enabled: !Theme.reducedMotion
                                NumberAnimation {
                                    duration: Theme.motion.base
                                }
                            }
                        }
                    }
                }

                Row {
                    id: buttons

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space.xs

                    Button {
                        objectName: "skip"
                        visible: !tour.last
                        small: true
                        tone: "ghost"
                        focusPolicy: Qt.NoFocus
                        text: qsTr("Skip")
                        onClicked: tour.skip()
                    }

                    Button {
                        objectName: "previous"
                        visible: tour.step > 0
                        small: true
                        focusPolicy: Qt.NoFocus
                        text: qsTr("Back")
                        onClicked: tour.previous()
                    }

                    Button {
                        objectName: "next"
                        small: true
                        tone: "primary"
                        focusPolicy: Qt.NoFocus
                        text: tour.last ? qsTr("Done") : qsTr("Next")
                        onClicked: tour.forward()
                    }
                }
            }
        }
    }
}
