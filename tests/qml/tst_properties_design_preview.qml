import QtQuick
import QtTest
import "../../design"

TestCase {
    id: testCase
    name: "PropertiesDesignPreview"
    when: windowShown
    width: 1000
    height: 820
    visible: true

    PropertiesPreview {
        id: preview
        anchors.fill: parent
    }

    function test_usesProductionPropertiesDialog() {
        const dialog = findChild(preview, "imagePropertiesDialog")
        verify(dialog !== null)
        tryCompare(dialog, "opened", true)
        const scroll = findChild(dialog, "imagePropertiesScroll")
        verify(scroll !== null)
        verify(dialog.width <= 600)
        verify(dialog.height <= 680)
    }
}
