import QtQuick
import QtTest
import "../../design"

TestCase {
    id: testCase
    name: "FullScreenDesignPreview"
    when: windowShown
    width: 1440
    height: 900
    visible: true

    FullScreenPreview {
        id: preview
        anchors.fill: parent
    }

    function test_productionPageAndPanelsArePreviewable() {
        const page = findChild(preview, "fullScreenPage")
        const canvas = findChild(page, "designFullScreenCanvas")
        const topPanel = findChild(page, "fullScreenTopPanel")
        const rightPanel = findChild(page, "fullScreenPropertiesCard")
        const bottomPanel = findChild(page, "fullScreenBottomPanel")
        const navigation = findChild(page, "fullScreenNavigationOverlay")
        const zoomLabel = findChild(page, "fullScreenZoomLabel")
        verify(page !== null)
        verify(canvas !== null)
        verify(topPanel !== null)
        verify(rightPanel !== null)
        verify(bottomPanel !== null)
        verify(navigation !== null)
        verify(zoomLabel !== null)
        verify(topPanel.visible)
        verify(rightPanel.visible)
        verify(bottomPanel.visible)
        verify(navigation.visible)
        compare(zoomLabel.text, "135%")
        compare(topPanel.color.toString(), bottomPanel.color.toString())
    }

    function test_propertyTabsExposeAllInspectorSections() {
        const card = findChild(preview, "fullScreenPropertiesCard")
        const fileTab = findChild(card, "fullScreenPropertyTab-0")
        const exifTab = findChild(card, "fullScreenPropertyTab-1")
        const histogramTab = findChild(card, "fullScreenPropertyTab-2")
        const rawTab = findChild(card, "fullScreenPropertyTab-3")
        verify(fileTab !== null)
        verify(exifTab !== null)
        verify(histogramTab !== null)
        verify(rawTab !== null)
        compare(fileTab.text, "File")
        compare(exifTab.text, "EXIF")
        compare(histogramTab.text, "Histogram")
        compare(rawTab.text, "RAW")
        mouseClick(exifTab, exifTab.width / 2, exifTab.height / 2, Qt.LeftButton)
        compare(card.currentTab, 1)
        mouseClick(histogramTab, histogramTab.width / 2, histogramTab.height / 2, Qt.LeftButton)
        compare(card.currentTab, 2)
        tryVerify(function() {
            const histogram = findChild(card, "propertiesHistogram")
            return histogram !== null && histogram.visible
        }, 1000)
        mouseClick(rawTab, rawTab.width / 2, rawTab.height / 2, Qt.LeftButton)
        compare(card.currentTab, 3)
    }

    function test_escapeAndCloseButtonRequestPageExit() {
        const page = findChild(preview, "fullScreenPage")
        const closeButton = findChild(page, "closeFullScreenButton")
        let closeCount = 0
        function countClose() { closeCount += 1 }
        page.closeRequested.connect(countClose)
        mouseClick(closeButton, closeButton.width / 2, closeButton.height / 2, Qt.LeftButton)
        compare(closeCount, 1)
        page.forceActiveFocus()
        keyClick(Qt.Key_Escape)
        compare(closeCount, 2)
        page.closeRequested.disconnect(countClose)
    }

    function test_rightInspectorHasStableEdgeToCardHoverCorridor() {
        const page = findChild(preview, "fullScreenPage")
        const card = findChild(page, "fullScreenPropertiesCard")
        const activator = findChild(page, "fullScreenRightEdgeActivator")
        const bridge = findChild(page, "fullScreenRightPanelBridge")
        verify(card !== null)
        verify(activator !== null)
        verify(bridge !== null)

        page.rightPanelVisible = false
        mouseMove(page, page.width - 2, 4)
        tryCompare(page, "rightPanelVisible", true, 500)
        verify(card.y <= 12)

        mouseMove(page, card.x - 6, 4)
        wait(1550)
        verify(page.rightPanelVisible)

        mouseMove(page, 20, page.height / 2)
        tryCompare(page, "rightPanelVisible", false, 1900)
    }
}
