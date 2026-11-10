pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: page

    required property string serial
    property bool transfersOpen: false
    property int lastRow: -1
    readonly property bool canChange: PhoneFiles.online && !PhoneFiles.busy
    readonly property string folderName: PhoneFiles.crumbs.length > 1
                                         ? PhoneFiles.crumbs[PhoneFiles.crumbs.length - 1].name : qsTr("Phone storage")
    readonly property int nameIndent: 2 * Theme.space.md + Theme.size.icon
    readonly property int dateColumn: Theme.size.fieldWidth
    readonly property int sizeColumn: Theme.size.numberFieldWide
    readonly property int transferRow: Theme.size.listItem + Theme.space.md

    signal back()

    function askDelete() {
        if (PhoneFiles.selectedCount > 0 && page.canChange)
            deleteDialog.open()
    }

    function askRename() {
        if (PhoneFiles.selectedName !== "" && page.canChange)
            renameDialog.start(PhoneFiles.selectedName)
    }

    function askFolder() {
        if (page.canChange)
            folderDialog.start(qsTr("New folder"))
    }

    function chooseDownload() {
        if (PhoneFiles.selectedCount > 0 && PhoneFiles.online)
            downloadPicker.open()
    }

    function openRow(row: int) {
        page.lastRow = -1
        PhoneFiles.enter(row)
    }

    function dragOut(row: int) {
        if (!PhoneFiles.isSelected(row))
            PhoneFiles.select(row, Qt.NoModifier)
        const problem = PhoneFiles.prepareDrag()
        if (problem === "")
            return
        dragNotice.actionText = PhoneFiles.selectedCount > 0 && PhoneFiles.online ? qsTr("Download to…") : ""
        dragNotice.show(problem, "warn")
    }

    function moveSelection(step: int, modifiers: int) {
        if (list.count === 0)
            return
        const row = Math.max(0, Math.min(list.count - 1, page.lastRow < 0 ? 0 : page.lastRow + step))
        PhoneFiles.select(row, modifiers)
        page.lastRow = row
        list.positionViewAtIndex(row, ListView.Contain)
    }

    function transferText(status: string, done: real, total: real, message: string): string {
        switch (status) {
        case "queued":
            return qsTr("Waiting")
        case "running":
            return total > 0 ? qsTr("%1 of %2").arg(PhoneFiles.sizeText(done)).arg(PhoneFiles.sizeText(total))
                             : qsTr("Starting…")
        case "done":
            return qsTr("Done")
        case "cancelled":
            return qsTr("Cancelled")
        default:
            return message !== "" ? message : qsTr("Failed")
        }
    }

    onSerialChanged: PhoneFiles.open(serial)
    Component.onCompleted: PhoneFiles.open(serial)
    Component.onDestruction: PhoneFiles.close()

    Connections {
        target: PhoneFiles

        function onNotice(text: string, tone: string) {
            notice.show(text, tone)
        }

        function onFolderChanged() {
            page.lastRow = -1
        }
    }

    Row {
        id: crumbs

        x: Theme.size.panelPaddingH - Theme.space.md
        y: Theme.size.panelPaddingV - Theme.space.sm
        spacing: Theme.space.xs

        Button {
            objectName: "filesBack"
            anchors.verticalCenter: parent.verticalCenter
            small: true
            tone: "ghost"
            iconName: "arrow-left"
            text: qsTr("Devices")
            onClicked: page.back()
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"
            size: Theme.size.iconSmall
            color: Theme.colors.textTertiary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: PhoneFiles.phoneName
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.label.size
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"
            size: Theme.size.iconSmall
            color: Theme.colors.textTertiary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: qsTr("Files")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: Font.Medium
        }
    }

    Row {
        id: header

        x: Theme.size.panelPaddingH
        anchors.top: crumbs.bottom
        anchors.topMargin: Theme.space.sm
        spacing: Theme.space.lg

        Text {
            id: title

            textFormat: Text.PlainText
            text: qsTr("Files")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.display.size
            font.weight: Theme.type.display.weight
        }

        Text {
            objectName: "filesSummary"
            anchors.baseline: title.baseline
            visible: PhoneFiles.online
            textFormat: Text.PlainText
            text: PhoneFiles.selectedCount > 0
                  ? qsTr("%n selected", "", PhoneFiles.selectedCount)
                    + (PhoneFiles.selectedBytes > 0 ? " · " + PhoneFiles.sizeText(PhoneFiles.selectedBytes) : "")
                  : qsTr("%n item(s)", "", PhoneFiles.itemCount)
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.body.size
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.verticalCenter: header.verticalCenter
        spacing: Theme.space.sm

        Button {
            objectName: "filesNewFolder"
            anchors.verticalCenter: parent.verticalCenter
            iconName: "plus"
            text: qsTr("New folder")
            enabled: page.canChange
            onClicked: page.askFolder()
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.size.stroke

            Button {
                objectName: "filesUpload"
                tone: "primary"
                joined: "start"
                iconName: "share"
                text: qsTr("Upload…")
                enabled: PhoneFiles.online
                onClicked: uploadPicker.open()
            }

            Button {
                id: uploadMore

                objectName: "filesUploadMenu"
                tone: "primary"
                joined: "end"
                iconName: "chevron-down"
                description: qsTr("More ways to upload")
                enabled: PhoneFiles.online
                onClicked: uploadMenu.open()

                Menu {
                    id: uploadMenu

                    objectName: "uploadMenu"
                    x: uploadMore.width - width
                    y: uploadMore.height + Theme.space.xs

                    MenuItem {
                        objectName: "uploadFiles"
                        iconName: "doc-page"
                        text: qsTr("Upload files…")
                        description: qsTr("Copies them into this folder on the phone")
                        onTriggered: uploadPicker.open()
                    }
                    MenuItem {
                        objectName: "uploadFolder"
                        iconName: "folder-open"
                        text: qsTr("Upload a folder…")
                        description: qsTr("Copies the folder with everything in it")
                        onTriggered: uploadFolderPicker.open()
                    }
                }
            }
        }
    }

    Item {
        id: pathBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: Theme.size.panelPaddingH - Theme.space.sm
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: Theme.space.lg
        height: Theme.size.control

        Button {
            id: upButton

            objectName: "filesUp"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            small: true
            tone: "ghost"
            iconName: "go-up"
            toolTip: qsTr("Up one folder")
            Accessible.name: qsTr("Up one folder")
            enabled: PhoneFiles.canGoUp
            onClicked: PhoneFiles.up()
        }

        ListView {
            id: crumbList

            objectName: "filesCrumbs"
            anchors.left: upButton.right
            anchors.right: actions.left
            anchors.leftMargin: Theme.space.xs
            anchors.rightMargin: Theme.space.md
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.size.controlSmall
            orientation: ListView.Horizontal
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: PhoneFiles.crumbs
            onCountChanged: Qt.callLater(crumbList.positionViewAtEnd)
            onWidthChanged: Qt.callLater(crumbList.positionViewAtEnd)

            delegate: Row {
                id: crumb

                required property var modelData
                required property int index
                readonly property bool last: index === crumbList.count - 1
                readonly property string label: modelData.name !== "" ? modelData.name : qsTr("Phone storage")

                height: crumbList.height
                spacing: Theme.space.xxs

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: crumb.index > 0
                    name: "chevron-right"
                    size: Theme.size.iconSmall
                    color: Theme.colors.textTertiary
                }

                Button {
                    objectName: "filesCrumb"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !crumb.last
                    small: true
                    tone: "ghost"
                    text: crumb.label
                    onClicked: PhoneFiles.openFolder(crumb.modelData.path)
                }

                Text {
                    objectName: "filesCrumbHere"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: crumb.last
                    leftPadding: Theme.space.sm
                    rightPadding: Theme.space.sm
                    textFormat: Text.PlainText
                    text: crumb.label
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.Medium
                }
            }
        }

        Row {
            id: actions

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.xxs

            Button {
                objectName: "filesDownload"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                iconName: "import"
                text: qsTr("Download to…")
                enabled: PhoneFiles.online && PhoneFiles.selectedCount > 0
                onClicked: page.chooseDownload()
            }

            Button {
                objectName: "filesRename"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                iconName: "pencil"
                text: qsTr("Rename…")
                enabled: page.canChange && PhoneFiles.selectedName !== ""
                onClicked: page.askRename()
            }

            Button {
                objectName: "filesDelete"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                iconName: "trash-2"
                text: qsTr("Delete…")
                enabled: page.canChange && PhoneFiles.selectedCount > 0
                onClicked: page.askDelete()
            }

            Button {
                objectName: "filesRefresh"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                iconName: "refresh-cw"
                toolTip: qsTr("Refresh")
                Accessible.name: qsTr("Refresh")
                enabled: PhoneFiles.online
                onClicked: PhoneFiles.refresh()
            }
        }
    }

    InlineAlert {
        id: alert

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: pathBar.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: Theme.space.sm
        visible: PhoneFiles.problem !== ""
        tone: "warn"
        title: PhoneFiles.problem
        actionText: PhoneFiles.online ? qsTr("Try again") : ""
        onActionTriggered: PhoneFiles.refresh()
    }

    Item {
        id: columns

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: alert.visible ? alert.bottom : pathBar.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: Theme.space.md
        height: Theme.type.caption.lineHeight + Theme.space.sm

        Text {
            x: page.nameIndent
            textFormat: Text.PlainText
            text: qsTr("Name")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Text {
            x: parent.width - page.sizeColumn - page.dateColumn - Theme.space.md
            width: page.dateColumn
            textFormat: Text.PlainText
            text: qsTr("Modified")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Text {
            x: parent.width - page.sizeColumn - Theme.space.md
            width: page.sizeColumn
            horizontalAlignment: Text.AlignRight
            textFormat: Text.PlainText
            text: qsTr("Size")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }
    }

    ListView {
        id: list

        objectName: "filesList"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: columns.bottom
        anchors.bottom: transfersPanel.visible ? transfersPanel.top : parent.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.topMargin: Theme.space.xs
        anchors.bottomMargin: transfersPanel.visible ? Theme.space.md : Theme.size.panelPaddingV
        clip: true
        focus: true
        boundsBehavior: Flickable.StopAtBounds
        model: PhoneFiles.entries
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Files on the phone")

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Delete) {
                page.askDelete()
            } else if (event.key === Qt.Key_F2) {
                page.askRename()
            } else if (event.key === Qt.Key_Backspace) {
                PhoneFiles.up()
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (page.lastRow >= 0 && PhoneFiles.isSelected(page.lastRow))
                    page.openRow(page.lastRow)
            } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                PhoneFiles.selectAll()
            } else if (event.key === Qt.Key_Escape) {
                PhoneFiles.clearSelection()
            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                page.moveSelection(event.key === Qt.Key_Up ? -1 : 1, event.modifiers & Qt.ShiftModifier)
            } else {
                return
            }
            event.accepted = true
        }

        TapHandler {
            onTapped: {
                PhoneFiles.clearSelection()
                list.forceActiveFocus()
            }
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                PhoneFiles.clearSelection()
                folderMenu.popup()
            }
        }

        delegate: Item {
            id: row

            required property int index
            required property string name
            required property bool directory
            required property string sizeText
            required property var modified
            required property bool selected

            objectName: "fileRow"
            width: ListView.view.width
            height: Theme.size.listItem
            Accessible.role: Accessible.ListItem
            Accessible.name: name
            Accessible.selected: selected

            Rectangle {
                anchors.fill: parent
                radius: Theme.radius.sm
                color: row.selected ? Theme.colors.stateSelected
                                    : rowMouse.containsMouse ? Theme.colors.stateHover : "transparent"
                border.width: row.selected ? Theme.size.stroke : 0
                border.color: Theme.colors.strokeDefault
            }

            Icon {
                x: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                name: row.directory ? "folder-open" : "doc-page"
                size: Theme.size.icon
                color: row.directory ? Theme.colors.accentText : Theme.colors.textSecondary
            }

            Text {
                x: page.nameIndent
                width: Math.max(0, parent.width - page.nameIndent - page.dateColumn - page.sizeColumn - 2 * Theme.space.md)
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: row.name
                elide: Text.ElideMiddle
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
            }

            Text {
                x: parent.width - page.sizeColumn - page.dateColumn - Theme.space.md
                width: page.dateColumn
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: row.modified ? row.modified.toLocaleString(Qt.locale(), Locale.ShortFormat) : ""
                elide: Text.ElideRight
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Text {
                x: parent.width - page.sizeColumn - Theme.space.md
                width: page.sizeColumn
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                textFormat: Text.PlainText
                text: row.sizeText
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                font.features: ({ "tnum": 1 })
            }

            MouseArea {
                id: rowMouse

                property point origin
                property bool dragging: false

                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                preventStealing: true
                cursorShape: rowMouse.dragging && rowMouse.pressed ? Qt.DragCopyCursor : Qt.ArrowCursor
                onPressed: mouse => {
                    rowMouse.origin = Qt.point(mouse.x, mouse.y)
                    rowMouse.dragging = false
                    list.forceActiveFocus()
                }
                onPositionChanged: mouse => {
                    if (!(rowMouse.pressedButtons & Qt.LeftButton) || rowMouse.dragging)
                        return
                    if (Math.abs(mouse.x - rowMouse.origin.x) + Math.abs(mouse.y - rowMouse.origin.y)
                            < Application.styleHints.startDragDistance)
                        return
                    rowMouse.dragging = true
                    page.lastRow = row.index
                    page.dragOut(row.index)
                }
                onClicked: mouse => {
                    if (rowMouse.dragging)
                        return
                    page.lastRow = row.index
                    if (mouse.button === Qt.RightButton) {
                        if (!row.selected)
                            PhoneFiles.select(row.index, Qt.NoModifier)
                        fileMenu.popup()
                        return
                    }
                    PhoneFiles.select(row.index, mouse.modifiers)
                }
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton && row.directory)
                        page.openRow(row.index)
                }
            }
        }
    }

    Text {
        anchors.centerIn: list
        width: Math.min(implicitWidth, list.width)
        visible: list.count === 0 && PhoneFiles.problem === ""
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        text: PhoneFiles.loading ? qsTr("Reading the folder…")
                                 : qsTr("This folder is empty. Drop files here to upload them.")
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.body.size
        wrapMode: Text.Wrap
    }

    DropArea {
        id: fileDrop

        objectName: "filesDropArea"
        anchors.fill: list
        enabled: PhoneFiles.online && !chipMouse.drag.active
        onEntered: drag => {
            let local = false
            for (let i = 0; i < drag.urls.length; ++i) {
                if (drag.urls[i].toString().startsWith("file:"))
                    local = true
            }
            if (local)
                drag.accept(Qt.CopyAction)
            else
                drag.accepted = false
        }
        onDropped: drop => {
            drop.accept(Qt.CopyAction)
            if (PhoneFiles.upload(drop.urls) > 0)
                page.transfersOpen = true
        }

        Rectangle {
            anchors.fill: parent
            visible: fileDrop.containsDrag
            radius: Theme.radius.lg
            color: Theme.colors.bgFrosted
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accentLine

            Column {
                anchors.centerIn: parent
                spacing: Theme.space.md

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "share"
                    size: Theme.size.iconHero
                    color: Theme.colors.accentText
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    textFormat: Text.PlainText
                    text: qsTr("Drop to upload to %1").arg(page.folderName)
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.subtitle.size
                    font.weight: Theme.type.subtitle.weight
                }
            }
        }
    }

    Rectangle {
        id: dragChip

        objectName: "dragChip"
        anchors.horizontalCenter: list.horizontalCenter
        anchors.bottom: list.bottom
        anchors.bottomMargin: Theme.space.md
        visible: PhoneFiles.dragPhase !== 0
        width: chipRow.implicitWidth + Theme.space.md + Theme.space.xs
        height: Theme.size.control + Theme.space.xs
        radius: height / 2
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: PhoneFiles.dragPhase === 2 ? Theme.colors.accentLine : Theme.colors.strokeDefault

        Drag.active: chipMouse.drag.active
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction
        Drag.mimeData: ({ "text/uri-list": PhoneFiles.dragUrls })
        Drag.onDragFinished: dropAction => {
            if (dropAction !== Qt.IgnoreAction)
                PhoneFiles.finishDrag()
        }

        MouseArea {
            id: chipMouse

            anchors.fill: parent
            enabled: PhoneFiles.dragPhase === 2
            drag.target: dragProxy
            cursorShape: Qt.OpenHandCursor
        }

        Row {
            id: chipRow

            x: Theme.space.md
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: PhoneFiles.dragPhase === 2 ? "move" : "import"
                size: Theme.size.iconSmall
                color: Theme.colors.accentText
            }

            Text {
                objectName: "dragChipText"
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: PhoneFiles.dragPhase === 2
                      ? qsTr("Drag %n file(s) from here to a folder", "", PhoneFiles.dragCount)
                      : qsTr("Getting %n file(s) ready to drag… %1%", "", PhoneFiles.dragCount)
                        .arg(Math.round(100 * PhoneFiles.dragDone / Math.max(1, PhoneFiles.dragTotal)))
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
            }

            Button {
                objectName: "dragCancel"
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                iconName: "x"
                description: qsTr("Cancel")
                toolTip: qsTr("Cancel")
                onClicked: PhoneFiles.cancelDrag()
            }
        }
    }

    Item {
        id: dragProxy
    }

    Rectangle {
        id: transfersPanel

        objectName: "filesTransfers"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.size.panelPaddingH
        anchors.bottomMargin: Theme.size.panelPaddingV
        visible: PhoneFiles.transferCount > 0
        height: transfersHeader.height + (page.transfersOpen ? transferList.height + Theme.space.xs : 0)
        radius: Theme.radius.lg
        color: Theme.colors.bgOverlay
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle
        clip: true

        Item {
            id: transfersHeader

            width: parent.width
            height: Theme.size.control + Theme.space.xs

            Button {
                objectName: "filesTransfersToggle"
                x: Theme.space.xs
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                iconName: page.transfersOpen ? "chevron-down" : "chevron-right"
                text: PhoneFiles.activeTransfers > 0 ? qsTr("Transfers · %n running", "", PhoneFiles.activeTransfers)
                                                     : qsTr("Transfers")
                onClicked: page.transfersOpen = !page.transfersOpen
            }

            Button {
                objectName: "filesClearTransfers"
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.xs
                anchors.verticalCenter: parent.verticalCenter
                visible: PhoneFiles.transferCount > PhoneFiles.activeTransfers
                small: true
                tone: "ghost"
                text: qsTr("Clear finished")
                onClicked: PhoneFiles.clearTransfers()
            }
        }

        ListView {
            id: transferList

            objectName: "transferList"
            anchors.top: transfersHeader.bottom
            width: parent.width
            height: Math.min(count, 4) * page.transferRow
            visible: page.transfersOpen
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: PhoneFiles.transfers

            delegate: Item {
                id: transfer

                required property real jobId
                required property string direction
                required property string name
                required property real done
                required property real total
                required property string status
                required property string message
                required property real progress
                required property bool active

                objectName: "transferRow"
                width: ListView.view.width
                height: page.transferRow
                Accessible.role: Accessible.ListItem
                Accessible.name: name

                Icon {
                    id: transferIcon

                    x: Theme.space.md
                    anchors.verticalCenter: transferName.verticalCenter
                    name: transfer.direction === "push" ? "share" : "import"
                    size: Theme.size.iconSmall
                    color: transfer.status === "failed" ? Theme.colors.dangerText : Theme.colors.textSecondary
                }

                Text {
                    id: transferName

                    x: transferIcon.x + transferIcon.width + Theme.space.sm
                    y: Theme.space.sm
                    width: Math.max(0, transferState.x - x - Theme.space.md)
                    textFormat: Text.PlainText
                    text: transfer.name
                    elide: Text.ElideMiddle
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                }

                Text {
                    id: transferState

                    x: cancelTransfer.x - width - Theme.space.sm
                    anchors.verticalCenter: transferName.verticalCenter
                    width: Math.min(implicitWidth, transfer.width / 2)
                    textFormat: Text.PlainText
                    text: page.transferText(transfer.status, transfer.done, transfer.total, transfer.message)
                    elide: Text.ElideRight
                    color: transfer.status === "failed" ? Theme.colors.dangerText : Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                Button {
                    id: cancelTransfer

                    objectName: "cancelTransfer"
                    x: transfer.width - width - Theme.space.xs
                    anchors.verticalCenter: transferName.verticalCenter
                    visible: transfer.active
                    small: true
                    tone: "ghost"
                    iconName: "x"
                    description: qsTr("Cancel the transfer")
                    toolTip: qsTr("Cancel")
                    onClicked: PhoneFiles.cancelTransfer(transfer.jobId)
                }

                Rectangle {
                    x: transferName.x
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.space.sm
                    width: transfer.width - x - Theme.space.md
                    height: Theme.space.xs
                    radius: height / 2
                    visible: transfer.active
                    color: Theme.colors.strokeSubtle

                    Rectangle {
                        width: parent.width * transfer.progress
                        height: parent.height
                        radius: height / 2
                        color: Theme.colors.accent
                    }
                }
            }
        }
    }

    Menu {
        id: fileMenu

        objectName: "fileMenu"

        MenuItem {
            objectName: "fileMenuOpen"
            visible: page.lastRow >= 0 && PhoneFiles.selectedCount === 1 && PhoneFiles.selectionHasFolders
            height: visible ? implicitHeight : 0
            iconName: "folder-open"
            text: qsTr("Open")
            onTriggered: page.openRow(page.lastRow)
        }
        MenuItem {
            objectName: "fileMenuDownload"
            iconName: "import"
            text: qsTr("Download to…")
            enabled: PhoneFiles.online
            onTriggered: page.chooseDownload()
        }
        MenuItem {
            objectName: "fileMenuRename"
            iconName: "pencil"
            text: qsTr("Rename…")
            enabled: page.canChange && PhoneFiles.selectedName !== ""
            onTriggered: page.askRename()
        }
        MenuSeparator {
        }
        MenuItem {
            objectName: "fileMenuDelete"
            iconName: "trash-2"
            text: qsTr("Delete…")
            danger: true
            enabled: page.canChange
            onTriggered: page.askDelete()
        }
    }

    Menu {
        id: folderMenu

        objectName: "folderMenu"

        MenuItem {
            objectName: "folderMenuNew"
            iconName: "plus"
            text: qsTr("New folder…")
            enabled: page.canChange
            onTriggered: page.askFolder()
        }
        MenuItem {
            iconName: "share"
            text: qsTr("Upload files…")
            enabled: PhoneFiles.online
            onTriggered: uploadPicker.open()
        }
        MenuItem {
            iconName: "folder-open"
            text: qsTr("Upload a folder…")
            enabled: PhoneFiles.online
            onTriggered: uploadFolderPicker.open()
        }
        MenuSeparator {
        }
        MenuItem {
            iconName: "refresh-cw"
            text: qsTr("Refresh")
            enabled: PhoneFiles.online
            onTriggered: PhoneFiles.refresh()
        }
    }

    component NameDialog: Dialog {
        id: nameDialog

        property string current
        property string acceptLabel
        readonly property string problem: nameField.text.trim() === ""
                                          ? "" : PhoneFiles.nameProblem(nameField.text, nameDialog.current)
        readonly property bool ready: nameField.text.trim() !== "" && problem === "" && PhoneFiles.online

        signal chosen(string name)

        function start(name: string) {
            nameDialog.current = name
            nameField.text = name
            nameDialog.open()
            const dot = name.lastIndexOf(".")
            nameField.select(0, dot > 0 ? dot : name.length)
            nameField.forceActiveFocus()
        }

        function submit() {
            if (nameDialog.ready)
                nameDialog.chosen(nameField.text)
        }

        footer: Item {
            implicitHeight: nameButtons.implicitHeight + 2 * Theme.space.xl

            Row {
                id: nameButtons

                anchors.right: parent.right
                anchors.rightMargin: Theme.space.xxl
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.sm

                Button {
                    objectName: "reject"
                    text: qsTr("Cancel")
                    onClicked: nameDialog.reject()
                }

                Button {
                    objectName: "accept"
                    tone: "primary"
                    text: nameDialog.acceptLabel
                    enabled: nameDialog.ready
                    onClicked: nameDialog.submit()
                }
            }
        }

        TextField {
            id: nameField

            objectName: "nameField"
            width: nameDialog.availableWidth
            maximumLength: 255
            selectByMouse: true
            errorText: nameDialog.problem
            Accessible.name: qsTr("Name")
            onAccepted: nameDialog.submit()
        }
    }

    NameDialog {
        id: folderDialog

        objectName: "newFolderDialog"
        title: qsTr("New folder")
        acceptLabel: qsTr("Create")
        onChosen: name => {
            if (PhoneFiles.makeFolder(name))
                folderDialog.close()
        }
    }

    NameDialog {
        id: renameDialog

        objectName: "renameDialog"
        title: qsTr("Rename")
        acceptLabel: qsTr("Rename")
        onChosen: name => {
            if (PhoneFiles.rename(renameDialog.current, name))
                renameDialog.close()
        }
    }

    Dialog {
        id: deleteDialog

        objectName: "deleteDialog"
        title: qsTr("Delete %n item(s)?", "", PhoneFiles.selectedCount)
        acceptText: qsTr("Delete")
        danger: true

        Text {
            width: deleteDialog.availableWidth
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: PhoneFiles.selectedCount === 1 && PhoneFiles.selectedName !== ""
                  ? qsTr("“%1” will be deleted from the phone. This cannot be undone.").arg(PhoneFiles.selectedName)
                  : qsTr("They will be deleted from the phone, folders with everything in them. This cannot be undone.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }

        onAccepted: PhoneFiles.removeSelected()
    }

    FileDialog {
        id: uploadPicker

        title: qsTr("Upload files to the phone")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            if (PhoneFiles.upload(uploadPicker.selectedFiles) > 0)
                page.transfersOpen = true
        }
    }

    FolderDialog {
        id: uploadFolderPicker

        title: qsTr("Upload a folder to the phone")
        onAccepted: {
            if (PhoneFiles.upload([uploadFolderPicker.selectedFolder]) > 0)
                page.transfersOpen = true
        }
    }

    FolderDialog {
        id: downloadPicker

        title: qsTr("Download to")
        currentFolder: PhoneFiles.downloadsFolder
        onAccepted: {
            if (PhoneFiles.downloadSelected(downloadPicker.selectedFolder) > 0)
                page.transfersOpen = true
        }
    }

    Toast {
        id: notice
    }

    Toast {
        id: dragNotice

        objectName: "dragNotice"
        onActionTriggered: page.chooseDownload()
    }
}
