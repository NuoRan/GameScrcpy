import QtQuick

QtObject {
    property bool dark: true

    final readonly property color ambient: dark ? "#8C000000" : "#1F000000"
    final readonly property int ambientBlur: 64
    final readonly property int ambientOffset: 24
    final readonly property color key: dark ? "#59000000" : "#14000000"
    final readonly property int keyBlur: 10
    final readonly property int keyOffset: 2
    final readonly property int backdropBlur: 24
}
