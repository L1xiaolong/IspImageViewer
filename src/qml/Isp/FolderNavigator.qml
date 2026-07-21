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

    function revealCurrentFolder() {
        if (root.designMode || root.controller.currentFolderTreeIndex === undefined)
            return
        folderTree.expandToIndex(root.controller.currentFolderTreeIndex)
    }

    Timer {
        id: treeRevealTimer
        interval: 180
        repeat: true
        property int remaining: 0
        onTriggered: {
            root.revealCurrentFolder()
            remaining -= 1
            if (remaining <= 0)
                stop()
        }
    }

    Connections {
        target: root.controller
        function onCurrentDirectoryChanged() {
            root.revealCurrentFolder()
            treeRevealTimer.remaining = 3
            treeRevealTimer.restart()
        }
    }

    Component.onCompleted: {
        root.revealCurrentFolder()
        treeRevealTimer.remaining = 3
        treeRevealTimer.start()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Column {
                id: navigationColumn
                width: root.width - 24
                spacing: 2

                Text {
                    text: "RECENT"
                    color: Theme.mutedInk
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    leftPadding: 6
                    topPadding: 8
                    bottomPadding: 4
                }
                Repeater {
                    model: root.browsingEnabled ? root.controller.recentFolders.slice(0, 4) : []
                    delegate: Rectangle {
                        required property string modelData
                        width: navigationColumn.width
                        height: 30
                        radius: Theme.radius
                        color: recentMouse.containsMouse ? Theme.softHover : "transparent"
                        Image {
                            x: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18
                            height: 18
                            source: root.iconPrefix + "folder.svg"
                        }
                        Text {
                            x: 32
                            width: parent.width - 38
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.split(/[\\/]/).pop()
                            elide: Text.ElideMiddle
                            color: Theme.graphiteInk
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: recentMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.controller.openDirectory(modelData)
                        }
                    }
                }
                Text {
                    text: "PROJECT TREE"
                    color: Theme.mutedInk
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    leftPadding: 6
                    topPadding: 12
                    bottomPadding: 4
                }

                TreeView {
                    id: folderTree
                    width: root.width - 24
                    height: Math.max(280, contentHeight)
                    model: root.controller.folderTree
                    rootIndex: root.designMode ? undefined : root.controller.folderRootIndex
                    columnWidthProvider: function (column) {
                        return width;
                    }
                    boundsBehavior: Flickable.StopAtBounds
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
                        implicitHeight: 28

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius
                            color: filePath === root.controller.currentDirectory ? Theme.softHover : treeMouse.containsMouse ? Theme.sensorWhite : "transparent"
                        }
                        Text {
                            x: 5 + treeDelegate.depth * 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: treeDelegate.hasChildren ? (treeDelegate.expanded ? "⌄" : "›") : ""
                            color: Theme.mutedInk
                            font.pixelSize: 14
                        }
                        Image {
                            x: 20 + treeDelegate.depth * 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: 17
                            height: 17
                            source: root.iconPrefix + "folder.svg"
                        }
                        Text {
                            x: 42 + treeDelegate.depth * 14
                            width: parent.width - x - 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: treeDelegate.display
                            elide: Text.ElideMiddle
                            color: Theme.graphiteInk
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: treeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: function (mouse) {
                                if (treeDelegate.hasChildren && mouse.x < 38 + treeDelegate.depth * 14)
                                    treeDelegate.treeView.toggleExpanded(treeDelegate.row);
                                root.controller.openDirectory(treeDelegate.filePath);
                            }
                            onDoubleClicked: if (treeDelegate.hasChildren)
                                treeDelegate.treeView.toggleExpanded(treeDelegate.row)
                        }
                    }
                }
            }
        }
    }
}
