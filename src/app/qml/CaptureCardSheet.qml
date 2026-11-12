pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingsSheet {
    id: sheet

    property string serial
    readonly property DeviceRow device: {
        for (let index = 0; index < rows.count; ++index) {
            const row = rows.objectAt(index) as DeviceRow
            if (row !== null && row.serial === sheet.serial)
                return row
        }
        return null
    }
    readonly property var capture: sheet.device !== null ? sheet.device.capture : ({})
    readonly property var methods: sheet.capture.touchMethods ?? []
    readonly property var phones: sheet.capture.phones ?? []
    readonly property var formats: [""].concat(sheet.capture.formats ?? [])
    readonly property var profiles: [{ id: "", name: qsTr("No profile") }]
                                    .concat(ProfileLibrary.entries.filter(entry => entry.error === ""))
    readonly property string touch: sheet.capture.touch ?? "none"
    readonly property string assignment: sheet.capture.assignment ?? "standalone"
    readonly property bool assigned: sheet.assignment === "assigned"
    readonly property string phoneName: {
        const name = sheet.capture.phoneName ?? ""
        return name !== "" ? name : sheet.capture.phone ?? ""
    }

    signal assignWanted()

    component DeviceRow: QtObject {
        required property string serial
        required property string name
        required property string pill
        required property string tone
        required property bool connected
        required property var capture
    }

    function show(target: string): bool {
        for (let index = 0; index < rows.count; ++index) {
            const row = rows.objectAt(index) as DeviceRow
            if (row !== null && row.serial === target && row.capture.isCapture === true) {
                sheet.serial = target
                sheet.open()
                return true
            }
        }
        return false
    }

    function closeIfGone() {
        if (sheet.device === null)
            sheet.close()
    }

    function openPhoneSettings() {
        const phone = sheet.capture.phone ?? ""
        if (phone === "")
            return
        const entry = DeviceManager.phonesForCapture(sheet.serial).find(candidate => candidate.deviceId === phone)
        const target = entry?.serial ?? ""
        sheet.close()
        if (target === "" || !Commands.trigger("device.settings", target))
            SettingsCatalog.openForDevice(phone, sheet.phoneName)
    }

    function formatLabel(name: string): string {
        if (name === "") {
            const live = sheet.capture.format ?? ""
            return (sheet.capture.chosenFormat ?? "") === "" && live !== "" ? qsTr("Automatic · %1").arg(live)
                                                                             : qsTr("Automatic")
        }
        const parts = /^(\d+)x(\d+)@([\d.]+)\/(\w+)$/.exec(name)
        return parts === null ? name
                              : qsTr("%1 × %2 · %3 fps · %4").arg(parts[1]).arg(parts[2]).arg(parts[3]).arg(parts[4])
    }

    objectName: "captureSheet"
    iconName: "monitor-play"
    title: sheet.device !== null ? sheet.device.name : ""
    subtitle: qsTr("Capture card · changes apply at once")
    statusText: sheet.device !== null ? sheet.device.pill : ""
    statusTone: sheet.device !== null ? sheet.device.tone : "idle"
    alternativeText: qsTr("About capture cards")
    onAlternativeChosen: Commands.trigger("app.help", "capture-card")
    onDeviceChanged: {
        if (sheet.device === null && sheet.opened)
            Qt.callLater(sheet.closeIfGone)
    }

    Instantiator {
        id: rows

        model: DeviceManager
        delegate: DeviceRow {}
    }

    SettingsGroup {
        objectName: "capturePhoneGroup"
        title: qsTr("Phone")

        SettingsRow {
            objectName: "captureAssignment"
            width: parent.width
            title: sheet.assigned ? sheet.phoneName
                                  : sheet.assignment === "ask" ? qsTr("Not decided yet") : qsTr("Used on its own")

            Button {
                objectName: "captureSheetChoosePhone"
                small: true
                iconName: "link"
                text: sheet.assigned ? qsTr("Change…") : qsTr("Choose phone…")
                onClicked: sheet.assignWanted()
            }

            Button {
                objectName: "captureUseAlone"
                visible: sheet.assigned
                small: true
                tone: "ghost"
                iconName: "unlink"
                text: qsTr("Use on its own")
                onClicked: DeviceManager.assignCapture(sheet.serial, "standalone")
            }
        }

        SettingsRow {
            objectName: "captureTouchElsewhere"
            width: parent.width
            divider: true
            visible: sheet.assigned
            title: qsTr("Touch is chosen in this phone's settings.")

            Button {
                objectName: "capturePhoneSettings"
                small: true
                tone: "ghost"
                iconName: "sliders-horizontal"
                text: qsTr("Phone settings…")
                onClicked: sheet.openPhoneSettings()
            }
        }
    }

    SettingsGroup {
        objectName: "captureTouchGroup"
        title: qsTr("Touch")
        visible: !sheet.assigned

        SettingsRow {
            width: parent.width
            title: qsTr("Touch method")
            description: qsTr("How your taps reach the phone: not at all, so you only watch the picture; over the "
                              + "phone's USB cable with AOA; or through a way an extension adds, such as a touch box "
                              + "on the devices page.")

            Select {
                id: touchChoice

                objectName: "captureTouch"
                model: sheet.methods.map(method => method.name)
                currentIndex: Math.max(0, sheet.methods.findIndex(method => method.id === sheet.touch))
                Accessible.name: qsTr("Touch method")
                onActivated: index => {
                    DeviceManager.setCaptureOption(sheet.serial, "touch", sheet.methods[index].id)
                    touchChoice.currentIndex = Qt.binding(() => Math.max(0, sheet.methods.findIndex(
                                                                             method => method.id === sheet.touch)))
                }
            }
        }

        SettingsRow {
            objectName: "capturePhoneRow"
            width: parent.width
            divider: true
            visible: sheet.touch === "aoa"
            title: qsTr("Phone on USB")
            note: sheet.phones.length === 0 ? qsTr("Connect the phone to this computer with a USB cable") : ""

            Select {
                id: phoneChoice

                objectName: "capturePhone"
                visible: sheet.phones.length > 0
                model: sheet.phones.map(phone => phone.ready ? phone.name
                                                             : qsTr("%1 · needs the WinUSB driver").arg(phone.name))
                currentIndex: sheet.phones.findIndex(phone => phone.serial === sheet.capture.aoaPhone)
                displayText: phoneChoice.currentIndex >= 0 ? phoneChoice.currentText : qsTr("Choose a phone")
                Accessible.name: qsTr("Phone on USB")
                onActivated: index => {
                    DeviceManager.setCaptureOption(sheet.serial, "aoaPhone", sheet.phones[index].serial)
                    phoneChoice.currentIndex = Qt.binding(() => sheet.phones.findIndex(
                                                              phone => phone.serial === sheet.capture.aoaPhone))
                }
            }

            Button {
                objectName: "captureDriverHelp"
                visible: sheet.phones.length === 0
                small: true
                tone: "ghost"
                iconName: "circle-help"
                text: qsTr("How to install the driver")
                onClicked: Commands.trigger("app.help", "aoa-driver")
            }
        }
    }

    SettingsGroup {
        objectName: "capturePictureGroup"
        title: qsTr("Picture and sound")

        SettingsRow {
            width: parent.width
            title: qsTr("Picture format")

            Select {
                id: formatChoice

                objectName: "captureFormat"
                model: sheet.formats.map(name => sheet.formatLabel(name))
                currentIndex: Math.max(0, sheet.formats.indexOf(sheet.capture.chosenFormat ?? ""))
                Accessible.name: qsTr("Picture format")
                onActivated: index => {
                    DeviceManager.setCaptureOption(sheet.serial, "format", sheet.formats[index])
                    formatChoice.currentIndex = Qt.binding(() => Math.max(0, sheet.formats.indexOf(
                                                                              sheet.capture.chosenFormat ?? "")))
                }
            }
        }

        SettingsRow {
            width: parent.width
            divider: true
            title: qsTr("Sound")
            note: sheet.device !== null && sheet.device.connected && sheet.capture.sound === true
                  && sheet.capture.soundFound !== true ? qsTr("No sound device of its own was found") : ""

            Switch {
                id: soundSwitch

                objectName: "captureSound"
                checked: sheet.capture.sound === true
                Accessible.name: qsTr("Sound")
                onToggled: {
                    DeviceManager.setCaptureOption(sheet.serial, "sound", soundSwitch.checked)
                    soundSwitch.checked = Qt.binding(() => sheet.capture.sound === true)
                }
            }
        }
    }

    SettingsGroup {
        objectName: "captureMappingGroup"
        title: qsTr("Key mapping")

        SettingsRow {
            width: parent.width
            title: qsTr("Profile")

            Select {
                id: profileChoice

                objectName: "captureProfile"
                model: sheet.profiles.map(entry => entry.name)
                currentIndex: Math.max(0, sheet.profiles.findIndex(entry => entry.id === (sheet.capture.profileId
                                                                                         ?? "")))
                Accessible.name: qsTr("Profile")
                onActivated: index => {
                    DeviceManager.useProfile(sheet.serial, sheet.profiles[index].id)
                    profileChoice.currentIndex = Qt.binding(() => Math.max(0, sheet.profiles.findIndex(
                                                                               entry => entry.id === (sheet.capture.profileId
                                                                                                      ?? ""))))
                }
            }
        }
    }
}
