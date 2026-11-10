pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App
import "UiMarkdown.js" as UiMarkdown

Item {
    id: root

    property var host: null
    property var path: []
    readonly property var node: root.host !== null ? root.host.nodeAt(root.path) : null
    readonly property string type: root.node?.type ?? ""
    readonly property string nodeId: root.node?.id ?? ""
    readonly property string title: root.node?.title ?? ""
    readonly property int childCount: (root.node?.children ?? []).length
    readonly property bool usable: root.node?.enabled !== false
    readonly property var blocks: ["column", "row", "group", "list", "table", "chart", "separator", "deviceItem", "steps",
                                   "keyValue"]

    function send(kind: string, value: var) {
        if (root.host !== null)
            root.host.send(kind, root.nodeId, value)
    }

    function release() {
        if (root.host !== null)
            root.host.release()
    }

    function toneColor(tone: string, quiet: bool): color {
        switch (tone) {
        case "muted":
            return Theme.colors.textTertiary
        case "accent":
            return Theme.colors.accentText
        case "good":
            return Theme.colors.successText
        case "warn":
            return Theme.colors.warningText
        case "bad":
            return Theme.colors.dangerText
        default:
            return quiet ? Theme.colors.textSecondary : Theme.colors.textPrimary
        }
    }

    function decimalsOf(values: var): int {
        let most = 0
        for (const value of values) {
            if (typeof value !== "number" || !Number.isFinite(value))
                continue
            const written = String(value)
            const dot = written.indexOf(".")
            if (written.indexOf("e") >= 0)
                most = 4
            else if (dot >= 0)
                most = Math.max(most, written.length - dot - 1)
        }
        return Math.min(4, most)
    }

    implicitWidth: content.implicitWidth
    implicitHeight: content.implicitHeight

    component Child: Loader {
        id: child

        required property int index
        required property var owner

        Component.onCompleted: child.setSource(Qt.resolvedUrl("UiNode.qml"),
                                               { host: child.owner.host, path: child.owner.path.concat([child.index]) })
    }

    component Caption: Text {
        visible: text !== ""
        textFormat: Text.PlainText
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.caption.size
        font.weight: Font.Medium
        wrapMode: Text.Wrap
    }

    Loader {
        id: content

        width: root.width
        sourceComponent: {
            switch (root.type) {
            case "column":
                return columnPart
            case "row":
                return rowPart
            case "group":
                return groupPart
            case "text":
                return textPart
            case "button":
                return buttonPart
            case "field":
                return fieldPart
            case "number":
                return numberPart
            case "toggle":
                return togglePart
            case "choice":
                return choicePart
            case "slider":
                return sliderPart
            case "list":
                return listPart
            case "table":
                return tablePart
            case "chart":
                return chartPart
            case "canvas":
                return canvasPart
            case "image":
                return imagePart
            case "progress":
                return progressPart
            case "separator":
                return separatorPart
            case "deviceItem":
                return deviceItemPart
            case "steps":
                return stepsPart
            case "qr":
                return qrPart
            case "keyValue":
                return keyValuePart
            default:
                return null
            }
        }
    }

    Component {
        id: columnPart

        Column {
            id: column

            spacing: Theme.space.md

            Repeater {
                model: root.childCount

                delegate: Child {
                    width: column.width
                    owner: root
                }
            }
        }
    }

    Component {
        id: rowPart

        Flow {
            id: flow

            spacing: Theme.space.sm

            Repeater {
                model: root.childCount

                delegate: Child {
                    id: cell

                    readonly property string kind: (root.node?.children ?? [])[cell.index]?.type ?? ""

                    owner: root
                    width: root.blocks.includes(cell.kind) ? flow.width : Math.min(cell.implicitWidth, flow.width)
                }
            }
        }
    }

    Component {
        id: groupPart

        Rectangle {
            implicitHeight: groupBody.implicitHeight + 2 * Theme.space.md
            radius: Theme.radius.lg
            color: Theme.colors.stateHover
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle

            Accessible.role: Accessible.Grouping
            Accessible.name: root.title

            Column {
                id: groupBody

                x: Theme.space.md
                y: Theme.space.md
                width: parent.width - 2 * Theme.space.md
                spacing: Theme.space.sm

                Text {
                    width: parent.width
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: root.title
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: root.childCount

                    delegate: Child {
                        width: groupBody.width
                        owner: root
                    }
                }
            }
        }
    }

    Component {
        id: textPart

        Text {
            id: words

            readonly property string look: root.node?.style ?? "body"
            readonly property bool marked: (root.node?.format ?? "plain") === "markdown"

            textFormat: words.marked ? Text.StyledText : Text.PlainText
            text: words.marked ? UiMarkdown.styled(root.node?.text ?? "", String(Theme.colors.accentText))
                               : root.node?.text ?? ""
            color: root.toneColor(root.node?.tone ?? "normal", words.look === "caption")
            font.pixelSize: words.look === "heading" ? Theme.type.heading.size
                          : words.look === "caption" ? Theme.type.caption.size : Theme.type.body.size
            font.weight: words.look === "heading" ? Theme.type.heading.weight : Font.Normal
            lineHeight: words.look === "caption" ? Theme.type.caption.lineHeight
                      : words.look === "heading" ? Theme.type.heading.lineHeight : Theme.type.body.lineHeight
            lineHeightMode: Text.FixedHeight
            wrapMode: Text.Wrap
        }
    }

    Component {
        id: buttonPart

        Item {
            implicitWidth: button.implicitWidth
            implicitHeight: button.implicitHeight

            Button {
                id: button

                width: Math.min(button.implicitWidth, parent.width)
                enabled: root.usable
                focusPolicy: Qt.TabFocus
                text: root.title
                iconName: ExtensionPanels.iconOf(root.node?.icon ?? "")
                tone: {
                    switch (root.node?.style ?? "normal") {
                    case "primary":
                        return "primary"
                    case "danger":
                        return "danger"
                    default:
                        return "secondary"
                    }
                }
                onClicked: root.send("click", null)
            }
        }
    }

    Component {
        id: fieldPart

        Item {
            implicitWidth: Math.max(Theme.size.fieldWidth, fieldTitle.implicitWidth)
            implicitHeight: fieldColumn.implicitHeight

            Column {
                id: fieldColumn

                width: parent.width
                spacing: Theme.space.xs

                Caption {
                    id: fieldTitle

                    width: parent.width
                    text: root.title
                }

                TextField {
                    id: field

                    property string sent

                    function sync() {
                        field.text = root.node?.value ?? ""
                        field.sent = field.text
                    }

                    function report() {
                        if (field.text === field.sent)
                            return
                        field.sent = field.text
                        root.send("change", field.text)
                    }

                    width: parent.width
                    enabled: root.usable
                    maximumLength: 4000
                    placeholderText: root.node?.placeholder ?? ""
                    Accessible.name: root.title !== "" ? root.title : field.placeholderText
                    Component.onCompleted: field.sync()
                    onEditingFinished: field.report()
                    onAccepted: {
                        field.report()
                        root.send("submit", field.text)
                        root.release()
                    }
                    Keys.onEscapePressed: {
                        field.sync()
                        root.release()
                    }
                }

                Connections {
                    target: root

                    function onNodeChanged() {
                        if (!field.activeFocus)
                            field.sync()
                    }
                }
            }
        }
    }

    Component {
        id: numberPart

        Item {
            implicitWidth: Math.max(Theme.size.numberFieldWide, numberTitle.implicitWidth)
            implicitHeight: numberColumn.implicitHeight

            Column {
                id: numberColumn

                width: parent.width
                spacing: Theme.space.xs

                Caption {
                    id: numberTitle

                    width: parent.width
                    text: root.title
                }

                NumberField {
                    id: number

                    width: Theme.size.numberFieldWide
                    enabled: root.usable
                    unit: root.node?.unit ?? ""
                    numbers: NumberScale {
                        minimum: root.node?.min ?? -Infinity
                        maximum: root.node?.max ?? Infinity
                        step: root.node?.step ?? 1
                        decimals: root.decimalsOf([root.node?.value, root.node?.min, root.node?.max, root.node?.step])
                    }
                    Accessible.name: root.title
                    Component.onCompleted: number.value = root.node?.value ?? 0
                    onSubmitted: picked => {
                        number.value = picked
                        root.send("change", picked)
                    }
                    onAccepted: root.release()
                }

                Connections {
                    target: root

                    function onNodeChanged() {
                        if (!number.activeFocus)
                            number.value = root.node?.value ?? 0
                    }
                }
            }
        }
    }

    Component {
        id: togglePart

        Item {
            implicitWidth: Theme.size.switchWidth + Theme.space.md + toggleLabel.implicitWidth
            implicitHeight: Math.max(Theme.size.switchHeight, toggleLabel.implicitHeight)

            Switch {
                id: toggle

                y: (parent.height - toggle.height) / 2
                enabled: root.usable
                focusPolicy: Qt.TabFocus
                Accessible.name: root.title
                Component.onCompleted: toggle.checked = root.node?.value === true
                onToggled: root.send("change", toggle.checked)
            }

            Text {
                id: toggleLabel

                x: toggle.width + Theme.space.md
                y: (parent.height - toggleLabel.height) / 2
                width: parent.width - toggleLabel.x
                textFormat: Text.PlainText
                text: root.title
                color: root.usable ? Theme.colors.textPrimary : Theme.colors.textDisabled
                font.pixelSize: Theme.type.label.size
                wrapMode: Text.Wrap

                TapHandler {
                    enabled: root.usable
                    onTapped: {
                        toggle.toggle()
                        root.send("change", toggle.checked)
                    }
                }
            }

            Connections {
                target: root

                function onNodeChanged() {
                    toggle.checked = root.node?.value === true
                }
            }
        }
    }

    Component {
        id: choicePart

        Item {
            implicitWidth: Math.max(Theme.size.fieldWidth, choiceTitle.implicitWidth)
            implicitHeight: choiceColumn.implicitHeight

            Column {
                id: choiceColumn

                width: parent.width
                spacing: Theme.space.xs

                Caption {
                    id: choiceTitle

                    width: parent.width
                    text: root.title
                }

                Select {
                    id: choice

                    property var titles: []

                    function sync() {
                        const options = root.node?.options ?? []
                        const wanted = options.map(option => option.title)
                        if (JSON.stringify(wanted) !== JSON.stringify(choice.titles))
                            choice.titles = wanted
                        const value = root.node?.value ?? ""
                        choice.currentIndex = options.findIndex(option => option.id === value)
                    }

                    width: parent.width
                    enabled: root.usable
                    focusPolicy: Qt.TabFocus
                    model: choice.titles
                    Accessible.name: root.title
                    Component.onCompleted: choice.sync()
                    onActivated: index => {
                        const option = (root.node?.options ?? [])[index]
                        if (option !== undefined)
                            root.send("change", option.id)
                    }
                }

                Connections {
                    target: root

                    function onNodeChanged() {
                        choice.sync()
                    }
                }
            }
        }
    }

    Component {
        id: sliderPart

        Item {
            implicitWidth: Math.max(Theme.size.sliderWidth + Theme.space.md + Theme.size.sliderValue,
                                    sliderTitle.implicitWidth)
            implicitHeight: sliderColumn.implicitHeight

            Column {
                id: sliderColumn

                width: parent.width
                spacing: Theme.space.xxs

                Caption {
                    id: sliderTitle

                    width: parent.width
                    text: root.title
                }

                Slider {
                    id: slider

                    property real sent: NaN
                    readonly property int decimals: Math.max(root.decimalsOf([root.node?.min, root.node?.max,
                                                                              root.node?.step]),
                                                             slider.to - slider.from <= 1 ? 2 : 0)

                    function sync() {
                        slider.from = root.node?.min ?? 0
                        slider.to = root.node?.max ?? 1
                        slider.stepSize = root.node?.step ?? 0
                        slider.value = root.node?.value ?? slider.from
                        slider.sent = slider.value
                    }

                    function report() {
                        if (slider.value === slider.sent)
                            return
                        slider.sent = slider.value
                        root.send("change", slider.value)
                    }

                    width: parent.width
                    enabled: root.usable
                    focusPolicy: Qt.TabFocus
                    valueText: slider.value.toFixed(slider.decimals)
                    Accessible.name: root.title
                    Component.onCompleted: slider.sync()
                    onMoved: {
                        if (!slider.pressed)
                            slider.report()
                    }
                    onPressedChanged: {
                        if (!slider.pressed)
                            slider.report()
                    }
                }

                Connections {
                    target: root

                    function onNodeChanged() {
                        if (!slider.pressed)
                            slider.sync()
                    }
                }
            }
        }
    }

    Component {
        id: listPart

        UiList {
            node: root.node
            onPicked: item => root.send("select", item)
        }
    }

    Component {
        id: tablePart

        UiTable {
            node: root.node
        }
    }

    Component {
        id: chartPart

        UiChart {
            node: root.node
        }
    }

    Component {
        id: canvasPart

        UiCanvas {
            node: root.node
        }
    }

    Component {
        id: imagePart

        Item {
            id: imageBox

            readonly property real wantedWidth: (root.node?.width ?? 0) > 0 ? root.node.width : picture.implicitWidth
            readonly property real wantedHeight: (root.node?.height ?? 0) > 0 ? root.node.height : picture.implicitHeight
            readonly property real fit: imageBox.wantedWidth > 0 && imageBox.width > 0
                                        ? Math.min(1, imageBox.width / imageBox.wantedWidth) : 1

            implicitWidth: imageBox.wantedWidth
            implicitHeight: (imageTitle.visible ? imageTitle.implicitHeight + Theme.space.xs : 0)
                            + imageBox.wantedHeight * imageBox.fit

            Caption {
                id: imageTitle

                width: parent.width
                text: root.title
            }

            Image {
                id: picture

                y: imageTitle.visible ? imageTitle.implicitHeight + Theme.space.xs : 0
                width: imageBox.wantedWidth * imageBox.fit
                height: imageBox.wantedHeight * imageBox.fit
                source: root.node?.source ?? ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                Accessible.role: Accessible.Graphic
                Accessible.name: root.title
            }
        }
    }

    Component {
        id: progressPart

        Item {
            id: bar

            readonly property real amount: root.node?.value ?? -1
            readonly property bool known: bar.amount >= 0

            implicitWidth: Theme.size.sliderWidth
            implicitHeight: progressColumn.implicitHeight

            Accessible.role: Accessible.ProgressBar
            Accessible.name: root.title

            Column {
                id: progressColumn

                width: parent.width
                spacing: Theme.space.xs

                Item {
                    width: parent.width
                    height: Math.max(progressTitle.implicitHeight, percent.implicitHeight)
                    visible: progressTitle.text !== "" || bar.known

                    Text {
                        id: progressTitle

                        width: parent.width - (percent.visible ? percent.implicitWidth + Theme.space.sm : 0)
                        textFormat: Text.PlainText
                        text: root.title
                        elide: Text.ElideRight
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: Font.Medium
                    }

                    Text {
                        id: percent

                        anchors.right: parent.right
                        visible: bar.known
                        textFormat: Text.PlainText
                        text: qsTr("%1%").arg(Math.round(bar.amount * 100))
                        color: Theme.colors.textSecondary
                        font.family: Theme.type.monoFamily
                        font.pixelSize: Theme.type.caption.size
                    }
                }

                Rectangle {
                    id: track

                    width: parent.width
                    height: Theme.space.xs
                    radius: height / 2
                    color: Theme.colors.strokeSubtle
                    clip: true

                    Rectangle {
                        id: fill

                        property real sweep: 0

                        x: bar.known || Theme.reducedMotion ? 0 : fill.sweep * (track.width + fill.width) - fill.width
                        width: bar.known ? track.width * Math.min(1, bar.amount) : track.width / 3
                        height: track.height
                        radius: height / 2
                        color: Theme.colors.accent

                        NumberAnimation on sweep {
                            running: !bar.known && bar.visible && !Theme.reducedMotion
                            from: 0
                            to: 1
                            duration: Theme.motion.pulse
                            loops: Animation.Infinite
                        }
                    }
                }
            }
        }
    }

    Component {
        id: separatorPart

        Item {
            implicitHeight: Theme.size.stroke + 2 * Theme.space.xs

            Accessible.role: Accessible.Separator

            Rectangle {
                y: Theme.space.xs
                width: parent.width
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }
        }
    }

    Component {
        id: deviceItemPart

        Rectangle {
            id: device

            readonly property string deviceState: root.node?.state ?? ""
            readonly property bool clickable: root.nodeId !== ""

            implicitWidth: Theme.size.fieldWidth
            implicitHeight: Math.max(deviceTile.implicitHeight, deviceText.implicitHeight) + 2 * Theme.space.sm
            radius: Theme.radius.md
            color: device.clickable && deviceHover.hovered ? Theme.colors.stateHover : "transparent"
            activeFocusOnTab: device.clickable

            Accessible.role: device.clickable ? Accessible.Button : Accessible.ListItem
            Accessible.name: root.title
            Accessible.description: devicePill.visible ? devicePill.text : ""
            Keys.onSpacePressed: root.send("click", null)
            Keys.onReturnPressed: root.send("click", null)

            HoverHandler {
                id: deviceHover

                enabled: device.clickable
            }

            TapHandler {
                enabled: device.clickable
                onTapped: root.send("click", null)
            }

            Tile {
                id: deviceTile

                x: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                iconName: (root.node?.iconPath ?? "") !== "" ? "" : ExtensionPanels.iconOf(root.node?.icon ?? "") || "plug"
                iconPath: root.node?.iconPath ?? ""
                tone: "amber"
            }

            Column {
                id: deviceText

                anchors.left: deviceTile.right
                anchors.leftMargin: Theme.space.md
                anchors.right: devicePill.visible ? devicePill.left : parent.right
                anchors.rightMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.xxs

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: root.title
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: root.node?.subtitle ?? ""
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    elide: Text.ElideRight
                }
            }

            StatusPill {
                id: devicePill

                anchors.right: parent.right
                anchors.rightMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                visible: device.deviceState !== ""
                text: {
                    if ((root.node?.stateText ?? "") !== "")
                        return root.node.stateText
                    switch (device.deviceState) {
                    case "ready":
                        return qsTr("Online")
                    case "busy":
                        return qsTr("In use")
                    case "problem":
                        return qsTr("Needs attention")
                    default:
                        return qsTr("Offline")
                    }
                }
                tone: device.deviceState === "ready" ? "ok"
                      : device.deviceState === "busy" ? "warn"
                      : device.deviceState === "problem" ? "bad" : "idle"
            }

            FocusRing {
                visible: device.activeFocus
                baseRadius: device.radius
            }
        }
    }

    Component {
        id: stepsPart

        ConnectionStepper {
            implicitWidth: (root.node?.steps ?? []).length * Theme.size.stepWidth
            steps: root.node?.steps ?? []
            current: root.node?.current ?? 0
            failed: root.node?.failed === true
        }
    }

    Component {
        id: qrPart

        Item {
            id: qrBox

            readonly property int modules: root.node?.modules ?? 0

            implicitWidth: qrCode.implicitWidth
            implicitHeight: (qrTitle.visible ? qrTitle.implicitHeight + Theme.space.xs : 0) + qrCode.implicitHeight

            Caption {
                id: qrTitle

                width: parent.width
                text: root.title
            }

            QrCodeView {
                id: qrCode

                y: qrTitle.visible ? qrTitle.implicitHeight + Theme.space.xs : 0
                modules: qrBox.modules
                path: root.node?.path ?? ""
                moduleSize: Math.max(2, Math.min(5, Math.floor(Math.max(qrBox.width, Theme.size.fieldWidth)
                                                              / Math.max(1, qrBox.modules + 2 * qrCode.quietZone))))
                Accessible.role: Accessible.Graphic
                Accessible.name: root.title !== "" ? root.title : qsTr("QR code")
            }
        }
    }

    Component {
        id: keyValuePart

        GridLayout {
            id: pairs

            columns: 2
            columnSpacing: Theme.space.md
            rowSpacing: Theme.space.xs

            Repeater {
                model: 2 * (root.node?.rows ?? []).length

                delegate: Text {
                    id: cell

                    required property int index
                    readonly property bool isKey: cell.index % 2 === 0
                    readonly property var row: (root.node?.rows ?? [])[Math.floor(cell.index / 2)] ?? ({})

                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: !cell.isKey
                    Layout.maximumWidth: cell.isKey ? pairs.width / 2 : Number.POSITIVE_INFINITY
                    textFormat: Text.PlainText
                    text: cell.isKey ? cell.row.key ?? "" : cell.row.value ?? ""
                    color: cell.isKey ? Theme.colors.textTertiary : Theme.colors.textPrimary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: cell.isKey ? Text.NoWrap : Text.Wrap
                    elide: cell.isKey ? Text.ElideRight : Text.ElideNone
                }
            }
        }
    }
}
