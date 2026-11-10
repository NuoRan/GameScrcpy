pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    property var info: ({})
    property int testTicket: 0
    readonly property var testProfiles: {
        const usable = ProfileLibrary.entries.filter(entry => entry.logic === true && entry.folder === true)
        const used = dialog.info.profileId ?? ""
        return usable.filter(entry => entry.id === used).concat(usable.filter(entry => entry.id !== used))
    }
    readonly property var frames: dialog.info.thumbnails ?? []
    readonly property int frameSlots: 6
    readonly property real frameWidth: Math.floor((frameRow.width - (dialog.frameSlots - 1) * frameRow.spacing)
                                                  / dialog.frameSlots)
    readonly property real frameHeight: Math.round(Math.min(Theme.size.previewShort,
                                                            dialog.frameWidth * (dialog.info.thumbnailAspect ?? 0.5625)))
    readonly property string details: [dialog.info.phone ?? "",
                                       (dialog.info.width ?? 0) > 0
                                       ? qsTr("%1 × %2").arg(dialog.info.width).arg(dialog.info.height) : "",
                                       dialog.info.size ?? "",
                                       qsTr("%n operation(s)", "", dialog.info.operations ?? 0)]
                                      .filter(part => part !== "").join(" · ")

    signal notified(string message, string icon)

    function show(shown: var) {
        dialog.info = shown
        dialog.open()
    }

    function exportVideo() {
        Recordings.exportVideo(dialog.info.folder ?? "", dialog.info.serial ?? "")
    }

    function testLogic() {
        const problem = DeviceManager.testOnRecording(dialog.info.serial ?? "", dialog.info.profileId ?? "",
                                                      dialog.info.folder ?? "")
        if (problem !== "")
            dialog.notified(problem, "triangle-alert")
    }

    function toast(text: string, tone: string, task: string) {
        if (notice.visible)
            notice.close()
        notice.task = task
        notice.actionText = task !== "" ? qsTr("Open") : ""
        notice.show(text, tone)
    }

    function turnIntoBlueprint() {
        dialog.close()
        const made = Recordings.turnIntoTask(dialog.info.folder ?? "", dialog.info.profileId ?? "")
        if (made.ok === true)
            dialog.toast(made.text, "ok", made.task)
        else
            dialog.notified(made.text ?? "", "triangle-alert")
    }

    function chooseTestProfile() {
        dialog.close()
        testPicker.open()
    }

    function saveTest(profile: var) {
        testPicker.close()
        dialog.testTicket = Recordings.saveAsTest(dialog.info.folder ?? "", profile.id)
        dialog.toast(qsTr("Running %1 on the recording…").arg(profile.name), "info", "")
    }

    title: qsTr("Recorded · %1").arg(dialog.info.duration ?? "")
    acceptText: qsTr("Export video")
    rejectText: qsTr("Later")
    alternativeText: qsTr("Show in folder")
    onAccepted: dialog.exportVideo()
    onAlternativeChosen: Recordings.showInFolder(dialog.info.folder ?? "")

    header: Item {
        implicitHeight: headerRow.implicitHeight + Theme.space.xxl

        Row {
            id: headerRow

            x: Theme.space.xxl
            y: Theme.space.xxl
            width: parent.width - 2 * Theme.space.xxl
            spacing: Theme.space.md

            Rectangle {
                id: mark

                objectName: "recordingTile"
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.size.tileLarge
                height: width
                radius: Theme.radius.lg
                color: Theme.colors.dangerSubtle

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.size.lockHalo
                    height: width
                    radius: width / 2
                    color: Theme.colors.danger
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, headerRow.width - mark.width - moreButton.width - closeButton.width
                                   - 3 * headerRow.spacing)
                spacing: Theme.space.xxs

                Text {
                    objectName: "recordingTitle"
                    width: parent.width
                    textFormat: Text.PlainText
                    text: dialog.title
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.subtitle.size
                    font.weight: Font.DemiBold
                }

                Text {
                    objectName: "recordingDetails"
                    width: parent.width
                    textFormat: Text.PlainText
                    text: dialog.details
                    elide: Text.ElideRight
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    font.features: ({ "tnum": 1 })
                }
            }

            Button {
                id: moreButton

                objectName: "recordingMore"
                anchors.verticalCenter: parent.verticalCenter
                tone: "ghost"
                small: true
                iconName: "ellipsis"
                toolTip: qsTr("More")
                onClicked: moreMenu.open()

                Menu {
                    id: moreMenu

                    x: moreButton.width - width
                    y: moreButton.height + Theme.space.xs

                    MenuItem {
                        objectName: "turnIntoBlueprint"
                        iconName: "workflow"
                        text: qsTr("Turn into blueprint…")
                        onTriggered: dialog.turnIntoBlueprint()
                    }

                    MenuItem {
                        objectName: "saveAsTest"
                        iconName: "flask-conical"
                        text: qsTr("Save as test…")
                        onTriggered: dialog.chooseTestProfile()
                    }
                }
            }

            Button {
                id: closeButton

                objectName: "recordingClose"
                anchors.verticalCenter: parent.verticalCenter
                tone: "ghost"
                small: true
                iconName: "x"
                description: qsTr("Close")
                onClicked: dialog.reject()
            }
        }
    }

    Column {
        width: parent.width
        spacing: Theme.space.md

        Text {
            objectName: "recordingProblem"
            width: parent.width
            visible: (dialog.info.problem ?? "") !== ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("The recording stopped early: %1").arg(dialog.info.problem ?? "")
            color: Theme.colors.dangerText
            font.pixelSize: Theme.type.caption.size
        }

        Row {
            id: frameRow

            objectName: "recordingFrames"
            width: parent.width
            visible: dialog.frames.length > 0
            spacing: Theme.space.xxs

            Repeater {
                model: dialog.frames

                delegate: ThumbnailView {
                    required property var modelData

                    width: dialog.frameWidth
                    height: dialog.frameHeight
                    radius: Theme.radius.xs
                    image: modelData
                }
            }
        }

        UseCard {
            objectName: "exportCard"
            iconName: "monitor-play"
            tone: "orange"
            title: qsTr("Export video")
            description: qsTr("An MP4 made straight from the recorded stream, without encoding it again; it takes "
                              + "seconds.")
            onClicked: {
                dialog.close()
                dialog.exportVideo()
            }
        }

        UseCard {
            objectName: "testCard"
            iconName: "flask-conical"
            tone: "teal"
            title: qsTr("Use to test logic")
            description: (dialog.info.profileName ?? "") !== ""
                         ? qsTr("Runs the logic of %1 on this recording, vision included, without the phone.")
                           .arg(dialog.info.profileName)
                         : qsTr("No profile was in use while recording, so there is no logic to run on it.")
            enabled: (dialog.info.profileId ?? "") !== ""
            onClicked: {
                dialog.close()
                dialog.testLogic()
            }
        }
    }

    Connections {
        target: Recordings

        function onTestSaved(ticket: int, ok: bool, text: string) {
            if (ticket !== dialog.testTicket)
                return
            dialog.testTicket = 0
            if (ok) {
                dialog.toast(text, "ok", "")
                return
            }
            if (notice.visible)
                notice.close()
            dialog.notified(text, "triangle-alert")
        }
    }

    Toast {
        id: notice

        property string task

        objectName: "recordingNotice"
        onActionTriggered: {
            const problem = DeviceManager.openTaskLogic(notice.task)
            if (problem !== "")
                dialog.notified(problem, "triangle-alert")
        }
    }

    Dialog {
        id: testPicker

        readonly property real bodyLimit: (parent ? parent.height : 0) - 2 * Theme.space.x3l - implicitHeaderHeight
                                          - implicitFooterHeight - topPadding - bottomPadding

        objectName: "recordingTestPicker"
        title: qsTr("Save as test")

        header: Column {
            topPadding: Theme.space.xxl
            leftPadding: Theme.space.xxl
            rightPadding: Theme.space.xxl
            spacing: Theme.space.xs

            Text {
                width: testPicker.availableWidth
                textFormat: Text.PlainText
                text: testPicker.title
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.subtitle.size
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                width: testPicker.availableWidth
                textFormat: Text.PlainText
                text: qsTr("Pick the profile whose logic runs on this recording. What it does now is kept as the "
                           + "expected result, so after you change the logic, Run tests in the logic view shows what "
                           + "changed.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }

        footer: Item {
            implicitHeight: cancelTest.implicitHeight + 2 * Theme.space.xl

            Button {
                id: cancelTest

                objectName: "reject"
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.xxl
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Cancel")
                onClicked: testPicker.reject()
            }
        }

        Flickable {
            id: testScroller

            width: testPicker.availableWidth
            height: implicitHeight
            implicitHeight: testPicker.bodyLimit > 0 ? Math.min(testList.implicitHeight, testPicker.bodyLimit)
                                                     : testList.implicitHeight
            contentWidth: width
            contentHeight: testList.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Column {
                id: testList

                objectName: "recordingTestProfiles"
                width: testScroller.width
                spacing: Theme.space.sm

                Repeater {
                    model: dialog.testProfiles

                    delegate: ProfileCard {}
                }

                Text {
                    objectName: "noTestProfiles"
                    width: parent.width
                    visible: dialog.testProfiles.length === 0
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: qsTr("No profile has logic yet. Add logic to a profile with Edit logic… on the Profiles "
                               + "page, then save the test.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                }
            }
        }
    }

    component ProfileCard: T.AbstractButton {
        id: profileCard

        required property var modelData
        readonly property bool recorded: profileCard.modelData.id === (dialog.info.profileId ?? "")

        objectName: "recordingTestProfile"
        width: parent ? parent.width : implicitWidth
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        padding: Theme.space.md
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: profileCard.modelData.name
        onClicked: dialog.saveTest(profileCard.modelData)

        background: Rectangle {
            radius: Theme.radius.lg
            color: profileCard.down ? Theme.colors.statePress : profileCard.hovered ? Theme.colors.stateSelected
                                                                                    : Theme.colors.stateHover
            border.width: Theme.size.stroke
            border.color: profileCard.hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault

            FocusRing {
                visible: profileCard.visualFocus
                baseRadius: Theme.radius.lg
            }
        }

        contentItem: Row {
            spacing: Theme.space.md

            Tile {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "flask-conical"
                tone: "teal"
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, profileCard.availableWidth - Theme.size.tile - Theme.space.md)
                spacing: Theme.space.xxs

                Text {
                    objectName: "recordingTestProfileName"
                    width: parent.width
                    textFormat: Text.PlainText
                    text: profileCard.modelData.name
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                }

                Text {
                    width: parent.width
                    visible: profileCard.recorded
                    textFormat: Text.PlainText
                    text: qsTr("In use while recording")
                    elide: Text.ElideRight
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }
    }

    component UseCard: T.AbstractButton {
        id: card

        required property string iconName
        required property string tone
        required property string title
        required property string description

        width: parent ? parent.width : implicitWidth
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        padding: Theme.space.md
        hoverEnabled: true
        opacity: enabled ? 1 : Theme.disabledOpacity
        Accessible.name: card.title
        Accessible.description: card.description

        background: Rectangle {
            radius: Theme.radius.lg
            color: card.down ? Theme.colors.statePress : card.hovered ? Theme.colors.stateSelected
                                                                      : Theme.colors.stateHover
            border.width: Theme.size.stroke
            border.color: card.hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault

            FocusRing {
                visible: card.visualFocus
                baseRadius: Theme.radius.lg
            }
        }

        contentItem: Row {
            spacing: Theme.space.md

            Tile {
                anchors.verticalCenter: parent.verticalCenter
                iconName: card.iconName
                tone: card.tone
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, card.availableWidth - Theme.size.tile - Theme.space.md)
                spacing: Theme.space.xxs

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: card.title
                    elide: Text.ElideRight
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                }

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: card.description
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }
    }
}
