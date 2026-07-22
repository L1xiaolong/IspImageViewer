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
    property bool showingFullScreen: false
    property bool forceApplicationClose: false
    property int previousVisibility: Window.Windowed

    function openFullScreen(paths, initialIndex) {
        previousVisibility = visibility
        showingCompare = false
        showingFullScreen = true
        fullScreenPage.open(paths, initialIndex)
        showFullScreen()
    }

    function closeCompare() {
        if (!showingCompare)
            return
        compareController.setHoldCandidate(false)
        showingCompare = false
        Qt.callLater(function() { browsePage.forceActiveFocus() })
    }

    function closeFullScreen() {
        if (!showingFullScreen)
            return
        showingFullScreen = false
        if (previousVisibility === Window.Maximized)
            showMaximized()
        else if (previousVisibility === Window.FullScreen)
            showFullScreen()
        else
            showNormal()
        browseController.refreshAll()
        Qt.callLater(function() { browsePage.forceActiveFocus() })
    }

    onClosing: function(close) {
        if (forceApplicationClose) {
            close.accepted = true
        } else if (showingFullScreen) {
            close.accepted = false
            closeFullScreen()
        } else if (showingCompare) {
            close.accepted = false
            closeCompare()
        }
    }

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
        propertiesController: imagePropertiesController
        rawController: rawParametersController
        visible: !window.showingCompare && !window.showingFullScreen
        onFullScreenRequested: function(paths, initialIndex) {
            window.openFullScreen(paths, initialIndex)
        }
    }
    ComparePage {
        id: comparePage
        objectName: "comparePage"
        controller: compareController
        anchors.fill: parent
        visible: window.showingCompare && !window.showingFullScreen
        onCloseRequested: window.closeCompare()
    }
    FullScreenPage {
        id: fullScreenPage
        controller: fullScreenController
        propertiesController: imagePropertiesController
        anchors.fill: parent
        visible: window.showingFullScreen
        onCloseRequested: window.closeFullScreen()
    }
    Connections {
        target: fullScreenController
        function onFilesystemChanged() { browseController.refreshAll() }
    }
}
