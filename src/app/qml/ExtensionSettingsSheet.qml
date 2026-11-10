pragma ComponentBehavior: Bound

import QtQuick
import QtQml.Models
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App
import "ExtensionLook.js" as ExtensionLook

SettingsSheet {
    id: sheet

    property string entryKey
    property int revision: 0
    readonly property var entry: ExtensionHost.entries.find(item => item.key === sheet.entryKey) ?? null
    readonly property string extensionId: sheet.entry !== null ? sheet.entry.id : ""
    readonly property string lead: sheet.entry !== null ? ExtensionLook.lead(sheet.entry) : ""
    readonly property var parameters: SettingsCatalog.extensionGroups.find(group => group.id === sheet.extensionId)?.settings
                                      ?? []
    readonly property var grants: HardwareAccess.grants[sheet.extensionId] ?? []
    readonly property var addOns: ExtensionHost.addOnsRevision >= 0 && ExtensionHost.nativeAttachments.length >= 0
                                   && sheet.extensionId !== "" ? ExtensionHost.globalAddOns(sheet.extensionId) : []
    readonly property var settingRows: {
        const found = []
        if (sheet.revision < 0)
            return found
        for (let i = 0; i < parameterRows.count; ++i) {
            const row = parameterRows.itemAt(i) as SettingRow
            if (row !== null)
                found.push(row)
        }
        return found
    }
    readonly property bool restorable: sheet.settingRows.some(row => row.changed)

    function openFor(item: var) {
        SettingsCatalog.scoped = false
        sheet.entryKey = item.key ?? ""
        sheet.open()
    }

    function restoreAll() {
        for (const row of sheet.settingRows) {
            if (row.changed)
                row.restore()
        }
    }

    objectName: "extensionSettingsSheet"
    title: sheet.entry !== null ? sheet.entry.name : ""
    subtitle: qsTr("Extension · %1 · changes apply at once").arg(sheet.entry !== null ? sheet.entry.version : "")
    iconName: ExtensionLook.icon(sheet.lead)
    tone: ExtensionLook.tone(sheet.lead)
    alternativeText: sheet.restorable ? qsTr("Restore defaults") : ""
    onAlternativeChosen: {
        sheet.restoreAll()
        sheet.open()
    }
    onEntryChanged: {
        if (sheet.opened && (sheet.entry === null || sheet.entry.active !== true))
            sheet.close()
    }

    SettingsGroup {
        objectName: "extensionParameters"
        title: qsTr("Parameters")
        visible: sheet.parameters.length > 0

        Repeater {
            id: parameterRows

            model: sheet.parameters
            onItemAdded: sheet.revision += 1
            onItemRemoved: sheet.revision += 1

            delegate: DelegateChooser {
                role: "control"

                DelegateChoice {
                    roleValue: "switch"

                    SwitchSetting {
                        required property var modelData

                        path: modelData.path
                        visible: true
                        highlight: ""
                    }
                }
                DelegateChoice {
                    roleValue: "select"

                    SelectSetting {
                        id: choiceRow

                        required property var modelData

                        path: modelData.path
                        visible: true
                        highlight: ""
                        labels: choiceRow.entry?.choiceLabels ?? ({})
                    }
                }
                DelegateChoice {
                    roleValue: "slider"

                    SliderSetting {
                        id: sliderRow

                        required property var modelData

                        path: modelData.path
                        visible: true
                        highlight: ""
                        step: sliderRow.entry?.step ?? 1
                        decimals: sliderRow.entry?.decimals ?? 0
                    }
                }
                DelegateChoice {
                    roleValue: "number"

                    NumberSetting {
                        id: numberRow

                        required property var modelData

                        path: modelData.path
                        visible: true
                        highlight: ""
                        decimals: numberRow.entry?.decimals ?? 0
                    }
                }
                DelegateChoice {
                    roleValue: "text"

                    TextSetting {
                        required property var modelData

                        path: modelData.path
                        visible: true
                        highlight: ""
                    }
                }
            }
        }
    }

    SettingsGroup {
        objectName: "extensionAddOns"
        title: qsTr("Add-ons for every profile")
        visible: sheet.addOns.length > 0

        Text {
            x: Theme.space.lg + Theme.space.xxs
            width: parent.width - 2 * x
            topPadding: Theme.space.md
            bottomPadding: Theme.space.sm
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: qsTr("Turned-on add-ons work on every fitting key in every profile. To leave one key out, turn it off in that key's Add-ons in the key mapping editor.")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Repeater {
            model: sheet.addOns.length

            delegate: SettingsRow {
                id: addOnRow

                required property int index
                readonly property var modelData: index < sheet.addOns.length ? sheet.addOns[index] : ({})

                objectName: "extensionAddOn"
                width: parent.width
                divider: true
                title: modelData.title ?? ""
                description: modelData.description ?? ""
                note: qsTr("Works on: %1").arg(modelData.fits ?? "")

                Switch {
                    id: addOnOn

                    objectName: "extensionAddOnSwitch"
                    checked: addOnRow.modelData.on === true
                    Accessible.name: addOnRow.modelData.title ?? ""
                    onToggled: {
                        ExtensionHost.setGlobalAddOn(sheet.extensionId, addOnRow.modelData.name ?? "", checked)
                        addOnOn.checked = Qt.binding(() => addOnRow.modelData.on === true)
                    }
                }

                below: DeclaredParams {
                    width: parent ? parent.width : 0
                    visible: addOnRow.modelData.on === true && params.length > 0
                    params: addOnRow.modelData.params ?? []
                    onEdited: (key, json) => ExtensionHost.setGlobalAddOnParam(sheet.extensionId,
                                                                              addOnRow.modelData.name ?? "", key, json)
                }
            }
        }
    }

    SettingsGroup {
        objectName: "extensionDevices"
        title: qsTr("Devices it may use")
        visible: sheet.grants.length > 0

        AllowedDevices {
            owner: sheet.extensionId
        }
    }
}
