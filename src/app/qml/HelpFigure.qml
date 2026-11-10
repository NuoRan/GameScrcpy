pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: figure

    property string name
    readonly property var spec: figure.specOf(figure.name)
    readonly property Item art: loader.item as Item
    readonly property bool known: figure.spec.family !== ""
    readonly property real phoneScale: 0.8

    function specOf(name: string): var {
        switch (name) {
        case "hero-connecting":
            return { family: "connect", variant: "hero" }
        case "device-stepper":
            return { family: "connect", variant: "stepper" }
        case "pair-qr":
            return { family: "connect", variant: "qr" }
        case "phone-usb":
            return { family: "phone", variant: "waiting", caption: qsTr("Plug the phone into this computer") }
        case "phone-usbmode":
            return { family: "phone", variant: "usb" }
        case "phone-allow":
            return { family: "phone", variant: "allow" }
        case "phone-ready":
            return { family: "phone", variant: "ready", caption: qsTr("Mirroring") }
        case "phone-pairing":
            return { family: "phone", variant: "pairing", title: qsTr("Wireless debugging") }
        case "phone-scan":
            return { family: "phone", variant: "scan" }
        case "mirror-overview":
            return { family: "mirror", variant: "overview" }
        case "mirror-toolbar":
            return { family: "mirror", variant: "toolbar" }
        case "camera-lock":
            return { family: "mirror", variant: "cameraLock" }
        case "window-modes":
            return { family: "mirror", variant: "windowModes" }
        case "editor-overview":
            return { family: "editor", variant: "overview" }
        case "editor-components":
            return { family: "editor", variant: "components" }
        case "editor-drag":
            return { family: "editor", variant: "drag" }
        case "editor-capture":
            return { family: "editor", variant: "capture" }
        case "editor-conflict":
            return { family: "editor", variant: "conflict" }
        case "editor-try":
            return { family: "editor", variant: "try" }
        case "profile-link":
            return { family: "profile", variant: "link" }
        case "profile-share":
            return { family: "profile", variant: "share" }
        case "script-workbench":
            return { family: "script", variant: "workbench" }
        case "until-flow":
            return { family: "script", variant: "until" }
        case "vision-freeze":
            return { family: "vision", variant: "freeze" }
        case "vision-draw":
            return { family: "vision", variant: "draw" }
        case "vision-name":
            return { family: "vision", variant: "name" }
        case "vision-test":
            return { family: "vision", variant: "test" }
        case "zadig-open":
            return { family: "driver", variant: "open" }
        case "zadig-list":
            return { family: "driver", variant: "list" }
        case "zadig-pick":
            return { family: "driver", variant: "pick" }
        case "zadig-install":
            return { family: "driver", variant: "install" }
        case "driver-done":
            return { family: "driver", variant: "done" }
        default:
            return { family: "", variant: "" }
        }
    }

    implicitWidth: figure.art ? figure.art.implicitWidth : 0
    implicitHeight: figure.art ? figure.art.implicitHeight : 0

    Accessible.role: Accessible.Graphic

    Loader {
        id: loader

        anchors.centerIn: parent
        sourceComponent: {
            switch (figure.spec.family) {
            case "connect":
                return connectArt
            case "phone":
                return phoneArt
            case "mirror":
                return mirrorArt
            case "editor":
                return editorArt
            case "profile":
                return profileArt
            case "script":
                return scriptArt
            case "vision":
                return visionArt
            case "driver":
                return driverArt
            default:
                return null
            }
        }
    }

    Component {
        id: connectArt

        HelpArtConnect {
            kind: figure.spec.variant
        }
    }

    Component {
        id: phoneArt

        Item {
            implicitWidth: phone.implicitWidth * figure.phoneScale
            implicitHeight: phone.implicitHeight * figure.phoneScale

            GuidePhone {
                id: phone

                transformOrigin: Item.TopLeft
                scale: figure.phoneScale
                screen: figure.spec.variant
                title: figure.spec.title ?? ""
                caption: figure.spec.caption ?? ""
            }
        }
    }

    Component {
        id: mirrorArt

        HelpArtMirror {
            kind: figure.spec.variant
        }
    }

    Component {
        id: editorArt

        HelpArtEditor {
            stage: figure.spec.variant
        }
    }

    Component {
        id: profileArt

        HelpArtProfile {
            kind: figure.spec.variant
        }
    }

    Component {
        id: scriptArt

        HelpArtScript {
            kind: figure.spec.variant
        }
    }

    Component {
        id: visionArt

        HelpArtScript {
            kind: "vision"
            stage: figure.spec.variant
        }
    }

    Component {
        id: driverArt

        HelpArtDriver {
            kind: figure.spec.variant
        }
    }
}
