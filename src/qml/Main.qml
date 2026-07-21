import QtQuick
import QtQuick.Controls
import "Isp"
import "Pages"

ApplicationWindow {
    id: window
    objectName: "qmlMainWindow"
    visible: true
    width: 1440
    height: 900
    minimumWidth: 980
    minimumHeight: 640
    title: "ISP Image Viewer"
    color: Theme.sensorWhite

    BrowsePage {
        anchors.fill: parent
        controller: browseController
    }
}
