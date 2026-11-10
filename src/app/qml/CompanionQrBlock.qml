pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.App

Column {
    id: block

    property var qr: ({})
    readonly property bool offline: block.qr.offline === true
    readonly property int modules: block.qr.modules ?? 0
    readonly property bool shown: (block.qr.path ?? "") !== "" || block.offline

    objectName: "companionQr"
    visible: block.shown
    spacing: Theme.space.sm

    QrCodeView {
        objectName: "companionQrCode"
        anchors.horizontalCenter: parent.horizontalCenter
        visible: !block.offline && block.modules > 0
        modules: block.modules
        path: block.qr.path ?? ""
        moduleSize: Math.max(2, Math.min(5, Math.floor(block.width / Math.max(1, block.modules + 8))))
    }

    Text {
        objectName: "companionQrTitle"
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        text: block.offline ? qsTr("Connect this computer to the phone's Wi-Fi to connect GameScrcpy Companion")
                            : qsTr("Scan with GameScrcpy Companion to connect")
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.label.size
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
    }

    Text {
        objectName: "companionQrNote"
        visible: !block.offline
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        text: qsTr("Not installed yet? Scan with the phone's camera to download it from this computer.")
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        wrapMode: Text.Wrap
    }

    Text {
        objectName: "companionQrAddress"
        visible: !block.offline && (block.qr.address ?? "") !== ""
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        text: block.qr.address ?? ""
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        font.family: Theme.type.monoFamily
        elide: Text.ElideMiddle
    }
}
