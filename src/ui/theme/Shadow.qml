import QtQuick

QtObject {
    property bool dark: true

    readonly property color ambient: dark ? "#8C000000" : "#1F000000"
    readonly property int ambientBlur: 64
    readonly property int ambientOffset: 24
    readonly property color key: dark ? "#59000000" : "#14000000"
    readonly property int keyBlur: 10
    readonly property int keyOffset: 2
    readonly property int backdropBlur: 24
}
