pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: panel

    required property ScriptWorkbench bench

    signal jumped(string path, int line, int column)

    function colorOf(level: string): color {
        if (level === "error")
            return Theme.colors.dangerText
        if (level === "warning")
            return Theme.colors.warningText
        return Theme.colors.textPrimary
    }

    Tabs {
        id: tabs

        objectName: "panelTabs"
        x: Theme.space.lg
        y: Theme.space.xs
        model: [qsTr("Console"), panel.bench.problems.length > 0
                                 ? { text: qsTr("Problems"), count: panel.bench.problems.length } : qsTr("Problems"),
                (panel.bench.references.count ?? 0) > 0
                ? { text: qsTr("References"), count: panel.bench.references.count } : qsTr("References")]
    }

    Connections {
        target: panel.bench

        function onReferencesChanged() {
            tabs.currentIndex = 2
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.space.sm
        anchors.verticalCenter: tabs.verticalCenter
        spacing: Theme.space.xs

        Text {
            objectName: "checking"
            anchors.verticalCenter: parent.verticalCenter
            visible: tabs.currentIndex === 1 && panel.bench.checking
            textFormat: Text.PlainText
            text: qsTr("Checking…")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        ToolbarButton {
            objectName: "clearConsole"
            anchors.verticalCenter: parent.verticalCenter
            visible: tabs.currentIndex === 0
            iconName: "trash-2"
            text: qsTr("Clear the console")
            onClicked: panel.bench.clearConsole()
        }
    }

    Rectangle {
        id: tabsLine

        y: tabs.y + tabs.height
        width: parent.width
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    ListView {
        id: consoleView

        property bool following: true

        objectName: "console"
        anchors.fill: parent
        anchors.topMargin: tabsLine.y + tabsLine.height
        visible: tabs.currentIndex === 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: panel.bench.console
        onContentYChanged: consoleView.following = consoleView.atYEnd
        onCountChanged: {
            if (consoleView.following)
                Qt.callLater(consoleView.positionViewAtEnd)
        }

        delegate: Item {
            id: entry

            required property string time
            required property string text
            required property string level
            required property string file
            required property int line
            required property string place

            objectName: "consoleLine"
            width: ListView.view.width
            height: Math.max(Theme.size.listItem, message.implicitHeight + 2 * Theme.space.xs)

            Rectangle {
                anchors.fill: parent
                visible: entry.level !== "info"
                color: entry.level === "error" ? Theme.colors.dangerSubtle : Theme.colors.warningSubtle
            }

            Text {
                id: stamp

                x: Theme.space.lg
                y: Theme.space.xs
                height: Theme.size.listItem - 2 * Theme.space.xs
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                text: entry.time
                color: Theme.colors.textTertiary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size
            }

            Text {
                id: message

                anchors.left: stamp.right
                anchors.leftMargin: Theme.space.md
                anchors.right: source.left
                anchors.rightMargin: Theme.space.md
                y: Theme.space.xs
                topPadding: (Theme.size.listItem - 2 * Theme.space.xs - Theme.type.label.lineHeight) / 2
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: entry.text
                color: panel.colorOf(entry.level)
                font.pixelSize: Theme.type.label.size
            }

            Text {
                id: source

                objectName: "consolePlace"
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.lg
                y: Theme.space.xs
                height: Theme.size.listItem - 2 * Theme.space.xs
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                text: entry.place
                color: placeHover.hovered ? Theme.colors.accentText : Theme.colors.textTertiary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size

                HoverHandler {
                    id: placeHover

                    enabled: entry.file !== ""
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    enabled: entry.file !== ""
                    onTapped: panel.jumped(entry.file, entry.line, 1)
                }
            }
        }
    }

    Text {
        objectName: "consoleEmpty"
        anchors.centerIn: consoleView
        width: Math.min(consoleView.width - 2 * Theme.space.xxl, implicitWidth)
        visible: consoleView.visible && consoleView.count === 0
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        text: panel.bench.phone === "" ? qsTr("Mirror a phone to see the logic's log here")
                                       : qsTr("Log lines and errors of the logic on this phone appear here")
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.label.size
    }

    ListView {
        id: problemsView

        objectName: "problems"
        anchors.fill: parent
        anchors.topMargin: tabsLine.y + tabsLine.height
        visible: tabs.currentIndex === 1
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: panel.bench.problems

        header: Text {
            width: problemsView.width
            visible: panel.bench.checkNotice !== "" && panel.bench.problems.length > 0
            height: visible ? implicitHeight : 0
            leftPadding: Theme.space.lg
            rightPadding: Theme.space.lg
            topPadding: Theme.space.xs
            bottomPadding: Theme.space.xs
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: panel.bench.checkNotice
            color: Theme.colors.warningText
            font.pixelSize: Theme.type.caption.size
        }

        delegate: Rectangle {
            id: problem

            required property var modelData
            readonly property bool reachable: modelData.inside === true && modelData.file !== ""

            objectName: "problemRow"
            width: ListView.view.width
            height: Theme.size.listItem
            color: problemHover.hovered && problem.reachable ? Theme.colors.stateHover : "transparent"

            HoverHandler {
                id: problemHover

                cursorShape: problem.reachable ? Qt.PointingHandCursor : Qt.ArrowCursor
            }

            TapHandler {
                enabled: problem.reachable
                onTapped: panel.jumped(problem.modelData.file, problem.modelData.line, problem.modelData.column)
            }

            Icon {
                id: severity

                x: Theme.space.lg
                anchors.verticalCenter: parent.verticalCenter
                name: problem.modelData.severity === "error" ? "circle-x" : "triangle-alert"
                size: Theme.size.iconSmall
                color: problem.modelData.severity === "error" ? Theme.colors.dangerText : Theme.colors.warningText
            }

            Text {
                anchors.left: severity.right
                anchors.leftMargin: Theme.space.sm
                anchors.right: where.left
                anchors.rightMargin: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: problem.modelData.message
                elide: Text.ElideRight
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
            }

            Text {
                id: where

                anchors.right: parent.right
                anchors.rightMargin: Theme.space.lg
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: [problem.modelData.place, problem.modelData.code].filter(part => part !== "").join(" · ")
                color: Theme.colors.textTertiary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size
            }
        }
    }

    Text {
        objectName: "problemsEmpty"
        anchors.centerIn: problemsView
        width: Math.min(problemsView.width - 2 * Theme.space.xxl, implicitWidth)
        visible: problemsView.visible && panel.bench.problems.length === 0
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        text: panel.bench.checkNotice !== "" ? panel.bench.checkNotice
              : !panel.bench.hasLogic ? qsTr("Add logic to check it")
              : panel.bench.checking ? qsTr("Checking…") : qsTr("No problems")
        color: panel.bench.checkNotice !== "" ? Theme.colors.warningText : Theme.colors.textTertiary
        font.pixelSize: Theme.type.label.size
    }

    ListView {
        id: referencesView

        readonly property string name: panel.bench.references.name ?? ""
        readonly property int found: panel.bench.references.count ?? 0
        readonly property int more: panel.bench.references.more ?? 0

        objectName: "references"
        anchors.fill: parent
        anchors.topMargin: tabsLine.y + tabsLine.height
        visible: tabs.currentIndex === 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: panel.bench.references.places ?? []

        header: Text {
            objectName: "referencesTitle"
            width: referencesView.width
            visible: referencesView.found > 0
            height: visible ? implicitHeight : 0
            leftPadding: Theme.space.lg
            rightPadding: Theme.space.lg
            topPadding: Theme.space.xs
            bottomPadding: Theme.space.xs
            textFormat: Text.PlainText
            elide: Text.ElideRight
            text: qsTr("%n reference(s) to %1", "", referencesView.found).arg(referencesView.name)
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
        }

        footer: Text {
            objectName: "referencesMore"
            width: referencesView.width
            visible: referencesView.more > 0
            height: visible ? implicitHeight : 0
            leftPadding: Theme.space.lg
            rightPadding: Theme.space.lg
            topPadding: Theme.space.xs
            bottomPadding: Theme.space.xs
            textFormat: Text.PlainText
            text: qsTr("and %n more", "", referencesView.more)
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        delegate: Rectangle {
            id: reference

            required property var modelData

            objectName: "referenceRow"
            width: ListView.view.width
            height: Theme.size.listItem
            color: referenceHover.hovered ? Theme.colors.stateHover : "transparent"

            HoverHandler {
                id: referenceHover

                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: panel.jumped(reference.modelData.file, reference.modelData.line, reference.modelData.column)
            }

            Text {
                id: referencePlace

                x: Theme.space.lg
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, parent.width / 3)
                textFormat: Text.PlainText
                text: reference.modelData.place
                elide: Text.ElideMiddle
                color: Theme.colors.textTertiary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.caption.size
            }

            Text {
                anchors.left: referencePlace.right
                anchors.leftMargin: Theme.space.md
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.lg
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: reference.modelData.text
                elide: Text.ElideRight
                color: Theme.colors.textPrimary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.mono.size
            }
        }
    }

    Text {
        objectName: "referencesEmpty"
        anchors.centerIn: referencesView
        width: Math.min(referencesView.width - 2 * Theme.space.xxl, implicitWidth)
        visible: referencesView.visible && referencesView.found === 0
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        text: referencesView.name !== "" ? qsTr("No references to %1 in this profile").arg(referencesView.name)
                                         : qsTr("Right-click a name in the code and choose Find references to list where it is used")
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.label.size
    }
}
