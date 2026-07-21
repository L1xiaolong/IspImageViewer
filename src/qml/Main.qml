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
    property bool showingCompare: false

    Component.onCompleted: {
        if (initialComparePaths.length >= 2) {
            showingCompare = true
            comparePage.open(initialComparePaths)
        }
    }
    Connections {
        target: browseController
        function onCompareRequested(paths) {
            window.showingCompare = true
            comparePage.open(paths)
        }
    }

    BrowsePage {
        id: browsePage
        anchors.fill: parent
        controller: browseController.activePane
        workspaceController: browseController
        visible: !window.showingCompare
    }
    ComparePage {
        id: comparePage
        objectName: "comparePage"
        anchors.fill: parent
        visible: window.showingCompare
        onCloseRequested: window.showingCompare = false
    }
}
