import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Isp"

Item {
    id: root
    property var paths: []
    property bool showFile: true
    property bool showExif: false
    property bool showHistogram: false
    property int histogramSource: 0
    property bool showPixel: false
    property var pixelValues: []
    property string transientMessage: ""
    property bool transientError: false
    signal closeRequested()

    function open(selectedPaths) {
        paths = selectedPaths
        showFile = true
        showExif = false
        showHistogram = false
        showPixel = false
        pixelValues = []
        transientMessage = ""
        transientError = false
        compareController.setHoldCandidate(false)
        compareController.setPresentationMode(0)
        compareController.setSplitAmount(0.5)
        compareController.setSynchronized(true)
        compareController.setPaths(selectedPaths)
        compareController.fitAll()
        forceActiveFocus()
    }
    function showTransientMessage(message, isError) {
        transientMessage = message
        transientError = isError
        transientMessageTimer.restart()
    }
    function saveScreenshot() {
        const scheduled = stage.grabToImage(function(result) {
            const path = compareController.chooseScreenshotPath()
            if (!path)
                return
            if (result.saveToFile(path))
                root.showTransientMessage("Screenshot saved: " + path, false)
            else
                root.showTransientMessage("Unable to save screenshot: " + path, true)
        })
        if (!scheduled)
            root.showTransientMessage("Unable to capture the comparison area", true)
    }
    function comparisonFileText(slot) {
        var currentRevision = compareController.revision
        return compareController.fileText(slot) || "Loading…"
    }
    function comparisonExifText(slot) {
        var currentRevision = compareController.revision
        return compareController.cameraText(slot)
    }
    function comparisonColumns() {
        return root.paths.length === 4 ? 2 : Math.max(1, root.paths.length)
    }
    function comparisonRows() {
        return root.paths.length === 4 ? 2 : 1
    }
    function cellX(slot, availableWidth) {
        if (compareController.presentationMode !== 0) return 0
        var columns = comparisonColumns()
        var cellWidth = (availableWidth - 2 * (columns - 1)) / columns
        return (slot % columns) * (cellWidth + 2)
    }
    function cellY(slot, availableHeight) {
        if (compareController.presentationMode !== 0) return 0
        var rows = comparisonRows()
        var cellHeight = (availableHeight - 2 * (rows - 1)) / rows
        return Math.floor(slot / comparisonColumns()) * (cellHeight + 2)
    }
    function cellWidth(availableWidth) {
        if (compareController.presentationMode !== 0) return availableWidth
        return (availableWidth - 2 * (comparisonColumns() - 1)) / comparisonColumns()
    }
    function cellHeight(availableHeight) {
        if (compareController.presentationMode !== 0) return availableHeight
        return (availableHeight - 2 * (comparisonRows() - 1)) / comparisonRows()
    }
    function requestHistograms() {
        for (var slot = 0; slot < root.paths.length; ++slot)
            compareController.requestHistogram(slot, root.histogramSource)
    }
    onShowHistogramChanged: if (showHistogram) requestHistograms()
    onHistogramSourceChanged: if (showHistogram) requestHistograms()
    onShowPixelChanged: {
        if (showPixel && pixelValues.length === 0) {
            var placeholders = []
            for (var slot = 0; slot < root.paths.length; ++slot)
                placeholders.push("Move the pointer over an image")
            pixelValues = placeholders
        }
    }
    Keys.onEscapePressed: closeRequested()
    Keys.onPressed: function(event) { if (event.key === Qt.Key_B && event.modifiers === Qt.NoModifier && !event.isAutoRepeat) { compareController.setHoldCandidate(true); event.accepted = true } }
    Keys.onReleased: function(event) { if (event.key === Qt.Key_B && event.modifiers === Qt.NoModifier && !event.isAutoRepeat) { compareController.setHoldCandidate(false); event.accepted = true } }
    Connections {
        target: compareController
        function onFrameChanged(slot, fullResolution) { if (root.showHistogram) compareController.requestHistogram(slot, root.histogramSource) }
    }
    Timer {
        id: transientMessageTimer
        interval: 5000
        onTriggered: root.transientMessage = ""
    }
    Connections {
        target: root.Window.window
        function onActiveChanged() {
            if (root.Window.window && !root.Window.window.active)
                compareController.setHoldCandidate(false)
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.sensorWhite }
    Rectangle {
        id: toolbar; height: Theme.toolbarHeight; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        color: Theme.paperWhite; border.color: Theme.opticalGray
        RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 7
            AppIconButton { iconSource: "qrc:/icons/ui/back.svg"; toolTipText: "Back"; onClicked: root.closeRequested() }
            Rectangle { width: 1; height: 22; color: Theme.opticalGray }
            Label { text: "Compare"; color: Theme.graphiteInk; font.pixelSize: 15; font.weight: Font.DemiBold }
            Label { text: root.paths.length + " images"; color: Theme.mutedInk; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            AppIconButton { checkable: true; checked: compareController.presentationMode === 0; iconSource: "qrc:/icons/ui/columns.svg"; toolTipText: "Side by side"; onClicked: compareController.setPresentationMode(0) }
            AppIconButton { enabled: root.paths.length === 2; checkable: true; checked: compareController.presentationMode === 1; iconSource: "qrc:/icons/ui/compare.svg"; toolTipText: "Vertical split"; onClicked: compareController.setPresentationMode(1) }
            AppIconButton { enabled: root.paths.length === 2; checkable: true; checked: compareController.presentationMode === 2; iconSource: "qrc:/icons/ui/sliders.svg"; toolTipText: "Horizontal split"; onClicked: compareController.setPresentationMode(2) }
            AppIconButton { enabled: root.paths.length === 2 && compareController.presentationMode === 0; checkable: true; checked: compareController.holdCandidate; iconSource: "qrc:/icons/ui/compare.svg"; toolTipText: "Hold B to show the candidate over the left image"; onPressed: compareController.setHoldCandidate(true); onReleased: compareController.setHoldCandidate(false) }
            Rectangle { width: 1; height: 22; color: Theme.opticalGray }
            AppIconButton { checkable: true; checked: compareController.synchronized; iconSource: "qrc:/icons/ui/columns.svg"; toolTipText: "Synchronize view"; onClicked: compareController.setSynchronized(checked) }
            AppIconButton { iconSource: "qrc:/icons/ui/gallery.svg"; toolTipText: "Fit all images"; onClicked: compareController.fitAll() }
            AppIconButton { iconSource: "qrc:/icons/ui/actual-size.svg"; toolTipText: "Show actual pixels (1:1)"; onClicked: compareController.actualPixelsAll() }
            AppIconButton { iconSource: "qrc:/icons/ui/info.svg"; checkable: true; checked: root.showFile; toolTipText: "File information"; onClicked: root.showFile = checked }
            AppIconButton { iconSource: "qrc:/icons/ui/sliders.svg"; checkable: true; checked: root.showExif; toolTipText: "EXIF information"; onClicked: root.showExif = checked }
            AppIconButton { iconSource: "qrc:/icons/ui/gallery.svg"; checkable: true; checked: root.showHistogram; toolTipText: "Histogram"; onClicked: root.showHistogram = checked }
            AppIconButton { iconSource: "qrc:/icons/ui/search.svg"; checkable: true; checked: root.showPixel; toolTipText: "Pixel values"; onClicked: root.showPixel = checked }
            AppIconButton { iconSource: "qrc:/icons/ui/more.svg"; toolTipText: "Save screenshot"; onClicked: root.saveScreenshot() }
        }
    }
    Item {
        id: stage; anchors.top: toolbar.bottom; anchors.bottom: footer.top; anchors.left: parent.left; anchors.right: parent.right
        Item {
            id: comparisonArea
            anchors.fill: parent
            anchors.margins: 8
            clip: true

            ImageCanvas {
                id: comparisonCanvas
                anchors.fill: parent
                presentationMode: compareController.presentationMode
                compareAmount: compareController.splitAmount
                viewSynchronized: compareController.synchronized
                Component.onCompleted: compareController.attachCanvas(comparisonCanvas)
                onCompareAmountChanged: compareController.setSplitAmount(compareAmount)
                onPixelHovered: function(sourceSlot, pixel, color, valid) {
                    if (valid)
                        root.pixelValues = compareController.pixelTexts(sourceSlot, pixel.x, pixel.y)
                }
            }

            Rectangle {
                visible: compareController.presentationMode === 0 && root.comparisonColumns() > 1
                x: root.cellWidth(parent.width)
                width: 2
                height: parent.height
                color: Theme.sensorWhite
            }
            Rectangle {
                visible: compareController.presentationMode === 0 && root.paths.length === 3
                x: root.cellWidth(parent.width) * 2 + 2
                width: 2
                height: parent.height
                color: Theme.sensorWhite
            }
            Rectangle {
                visible: compareController.presentationMode === 0 && root.paths.length === 4
                y: root.cellHeight(parent.height)
                width: parent.width
                height: 2
                color: Theme.sensorWhite
            }
            Rectangle {
                visible: compareController.presentationMode > 0 && root.paths.length === 2
                color: Theme.paperWhite
                width: compareController.presentationMode === 1 ? 2 : parent.width
                height: compareController.presentationMode === 1 ? parent.height : 2
                x: compareController.presentationMode === 1
                   ? comparisonCanvas.dividerPosition - 1 : 0
                y: compareController.presentationMode === 2
                   ? comparisonCanvas.dividerPosition - 1 : 0
            }

            Repeater {
                model: root.paths
                delegate: Item {
                    property var navigationData: {
                        var currentNavigationRevision = comparisonCanvas.navigationRevision
                        return comparisonCanvas.navigationState(index)
                    }
                    x: root.cellX(index, comparisonArea.width)
                    y: root.cellY(index, comparisonArea.height)
                    width: root.cellWidth(comparisonArea.width)
                    height: root.cellHeight(comparisonArea.height)
                    visible: compareController.presentationMode === 0 || index === 0
                    clip: true

                    Rectangle {
                        visible: parent.navigationData.visible === true
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 12
                        width: parent.navigationData.width || 0
                        height: parent.navigationData.height || 0
                        radius: 5
                        color: "#A60C0C0E"
                        border.width: 1
                        border.color: "#AAFFFFFF"
                        Image {
                            anchors.fill: parent
                            anchors.margins: 5
                            source: "image://thumbnail/" + encodeURIComponent(root.paths[index])
                            sourceSize: Qt.size(180, 130)
                            fillMode: Image.Stretch
                            opacity: 0.7
                        }
                        Rectangle {
                            property rect normalizedViewport: parent.parent.navigationData.viewport || Qt.rect(0, 0, 0, 0)
                            x: 5 + normalizedViewport.x * (parent.width - 10)
                            y: 5 + normalizedViewport.y * (parent.height - 10)
                            width: normalizedViewport.width * (parent.width - 10)
                            height: normalizedViewport.height * (parent.height - 10)
                            color: "#18FFFFFF"
                            border.width: 2
                            border.color: "white"
                        }
                        Label {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: 10
                            text: parent.parent.navigationData.zoom || ""
                            color: "white"
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            style: Text.Outline
                            styleColor: "#D9000000"
                        }
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 9
                        spacing: 3
                        Rectangle {
                            visible: root.showFile
                            height: 24
                            radius: 3
                            color: "#C91E2227"
                            width: Math.min(parent.parent.width - 18, Math.min(260, file.implicitWidth + 18))
                            Label { id: file; anchors.centerIn: parent; width: parent.width - 12; text: root.comparisonFileText(index); color: "white"; font.pixelSize: 12; elide: Text.ElideMiddle }
                        }
                        Rectangle {
                            visible: root.showExif
                            height: 24
                            radius: 3
                            color: "#C91E2227"
                            width: Math.min(parent.parent.width - 18, Math.min(420, exif.implicitWidth + 18))
                            Label { id: exif; anchors.centerIn: parent; width: parent.width - 12; text: root.comparisonExifText(index); color: "white"; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                    }
                    Rectangle {
                        visible: root.showPixel && root.pixelValues[index]
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 9
                        color: "#C91E2227"
                        radius: 3
                        width: Math.min(parent.width - 18, Math.min(310, pixel.implicitWidth + 18))
                        height: pixel.implicitHeight + 10
                        Label { id: pixel; anchors.centerIn: parent; width: parent.width - 12; text: root.pixelValues[index] || ""; color: "white"; font.pixelSize: 10; font.family: Theme.monoFont; elide: Text.ElideRight }
                    }
                    HistogramPanel {
                        visible: root.showHistogram
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 9
                        width: Math.min(implicitWidth, parent.width - 18)
                        height: Math.min(implicitHeight, parent.height - 18)
                        slot: index
                        source: root.histogramSource
                        Component.onCompleted: if (root.showHistogram) request()
                    }
                }
            }
        }
    }
    Rectangle { id: footer; height: 30; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; color: Theme.paperWhite; border.color: Theme.opticalGray
        Label {
            anchors.centerIn: parent
            width: parent.width - 28
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            text: root.transientMessage.length > 0
                  ? root.transientMessage
                  : compareController.presentationMode === 0
                    ? "Scroll to zoom  ·  Drag to pan  ·  Hold B to inspect candidate"
                    : "Drag divider  ·  Scroll to zoom  ·  Drag to pan"
            color: root.transientMessage.length > 0 && root.transientError
                   ? Theme.danger : Theme.mutedInk
            font.pixelSize: 11
        }
    }
}
