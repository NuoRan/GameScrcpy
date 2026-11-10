import QtQuick

QtObject {
    property bool reduced: false

    readonly property int fast: reduced ? 0 : 120
    readonly property int base: reduced ? 0 : 200
    readonly property int medium: reduced ? 0 : 280
    readonly property int slow: reduced ? 0 : 400
    readonly property int fade: 160

    readonly property int pulse: 1000
    readonly property real pulseLow: 0.35
    readonly property int tick: 1000

    readonly property int tooltipDelay: 500
    readonly property int toastStay: 3000
    readonly property int toolbarHideDelay: 2000
    readonly property int toolbarDwell: 400
    readonly property int toolbarCooldown: 500
    readonly property int capsuleStay: 3000
    readonly property int infoDelay: 400
    readonly property int hintIntro: 3000
    readonly property int guideStep: 5200
    readonly property int guideHold: 1400
    readonly property real enterScale: 0.98

    readonly property list<real> standard: [0.2, 0.0, 0.0, 1.0, 1.0, 1.0]
    readonly property list<real> emphasized: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
    readonly property list<real> exit: [0.3, 0.0, 1.0, 1.0, 1.0, 1.0]
}
