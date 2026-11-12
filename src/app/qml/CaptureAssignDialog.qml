pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Dialog {
    id: dialog

    property string serial
    property var phones: []
    property int ticket: 0
    property bool comparing: false
    property string suggested
    property string chosen
    property bool picked: false
    readonly property real bodyLimit: (parent ? parent.height : 0) - 2 * Theme.space.x3l - implicitHeaderHeight
                                      - implicitFooterHeight - topPadding - bottomPadding

    function show(target: string, capture: var) {
        const assignment = capture?.assignment ?? ""
        const current = assignment === "assigned" ? capture.phone ?? ""
                      : assignment === "standalone" ? "standalone" : ""
        dialog.serial = target
        dialog.phones = DeviceManager.phonesForCapture(target)
        dialog.suggested = ""
        dialog.picked = false
        dialog.chosen = dialog.listed(current) ? current : ""
        dialog.ticket = 0
        dialog.comparing = true
        dialog.open()
        const issued = DeviceManager.suggestPhoneForCapture(target)
        if (dialog.comparing) {
            dialog.ticket = issued
            dialog.comparing = issued !== 0
        }
    }

    function listed(id: string): bool {
        return id === "standalone" || (id !== "" && dialog.phones.some(phone => phone.deviceId === id))
    }

    function choose(id: string) {
        dialog.chosen = id
        dialog.picked = true
    }

    function refresh() {
        dialog.phones = DeviceManager.phonesForCapture(dialog.serial)
        if (!dialog.listed(dialog.chosen))
            dialog.chosen = ""
    }

    function kindText(kind: string): string {
        switch (kind) {
        case "aoa":
            return qsTr("AOA on USB")
        case "demo":
            return qsTr("Demo device")
        default:
            return qsTr("USB debugging")
        }
    }

    function confirmFocused() {
        if (cancelButton.activeFocus)
            dialog.reject()
        else if (dialog.chosen !== "")
            dialog.accept()
    }

    objectName: "captureAssignDialog"
    implicitWidth: Theme.size.sheetWidth
    width: parent ? Math.min(implicitWidth, parent.width - 2 * Theme.space.lg) : implicitWidth
    title: qsTr("Which phone is this the picture of?")
    onAccepted: DeviceManager.assignCapture(dialog.serial, dialog.chosen)

    footer: Item {
        implicitHeight: buttons.implicitHeight + 2 * Theme.space.xl

        Keys.onReturnPressed: dialog.confirmFocused()
        Keys.onEnterPressed: dialog.confirmFocused()

        Row {
            id: buttons

            anchors.right: parent.right
            anchors.rightMargin: Theme.space.xxl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Button {
                id: cancelButton

                objectName: "captureAssignCancel"
                text: qsTr("Cancel")
                onClicked: dialog.reject()
            }

            Button {
                objectName: "captureAssignConfirm"
                focus: true
                tone: "primary"
                enabled: dialog.chosen !== ""
                text: qsTr("Assign")
                onClicked: dialog.accept()
            }
        }
    }

    Connections {
        target: DeviceManager
        enabled: dialog.visible

        function onLinkRevisionChanged() {
            dialog.refresh()
        }

        function onPhoneSuggested(ticket: int, captureSerial: string, deviceId: string) {
            if (!dialog.comparing || captureSerial !== dialog.serial || (dialog.ticket !== 0 && ticket !== dialog.ticket))
                return
            dialog.comparing = false
            dialog.suggested = deviceId
            if (deviceId !== "" && !dialog.picked && dialog.listed(deviceId))
                dialog.chosen = deviceId
        }
    }

    Flickable {
        id: scroller

        width: dialog.availableWidth
        height: implicitHeight
        implicitHeight: dialog.bodyLimit > 0 ? Math.min(body.implicitHeight, dialog.bodyLimit) : body.implicitHeight
        contentWidth: width
        contentHeight: body.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: body

            objectName: "captureAssignList"
            width: scroller.width
            spacing: Theme.space.md

            Text {
                objectName: "captureAssignCaption"
                width: parent.width
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("The phone's picture appears on this capture card; touch can then go through USB debugging "
                           + "(also wireless), AOA or a touch box from an extension.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            Row {
                objectName: "captureAssignComparing"
                visible: dialog.comparing && dialog.phones.length > 0
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
                    text: qsTr("Comparing the pictures…")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }
            }

            Text {
                objectName: "captureAssignUnsure"
                width: parent.width
                visible: !dialog.comparing && dialog.suggested === "" && dialog.phones.length > 0
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("Could not tell from the picture; choose the phone yourself.")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Text {
                objectName: "captureAssignNoPhone"
                width: parent.width
                visible: dialog.phones.length === 0
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("No phone is connected. Connect the phone with USB debugging or wireless debugging first, "
                           + "or use the capture card on its own.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }

            Repeater {
                model: dialog.phones

                delegate: Choice {
                    id: phoneChoice

                    required property var modelData
                    readonly property string kind: phoneChoice.modelData.kind ?? ""

                    objectName: "captureAssignPhone"
                    choiceId: phoneChoice.modelData.deviceId ?? ""
                    iconName: phoneChoice.kind === "aoa" ? "usb" : "smartphone"
                    tone: phoneChoice.kind === "aoa" ? "amber" : phoneChoice.kind === "demo" ? "" : "azure"
                    text: phoneChoice.modelData.name ?? ""
                    detail: [dialog.kindText(phoneChoice.kind),
                             phoneChoice.modelData.online === false ? qsTr("Offline") : ""]
                            .filter(part => part !== "").join(" · ")
                    suggested: phoneChoice.choiceId !== "" && phoneChoice.choiceId === dialog.suggested
                }
            }

            Choice {
                objectName: "captureAssignStandalone"
                choiceId: "standalone"
                iconName: "monitor-play"
                text: qsTr("No phone: use this capture card on its own")
                detail: qsTr("Touch it with AOA or a touch box from an extension, or just watch the picture.")
            }
        }
    }

    component Choice: T.AbstractButton {
        id: choice

        property string choiceId
        property string iconName
        property string tone
        property string detail
        property bool suggested: false
        readonly property bool selected: choice.choiceId === dialog.chosen

        width: parent ? parent.width : implicitWidth
        implicitHeight: Math.max(Theme.size.settingsRow, implicitContentHeight + topPadding + bottomPadding)
        padding: Theme.space.md
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.RadioButton
        Accessible.name: choice.text
        Accessible.description: choice.detail
        Accessible.checkable: true
        Accessible.checked: choice.selected
        onClicked: dialog.choose(choice.choiceId)

        background: Rectangle {
            radius: Theme.radius.lg
            color: choice.selected ? Theme.colors.accentSubtle
                 : choice.down ? Theme.colors.statePress
                 : choice.hovered ? Theme.colors.stateSelected : Theme.colors.stateHover
            border.width: Theme.size.stroke
            border.color: choice.selected ? Theme.colors.accentLine
                        : choice.hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault

            FocusRing {
                visible: choice.visualFocus
                baseRadius: Theme.radius.lg
            }
        }

        contentItem: RowLayout {
            spacing: Theme.space.md

            Tile {
                iconName: choice.iconName
                tone: choice.tone
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space.xxs

                Text {
                    objectName: "captureAssignName"
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: choice.text
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    objectName: "captureAssignDetail"
                    Layout.fillWidth: true
                    visible: choice.detail !== ""
                    textFormat: Text.PlainText
                    text: choice.detail
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }

            StatusPill {
                objectName: "captureAssignSuggested"
                visible: choice.suggested
                tone: "ok"
                text: qsTr("Looks like this one")
            }

            Icon {
                name: "check"
                opacity: choice.selected ? 1 : 0
                size: Theme.size.iconSmall
                color: Theme.colors.accentText
            }
        }
    }
}
