import QtQuick

Rectangle {
    required property int modules
    required property string path
    property int moduleSize: 4

    readonly property int quietZone: 4

    implicitWidth: (modules + 2 * quietZone) * moduleSize
    implicitHeight: implicitWidth
}
