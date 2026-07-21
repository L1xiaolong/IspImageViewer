// qmllint disable unqualified
import QtQuick
import QtTest
import "../../design"
import "../../src/qml/Pages"

TestCase {
    id: testCase
    name: "WorkspaceInteractions"
    when: windowShown
    width: 1440
    height: 900
    visible: true

    MockBrowseWorkspace {
        id: workspace
        paneCount: 4
    }

    BrowsePage {
        id: page
        anchors.fill: parent
        controller: workspace.activePane
        workspaceController: workspace
        designMode: true
        iconPrefix: Qt.resolvedUrl("../../assets/icons/ui/").toString()
    }

    function init() {
        workspace.paneOrder = workspace.allPanes.slice();
        workspace.paneCount = 4;
        workspace.activePaneIndex = 0;
        workspace.pane0.setDisplayMode(0);
        wait(120);
    }

    function test_fourPanesUseEqualTwoByTwoGrid() {
        const first = findChild(page, "browserPane-0");
        const second = findChild(page, "browserPane-1");
        const third = findChild(page, "browserPane-2");
        const fourth = findChild(page, "browserPane-3");
        verify(first && second && third && fourth);
        verify(Math.abs(first.width - second.width) < 2);
        verify(Math.abs(third.width - fourth.width) < 2);
        verify(Math.abs(first.height - third.height) < 2);
    }

    function test_threePanesUseEqualHorizontalColumns() {
        workspace.paneCount = 3;
        wait(120);
        const first = findChild(page, "browserPane-0");
        const second = findChild(page, "browserPane-1");
        const third = findChild(page, "browserPane-2");
        verify(first && second && third);
        verify(Math.abs(first.width - second.width) < 2);
        verify(Math.abs(second.width - third.width) < 2);
    }

    function test_splitterDragAndDoubleClickReset() {
        workspace.paneCount = 2;
        wait(120);
        const first = findChild(page, "browserPane-0");
        const second = findChild(page, "browserPane-1");
        const handle = findChild(page, "horizontalPaneHandle");
        verify(first && second && handle);
        mouseDrag(handle, handle.width / 2, handle.height / 2, 100, 0, Qt.LeftButton);
        verify(Math.abs(first.width - second.width) > 100);
        mouseDoubleClick(handle, handle.width / 2, handle.height / 2, Qt.LeftButton);
        wait(40);
        verify(Math.abs(first.width - second.width) < 2);
    }

    function test_closeAllPanesShowsEmptyWorkspace() {
        workspace.closePane(3);
        workspace.closePane(2);
        workspace.closePane(1);
        workspace.closePane(0);
        wait(120);
        compare(workspace.paneCount, 0);
        verify(!workspace.hasActivePane);
        verify(findChild(page, "browserPane-0") === null);
    }
}
