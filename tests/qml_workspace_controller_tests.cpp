#include "io/qt_image_decoder.h"
#include "io/image_loader.h"
#include "qml/browse_controller.h"
#include "qml/browse_workspace_controller.h"
#include "qml/thumbnail_image_provider.h"
#include "ui/thumbnail_model.h"

#include <QImage>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace ispview {
namespace {

class RawParameterColorDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] bool canDecode(const QString&) const override { return true; }

    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override {
        QImage image(8, 8, QImage::Format_RGBA8888);
        image.fill(request.rawParameters && request.rawParameters->size.width() == 4
                       ? QColor(Qt::red) : QColor(Qt::green));
        auto frame = std::make_shared<ImageFrame>();
        frame->descriptor.size = image.size();
        frame->metadata.path = request.path;
        frame->metadata.sourceSize = request.rawParameters
                                         ? request.rawParameters->size : image.size();
        frame->storage = std::move(image);
        return {std::move(frame), {}};
    }
};

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
    void addsActivatesAndClosesOneToFourPanes();
    void keepsPaneStateIndependent();
    void aggregatesUniqueSelectionsInStableOrder();
    void copiesDropsIntoSubfoldersAndAcrossPanes();
    void emptyPaneOpensDroppedFoldersAndImageLocations();
    void rawParametersRefreshEveryPaneAndQmlProvider();
};

void QmlWorkspaceControllerTests::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("ISPViewTests"));
    QCoreApplication::setApplicationName(QStringLiteral("QmlWorkspaceControllerTests"));
    QSettings().clear();
}

void QmlWorkspaceControllerTests::addsActivatesAndClosesOneToFourPanes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), directory.path());

    QCOMPARE(workspace.paneCount(), 1);
    QCOMPARE(workspace.activePaneIndex(), 0);
    QVERIFY(workspace.hasActivePane());
    workspace.setActiveDisplayMode(2);
    QCOMPARE(paneAt(workspace, 0)->displayMode(), 2);

    workspace.addFileManagerPane();
    QCOMPARE(workspace.paneCount(), 2);
    QCOMPARE(workspace.activePaneIndex(), 1);
    QCOMPARE(paneAt(workspace, 0)->displayMode(), 0);
    QCOMPARE(paneAt(workspace, 1)->displayMode(), 0);
    QVERIFY(paneAt(workspace, 1)->currentDirectory().isEmpty());
    workspace.setActiveDisplayMode(2);
    QCOMPARE(paneAt(workspace, 1)->displayMode(), 0);

    workspace.addFileManagerPane();
    QCOMPARE(workspace.activePaneIndex(), 2);
    for (int index = 0; index < workspace.paneCount(); ++index)
        QCOMPARE(paneAt(workspace, index)->displayMode(), 1);
    workspace.setActiveDisplayMode(0);
    QCOMPARE(paneAt(workspace, 2)->displayMode(), 1);
    workspace.addFileManagerPane();
    QCOMPARE(workspace.paneCount(), 4);
    QCOMPARE(workspace.activePaneIndex(), 3);
    for (int index = 0; index < workspace.paneCount(); ++index)
        QCOMPARE(paneAt(workspace, index)->displayMode(), 1);
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
    QCOMPARE(workspace.paneCount(), 1);
    QCOMPARE(workspace.activePaneIndex(), 0);
    QVERIFY(workspace.hasActivePane());
    QVERIFY(paneAt(workspace, 0)->currentDirectory().isEmpty());
    workspace.setActiveDisplayMode(2);
    QCOMPARE(paneAt(workspace, 0)->displayMode(), 2);
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

    workspace.selectPath(0, a);
    workspace.selectPath(0, b, false, true);
    workspace.selectPath(1, b, false, true);
    workspace.selectPath(1, c, false, true);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({a, b, c}));
    QCOMPARE(workspace.workspaceSelectionCount(), 3);
    QVERIFY(workspace.canCompare());

    QSignalSpy compareSpy(&workspace, &BrowseWorkspaceController::compareRequested);
    workspace.compareSelected();
    QCOMPARE(compareSpy.size(), 1);
    QCOMPARE(compareSpy.constFirst().constFirst().toStringList(), QStringList({a, b, c}));

    workspace.selectPath(1, d, false, true);
    workspace.selectPath(1, e, false, true);
    QCOMPARE(workspace.workspaceSelectionCount(), 5);
    QVERIFY(!workspace.canCompare());
    workspace.compareSelected();
    QCOMPARE(compareSpy.size(), 1);

    workspace.closePane(1);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({a, b}));
    QVERIFY(workspace.canCompare());

    workspace.addFileManagerPane();
    workspace.selectPath(1, c, false, false);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({c}));
    workspace.selectPath(0, a, false, true);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({a, c}));
    workspace.selectPath(0, a, false, true);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({c}));
}

void QmlWorkspaceControllerTests::copiesDropsIntoSubfoldersAndAcrossPanes() {
    QTemporaryDir sourceDirectory;
    QTemporaryDir targetDirectory;
    QVERIFY(sourceDirectory.isValid());
    QVERIFY(targetDirectory.isValid());
    const QString source = createImage(sourceDirectory, QStringLiteral("drag.png"));
    const QString child = sourceDirectory.filePath(QStringLiteral("child"));
    QVERIFY(QDir().mkdir(child));

    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(),
                                        sourceDirectory.path());
    BrowseController* first = paneAt(workspace, 0);
    first->copyDroppedUrlsInto({QUrl::fromLocalFile(source)}, child);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(child).filePath(QStringLiteral("drag.png"))),
                             3000);

    workspace.addFileManagerPane();
    BrowseController* second = paneAt(workspace, 1);
    second->openDirectory(targetDirectory.path());
    second->copyDroppedUrls({QUrl::fromLocalFile(source)});
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(targetDirectory.filePath(QStringLiteral("drag.png"))), 3000);
}

void QmlWorkspaceControllerTests::emptyPaneOpensDroppedFoldersAndImageLocations() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString image = createImage(directory, QStringLiteral("open.png"));
    QVERIFY(!image.isEmpty());

    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), directory.path());
    workspace.addFileManagerPane();
    BrowseController* empty = paneAt(workspace, 1);
    QVERIFY(empty->currentDirectory().isEmpty());
    empty->openDroppedUrls({QUrl::fromLocalFile(image)});
    QCOMPARE(empty->currentDirectory(), directory.path());
    QCOMPARE(empty->selectedPaths(), QStringList({image}));

    QTemporaryDir anotherDirectory;
    QVERIFY(anotherDirectory.isValid());
    workspace.addFileManagerPane();
    empty = paneAt(workspace, 2);
    empty->openDroppedUrls({QUrl::fromLocalFile(anotherDirectory.path())});
    QCOMPARE(empty->currentDirectory(), anotherDirectory.path());
}

void QmlWorkspaceControllerTests::rawParametersRefreshEveryPaneAndQmlProvider() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString rawPath = directory.filePath(QStringLiteral("capture.raw"));
    QFile rawFile(rawPath);
    QVERIFY(rawFile.open(QIODevice::WriteOnly));
    QCOMPARE(rawFile.write(QByteArray(256, '\0')), 256);
    rawFile.close();

    auto decoder = std::make_shared<RawParameterColorDecoder>();
    BrowseWorkspaceController workspace(decoder, directory.path());
    workspace.addFileManagerPane();
    paneAt(workspace, 1)->openDirectory(directory.path());

    const auto indexForPath = [&rawPath](BrowseController* pane) {
        QAbstractItemModel* model = pane->thumbnails();
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            if (index.data(ThumbnailModel::PathRole).toString() == rawPath) return index;
        }
        return QModelIndex{};
    };
    QTRY_VERIFY_WITH_TIMEOUT(indexForPath(paneAt(workspace, 0)).isValid(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(indexForPath(paneAt(workspace, 1)).isValid(), 3000);
    const QString beforeFirst =
        indexForPath(paneAt(workspace, 0)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    const QString beforeSecond =
        indexForPath(paneAt(workspace, 1)).data(ThumbnailModel::ThumbnailUrlRole).toString();

    RawImageParameters parameters;
    parameters.size = {4, 4};
    parameters.format = RawPixelFormat::Raw16;
    parameters.rowStride = 8;
    workspace.loader()->setRawParameters(rawPath, parameters);

    const QString firstUrl =
        indexForPath(paneAt(workspace, 0)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    const QString secondUrl =
        indexForPath(paneAt(workspace, 1)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    QVERIFY(firstUrl != beforeFirst);
    QVERIFY(secondUrl != beforeSecond);
    QCOMPARE(firstUrl, secondUrl);

    ThumbnailImageProvider provider(decoder, workspace.loader());
    const QString encodedPath = QString::fromLatin1(QUrl::toPercentEncoding(rawPath));
    QImage firstImage = provider.requestImage(encodedPath + QStringLiteral("?v=first"), nullptr,
                                              QSize(16, 16));
    QCOMPARE(firstImage.pixelColor(0, 0), QColor(Qt::red));

    parameters.size = {8, 4};
    parameters.rowStride = 16;
    workspace.loader()->setRawParameters(rawPath, parameters);
    const QString refreshedUrl =
        indexForPath(paneAt(workspace, 1)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    QVERIFY(refreshedUrl != secondUrl);
    QImage refreshedImage = provider.requestImage(encodedPath + QStringLiteral("?v=second"),
                                                  nullptr, QSize(16, 16));
    QCOMPARE(refreshedImage.pixelColor(0, 0), QColor(Qt::green));
}

} // namespace ispview

QTEST_MAIN(ispview::QmlWorkspaceControllerTests)
#include "qml_workspace_controller_tests.moc"
