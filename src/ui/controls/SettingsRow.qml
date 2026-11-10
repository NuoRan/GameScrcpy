import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme

Item {
    id: row

    property string title
    property string description
    property string note
    property string highlight
    property var keys: []
    property bool overridden: false
    property bool divider: false
    property bool nested: false
    default property alias control: trailing.data
    property alias below: belowArea.data

    readonly property int inset: Theme.space.lg + Theme.space.xxs
    readonly property int lead: nested ? inset + Theme.space.xl : inset
    readonly property bool explainedInline: highlight !== "" && description !== ""
    readonly property bool stacked: belowArea.children.length > 0
    readonly property real headHeight: Math.max(texts.implicitHeight, trailing.implicitHeight)

    signal resetRequested()

    function marked(text: string, query: string): string {
        const lower = text.toLowerCase()
        const hit = new Array(text.length).fill(false)
        for (const term of query.toLowerCase().split(/\s+/)) {
            if (term === "")
                continue
            for (let at = lower.indexOf(term); at >= 0; at = lower.indexOf(term, at + 1))
                hit.fill(true, at, at + term.length)
        }
        const entities = { "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;" }
        let result = ""
        for (let i = 0; i < text.length; ++i) {
            if (hit[i] && (i === 0 || !hit[i - 1]))
                result += "<font color=\"" + Theme.colors.accentText.toString() + "\">"
            result += entities[text[i]] ?? text[i]
            if (hit[i] && (i === text.length - 1 || !hit[i + 1]))
                result += "</font>"
        }
        return result
    }

    implicitWidth: lead + texts.implicitWidth + Theme.space.xxl + trailing.implicitWidth + inset
    implicitHeight: stacked ? headHeight + Theme.space.sm + belowArea.implicitHeight + 2 * Theme.space.md
                            : Math.max(Theme.size.settingsRow, headHeight + 2 * Theme.space.md)

    Accessible.description: description

    Rectangle {
        visible: row.divider
        x: row.inset
        width: row.width - 2 * row.inset
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    Rectangle {
        objectName: "nestLine"
        visible: row.nested
        x: row.inset + Theme.space.xs + Theme.space.xxs
        width: Theme.size.focusRing
        height: row.height
        radius: width / 2
        color: Theme.colors.strokeDefault
    }

    Column {
        id: texts

        anchors.left: parent.left
        anchors.right: trailing.left
        anchors.leftMargin: row.lead
        anchors.rightMargin: Theme.space.xxl
        y: row.stacked ? Theme.space.md + (row.headHeight - height) / 2 : (row.height - height) / 2
        spacing: Theme.space.xxs

        Row {
            spacing: Theme.space.sm

            Text {
                objectName: "title"
                anchors.verticalCenter: parent.verticalCenter
                text: row.highlight === "" ? row.title : row.marked(row.title, row.highlight)
                textFormat: row.highlight === "" ? Text.PlainText : Text.StyledText
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
                font.weight: Font.Medium
            }

            Row {
                objectName: "keys"
                anchors.verticalCenter: parent.verticalCenter
                visible: row.keys.length > 0
                spacing: Theme.space.xxs

                Repeater {
                    model: row.keys

                    delegate: Keycap {
                        required property string modelData

                        text: modelData
                    }
                }
            }

            InfoButton {
                objectName: "info"
                anchors.verticalCenter: parent.verticalCenter
                visible: row.description !== "" && !row.explainedInline
                title: row.title
                info: row.description
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: row.overridden
                width: badge.implicitWidth + 2 * Theme.space.xs + Theme.space.xxs
                height: Theme.size.pill
                radius: Theme.radius.xs
                color: Theme.colors.accentSubtle

                Text {
                    id: badge

                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: qsTr("Overridden")
                    color: Theme.colors.accentText
                    font.pixelSize: Theme.type.micro.size
                    font.weight: Theme.type.micro.weight
                }
            }
        }

        Text {
            objectName: "description"
            visible: row.explainedInline
            width: parent.width
            textFormat: Text.StyledText
            text: row.explainedInline ? row.marked(row.description, row.highlight) : ""
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
            wrapMode: Text.Wrap
        }

        Text {
            objectName: "note"
            visible: row.note !== ""
            width: parent.width
            textFormat: Text.PlainText
            text: row.note
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
            wrapMode: Text.Wrap
        }
    }

    RowLayout {
        id: trailing

        anchors.right: parent.right
        anchors.rightMargin: row.inset
        y: row.stacked ? Theme.space.md + (row.headHeight - height) / 2 : (row.height - height) / 2
        spacing: Theme.space.sm

        Button {
            objectName: "reset"
            visible: row.overridden
            tone: "ghost"
            small: true
            text: qsTr("Reset")
            onClicked: {
                if (activeFocus)
                    nextItemInFocusChain(true)?.forceActiveFocus(Qt.TabFocusReason)
                row.resetRequested()
            }
        }
    }

    Column {
        id: belowArea

        x: row.lead
        y: Theme.space.md + row.headHeight + Theme.space.sm
        width: row.width - row.lead - row.inset
        spacing: Theme.space.sm
    }
}
