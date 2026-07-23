pragma Singleton
import QtQuick

QtObject {
    readonly property color sensorWhite: "#F4F5F2"
    readonly property color paperWhite: "#FCFCFA"
    readonly property color opticalGray: "#DDE1E3"
    readonly property color softHover: "#EDF0F0"
    readonly property color graphiteInk: "#25303A"
    readonly property color mutedInk: "#69747D"
    readonly property color probeBlue: "#356AE6"
    readonly property color exposureAmber: "#B87524"
    readonly property color danger: "#C94E4E"

    readonly property int unit: 4
    readonly property int toolbarHeight: 52
    readonly property int sidebarWidth: 272
    readonly property int radius: 4
    readonly property int iconSize: 20
    readonly property int touchTarget: 32
    readonly property int fast: 120
    readonly property int normal: 180
    readonly property string uiFont: "Inter"
    readonly property string monoFont: "JetBrains Mono"
}
