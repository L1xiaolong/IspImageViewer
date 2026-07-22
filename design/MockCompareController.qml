import QtQuick

QtObject {
    id: root

    property var paths: []
    property int presentationMode: 0
    property real splitAmount: 0.5
    property bool syncEnabled: true
    property bool holdCandidate: false
    property bool fileInformationVisible: true
    property bool exifVisible: true
    property bool histogramVisible: true
    property bool pixelValueVisible: true
    property int revision: 1
    property int histogramRevision: 1
    property var previewUrls: [
        Qt.resolvedUrl("../test_images/XAG040_0001.JPG"),
        Qt.resolvedUrl("../test_images/XAG040_0002.JPG")
    ]

    signal frameChanged(int slot, bool fullResolution)
    signal histogramChanged(int slot)

    function setPaths(value) { paths = value }
    function setPresentationMode(value) { presentationMode = Math.max(0, Math.min(1, value)) }
    function setSplitAmount(value) { splitAmount = Math.max(0, Math.min(1, value)) }
    function setSynchronized(value) { syncEnabled = value }
    function setHoldCandidate(value) { holdCandidate = value }
    function setFileInformationVisible(value) { fileInformationVisible = value }
    function setExifVisible(value) { exifVisible = value }
    function setHistogramVisible(value) { histogramVisible = value }
    function setPixelValueVisible(value) { pixelValueVisible = value }
    function attachCanvas(canvas) {}
    function fitAll() {}
    function actualPixelsAll() {}
    function requestHistogram(slot) { histogramChanged(slot) }
    function fileText(slot) { return ["XAG040_0001.JPG", "XAG040_0002.JPG"][slot] || "" }
    function cameraText(slot) {
        return slot === 0 ? "Sony ILCE-7RM5  •  1/320 s  •  f/5.6  •  ISO 100"
                          : "DJI FC3582  •  1/640 s  •  f/2.8  •  ISO 100"
    }
    function pixelTexts(sourceSlot, x, y) {
        return ["(" + x + "," + y + ") RGBA(92,118,73,255)",
                "(" + x + "," + y + ") RGBA(88,113,70,255)"]
    }
    function histogram(slot) {
        const bins = []
        for (let index = 0; index < 256; ++index) {
            const center = slot === 0 ? 112 : 126
            const distance = (index - center) / 42
            bins.push(Math.round(1600 * Math.exp(-distance * distance)))
        }
        return {
            "valid": true,
            "maximumValue": 255,
            "channels": [{ "name": "Luma", "color": "#F4F5F2", "bins": bins }]
        }
    }
}
