pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

T.Popup {
    id: guide

    property int page: 0
    property string vendorId: "other"
    property int step: 0
    property bool playing: true

    readonly property var vendors: setup.vendors
    readonly property var vendor: setup.vendor
    readonly property var steps: setup.steps
    readonly property var current: guide.steps[Math.min(guide.step, guide.steps.length - 1)]
    readonly property var lead: DeviceManager.leadDevice
    readonly property string leadState: guide.lead.state ?? ""
    readonly property string leadName: guide.lead.name ?? ""
    readonly property bool waiting: guide.leadState === "" || guide.leadState === "offline"
    readonly property bool found: guide.leadState === "ready" || guide.leadState === "busy"
                                  || guide.leadState === "streaming"
    readonly property bool canMirror: guide.leadState === "ready"
                                      || (guide.leadState === "failed" && guide.lead.canConnect === true)

    readonly property string connectTitle: {
        switch (guide.leadState) {
        case "unauthorized":
            return qsTr("Allow this computer on the phone")
        case "ready":
            return qsTr("%1 is ready").arg(guide.leadName)
        case "busy":
            return qsTr("Connecting to %1").arg(guide.leadName)
        case "streaming":
            return qsTr("%1 is on screen").arg(guide.leadName)
        case "failed":
            return qsTr("Could not connect to %1").arg(guide.leadName)
        default:
            return qsTr("Connect your phone")
        }
    }

    readonly property string connectDetail: {
        switch (guide.leadState) {
        case "offline":
            return qsTr("%1 is plugged in but not answering. Unplug the cable and plug it in again.")
                       .arg(guide.leadName)
        case "unauthorized":
            return qsTr("Unlock the phone. When it asks “Allow USB debugging?”, tick “Always allow from this computer” and tap Allow.")
        case "ready":
            return qsTr("Start mirroring to see and control the phone on this computer.")
        case "busy":
            return qsTr("The first connection takes a few seconds.")
        case "streaming":
            return qsTr("Its window is open. You can close this guide.")
        case "failed":
            return qsTr("%1. %2").arg(guide.lead.problem ?? "").arg(guide.lead.action ?? "")
        default:
            return qsTr("Plug the phone into this computer with a USB cable. It shows up here by itself.")
        }
    }

    readonly property string primaryText: {
        if (guide.page === 0)
            return qsTr("Get started")
        if (guide.page === 1)
            return qsTr("Next")
        if (guide.canMirror)
            return guide.leadState === "failed" ? qsTr("Try again") : qsTr("Start mirroring")
        return qsTr("Done")
    }

    function text(entry: var): string {
        return setup.text(entry)
    }

    function choose(vendorId: string) {
        guide.vendorId = vendorId
        guide.step = 0
        guide.playing = true
    }

    function show(start: int, vendorId: string, stepScreen: string) {
        guide.page = start
        const fromLead = guide.lead.vendor ?? ""
        guide.choose(vendorId !== "" ? vendorId : fromLead !== "" ? fromLead : guide.vendorId)
        const at = guide.steps.findIndex(entry => entry.screen === stepScreen)
        if (at >= 0) {
            guide.step = at
            guide.playing = false
        }
        guide.open()
    }

    function showSteps() {
        const parts = (guide.lead.guide ?? "").split("/")
        guide.show(1, parts[0] ?? "", parts[1] ?? "")
    }

    function advance() {
        if (guide.page < 2) {
            guide.page += 1
            return
        }
        if (guide.canMirror) {
            const serial = guide.lead.serial ?? ""
            guide.close()
            Commands.trigger("device.mirror", serial)
            return
        }
        guide.close()
    }

    function connectWirelessly() {
        guide.close()
        Commands.trigger("wireless.pair")
    }

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.size.guideWidth, (guide.parent?.width ?? Theme.size.guideWidth) - 2 * Theme.space.x3l)
    height: Math.min(Theme.size.guideHeight, (guide.parent?.height ?? Theme.size.guideHeight) - 2 * Theme.space.x3l)
    modal: true
    focus: true
    padding: 0
    closePolicy: T.Popup.CloseOnEscape
    onClosed: AppSettings.markSetupGuideShown()

    Timer {
        interval: Theme.motion.guideStep
        repeat: true
        running: guide.opened && guide.page === 1 && guide.playing && !Theme.reducedMotion && !stepsHover.hovered
        onTriggered: guide.step = (guide.step + 1) % guide.steps.length
    }

    SetupSteps {
        id: setup

        vendorId: guide.vendorId
    }

    component GuidePage: Item {
        id: section

        property bool shown: false

        opacity: section.shown ? 1 : 0
        visible: section.opacity > 0
        transform: Translate {
            y: section.shown ? 0 : Theme.space.sm

            Behavior on y {
                NumberAnimation {
                    duration: Theme.motion.base
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.motion.standard
                }
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }
    }

    component Feature: Row {
        id: feature

        property string iconName
        property string title
        property string detail

        spacing: Theme.space.md

        Rectangle {
            id: tile

            width: Theme.size.guideTile
            height: width
            radius: Theme.radius.md
            color: Theme.colors.bgOverlay
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle

            Icon {
                anchors.centerIn: parent
                name: feature.iconName
                size: Theme.size.icon
                color: Theme.colors.accentText
            }
        }

        Column {
            width: feature.width - tile.width - feature.spacing
            spacing: Theme.space.xxs

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: feature.title
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.Medium
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: feature.detail
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }
    }

    component Chip: T.Button {
        id: chip

        property bool selected: false

        implicitWidth: chipLabel.implicitWidth + 2 * Theme.space.md
        implicitHeight: Theme.size.controlSmall
        hoverEnabled: true
        font.pixelSize: Theme.type.caption.size
        font.weight: chip.selected ? Font.DemiBold : Font.Medium
        Accessible.name: chip.text

        contentItem: Text {
            id: chipLabel

            textFormat: Text.PlainText
            text: chip.text
            font: chip.font
            color: chip.selected ? Theme.colors.accentText : Theme.colors.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            id: chipFace

            radius: height / 2
            color: chip.selected ? Theme.colors.accentSubtle : Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: chip.selected ? Theme.colors.accentLine : Theme.colors.strokeDefault

            StateLayer {
                anchors.fill: parent
                radius: chipFace.radius
                hovered: chip.hovered
                pressed: chip.down
            }

            FocusRing {
                visible: chip.visualFocus
                baseRadius: chipFace.radius
            }
        }
    }

    component StepRow: T.AbstractButton {
        id: stepRow

        property int number: 1
        property string title
        property string detail
        property string note
        property bool current: false

        implicitHeight: stepContent.implicitHeight + topPadding + bottomPadding
        topPadding: Theme.space.sm
        bottomPadding: Theme.space.sm
        hoverEnabled: true
        Accessible.name: stepRow.title

        background: Rectangle {
            id: stepFace

            radius: Theme.radius.lg
            color: stepRow.current ? Theme.colors.bgOverlay : "transparent"
            border.width: stepRow.current ? Theme.size.stroke : 0
            border.color: Theme.colors.strokeSubtle

            StateLayer {
                anchors.fill: parent
                radius: stepFace.radius
                hovered: stepRow.hovered && !stepRow.current
                pressed: stepRow.down
            }

            FocusRing {
                visible: stepRow.visualFocus
                baseRadius: stepFace.radius
            }
        }

        contentItem: Item {
            id: stepContent

            implicitHeight: Math.max(badge.height, texts.implicitHeight)

            Rectangle {
                id: badge

                x: Theme.space.sm
                width: Theme.size.guideBadge
                height: width
                radius: width / 2
                color: stepRow.current ? Theme.colors.accent : Theme.colors.bgOverlay
                border.width: stepRow.current ? 0 : Theme.size.stroke
                border.color: Theme.colors.strokeDefault

                Text {
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: stepRow.number.toString()
                    color: stepRow.current ? Theme.colors.textOnAccent : Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                }
            }

            Column {
                id: texts

                anchors.left: badge.right
                anchors.leftMargin: Theme.space.md
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.sm
                spacing: Theme.space.xxs

                Text {
                    width: parent.width
                    height: Math.max(implicitHeight, Theme.size.guideBadge)
                    textFormat: Text.PlainText
                    text: stepRow.title
                    color: stepRow.current ? Theme.colors.textPrimary : Theme.colors.textSecondary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.Medium
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: stepRow.current
                    textFormat: Text.PlainText
                    text: stepRow.detail
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: stepRow.current && stepRow.note !== ""
                    textFormat: Text.PlainText
                    text: stepRow.note
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    background: PopupSurface {
        radius: Theme.radius.xl
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

    contentItem: Item {
        id: frame

        focus: true
        Keys.onReturnPressed: guide.advance()
        Keys.onEnterPressed: guide.advance()

        Rectangle {
            id: art

            x: Theme.space.sm
            y: Theme.space.sm
            width: Theme.size.guideArt
            height: frame.height - 2 * Theme.space.sm
            radius: Theme.radius.lg
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle
            clip: true

            GuideHero {
                anchors.centerIn: parent
                opacity: guide.page === 0 ? 1 : 0
                visible: opacity > 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.motion.base
                    }
                }
            }

            GuidePhone {
                anchors.centerIn: parent
                opacity: guide.page === 1 ? 1 : 0
                visible: opacity > 0
                screen: guide.current.screen
                title: guide.current.phoneTitle
                target: guide.current.target

                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.motion.base
                    }
                }
            }

            GuidePhone {
                anchors.centerIn: parent
                opacity: guide.page === 2 ? 1 : 0
                visible: opacity > 0
                screen: guide.found ? "ready"
                      : guide.leadState === "unauthorized" ? "allow"
                      : guide.leadState === "failed" ? "failed" : "waiting"
                caption: guide.found || guide.leadState === "failed" ? guide.leadName : qsTr("Waiting for the phone")

                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.motion.base
                    }
                }
            }
        }

        Button {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.space.lg
            visible: guide.page < 2
            small: true
            tone: "ghost"
            text: qsTr("Skip")
            onClicked: guide.close()
        }

        Item {
            id: column

            anchors.left: art.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: footer.top
            anchors.leftMargin: Theme.space.x3l
            anchors.rightMargin: Theme.space.x3l
            anchors.topMargin: Theme.space.x5l
            anchors.bottomMargin: Theme.space.lg

            GuidePage {
                anchors.fill: parent
                shown: guide.page === 0

                Column {
                    width: parent.width
                    spacing: Theme.space.lg

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: qsTr("Welcome to GameScrcpy")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.display.size
                        font.weight: Theme.type.display.weight
                        wrapMode: Text.Wrap
                    }

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: qsTr("Mirror your Android phone on this computer and play its games with a keyboard and mouse.")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.body.size
                        wrapMode: Text.Wrap
                    }

                    Column {
                        width: parent.width
                        topPadding: Theme.space.sm
                        spacing: Theme.space.lg

                        Feature {
                            width: parent.width
                            iconName: "monitor-play"
                            title: qsTr("Low-latency mirroring")
                            detail: qsTr("Over USB or WiFi, with the phone's picture and sound.")
                        }

                        Feature {
                            width: parent.width
                            iconName: "keyboard"
                            title: qsTr("Keyboard and mouse for games")
                            detail: qsTr("A key mapping profile for each game, switched as you open it.")
                        }

                        Feature {
                            width: parent.width
                            iconName: "puzzle"
                            title: qsTr("Make it yours")
                            detail: qsTr("Extensions add picture filters and themes.")
                        }
                    }
                }

                Text {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    textFormat: Text.PlainText
                    text: qsTr("Open source under the Apache License 2.0. Please follow local laws and each game's terms of use.")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }

            GuidePage {
                anchors.fill: parent
                shown: guide.page === 1

                Column {
                    width: parent.width
                    spacing: Theme.space.md

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: qsTr("Turn on USB debugging")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.display.size
                        font.weight: Theme.type.display.weight
                        wrapMode: Text.Wrap
                    }

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: qsTr("A one-time setup on the phone. Choose its brand for the exact steps.")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.body.size
                        wrapMode: Text.Wrap
                    }

                    Flow {
                        width: parent.width
                        spacing: Theme.space.xs

                        Repeater {
                            model: guide.vendors

                            delegate: Chip {
                                required property var modelData

                                text: guide.text(modelData.name)
                                selected: modelData.id === guide.vendorId
                                onClicked: guide.choose(modelData.id)
                            }
                        }
                    }

                    Column {
                        id: stepList

                        width: parent.width
                        topPadding: Theme.space.xs
                        spacing: Theme.space.xxs

                        HoverHandler {
                            id: stepsHover
                        }

                        Repeater {
                            model: guide.steps

                            delegate: StepRow {
                                required property var modelData
                                required property int index

                                width: stepList.width
                                number: index + 1
                                title: modelData.title
                                detail: modelData.detail
                                note: modelData.note
                                current: index === guide.step
                                onClicked: {
                                    guide.step = index
                                    guide.playing = false
                                }
                            }
                        }
                    }
                }

                Row {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    spacing: Theme.space.xs

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "search"
                        size: Theme.size.iconSmall
                        color: Theme.colors.textTertiary
                    }

                    Text {
                        width: parent.width - Theme.size.iconSmall - parent.spacing
                        textFormat: Text.PlainText
                        text: qsTr("Can't find it? Search the phone's Settings for “Developer”.")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }
                }
            }

            GuidePage {
                anchors.fill: parent
                shown: guide.page === 2

                Column {
                    width: parent.width
                    spacing: Theme.space.md

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: guide.connectTitle
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.display.size
                        font.weight: Theme.type.display.weight
                        wrapMode: Text.Wrap
                    }

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: guide.connectDetail
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.body.size
                        wrapMode: Text.Wrap
                    }

                    InlineAlert {
                        width: parent.width
                        visible: !DeviceManager.adbReady && DeviceManager.setupProblem !== ""
                        tone: "warn"
                        title: DeviceManager.setupProblem
                        detail: DeviceManager.setupAction
                    }

                    Row {
                        visible: guide.waiting && DeviceManager.adbReady
                        spacing: Theme.space.sm

                        Rectangle {
                            id: beacon

                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.size.pillDot
                            height: width
                            radius: width / 2
                            color: Theme.colors.accent

                            SequentialAnimation on opacity {
                                running: !Theme.reducedMotion && beacon.visible && beacon.Window.active
                                loops: Animation.Infinite

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
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: qsTr("Waiting for a phone…")
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                        }
                    }

                    Column {
                        id: tips

                        width: parent.width
                        visible: guide.waiting
                        topPadding: Theme.space.xs
                        spacing: Theme.space.xs

                        Repeater {
                            model: [qsTr("Use a cable that carries data; some cables only charge."),
                                    qsTr("If the phone asks what the USB connection is for, choose File transfer."),
                                    qsTr("Not set up on the phone yet? Go back for the steps.")]

                            delegate: Row {
                                id: tip

                                required property string modelData

                                width: tips.width
                                spacing: Theme.space.sm

                                Rectangle {
                                    y: (Theme.type.caption.lineHeight - height) / 2
                                    width: Theme.space.xs
                                    height: width
                                    radius: width / 2
                                    color: Theme.colors.textTertiary
                                }

                                Text {
                                    id: tipText

                                    width: tip.width - Theme.space.xs - tip.spacing
                                    textFormat: Text.PlainText
                                    text: tip.modelData
                                    color: Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.caption.size
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Row {
                        spacing: Theme.space.sm

                        Button {
                            visible: guide.leadState === "unauthorized"
                                     || (guide.leadState === "failed" && (guide.lead.guide ?? "") !== "")
                            iconName: "circle-help"
                            text: qsTr("See the steps")
                            onClicked: guide.showSteps()
                        }

                        Button {
                            visible: !guide.found
                            tone: "ghost"
                            iconName: "wifi"
                            text: qsTr("Connect over WiFi instead")
                            onClicked: guide.connectWirelessly()
                        }
                    }
                }
            }
        }

        Item {
            id: footer

            anchors.left: column.left
            anchors.right: column.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.space.xxl
            height: Theme.size.control

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xs

                Repeater {
                    model: 3

                    delegate: Rectangle {
                        required property int index

                        width: index === guide.page ? Theme.size.guideDotActive : Theme.size.guideDot
                        height: Theme.size.guideDot
                        radius: height / 2
                        color: index === guide.page ? Theme.colors.accent : Theme.colors.strokeStrong

                        Behavior on width {
                            NumberAnimation {
                                duration: Theme.motion.base
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: Theme.motion.emphasized
                            }
                        }
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.sm

                Button {
                    visible: guide.page > 0
                    text: qsTr("Back")
                    onClicked: guide.page -= 1
                }

                Button {
                    tone: "primary"
                    text: guide.primaryText
                    onClicked: guide.advance()
                }
            }
        }
    }
}
