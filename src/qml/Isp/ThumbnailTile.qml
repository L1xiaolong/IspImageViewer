pragma ComponentBehavior: Bound
// qmllint disable unqualified
import QtQuick
import "."

Item {
    id: root
    required property var controller
    property var workspaceController: controller
    property string path
    property string fileName
    property string technicalLabel
    property url thumbnailUrl
    property bool directory: false
    property bool selected: false
    property int selectionOrdinal: 0
    property int displayMode: 0
    signal activated
    signal selectionRequested(bool extend, bool toggle)

    Drag.active: tileDragHandler.active && root.selected
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.mimeData: ({ "text/uri-list": root.controller.selectedUriList })
    Drag.imageSource: root.thumbnailUrl
    Drag.hotSpot.x: Math.min(root.width / 2, 80)
    Drag.hotSpot.y: Math.min(root.height / 2, 60)

    width: GridView.view ? GridView.view.cellWidth - 16 : 196
    height: displayMode === 1 ? 68 : width * 0.75 + (displayMode === 2 ? 40 : 66)

    Loader {
        anchors.fill: parent
        sourceComponent: root.displayMode === 1 ? listVisual : gridVisual
    }

    Component {
        id: gridVisual
        Item {
            Rectangle {
                anchors.fill: parent
                color: tileMouse.containsMouse ? Theme.paperWhite : "transparent"
                radius: Theme.radius
                border.width: 1
                border.color: tileMouse.containsMouse ? Theme.opticalGray : "transparent"
            }
            Rectangle {
                id: imageWell
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 8
                height: Math.round(width * 0.75)
                color: Theme.softHover
                border.color: Theme.opticalGray
                border.width: 1
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: root.directory ? 28 : 0
                    source: root.thumbnailUrl
                    asynchronous: true
                    cache: true
                    sourceSize: Qt.size(Math.max(320, width * 2), Math.max(240, height * 2))
                    fillMode: root.directory ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                }
            }
            Text {
                id: gridName
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: imageWell.bottom
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 8
                text: root.fileName
                elide: Text.ElideMiddle
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
            }
            Text {
                visible: root.displayMode !== 2
                anchors.left: gridName.left
                anchors.right: gridName.right
                anchors.top: gridName.bottom
                anchors.topMargin: 4
                text: root.technicalLabel
                elide: Text.ElideRight
                color: Theme.mutedInk
                font.family: Theme.monoFont
                font.pixelSize: 11
            }
        }
    }

    Component {
        id: listVisual
        Item {
            Rectangle {
                anchors.fill: parent
                color: tileMouse.containsMouse ? Theme.paperWhite : "transparent"
                radius: Theme.radius
                border.width: 1
                border.color: tileMouse.containsMouse ? Theme.opticalGray : "transparent"
            }
            Rectangle {
                id: listImageWell
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 72
                height: 52
                color: Theme.softHover
                border.color: Theme.opticalGray
                border.width: 1
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: root.directory ? 14 : 0
                    source: root.thumbnailUrl
                    asynchronous: true
                    cache: true
                    sourceSize: Qt.size(240, 160)
                    fillMode: root.directory ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                }
            }
            Text {
                id: listName
                anchors.left: listImageWell.right
                anchors.leftMargin: 14
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.top: parent.top
                anchors.topMargin: 15
                text: root.fileName
                elide: Text.ElideMiddle
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                anchors.left: listName.left
                anchors.right: listName.right
                anchors.top: listName.bottom
                anchors.topMargin: 5
                text: root.technicalLabel
                elide: Text.ElideRight
                color: Theme.mutedInk
                font.family: Theme.monoFont
                font.pixelSize: 11
            }
        }
    }

    Rectangle {
        visible: root.selected
        width: 2
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: Theme.probeBlue
    }

    Rectangle {
        visible: root.selected
        x: root.displayMode === 1 ? 12 : 14
        y: root.displayMode === 1 ? 10 : 14
        width: 18
        height: 18
        radius: 9
        color: Theme.probeBlue
        Text {
            anchors.centerIn: parent
            text: root.selectionOrdinal > 0 ? root.selectionOrdinal : "✓"
            color: "white"
            font.family: Theme.monoFont
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        id: tileMouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton) {
                if (!root.selected)
                    root.selectionRequested(false, false);
                if (root.directory)
                    folderContextMenu.popup();
                else if (root.path.toLowerCase().endsWith(".raw") ||
                         root.path.toLowerCase().endsWith(".yuv"))
                    rawContextMenu.popup();
                else
                    imageContextMenu.popup();
                return;
            }
            const toggle = (mouse.modifiers & Qt.ControlModifier) || (mouse.modifiers & Qt.MetaModifier);
            const extend = (mouse.modifiers & Qt.ShiftModifier);
            root.selectionRequested(extend, toggle);
        }
        onDoubleClicked: root.activated()
    }

    DragHandler {
        id: tileDragHandler
        acceptedButtons: Qt.LeftButton
        target: null
        enabled: root.path.length > 0
        onActiveChanged: {
            if (active && !root.selected)
                root.selectionRequested(false, false);
        }
    }

    AppMenu {
        id: imageContextMenu
        AppMenuItem {
            text: "Open full screen"
            onTriggered: root.activated()
        }
        AppMenuItem {
            text: "Compare selected"
            enabled: root.workspaceController.canCompare
            onTriggered: root.workspaceController.compareSelected()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Cut"
            shortcutText: Qt.platform.os === "osx" ? "⌘X" : "Ctrl+X"
            onTriggered: root.controller.copySelected(true)
        }
        AppMenuItem {
            text: "Copy"
            shortcutText: Qt.platform.os === "osx" ? "⌘C" : "Ctrl+C"
            onTriggered: root.controller.copySelected(false)
        }
        AppMenuItem {
            text: "Rename…"
            enabled: root.controller.selectionCount === 1
            onTriggered: root.controller.renameSelected()
        }
        AppMenuItem {
            text: "Move to Trash"
            destructive: true
            onTriggered: root.controller.moveSelectedToTrash()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Reveal in Finder / Explorer"
            enabled: root.controller.selectionCount === 1
            onTriggered: root.controller.revealSelected()
        }
        AppMenuItem {
            text: "Properties"
            enabled: root.controller.selectionCount === 1
            onTriggered: root.controller.showSelectedProperties()
        }
    }

    AppMenu {
        id: rawContextMenu
        AppMenuItem { text: "Open full screen"; onTriggered: root.activated() }
        AppMenuItem {
            text: "Compare selected"
            enabled: root.workspaceController.canCompare
            onTriggered: root.workspaceController.compareSelected()
        }
        AppMenuItem {
            text: "RAW/YUV parameters…"
            enabled: root.controller.canEditRaw
            onTriggered: root.controller.editSelectedRawParameters()
        }
        AppMenuSeparator {}
        AppMenuItem { text: "Cut"; shortcutText: Qt.platform.os === "osx" ? "⌘X" : "Ctrl+X"; onTriggered: root.controller.copySelected(true) }
        AppMenuItem { text: "Copy"; shortcutText: Qt.platform.os === "osx" ? "⌘C" : "Ctrl+C"; onTriggered: root.controller.copySelected(false) }
        AppMenuItem { text: "Rename…"; enabled: root.controller.selectionCount === 1; onTriggered: root.controller.renameSelected() }
        AppMenuItem { text: "Move to Trash"; destructive: true; onTriggered: root.controller.moveSelectedToTrash() }
        AppMenuSeparator {}
        AppMenuItem { text: "Reveal in Finder / Explorer"; enabled: root.controller.selectionCount === 1; onTriggered: root.controller.revealSelected() }
        AppMenuItem { text: "Properties"; enabled: root.controller.selectionCount === 1; onTriggered: root.controller.showSelectedProperties() }
    }

    AppMenu {
        id: folderContextMenu
        AppMenuItem { text: "Open folder"; onTriggered: root.activated() }
        AppMenuItem { text: "Paste into folder"; enabled: root.controller.canPaste; onTriggered: root.controller.pasteItemsInto(root.path) }
        AppMenuSeparator {}
        AppMenuItem { text: "Cut"; shortcutText: Qt.platform.os === "osx" ? "⌘X" : "Ctrl+X"; onTriggered: root.controller.copySelected(true) }
        AppMenuItem { text: "Copy"; shortcutText: Qt.platform.os === "osx" ? "⌘C" : "Ctrl+C"; onTriggered: root.controller.copySelected(false) }
        AppMenuItem { text: "Rename…"; enabled: root.controller.selectionCount === 1; onTriggered: root.controller.renameSelected() }
        AppMenuItem { text: "Move to Trash"; destructive: true; onTriggered: root.controller.moveSelectedToTrash() }
        AppMenuSeparator {}
        AppMenuItem { text: "Reveal in Finder / Explorer"; enabled: root.controller.selectionCount === 1; onTriggered: root.controller.revealSelected() }
        AppMenuItem { text: "Properties"; enabled: root.controller.selectionCount === 1; onTriggered: root.controller.showSelectedProperties() }
    }
}
