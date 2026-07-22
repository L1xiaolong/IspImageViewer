import QtQuick

QtObject {
    id: root

    property int gridCellWidth: 196
    property string filterText: ""
    property int sortMode: 0
    property int displayMode: 0
    property bool canGoBack: true
    property bool canGoForward: false
    property bool canGoUp: true
    property bool canPaste: true
    readonly property bool canCompare: selectionCount >= 2 && selectionCount <= 4
    property string currentDirectory: "/Images/ISP calibration"
    property string currentFolderName: "ISP calibration"
    property var currentFolderTreeIndex: undefined
    property var recentFolders: ["/Images/ISP calibration", "/Images/Outdoor samples"]
    property var selectedPaths: ["/Images/ISP calibration/XAG040_0001.JPG", "/Images/ISP calibration/XAG040_0002.JPG"]
    property var selectedFileUrls: selectedPaths.map(path => "file://" + path)
    property string selectedUriList: selectedFileUrls.join("\r\n")
    property int selectionCount: selectedPaths.length
    property string statusText: "16 items · " + selectionCount + " selected"
    property size galleryImageSize: Qt.size(4000, 3000)
    property bool galleryImageReady: true
    property string galleryInfoText: "4000 × 3000  8-bit  JPG  13.89 MiB"
    property bool canEditRaw: selectionCount === 1 &&
                              (selectedPaths[0].toLowerCase().endsWith(".raw") ||
                               selectedPaths[0].toLowerCase().endsWith(".yuv"))

    property ListModel thumbnails: ListModel {
        ListElement {
            path: "/Images/ISP calibration/XAG040_0001.JPG"
            fileName: "XAG040_0001.JPG"
            technicalLabel: "4000×3000 · JPG · 14,221 KB"
            thumbnailUrl: "../../../test_images/XAG040_0001.JPG"
            isDirectory: false
            isSelected: true
            selectionOrdinal: 1
        }
        ListElement {
            path: "/Images/ISP calibration/XAG040_0002.JPG"
            fileName: "XAG040_0002.JPG"
            technicalLabel: "4000×3000 · JPG · 4,262 KB"
            thumbnailUrl: "../../../test_images/XAG040_0002.JPG"
            isDirectory: false
            isSelected: true
            selectionOrdinal: 2
        }
        ListElement {
            path: "/Images/ISP calibration/XAG040_0003.JPG"
            fileName: "XAG040_0003.JPG"
            technicalLabel: "4000×3000 · JPG · 4,226 KB"
            thumbnailUrl: "../../../test_images/XAG040_0003.JPG"
            isDirectory: false
            isSelected: false
            selectionOrdinal: 0
        }
        ListElement {
            path: "/Images/ISP calibration/vlcsnap-27.png"
            fileName: "vlcsnap-2026-07-14-18h37m27s260.png"
            technicalLabel: "4000×3000 · PNG · 11,353 KB"
            thumbnailUrl: "../../../test_images/vlcsnap-2026-07-14-18h37m27s260.png"
            isDirectory: false
            isSelected: false
            selectionOrdinal: 0
        }
        ListElement {
            path: "/Images/ISP calibration/vlcsnap-36.png"
            fileName: "vlcsnap-2026-07-14-18h37m36s061.png"
            technicalLabel: "4000×3000 · PNG · 11,109 KB"
            thumbnailUrl: "../../../test_images/vlcsnap-2026-07-14-18h37m36s061.png"
            isDirectory: false
            isSelected: false
            selectionOrdinal: 0
        }
        ListElement {
            path: "/Images/ISP calibration/0-IMG_20230301_0011.JPG"
            fileName: "0-IMG_20230301_0011.JPG"
            technicalLabel: "6144×4096 · JPG · 2,459 KB"
            thumbnailUrl: "../../../test_images/0-IMG_20230301_0011.JPG"
            isDirectory: false
            isSelected: false
            selectionOrdinal: 0
        }
        ListElement {
            path: "/Images/ISP calibration/img.dng"
            fileName: "img.dng"
            technicalLabel: "5464×3070 · DNG · 24,572 KB"
            thumbnailUrl: "../../../test_images/res.png"
            isDirectory: false
            isSelected: false
            selectionOrdinal: 0
        }
        ListElement {
            path: "/Images/ISP calibration/2.png"
            fileName: "2.png"
            technicalLabel: "4000×3000 · PNG · 9,713 KB"
            thumbnailUrl: "../../../test_images/2.png"
            isDirectory: false
            isSelected: false
            selectionOrdinal: 0
        }
        ListElement {
            path: "/Images/ISP calibration/res.png"
            fileName: "res.png"
            technicalLabel: "4000×3000 · PNG · 13,193 KB"
            thumbnailUrl: "../../../test_images/res.png"
            isDirectory: false
            isSelected: false
            selectionOrdinal: 0
        }
        ListElement {
            path: "/Images/ISP calibration/xag_00001.raw"
            fileName: "xag_00001.raw"
            technicalLabel: "6236×4178 · RAW · 50,886 KB"
            thumbnailUrl: "../../../test_images/2.png"
            isDirectory: false
            isSelected: false
            selectionOrdinal: 0
        }
    }

    property ListModel folderTree: ListModel {
        ListElement {
            display: "ISP calibration"
            filePath: "/Images/ISP calibration"
        }
        ListElement {
            display: "Outdoor samples"
            filePath: "/Images/Outdoor samples"
        }
        ListElement {
            display: "Reference charts"
            filePath: "/Images/Reference charts"
        }
        ListElement {
            display: "RAW captures"
            filePath: "/Images/RAW captures"
        }
    }

    function setFilterText(text) {
        filterText = text;
        statusText = text.length > 0 ? "Filtered by ‘" + text + "’ · " + selectionCount + " selected" : "16 items · " + selectionCount + " selected";
    }

    function setSortMode(mode) {
        sortMode = mode;
    }

    function setDisplayMode(mode) {
        displayMode = Math.max(0, Math.min(2, mode));
    }

    function setWorkspaceSelectionOrder(paths) {
        for (let index = 0; index < thumbnails.count; ++index) {
            const ordinal = paths.indexOf(thumbnails.get(index).path) + 1;
            thumbnails.setProperty(index, "selectionOrdinal", ordinal);
        }
    }

    function selectPath(path, extend, toggle) {
        let paths = extend || toggle ? selectedPaths.slice() : [];
        const existing = paths.indexOf(path);
        if (toggle && existing >= 0)
            paths.splice(existing, 1);
        else if (existing < 0)
            paths.push(path);
        selectedPaths = paths;
        for (let index = 0; index < thumbnails.count; ++index) {
            const ordinal = paths.indexOf(thumbnails.get(index).path) + 1;
            thumbnails.setProperty(index, "isSelected", ordinal > 0);
            thumbnails.setProperty(index, "selectionOrdinal", ordinal);
        }
    }

    function clearSelection() {
        selectedPaths = [];
        for (let index = 0; index < thumbnails.count; ++index) {
            thumbnails.setProperty(index, "isSelected", false);
            thumbnails.setProperty(index, "selectionOrdinal", 0);
        }
    }

    function openDirectory(path) {
        currentDirectory = path;
        currentFolderName = path.split("/").pop();
    }
    function chooseDirectory() {
        statusText = "Folder chooser preview";
    }
    function navigateBack() {
        statusText = "Back";
    }
    function navigateForward() {
        statusText = "Forward";
    }
    function navigateUp() {
        statusText = "Parent folder";
    }
    function createFolder(name) {
        if (name.trim().length === 0)
            return "Enter a folder name.";
        statusText = "Created folder “" + name.trim() + "”";
        return "";
    }
    function refresh() {
        statusText = "Refreshed current folder";
    }
    function selectAll() {
        let paths = [];
        for (let index = 0; index < thumbnails.count; ++index)
            paths.push(thumbnails.get(index).path);
        selectedPaths = paths;
        selectionCount = paths.length;
    }
    function openCurrentDirectoryInFileManager() {
        statusText = "Open current folder in file manager";
    }
    function compareSelected() {
        statusText = "Compare " + selectionCount + " selected images";
    }
    function activatePath(path) {
        statusText = "Open " + path.split("/").pop();
    }
    function copySelected(cut) {
        statusText = cut ? "Cut selection" : "Copied selection";
    }
    function pasteItems() {
        statusText = "Paste preview";
    }
    function pasteItemsInto(path) {
        statusText = "Paste into " + path.split("/").pop();
    }
    function copyDroppedUrls(urls) {
        statusText = urls.length + " dropped item(s)";
    }
    function copyDroppedUrlsInto(urls, path) {
        statusText = urls.length + " item(s) dropped into " + String(path).split("/").pop();
    }
    function openDroppedUrls(urls) {
        statusText = urls.length + " dropped item(s) opened";
    }
    function renameSelected() {
        statusText = "Rename preview";
    }
    function moveSelectedToTrash() {
        statusText = "Move to Trash preview";
    }
    function revealSelected() {
        statusText = "Reveal in Finder preview";
    }
    function showSelectedProperties() {
        statusText = "Show selected properties";
    }
    function editSelectedRawParameters() {
        statusText = "Edit RAW/YUV parameters";
    }
    function setGalleryPath(path) {
        galleryImageReady = true;
    }
    function probeGalleryPixel(x, y) {
        return "x " + x + " · y " + y + " · RGBA(32, 64, 96, 255)";
    }
}
