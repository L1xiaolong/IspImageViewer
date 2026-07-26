import QtQuick
import QtTest
import "../../design"
import "../../src/qml/Pages"

TestCase {
    id: testCase
    name: "BrowseInteractions"
    when: windowShown
    width: 1440
    height: 900
    visible: true

    MockBrowseWorkspace {
        id: mockWorkspace
        paneCount: 1
    }

    readonly property var mockController: mockWorkspace.pane0

    BrowsePage {
        id: browsePage
        anchors.fill: parent
        controller: testCase.mockController
        workspaceController: mockWorkspace
        designMode: true
        iconPrefix: Qt.resolvedUrl("../../assets/icons/ui/").toString()
    }

    function init() {
        browsePage.displayMode = 2;
        mockController.clearSelection();
        mockController.selectPath(mockController.thumbnails.get(0).path, false, false);
        mockController.selectPath(mockController.thumbnails.get(1).path, false, true);
        wait(80);
    }

    function test_galleryThumbnailReceivesLeftAndRightClicks() {
        const thumbnailMouse = findChild(browsePage, "galleryMouse-2");
        verify(thumbnailMouse !== null);

        mouseClick(thumbnailMouse, thumbnailMouse.width / 2, thumbnailMouse.height / 2,
                   Qt.LeftButton);
        compare(mockController.selectedPaths.length, 1);
        compare(mockController.selectedPaths[0], mockController.thumbnails.get(2).path);

        const imageMenu = findChild(browsePage, "galleryFileContextMenu");
        verify(imageMenu !== null);
        mouseClick(thumbnailMouse, thumbnailMouse.width / 2, thumbnailMouse.height / 2,
                   Qt.RightButton);
        tryCompare(imageMenu, "opened", true);
        compare(mockController.selectedPaths[0], mockController.thumbnails.get(2).path);
        imageMenu.close();
    }

    function test_gallerySplitterChangesThumbnailWidth() {
        const handle = findChild(browsePage, "galleryResizeHandle");
        const panel = findChild(browsePage, "galleryStripPanel");
        verify(handle !== null);
        verify(panel !== null);
        const oldWidth = panel.width;
        mouseDrag(handle, handle.width / 2, handle.height / 2, -64, 0, Qt.LeftButton);
        verify(panel.width > oldWidth);
    }

    function test_previewInformationAndDragMimeMatchProductionContract() {
        const infoText = findChild(browsePage, "galleryInfoText");
        const card = findChild(browsePage, "galleryDelegate-0");
        verify(infoText !== null);
        verify(card !== null);
        compare(infoText.text, mockController.galleryInfoText);
        compare(card.Drag.mimeData["text/uri-list"], mockController.selectedUriList);
    }

    function test_sortActionsRouteToController() {
        const sortType = findChild(browsePage, "sortByTypeAction");
        const sortSize = findChild(browsePage, "sortBySizeAction");
        const sortModified = findChild(browsePage, "sortByModifiedAction");
        verify(sortType !== null);
        verify(sortSize !== null);
        verify(sortModified !== null);
        sortType.triggered();
        compare(mockController.sortMode, 3);
        sortSize.triggered();
        compare(mockController.sortMode, 2);
        sortModified.triggered();
        compare(mockController.sortMode, 1);
    }

    function test_renameAndTrashConfirmationsAreQmlDialogs() {
        mockController.clearSelection();
        mockController.selectPath(mockController.thumbnails.get(0).path, false, false);

        const renameDialog = findChild(browsePage, "renameDialog");
        verify(renameDialog !== null);
        mockController.renameSelected();
        tryCompare(renameDialog, "opened", true);
        compare(renameDialog.inputText, "XAG040_0001.JPG");
        renameDialog.close();

        const trashDialog = findChild(browsePage, "trashConfirmationDialog");
        verify(trashDialog !== null);
        mockController.moveSelectedToTrash();
        tryCompare(trashDialog, "opened", true);
        verify(trashDialog.message.indexOf("selected item") >= 0);
        trashDialog.close();
    }

    function test_propertiesAndRawCardsCanBeDraggedByTheirHeaders() {
        const propertiesDialog = findChild(browsePage, "imagePropertiesDialog")
        const propertiesHeader = findChild(propertiesDialog, "imagePropertiesDragHeader")
        const contactSheet = findChild(browsePage, "paneContactSheet-0")
        const galleryStrip = findChild(browsePage, "galleryStrip")
        verify(propertiesDialog !== null)
        verify(propertiesHeader !== null)
        verify(contactSheet !== null)
        verify(galleryStrip !== null)
        compare(contactSheet.interactive, true)
        compare(galleryStrip.interactive, true)
        propertiesDialog.openForPath(mockController.thumbnails.get(0).path)
        tryCompare(propertiesDialog, "opened", true)
        compare(contactSheet.interactive, false)
        compare(galleryStrip.interactive, false)
        const contactSheetY = contactSheet.contentY
        const galleryStripY = galleryStrip.contentY
        const propertiesX = propertiesDialog.x
        const propertiesY = propertiesDialog.y
        mouseDrag(propertiesHeader, propertiesHeader.width / 2, propertiesHeader.height / 2,
                  80, 45, Qt.LeftButton)
        verify(propertiesDialog.x > propertiesX)
        verify(propertiesDialog.y > propertiesY)
        compare(contactSheet.contentY, contactSheetY)
        compare(galleryStrip.contentY, galleryStripY)
        propertiesDialog.close()
        tryCompare(contactSheet, "interactive", true)
        tryCompare(galleryStrip, "interactive", true)

        const rawDialog = findChild(browsePage, "rawParametersDialog")
        const rawHeader = findChild(rawDialog, "rawParametersDragHeader")
        verify(rawDialog !== null)
        verify(rawHeader !== null)
        rawDialog.openForPath("/Images/ISP calibration/xag_00001.raw")
        tryCompare(rawDialog, "opened", true)
        compare(contactSheet.interactive, false)
        compare(galleryStrip.interactive, false)
        const rawX = rawDialog.x
        const rawY = rawDialog.y
        mouseDrag(rawHeader, rawHeader.width / 2, rawHeader.height / 2,
                  -80, 45, Qt.LeftButton)
        verify(rawDialog.x < rawX)
        verify(rawDialog.y > rawY)
        rawDialog.close()
        tryCompare(contactSheet, "interactive", true)
        tryCompare(galleryStrip, "interactive", true)
    }
}
