import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

T.Pane {
    id: bar

    readonly property var bisect: ExtensionHost.bisect
    readonly property string outcome: bar.bisect.outcome ?? ""
    readonly property string heading: {
        switch (bar.outcome) {
        case "found":
            return qsTr("%1 causes the problem").arg(bar.bisect.found?.name ?? "")
        case "notExtensions":
            return qsTr("The problem happens without extensions too")
        default:
            return qsTr("Finding the extension behind a problem: step %1 of about %2")
                .arg(bar.bisect.step ?? 1).arg(bar.bisect.steps ?? 1)
        }
    }
    readonly property string message: {
        const separator = qsTr(", ", "list separator")
        switch (bar.outcome) {
        case "found": {
            const needs = bar.bisect.found?.needs ?? []
            return needs.length > 0
                    ? qsTr("It needs %1, which may be the cause instead. Turn it off, or keep it on and tell its author.")
                      .arg(needs.join(separator))
                    : qsTr("Turn it off, or keep it on and tell its author.")
        }
        case "notExtensions":
            return qsTr("No extension is behind it. Select Done to load your extensions again.")
        default: {
            const loaded = bar.bisect.loaded ?? []
            return loaded.length === 0
                    ? qsTr("No extension is loaded now. Does the problem still happen?")
                    : qsTr("Loaded now: %1. Does the problem still happen?").arg(loaded.join(separator))
        }
        }
    }

    visible: bar.bisect.active === true
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: Theme.space.lg
    leftPadding: Theme.space.md + Theme.size.toastBar + Theme.space.md

    Accessible.role: Accessible.AlertMessage
    Accessible.name: bar.heading
    Accessible.description: bar.message

    background: Rectangle {
        id: face

        radius: Theme.radius.xl
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle

        Rectangle {
            x: Theme.space.md
            y: Theme.space.md
            width: Theme.size.toastBar
            height: face.height - 2 * Theme.space.md
            radius: width / 2
            color: bar.outcome === "found" ? Theme.colors.warning : Theme.colors.info
        }
    }

    contentItem: RowLayout {
        spacing: Theme.space.md

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space.xs

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: bar.heading
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
                font.weight: Font.Medium
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: bar.message
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }

        Button {
            visible: bar.outcome === "running"
            text: qsTr("Still happens")
            onClicked: ExtensionHost.answerBisect(true)
        }

        Button {
            visible: bar.outcome === "running"
            text: qsTr("Gone")
            onClicked: ExtensionHost.answerBisect(false)
        }

        Button {
            visible: bar.outcome === "running"
            tone: "ghost"
            text: qsTr("Stop")
            onClicked: ExtensionHost.stopBisect()
        }

        Button {
            visible: bar.outcome === "found"
            tone: "primary"
            text: qsTr("Turn it off")
            onClicked: ExtensionHost.concludeBisect(true)
        }

        Button {
            visible: bar.outcome === "found"
            tone: "ghost"
            text: qsTr("Keep it on")
            onClicked: ExtensionHost.concludeBisect(false)
        }

        Button {
            visible: bar.outcome === "notExtensions"
            tone: "primary"
            text: qsTr("Done")
            onClicked: ExtensionHost.concludeBisect(false)
        }
    }
}
