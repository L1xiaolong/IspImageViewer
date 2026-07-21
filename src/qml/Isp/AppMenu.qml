import QtQuick
import QtQuick.Controls

Menu {
    id: control
    implicitWidth: 220
    padding: 6
    topPadding: 6
    bottomPadding: 6
    margins: 8

    delegate: AppMenuItem {}

    background: Rectangle {
        color: "#FCFDFC"
        radius: 8
        border.color: "#D7DEE3"
        border.width: 1
    }
}
