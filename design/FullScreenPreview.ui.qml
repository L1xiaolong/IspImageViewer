import QtQuick
import "../src/qml/Pages"

Item {
    id: root
    width: 1440
    height: 900

    MockFullScreenController { id: mockFullScreen }
    MockImagePropertiesController { id: mockProperties }

    FullScreenPage {
        id: productionFullScreenPage
        anchors.fill: parent
        controller: mockFullScreen
        propertiesController: mockProperties
        designMode: true
        iconPrefix: Qt.resolvedUrl("../assets/icons/ui/").toString()
        Component.onCompleted: {
            open(mockFullScreen.paths, 0)
            topPanelVisible = true
            rightPanelVisible = true
            bottomPanelVisible = true
            pixelText = "(1842,1064) RGBA(80,120,160,255)"
        }
    }
}
