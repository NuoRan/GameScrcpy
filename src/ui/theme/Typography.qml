import QtQuick

QtObject {
    final readonly property TextStyle display: TextStyle { size: 28; lineHeight: 34; weight: Font.DemiBold }
    final readonly property TextStyle title: TextStyle { size: 20; lineHeight: 26; weight: Font.DemiBold }
    final readonly property TextStyle subtitle: TextStyle { size: 16; lineHeight: 22; weight: Font.Medium }
    final readonly property TextStyle heading: TextStyle { size: 15; lineHeight: 20; weight: Font.DemiBold }
    final readonly property TextStyle body: TextStyle { size: 14; lineHeight: 20 }
    final readonly property TextStyle prose: TextStyle { size: 14; lineHeight: 24 }
    final readonly property TextStyle lead: TextStyle { size: 15; lineHeight: 25 }
    final readonly property TextStyle label: TextStyle { size: 13; lineHeight: 18; weight: Font.Medium }
    final readonly property TextStyle caption: TextStyle { size: 12; lineHeight: 16 }
    final readonly property TextStyle micro: TextStyle { size: 11; lineHeight: 14; weight: Font.DemiBold }
    final readonly property TextStyle mono: TextStyle { size: 13; lineHeight: 18 }
    final readonly property TextStyle code: TextStyle { size: 13; lineHeight: 21 }

    final readonly property var installedFamilies: Qt.fontFamilies()

    final readonly property string iconFamily: installedFamilies.indexOf("Segoe Fluent Icons") >= 0
                                         ? "Segoe Fluent Icons" : "Segoe MDL2 Assets"
    final readonly property string monoFamily: installedFamilies.indexOf("Cascadia Mono") >= 0
                                         ? "Cascadia Mono" : "Consolas"
}
