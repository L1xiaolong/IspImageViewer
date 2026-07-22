import QtQuick
import "../src/qml/Pages"

Item {
    id: root
    width: 1440
    height: 900

    MockCompareController {
        id: mockCompareController
    }

    ComparePage {
        id: productionComparePage
        anchors.fill: parent
        controller: mockCompareController
        designMode: true
    }

    Component.onCompleted: productionComparePage.open([
        "/Images/ISP calibration/XAG040_0001.JPG",
        "/Images/Outdoor samples/XAG040_0002.JPG"
    ])
}
