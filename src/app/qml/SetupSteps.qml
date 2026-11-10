pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.App
import "HelpRich.js" as HelpRich

QtObject {
    id: setup

    property string vendorId: "other"
    property bool rich: false

    readonly property var vendors: SetupGuide.vendors
    readonly property var vendor: setup.vendors.find(entry => entry.id === setup.vendorId)
                                  ?? setup.vendors[setup.vendors.length - 1]
    readonly property var steps: setup.stepsFor(setup.vendor)

    function text(entry: var): string {
        return entry ? qsTranslate("PhoneVendors", entry.source, entry.comment) : ""
    }

    function lastPart(path: string): string {
        const parts = path.split(" > ")
        return parts[parts.length - 1]
    }

    function sentence(template: string, parts: list<string>): string {
        return HelpRich.sentence(template, parts, setup.rich ? Theme.colors : null)
    }

    function stepsFor(vendor: var): var {
        const list = [{
            screen: "about",
            title: qsTr("Turn on Developer options"),
            detail: setup.sentence(qsTr("Open %1 and tap “%2” 7 times, until the phone says you are now a developer."),
                                   [setup.text(vendor.aboutPath), setup.text(vendor.versionLabel)]),
            note: setup.text(vendor.versionNote),
            phoneTitle: setup.lastPart(setup.text(vendor.aboutPath)),
            target: setup.text(vendor.versionLabel)
        }, {
            screen: "developer",
            title: qsTr("Turn on USB debugging"),
            detail: setup.sentence(qsTr("Open %1 and turn on “USB debugging”."), [setup.text(vendor.developerPath)]),
            note: setup.text(vendor.debugNote),
            phoneTitle: setup.lastPart(setup.text(vendor.developerPath)),
            target: ""
        }]
        if (vendor.inputSwitch) {
            list.push({
                screen: "input",
                title: qsTr("Allow keyboard and mouse control"),
                detail: setup.sentence(qsTr("In the same place, also turn on “%1”."), [setup.text(vendor.inputSwitch)]),
                note: setup.text(vendor.inputNote),
                phoneTitle: setup.lastPart(setup.text(vendor.developerPath)),
                target: setup.text(vendor.inputSwitch)
            })
        }
        list.push({
            screen: "allow",
            title: qsTr("Allow this computer"),
            detail: setup.sentence(qsTr("Connect the phone with a USB cable. When it asks “Allow USB debugging?”, tick “Always allow from this computer” and tap Allow."), []),
            note: "",
            phoneTitle: "",
            target: ""
        })
        return list
    }
}
