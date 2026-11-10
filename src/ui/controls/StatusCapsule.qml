import QtQuick
import GameScrcpy.Theme

Rectangle {
    id: capsule

    property real framesPerSecond: -1
    property real latencyMs: -1
    property string transport
    property string profileName
    property string task
    property bool cameraLocked: false
    property string problem
    property bool flat: false
    property bool recording: false
    property string recordingText
    property string recordingNote
    readonly property bool hasContent: label.text !== "" || cameraLocked || problem !== "" || recording

    implicitWidth: content.implicitWidth + 2 * Theme.space.md
    implicitHeight: Theme.size.capsule
    visible: hasContent
    radius: height / 2
    color: flat ? "transparent" : Theme.reducedTransparency ? Theme.colors.bgElevated : Theme.colors.bgFrosted
    border.width: flat ? 0 : Theme.size.stroke
    border.color: Theme.colors.strokeStrong

    Accessible.role: Accessible.StaticText
    Accessible.name: [problem, recording ? recordingText + " · " + recordingNote : "", cameraLocked ? lockText.text : "",
                      label.visible ? label.text : ""].filter(part => part !== "").join(" · ")

    Row {
        id: content

        anchors.centerIn: parent
        spacing: Theme.space.sm

        Row {
            objectName: "problemMark"
            anchors.verticalCenter: parent.verticalCenter
            visible: capsule.problem !== ""
            spacing: Theme.space.sm

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.size.lockHalo
                height: width
                radius: width / 2
                color: Theme.colors.warningSubtle

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.size.lockMark
                    height: width
                    radius: width / 2
                    color: Theme.colors.warning
                }
            }

            Text {
                objectName: "problemText"
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: capsule.problem
                color: Theme.colors.warningText
                font.pixelSize: Theme.type.label.size
                font.weight: Font.DemiBold
            }
        }

        Row {
            objectName: "recordingMark"
            anchors.verticalCenter: parent.verticalCenter
            visible: capsule.recording
            spacing: Theme.space.sm

            Rectangle {
                objectName: "recordingDot"
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.size.lockHalo
                height: width
                radius: width / 2
                color: Theme.colors.dangerSubtle

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.size.lockMark
                    height: width
                    radius: width / 2
                    color: Theme.colors.danger
                }
            }

            Text {
                objectName: "recordingTime"
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: capsule.recordingText
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: capsule.recordingNote !== ""
                textFormat: Text.PlainText
                text: "/"
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.label.size
            }

            Text {
                objectName: "recordingNote"
                anchors.verticalCenter: parent.verticalCenter
                visible: text !== ""
                textFormat: Text.PlainText
                text: capsule.recordingNote
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.label.size
                font.weight: Theme.type.label.weight
            }
        }

        Row {
            id: lockMark

            objectName: "lockMark"
            anchors.verticalCenter: parent.verticalCenter
            visible: capsule.cameraLocked
            spacing: Theme.space.sm

            Rectangle {
                objectName: "lockDot"
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.size.lockHalo
                height: width
                radius: width / 2
                color: Theme.colors.accentRing

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.size.lockMark
                    height: width
                    radius: width / 2
                    color: Theme.colors.accent
                }
            }

            Text {
                id: lockText

                objectName: "lockText"
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("Camera locked")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.DemiBold
            }
        }

        Text {
            id: label

            objectName: "text"
            anchors.verticalCenter: parent.verticalCenter
            visible: text !== "" && !capsule.recording
            textFormat: Text.PlainText
            text: {
                const parts = []
                if (capsule.task !== "")
                    parts.push(capsule.task)
                if (capsule.profileName !== "")
                    parts.push(capsule.profileName)
                if (capsule.framesPerSecond >= 0)
                    parts.push(qsTr("%1 fps").arg(Math.round(capsule.framesPerSecond)))
                if (capsule.latencyMs >= 0)
                    parts.push(qsTr("%1 ms").arg(Math.round(capsule.latencyMs)))
                if (capsule.transport !== "")
                    parts.push(capsule.transport)
                return parts.join(" · ")
            }
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: Theme.type.label.weight
        }
    }
}
