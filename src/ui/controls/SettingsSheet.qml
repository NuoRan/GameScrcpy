import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme

Dialog {
    id: sheet

    property string iconName
    property string tone
    property string subtitle
    property string statusText
    property string statusTone: "idle"
    default property alias body: column.data

    readonly property real bodyLimit: Math.max(Theme.size.settingsRow,
                                               (parent ? parent.height : Theme.size.sheetWidth)
                                               - implicitHeaderHeight - implicitFooterHeight
                                               - topPadding - bottomPadding - 2 * Theme.space.xxl)

    implicitWidth: Theme.size.sheetWidth
    acceptText: qsTr("Done")
    rejectText: ""
    topPadding: Theme.space.lg
    bottomPadding: Theme.space.lg

    header: Item {
        implicitHeight: headRow.implicitHeight + Theme.space.xxl

        RowLayout {
            id: headRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.space.xxl
            anchors.rightMargin: Theme.space.xxl
            spacing: Theme.space.md

            Tile {
                visible: sheet.iconName !== ""
                large: true
                iconName: sheet.iconName
                tone: sheet.tone
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space.xxs

                Text {
                    objectName: "sheetTitle"
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: sheet.title
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.subtitle.size
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    objectName: "sheetSubtitle"
                    Layout.fillWidth: true
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: sheet.subtitle
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    elide: Text.ElideRight
                }
            }

            StatusPill {
                objectName: "sheetStatus"
                visible: sheet.statusText !== ""
                text: sheet.statusText
                tone: sheet.statusTone
            }
        }
    }

    contentItem: Flickable {
        id: scroller

        implicitWidth: column.implicitWidth
        implicitHeight: Math.min(column.implicitHeight, sheet.bodyLimit)
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column

            width: scroller.width
            spacing: Theme.space.lg
        }
    }
}
