#include "io/qt_image_decoder.h"
#include "qml/browse_controller.h"
#include "qml/browse_workspace_controller.h"

#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace ispview {
namespace {

BrowseController* paneAt(const BrowseWorkspaceController& workspace, int index) {
    const QVariantList panes = workspace.panes();
    return qobject_cast<BrowseController*>(panes.at(index).value<QObject*>());
}

QString createImage(QTemporaryDir& directory, const QString& name) {
    const QString path = directory.filePath(name);
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    if (!image.save(path)) return {};
    return path;
}

} // namespace

class QmlWorkspaceControllerTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void addsActivatesAndClosesZeroToFourPanes();
    void keepsPaneStateIndependent();
    void aggregatesUniqueSelectionsInStableOrder();
};

void QmlWorkspaceControllerTests::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("ISPViewTests"));
    QCoreApplication::setApplicationName(QStringLiteral("QmlWorkspaceControllerTests"));
    QSettings().clear();
}

void QmlWorkspaceControllerTests::addsActivatesAndClosesZeroToFourPanes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), directory.path());

    QCOMPARE(workspace.paneCount(), 1);
    QCOMPARE(workspace.activePaneIndex(), 0);
    QVERIFY(workspace.hasActivePane());
    paneAt(workspace, 0)->setDisplayMode(2);

    workspace.addFileManagerPane();
    QCOMPARE(workspace.paneCount(), 2);
    QCOMPARE(workspace.activePaneIndex(), 0);
    QCOMPARE(paneAt(workspace, 0)->displayMode(), 0);
    QVERIFY(paneAt(workspace, 1)->currentDirectory().isEmpty());

    workspace.addFileManagerPane();
    workspace.addFileManagerPane();
    QCOMPARE(workspace.paneCount(), 4);
    QVERIFY(!workspace.canAddPane());
    workspace.addFileManagerPane();
    QCOMPARE(workspace.paneCount(), 4);

    workspace.activatePane(2);
    QCOMPARE(workspace.activePaneIndex(), 2);
    QSignalSpy activePaneSpy(&workspace, &BrowseWorkspaceController::activePaneChanged);
    workspace.closePane(1);
    QCOMPARE(workspace.paneCount(), 3);
    QCOMPARE(workspace.activePaneIndex(), 1);
    QCOMPARE(activePaneSpy.size(), 1);
    workspace.closePane(1);
    QCOMPARE(workspace.activePaneIndex(), 1);
    workspace.closePane(1);
    QCOMPARE(workspace.activePaneIndex(), 0);
    workspace.closePane(0);
    QCOMPARE(workspace.paneCount(), 0);
    QCOMPARE(workspace.activePaneIndex(), -1);
    QVERIFY(!workspace.hasActivePane());

    workspace.addFileManagerPane();
    QCOMPARE(workspace.paneCount(), 1);
    QCOMPARE(workspace.activePaneIndex(), 0);
    QVERIFY(paneAt(workspace, 0)->currentDirectory().isEmpty());
}

void QmlWorkspaceControllerTests::keepsPaneStateIndependent() {
    QTemporaryDir firstDirectory;
    QTemporaryDir secondDirectory;
    QVERIFY(firstDirectory.isValid());
    QVERIFY(secondDirectory.isValid());
    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), firstDirectory.path());
    workspace.addFileManagerPane();

    BrowseController* first = paneAt(workspace, 0);
    BrowseController* second = paneAt(workspace, 1);
    QCOMPARE(first->loader(), second->loader());
    QCOMPARE(first->folderTree(), second->folderTree());
    second->openDirectory(secondDirectory.path());
    first->setFilterText(QStringLiteral("first"));
    first->setSortMode(2);
    second->setFilterText(QStringLiteral("second"));
    second->setSortMode(3);
    second->setDisplayMode(1);

    QCOMPARE(first->currentDirectory(), firstDirectory.path());
    QCOMPARE(second->currentDirectory(), secondDirectory.path());
    QCOMPARE(first->filterText(), QStringLiteral("first"));
    QCOMPARE(second->filterText(), QStringLiteral("second"));
    QCOMPARE(first->sortMode(), 2);
    QCOMPARE(second->sortMode(), 3);
    QCOMPARE(first->displayMode(), 0);
    QCOMPARE(second->displayMode(), 1);

    first->setSharedRecentFolders({firstDirectory.path()});
    QCOMPARE(second->recentFolders(), QStringList({firstDirectory.path()}));
}

void QmlWorkspaceControllerTests::aggregatesUniqueSelectionsInStableOrder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString a = createImage(directory, QStringLiteral("a.png"));
    const QString b = createImage(directory, QStringLiteral("b.png"));
    const QString c = createImage(directory, QStringLiteral("c.png"));
    const QString d = createImage(directory, QStringLiteral("d.png"));
    const QString e = createImage(directory, QStringLiteral("e.png"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty() && !d.isEmpty() && !e.isEmpty());

    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), directory.path());
    workspace.addFileManagerPane();
    BrowseController* first = paneAt(workspace, 0);
    BrowseController* second = paneAt(workspace, 1);

    first->selectPath(a);
    first->selectPath(b, false, true);
    second->selectPath(b);
    second->selectPath(c, false, true);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({a, b, c}));
    QCOMPARE(workspace.workspaceSelectionCount(), 3);
    QVERIFY(workspace.canCompare());

    QSignalSpy compareSpy(&workspace, &BrowseWorkspaceController::compareRequested);
    workspace.compareSelected();
    QCOMPARE(compareSpy.size(), 1);
    QCOMPARE(compareSpy.constFirst().constFirst().toStringList(), QStringList({a, b, c}));

    second->selectPath(d, false, true);
    second->selectPath(e, false, true);
    QCOMPARE(workspace.workspaceSelectionCount(), 5);
    QVERIFY(!workspace.canCompare());
    workspace.compareSelected();
    QCOMPARE(compareSpy.size(), 1);

    workspace.closePane(1);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({a, b}));
    QVERIFY(workspace.canCompare());
}

} // namespace ispview

QTEST_MAIN(ispview::QmlWorkspaceControllerTests)
#include "qml_workspace_controller_tests.moc"
