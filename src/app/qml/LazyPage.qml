import QtQuick

Item {
    id: lazy

    property Component page
    readonly property Item item: loader.item as Item

    function ensure(): Item {
        loader.active = true
        return loader.item as Item
    }

    onVisibleChanged: {
        if (visible)
            Qt.callLater(lazy.ensure)
    }

    Loader {
        id: loader

        anchors.fill: parent
        active: false
        sourceComponent: lazy.page
    }
}
