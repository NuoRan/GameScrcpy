pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: bar

    readonly property DeviceBatch batch: DeviceManager.batch
    readonly property bool shown: batch.selecting || batch.jobs.length > 0
    property bool expanded: true

    function launchApp(packageName: string, label: string) {
        bar.batch.launchApp(packageName, label)
        appPicker.close()
    }

    implicitHeight: shown ? column.implicitHeight : 0

    component DropButton: Button {
        id: drop

        contentItem: Item {
            implicitWidth: dropRow.implicitWidth
            implicitHeight: dropRow.implicitHeight

            Row {
                id: dropRow

                anchors.centerIn: parent
                spacing: drop.spacing

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: drop.iconName !== ""
                    name: drop.iconName
                    size: Theme.size.iconSmall
                    color: drop.foreground
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: drop.text
                    font: drop.font
                    color: drop.foreground
                }

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-down"
                    size: Theme.size.iconSmall
                    color: drop.foreground
                }
            }
        }
    }

    CommandState {
        id: tasksPageCommand

        command: "hub.tasks"
    }

    Column {
        id: column

        width: bar.width
        spacing: Theme.space.sm

        Rectangle {
            objectName: "batchActions"
            width: parent.width
            height: visible ? Theme.size.toolbar : 0
            visible: bar.batch.selecting
            radius: Theme.radius.xl
            color: Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space.lg
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.sm

                Text {
                    objectName: "batchCount"
                    anchors.verticalCenter: parent.verticalCenter
                    rightPadding: Theme.space.sm
                    textFormat: Text.PlainText
                    text: qsTr("%n selected", "", bar.batch.count)
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.DemiBold
                }

                DropButton {
                    id: taskButton

                    objectName: "batchStartTask"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    iconName: "play"
                    text: qsTr("Start task")
                    enabled: bar.batch.canStartTask
                    onClicked: taskMenu.open()

                    Menu {
                        id: taskMenu

                        objectName: "batchTaskMenu"
                        y: -height - Theme.space.xs

                        Instantiator {
                            model: TaskLibrary.tasks

                            delegate: MenuItem {
                                id: taskItem

                                required property var modelData

                                objectName: "batchTaskItem"
                                iconName: "play"
                                text: taskItem.modelData.name
                                onTriggered: bar.batch.startTask(taskItem.modelData.id, taskItem.modelData.name)
                            }

                            onObjectAdded: (index, object) => taskMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => taskMenu.removeItem(object)
                        }

                        MenuItem {
                            visible: TaskLibrary.tasks.length === 0
                            height: visible ? implicitHeight : 0
                            enabled: false
                            text: qsTr("No tasks yet")
                        }

                        MenuSeparator {}

                        MenuItem {
                            iconName: "bot"
                            text: qsTr("Open the Tasks page")
                            onTriggered: tasksPageCommand.trigger()
                        }
                    }
                }

                DropButton {
                    id: appButton

                    objectName: "batchLaunchApp"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    iconName: "layout-grid"
                    text: qsTr("Launch app")
                    enabled: bar.batch.canLaunchApp
                    onClicked: appPicker.open()

                    T.Popup {
                        id: appPicker

                        objectName: "batchAppPicker"
                        y: -height - Theme.space.xs
                        implicitWidth: implicitContentWidth + leftPadding + rightPadding
                        implicitHeight: implicitContentHeight + topPadding + bottomPadding
                        margins: Theme.space.sm
                        padding: Theme.space.md
                        focus: true
                        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

                        onOpened: {
                            AppCatalog.filter = 0
                            AppCatalog.query = ""
                            appSearch.text = ""
                            if (bar.batch.appSource !== "")
                                AppCatalog.open(bar.batch.appSource)
                            appSearch.forceActiveFocus()
                        }
                        onClosed: {
                            AppCatalog.query = ""
                            AppCatalog.close()
                        }

                        background: PopupSurface {}

                        contentItem: Column {
                            spacing: Theme.space.sm

                            Text {
                                width: appSearch.width
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                text: bar.batch.appSource === ""
                                      ? qsTr("None of the selected phones can list their apps. Enter a package name.")
                                      : qsTr("Apps on %1").arg(AppCatalog.phoneName)
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }

                            SearchField {
                                id: appSearch

                                objectName: "batchAppSearch"
                                width: Theme.size.fieldWidth + Theme.space.x5l
                                placeholderText: qsTr("Search apps or enter a package name")
                                onTextChanged: AppCatalog.query = text
                                Keys.onReturnPressed: {
                                    if (appSearch.text.trim() !== "")
                                        bar.launchApp(appSearch.text.trim(), "")
                                }
                            }

                            Text {
                                width: appSearch.width
                                visible: AppCatalog.loading || AppCatalog.problem !== ""
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                text: AppCatalog.problem !== "" ? AppCatalog.problem : qsTr("Reading the apps on the phone…")
                                color: Theme.colors.textSecondary
                                font.pixelSize: Theme.type.caption.size
                            }

                            ListView {
                                objectName: "batchAppList"
                                width: appSearch.width
                                height: Math.min(contentHeight, Theme.size.paletteListMax)
                                visible: count > 0
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                model: AppCatalog.apps

                                delegate: Item {
                                    id: appRow

                                    required property string packageName
                                    required property string label
                                    required property string iconUrl

                                    objectName: "batchAppRow"
                                    width: ListView.view.width
                                    height: Theme.size.paletteRow
                                    Accessible.role: Accessible.ListItem
                                    Accessible.name: appRow.label

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: Theme.radius.sm
                                        color: appHover.hovered ? Theme.colors.stateHover : "transparent"
                                    }

                                    Image {
                                        id: appIcon

                                        x: Theme.space.sm
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.size.iconLarge + Theme.space.xs
                                        height: width
                                        source: appRow.iconUrl
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        smooth: true
                                        mipmap: true
                                    }

                                    Column {
                                        anchors.left: appIcon.right
                                        anchors.leftMargin: Theme.space.sm
                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.space.sm
                                        anchors.verticalCenter: parent.verticalCenter

                                        Text {
                                            width: parent.width
                                            textFormat: Text.PlainText
                                            text: appRow.label
                                            elide: Text.ElideRight
                                            color: Theme.colors.textPrimary
                                            font.pixelSize: Theme.type.label.size
                                        }

                                        Text {
                                            width: parent.width
                                            textFormat: Text.PlainText
                                            text: appRow.packageName
                                            elide: Text.ElideMiddle
                                            color: Theme.colors.textTertiary
                                            font.pixelSize: Theme.type.caption.size
                                        }
                                    }

                                    HoverHandler {
                                        id: appHover
                                    }

                                    TapHandler {
                                        onTapped: bar.launchApp(appRow.packageName, appRow.label)
                                    }
                                }
                            }

                            Button {
                                objectName: "batchLaunchTyped"
                                width: appSearch.width
                                visible: appSearch.text.trim() !== ""
                                small: true
                                tone: "primary"
                                iconName: "play"
                                text: qsTr("Launch “%1” on each phone").arg(appSearch.text.trim())
                                onClicked: bar.launchApp(appSearch.text.trim(), "")
                            }
                        }
                    }
                }

                Button {
                    objectName: "batchInstall"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    iconName: "import"
                    text: qsTr("Install APK…")
                    enabled: bar.batch.canInstall
                    onClicked: apkDialog.open()
                }

                Button {
                    objectName: "batchSendFiles"
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    iconName: "share"
                    text: qsTr("Send files…")
                    enabled: bar.batch.canPushFiles
                    onClicked: filesDialog.open()
                }
            }

            Button {
                objectName: "batchClear"
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                small: true
                tone: "ghost"
                iconName: "x"
                text: qsTr("Clear selection")
                onClicked: bar.batch.clear()
            }
        }

        Rectangle {
            objectName: "batchProgress"
            width: parent.width
            height: visible ? progressColumn.implicitHeight + 2 * Theme.space.sm : 0
            visible: bar.batch.jobs.length > 0
            radius: Theme.radius.xl
            color: Theme.colors.bgOverlay
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle

            Column {
                id: progressColumn

                x: Theme.space.md
                y: Theme.space.sm
                width: parent.width - 2 * Theme.space.md
                spacing: Theme.space.xs

                Item {
                    width: parent.width
                    height: Theme.size.controlSmall

                    Button {
                        objectName: "batchFold"
                        anchors.left: parent.left
                        anchors.leftMargin: -Theme.space.sm
                        anchors.verticalCenter: parent.verticalCenter
                        small: true
                        tone: "ghost"
                        iconName: bar.expanded ? "chevron-down" : "chevron-right"
                        text: bar.batch.running > 0 ? qsTr("Progress · %n running", "", bar.batch.running)
                                                    : qsTr("Progress · all finished")
                        onClicked: bar.expanded = !bar.expanded
                    }

                    Button {
                        objectName: "batchClearFinished"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        small: true
                        tone: "ghost"
                        text: qsTr("Clear finished")
                        enabled: bar.batch.jobs.length > bar.batch.running
                        onClicked: bar.batch.clearFinished()
                    }
                }

                ListView {
                    objectName: "batchJobs"
                    width: parent.width
                    height: bar.expanded ? Math.min(contentHeight, Theme.size.batchJobs) : 0
                    visible: bar.expanded
                    clip: true
                    spacing: Theme.space.xxs
                    boundsBehavior: Flickable.StopAtBounds
                    model: bar.batch.jobs

                    delegate: Item {
                        id: job

                        required property var modelData
                        readonly property real progress: job.modelData.progress

                        objectName: "batchJob"
                        width: ListView.view.width
                        height: Theme.size.listItem

                        Text {
                            id: jobPhone

                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, job.width / 3)
                            textFormat: Text.PlainText
                            text: job.modelData.phone
                            elide: Text.ElideRight
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Font.Medium
                        }

                        Text {
                            anchors.left: jobPhone.right
                            anchors.leftMargin: Theme.space.md
                            anchors.right: jobStatus.left
                            anchors.rightMargin: Theme.space.md
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: job.modelData.name
                            elide: Text.ElideMiddle
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.label.size
                        }

                        Text {
                            id: jobStatus

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, job.width / 3)
                            horizontalAlignment: Text.AlignRight
                            textFormat: Text.PlainText
                            text: job.modelData.status
                            elide: Text.ElideRight
                            color: job.modelData.failed ? Theme.colors.dangerText
                                 : job.modelData.state === "done" ? Theme.colors.successText
                                                                  : Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: Theme.space.xs
                            radius: height / 2
                            visible: job.progress >= 0
                            color: Theme.colors.strokeSubtle

                            Rectangle {
                                width: parent.width * Math.max(0, job.progress)
                                height: parent.height
                                radius: height / 2
                                color: Theme.colors.accent
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: apkDialog

        title: qsTr("Choose an APK to install on the selected phones")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Android apps (*.apk)")]
        onAccepted: bar.batch.installApk(apkDialog.selectedFile)
    }

    FileDialog {
        id: filesDialog

        title: qsTr("Choose files to send to the Download folder of the selected phones")
        fileMode: FileDialog.OpenFiles
        onAccepted: bar.batch.pushFiles(filesDialog.selectedFiles)
    }
}
