pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.App

Item {
    id: layer

    property var keyHints: []
    property var liveControls: []
    property var aimingControls: []
    property var actionWheel: ({})
    property rect pictureRect
    property bool hintsAllowed: false
    property string profileKey
    readonly property bool hintsShown: hintsAllowed && keyHints.length > 0
                                       && AppSettings.values["keymap.hints"] !== false

    objectName: "gridHints"
    enabled: false

    KeyHintLayer {
        objectName: "gridKeyHints"
        anchors.fill: parent
        hints: layer.keyHints
        live: layer.liveControls
        aiming: layer.aimingControls
        picture: layer.pictureRect
        shown: layer.hintsShown
        restingOpacity: (AppSettings.values["keymap.hintOpacity"] ?? 60) / 100
        intro: AppSettings.values["keymap.hintIntro"] !== false
        introDuration: AppSettings.values["keymap.hintIntroMs"] ?? Theme.motion.hintIntro
        introKey: layer.profileKey
    }

    ActionWheelView {
        wheel: layer.actionWheel
        picture: layer.pictureRect
    }
}
