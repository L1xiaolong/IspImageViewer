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

    function openFullScreen(paths, initialIndex) {
        showingCompare = false
        showingFullScreen = true
        // Keep the main window's native state untouched. A separate full-screen window avoids
        // the Maximize <-> FullScreen transition that makes the QML scene and image canvas
        // resize twice on Windows.
        fullScreenWindow.showFullScreen()
        fullScreenPage.open(paths, initialIndex)
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

        // The main window never changed state, so closing the viewer is just a hide operation.
        // This keeps the maximized main page at one stable size and eliminates the final resize
        // frame as well.
        fullScreenWindow.hide()
        showingFullScreen = false
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

    Window {
        id: fullScreenWindow
        objectName: "qmlFullScreenWindow"
        visible: false
        color: "#A0A0A0"
        flags: Qt.Window | Qt.FramelessWindowHint
        transientParent: window

        FullScreenPage {
            id: fullScreenPage
            controller: fullScreenController
            propertiesController: imagePropertiesController
            anchors.fill: parent
            onCloseRequested: window.closeFullScreen()
        }

        onClosing: function(close) {
            close.accepted = false
            window.closeFullScreen()
        }
    }
    Connections {
        target: fullScreenController
        function onFilesystemChanged() { browseController.refreshAll() }
    }
}
