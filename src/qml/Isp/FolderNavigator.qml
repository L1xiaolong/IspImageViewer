pragma ComponentBehavior: Bound
// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

Rectangle {
    id: root
    required property var controller
    property bool designMode: false
    property bool browsingEnabled: true
    property string iconPrefix: "qrc:/icons/ui/"
    color: Theme.paperWhite
    enabled: browsingEnabled
    opacity: browsingEnabled ? 1 : 0.45
    border.color: Theme.opticalGray
    border.width: 0
    clip: true

    // ── VS Code-style constants ──
    readonly property int indentWidth: 16
    readonly property int itemHeight: 24
    readonly property int iconSize_: 16
    readonly property string chevronDown: "⌄"
    readonly property string chevronRight: "›"
    readonly property color indentGuideColor: "#D5D9DC"
    readonly property color selectionBg: "#E4E6F1"
    readonly property color hoverBg: Theme.softHover

    function revealCurrentFolder() {
        if (root.designMode || root.controller.currentDirectory.length === 0 ||
                root.controller.currentFolderTreeIndex === undefined)
            return
        folderTree.expandToIndex(root.controller.currentFolderTreeIndex)
    }

    Connections {
        target: root.controller
        function onCurrentDirectoryChanged() { root.revealCurrentFolder() }
    }

    Connections {
        target: root.designMode ? null : root.controller.folderTree
        function onDirectoryLoaded(path) {
            if (root.controller.currentDirectory.startsWith(path))
                root.revealCurrentFolder()
        }
    }

    Component.onCompleted: root.revealCurrentFolder()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // ── RECENT section header ──
        Rectangle {
            Layout.fillWidth: true
            height: 32
            color: "transparent"
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "RECENT"
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8
            }
        }
        Repeater {
            model: root.browsingEnabled ? root.controller.recentFolders.slice(0, 4) : []
            delegate: Rectangle {
                required property string modelData
                width: parent ? parent.width : 0
                height: root.itemHeight
                color: recentMouse.containsMouse ? root.hoverBg : "transparent"
                Image {
                    x: 20
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.iconSize_
                    height: root.iconSize_
                    source: root.iconPrefix + "folder.svg"
                    sourceSize: Qt.size(32, 32)
                }
                Text {
                    x: 42
                    width: parent.width - 50
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.split(/[\\/]/).pop()
                    elide: Text.ElideMiddle
                    color: Theme.graphiteInk
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: recentMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.controller.openDirectory(modelData)
                }
            }
        }

        // ── Spacing between sections ──
        Item { Layout.fillWidth: true; height: 10 }

        // ── PROJECT TREE section header ──
        Rectangle {
            Layout.fillWidth: true
            height: 32
            color: "transparent"
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "EXPLORER"
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8
            }
        }

        // ── VS Code-style file tree ──
        TreeView {
            id: folderTree
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.controller.folderTree
            rootIndex: root.designMode ? undefined : root.controller.folderRootIndex
            columnWidthProvider: function (column) { return width; }
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            delegate: Item {
                id: treeDelegate
                required property TreeView treeView
                required property bool isTreeNode
                required property bool expanded
                required property bool hasChildren
                required property int depth
                required property int row
                required property int column
                required property string display
                required property string filePath

                implicitWidth: folderTree.width
                implicitHeight: root.itemHeight

                // A directory node has children or is marked as a tree node by the model.
                readonly property bool isDirectory: treeDelegate.hasChildren || treeDelegate.isTreeNode
                readonly property bool isCurrentFolder: filePath === root.controller.currentDirectory

                // ── Background ──
                Rectangle {
                    anchors.fill: parent
                    color: treeDelegate.isCurrentFolder ? root.selectionBg
                           : treeMouse.containsMouse ? root.hoverBg : "transparent"
                }

                // ── Indent guides ──
                Row {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    Repeater {
                        model: treeDelegate.depth
                        delegate: Rectangle {
                            width: root.indentWidth
                            height: parent.height
                            color: "transparent"
                            // Vertical indent guide line on the right edge of each indent cell
                            Rectangle {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 1
                                color: root.indentGuideColor
                                opacity: 0.55
                            }
                        }
                    }
                }

                // ── Expand/collapse chevron ──
                Text {
                    x: treeDelegate.depth * root.indentWidth + 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.indentWidth
                    height: root.itemHeight
                    text: treeDelegate.isDirectory
                          ? (treeDelegate.expanded ? root.chevronDown : root.chevronRight)
                          : ""
                    color: Theme.mutedInk
                    font.pixelSize: 13
                    font.family: Theme.uiFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                // ── Icon ──
                Image {
                    x: treeDelegate.depth * root.indentWidth + root.indentWidth + 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.iconSize_
                    height: root.iconSize_
                    source: treeDelegate.isDirectory
                            ? root.iconPrefix + "folder.svg"
                            : root.iconPrefix + "actual-size.svg"
                    sourceSize: Qt.size(32, 32)
                    opacity: treeDelegate.isDirectory ? 0.8 : 0.65
                }

                // ── Label ──
                Text {
                    x: treeDelegate.depth * root.indentWidth + root.indentWidth + root.iconSize_ + 10
                    width: Math.max(0, parent.width - x - 10)
                    anchors.verticalCenter: parent.verticalCenter
                    text: treeDelegate.display
                    elide: Text.ElideRight
                    color: Theme.graphiteInk
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    font.weight: treeDelegate.isDirectory ? Font.DemiBold : Font.Normal
                }

                // ── Interaction ──
                MouseArea {
                    id: treeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function (mouse) {
                        if (mouse.button === Qt.RightButton) {
                            // Right-click: expand/collapse toggle for directories
                            if (treeDelegate.isDirectory) {
                                treeDelegate.treeView.toggleExpanded(treeDelegate.row);
                                root.controller.openDirectory(treeDelegate.filePath);
                            }
                            return;
                        }
                        // Left-click on chevron area: toggle expand and navigate
                        const chevronRightEdge = (treeDelegate.depth + 1) * root.indentWidth;
                        if (treeDelegate.isDirectory && mouse.x < chevronRightEdge + 4) {
                            treeDelegate.treeView.toggleExpanded(treeDelegate.row);
                            root.controller.openDirectory(treeDelegate.filePath);
                            return;
                        }
                        // Left-click on item body: activate
                        root.controller.activateTreeItem(treeDelegate.filePath);
                    }
                    onDoubleClicked: {
                        if (treeDelegate.isDirectory) {
                            treeDelegate.treeView.toggleExpanded(treeDelegate.row);
                            root.controller.openDirectory(treeDelegate.filePath);
                        }
                    }
                }
            }
        }
    }
}
