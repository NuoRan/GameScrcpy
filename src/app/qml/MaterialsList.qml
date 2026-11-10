pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: materials

    required property VisionWorkbench bench
    readonly property var items: materials.bench.requestItems
    property int menuIndex: -1

    function iconOf(kind: string): string {
        switch (kind) {
        case "template":
            return "square"
        case "region":
            return "expand"
        case "point":
            return "crosshair"
        }
        return "palette"
    }

    function kindText(kind: string): string {
        switch (kind) {
        case "template":
            return qsTr("Template")
        case "region":
            return qsTr("Region")
        case "point":
            return qsTr("Point")
        }
        return qsTr("Color")
    }

    function stateText(item: var): string {
        switch (item.state) {
        case "done":
            return item.resource === "" ? qsTr("Done") : qsTr("Done: %1").arg(item.resource)
        case "existing":
            return qsTr("Uses %1").arg(item.resource)
        case "skipped":
            return qsTr("Skipped")
        }
        return item.current === true ? qsTr("Now") : ""
    }

    function stateTone(item: var): color {
        switch (item.state) {
        case "done":
            return Theme.colors.successText
        case "existing":
            return Theme.colors.accentText
        case "skipped":
            return Theme.colors.textTertiary
        }
        return Theme.colors.accentText
    }

    Text {
        id: title

        objectName: "materialsTitle"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.space.lg
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        text: qsTr("An AI is asking you for materials")
        color: Theme.colors.textPrimary
        font.pixelSize: Theme.type.body.size
        font.weight: Font.DemiBold
    }

    Text {
        id: progress

        objectName: "materialsProgress"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: title.bottom
        anchors.leftMargin: Theme.space.lg
        anchors.rightMargin: Theme.space.lg
        anchors.topMargin: Theme.space.xs
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        text: qsTr("%1 of %2 answered").arg(materials.bench.requestDoneCount).arg(materials.items.length)
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.caption.size
    }

    Button {
        id: give

        objectName: "materialsGive"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space.lg
        tone: "primary"
        text: qsTr("Give to AI")
        enabled: materials.bench.requestAnswered
        toolTip: materials.bench.requestAnswered
                 ? qsTr("Saves these into the AI's draft of the profile and answers it")
                 : qsTr("Answer, skip or choose an existing resource for every item first")
        onClicked: materials.bench.giveRequest()
    }

    ListView {
        id: rows

        objectName: "materialsList"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: progress.bottom
        anchors.bottom: give.top
        anchors.topMargin: Theme.space.md
        anchors.bottomMargin: Theme.space.md
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: materials.items

        delegate: Rectangle {
            id: row

            required property int index
            required property var modelData

            readonly property bool pending: row.modelData.state === "pending"
            readonly property bool current: row.modelData.current === true
            readonly property var existing: row.modelData.existing ?? []

            objectName: "materialRow"
            width: ListView.view.width
            height: content.implicitHeight + 2 * Theme.space.md + Theme.space.xs
            radius: Theme.radius.md
            color: row.current ? Theme.colors.accentSubtle : "transparent"
            border.width: row.current ? Theme.size.stroke : 0
            border.color: Theme.colors.accent

            Column {
                id: content

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.space.md
                anchors.rightMargin: Theme.space.md
                spacing: Theme.space.xs

                Row {
                    spacing: Theme.space.sm

                    Icon {
                        name: materials.iconOf(row.modelData.kind)
                        size: Theme.size.iconSmall
                        color: Theme.colors.textSecondary
                    }

                    Text {
                        width: content.width - Theme.size.iconSmall - Theme.space.sm
                        textFormat: Text.PlainText
                        text: row.modelData.name
                        elide: Text.ElideRight
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        font.weight: Font.DemiBold
                    }
                }

                Text {
                    width: parent.width
                    visible: (row.modelData.note ?? "") !== ""
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: row.modelData.note ?? ""
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: {
                        const state = materials.stateText(row.modelData)
                        const kind = materials.kindText(row.modelData.kind)
                        return state === "" ? kind : kind + " · " + state
                    }
                    elide: Text.ElideRight
                    color: materials.stateTone(row.modelData)
                    font.pixelSize: Theme.type.caption.size
                }

                Row {
                    visible: row.pending
                    spacing: Theme.space.xs

                    Button {
                        small: true
                        tone: "ghost"
                        text: qsTr("Skip")
                        Accessible.name: qsTr("Skip %1").arg(row.modelData.name)
                        onClicked: materials.bench.skipRequest(row.index)
                    }

                    Button {
                        small: true
                        tone: "ghost"
                        visible: row.existing.length > 0
                        text: qsTr("Use existing…")
                        Accessible.name: qsTr("Use an existing resource for %1").arg(row.modelData.name)
                        onClicked: {
                            materials.menuIndex = row.index
                            const spot = row.mapToItem(materials, 0, row.height)
                            existingMenu.x = spot.x
                            existingMenu.y = spot.y
                            existingMenu.open()
                        }
                    }
                }
            }

            TapHandler {
                enabled: row.pending
                onTapped: materials.bench.chooseRequest(row.index)
            }
        }
    }

    Text {
        id: empty

        objectName: "materialsEmpty"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: progress.bottom
        anchors.margins: Theme.space.lg
        visible: materials.items.length === 0
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        text: qsTr("The list is empty")
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.label.size
    }

    Menu {
        id: existingMenu

        objectName: "materialsExisting"
        parent: materials

        Instantiator {
            model: materials.menuIndex >= 0 && materials.menuIndex < materials.items.length
                   ? (materials.items[materials.menuIndex].existing ?? []) : []

            delegate: MenuItem {
                required property string modelData

                text: modelData
                onTriggered: materials.bench.useExistingRequest(materials.menuIndex, modelData)
            }

            onObjectAdded: (index, object) => existingMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => existingMenu.removeItem(object)
        }
    }
}
