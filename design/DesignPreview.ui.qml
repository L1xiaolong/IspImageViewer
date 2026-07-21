import QtQuick
import Pages 1.0

// Design Studio composition root.
// This file contains no duplicated visual implementation: every visible item
// comes from the production BrowsePage and its shared Isp components.
Item {
    id: root
    width: 1440
    height: 900

    MockBrowseWorkspace {
        id: mockWorkspace
    }

    BrowsePage {
        id: productionBrowsePage
        anchors.fill: parent
        controller: mockWorkspace.activePane
        workspaceController: mockWorkspace
        designMode: true
        iconPrefix: Qt.resolvedUrl("../assets/icons/ui/").toString()
    }
}
