import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Item {
    id: card

    property var progress: ({})

    readonly property bool shown: progress.running !== undefined
    readonly property bool running: progress.running === true
    readonly property bool measured: progress.medianMs !== undefined
    readonly property string tone: running ? "info" : measured ? "ok" : "warn"
    readonly property string headline: {
        if (running)
            return qsTr("Measuring the real latency: press keys or tap the picture as usual")
        if (measured)
            return qsTr("Real latency %1 ms").arg(Math.round(progress.medianMs))
        if (progress.refused === true)
            return qsTr("This phone does not allow touch dots, so the real latency cannot be measured")
        if ((progress.missed ?? 0) > (progress.measured ?? 0))
            return qsTr("The touch dots could not be seen; try keys over darker parts of the picture")
        return qsTr("Too few presses to measure")
    }
    readonly property string detail: {
        if (running)
            return qsTr("%1 of %2").arg(progress.measured ?? 0).arg(progress.target ?? 0)
        if (measured)
            return qsTr("median of %1 presses, %2 to %3 ms").arg(progress.samples)
                .arg(Math.round(progress.lowMs)).arg(Math.round(progress.highMs))
        return ""
    }
    readonly property real widest: parent ? Math.min(Theme.size.toastMaxWidth, parent.width - 2 * Theme.space.sm)
                                          : Theme.size.toastMaxWidth

    signal stopRequested()
    signal againRequested()
    signal dismissed()

    visible: shown
    implicitWidth: Math.min(widest, content.implicitWidth + Theme.space.xs + Theme.space.xxs
                            + Theme.space.xs + Theme.space.xxs)
    implicitHeight: Math.max(Theme.size.toast, content.implicitHeight + 2 * Theme.space.xs)
    width: implicitWidth
    height: implicitHeight

    Accessible.role: Accessible.AlertMessage
    Accessible.name: [headline, detail].filter(part => part !== "").join(" · ")

    PopupSurface {
        anchors.fill: parent
        radius: Math.min(height, Theme.size.toast) / 2
    }

    Row {
        id: content

        x: Theme.space.xs + Theme.space.xxs
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space.sm

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.size.icon + 2 * Theme.space.sm
            height: width
            radius: width / 2
            color: card.tone === "info" ? Theme.colors.infoSubtle
                 : card.tone === "ok" ? Theme.colors.successSubtle : Theme.colors.warningSubtle

            Icon {
                anchors.centerIn: parent
                name: "gauge"
                size: Theme.size.icon
                color: card.tone === "info" ? Theme.colors.info
                     : card.tone === "ok" ? Theme.colors.success : Theme.colors.warning
            }
        }

        Text {
            id: headlineText

            objectName: "headline"
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, card.widest - 2 * (Theme.space.xs + Theme.space.xxs)
                            - Theme.size.icon - 2 * Theme.space.sm - detailText.width - action.width
                            - dismiss.width - 5 * content.spacing)
            textFormat: Text.PlainText
            text: card.headline
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: card.measured ? Font.DemiBold : Theme.type.label.weight
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            id: detailText

            objectName: "detail"
            anchors.verticalCenter: parent.verticalCenter
            visible: text !== ""
            textFormat: Text.PlainText
            text: card.detail
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.label.size
            font.features: ({ "tnum": 1 })
        }

        Button {
            id: action

            objectName: "action"
            anchors.verticalCenter: parent.verticalCenter
            tone: "ghost"
            small: true
            text: card.running ? qsTr("Stop") : qsTr("Measure again")
            focusPolicy: Qt.NoFocus
            onClicked: card.running ? card.stopRequested() : card.againRequested()
        }

        Button {
            id: dismiss

            objectName: "dismiss"
            anchors.verticalCenter: parent.verticalCenter
            tone: "ghost"
            small: true
            iconName: "x"
            description: qsTr("Close")
            focusPolicy: Qt.NoFocus
            onClicked: card.dismissed()
        }
    }
}
