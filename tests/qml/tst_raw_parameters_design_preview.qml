import QtQuick
import QtTest
import "../../design"

TestCase {
    id: testCase
    name: "RawParametersDesignPreview"
    when: windowShown
    width: 1000
    height: 860
    visible: true

    RawParametersPreview { id: preview; anchors.fill: parent }

    function test_productionRawEditorIsPreviewable() {
        const dialog = findChild(preview, "rawParametersDialog")
        verify(dialog !== null)
        tryCompare(dialog, "opened", true)
        verify(findChild(dialog, "rawParameterScroll") !== null)
        verify(dialog.width <= 570)
        verify(dialog.height <= 720)
    }
}
