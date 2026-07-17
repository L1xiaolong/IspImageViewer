#include "io/default_image_decoder.h"
#include "io/image_loader.h"
#include "io/qt_image_decoder.h"
#include "io/raw_image_decoder.h"
#include "io/raw_preset_store.h"
#include "platform/platform_services.h"
#include "render/image_canvas.h"
#include "render/navigation_thumbnail_overlay.h"
#include "ui/compare_window.h"
#include "ui/full_screen_window.h"
#include "ui/histogram_panel.h"
#include "ui/image_info_panel.h"
#include "ui/image_properties_panel.h"
#include "ui/local_file_drop.h"
#include "ui/main_window.h"
#include "ui/multi_folder_window.h"
#include "ui/path_breadcrumb.h"
#include "ui/raw_parameter_panel.h"
#include "ui/thumbnail_filter_proxy_model.h"
#include "ui/thumbnail_model.h"
#include "ui/thumbnail_view.h"
#include "ui/trash_confirmation.h"

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QShortcut>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <memory>

namespace ispview {
namespace {} // namespace

class UiTests final : public QObject {
    Q_OBJECT

  private slots:
    void thumbnailModelAppliesIncrementalDirectoryChanges();
    void thumbnailBrowserFoldersSortingDragAndMainWindowCommands();
    void mainWindowProvidesExplorerStyleFileCommands();
    void trashConfirmationPersistsOnlyAffirmativeSuppression();
    void rawThumbnailLoadsAutomaticallyFromSidecar();
    void mainWindowUsesDockForUnconfiguredRawParameters();
    void rawParameterPanelEmitsDebouncedSingleFrameParameters();
    void fullScreenNavigatesEncodedImagesWithKeyboardAndButtons();
    void fullScreenUsesEdgePanelsContextActionsAndNoTimeline();
    void mainWindowBuildsCurrentDisplayHistogram();
    void histogramPanelCoalescesRapidFrames();
    void histogramPanelAnalyzesRawPlanesAndRoi();
    void imageInfoPanelShowsTypedCameraAndRawMetadata();
    void mainWindowLoadsLocalDngIntoInformationPanel();
    void imageCanvasWheelPanFitAndResizePreserveFrame();
    void imageCanvasSelectsNormalizedRoi();
    void compareWindowProvidesImmersiveTwoImageControls();
    void multiFolderWindowStartsWithFourIndependentBrowsers();
};

void UiTests::thumbnailModelAppliesIncrementalDirectoryChanges() {
    auto decoder = std::make_shared<QtImageDecoder>();
    ImageLoader loader(decoder);
    ThumbnailModel model(&loader);
    const QDateTime initialTime = QDateTime::fromMSecsSinceEpoch(1000);
    model.setFiles({{QStringLiteral("/images/a.png"), QStringLiteral("a.png"), 10, initialTime},
                    {QStringLiteral("/images/c.png"), QStringLiteral("c.png"), 30, initialTime}});

    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.updateFiles(
        {{QStringLiteral("/images/b.png"), QStringLiteral("b.png"), 20, initialTime},
         {QStringLiteral("/images/c.png"), QStringLiteral("c.png"), 31, initialTime.addMSecs(1)}});

    QCOMPARE(reset.count(), 0);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.pathAt(0), QStringLiteral("/images/b.png"));
    QCOMPARE(model.pathAt(1), QStringLiteral("/images/c.png"));
    QCOMPARE(model.index(1).data(ThumbnailModel::SizeRole).toLongLong(), 31);
}

void UiTests::thumbnailBrowserFoldersSortingDragAndMainWindowCommands() {
    auto decoder = std::make_shared<QtImageDecoder>();
    ImageLoader loader(decoder);
    ThumbnailModel model(&loader);
    const QDateTime earlier = QDateTime::fromMSecsSinceEpoch(1000);
    const QDateTime later = QDateTime::fromMSecsSinceEpoch(2000);
    model.setFiles(
        {{QStringLiteral("/images/with-images"), QStringLiteral("with-images"), 0, earlier, true},
         {QStringLiteral("/images/scene10.png"), QStringLiteral("scene10.png"), 20, later},
         {QStringLiteral("/images/scene2.jpg"), QStringLiteral("scene2.jpg"), 10, earlier}});
    QCOMPARE(model.index(0).data(Qt::DisplayRole).toString(), QStringLiteral("with-images"));
    QVERIFY(model.index(0).data(ThumbnailModel::DirectoryRole).toBool());
    QVERIFY(!model.index(0).data(Qt::DecorationRole).value<QPixmap>().isNull());
    QCOMPARE(model.index(1).data(Qt::DisplayRole).toString(), QStringLiteral("scene10.png"));

    std::unique_ptr<QMimeData> dragged(model.mimeData({model.index(2)}));
    QCOMPARE(dragged->urls().size(), 1);
    QCOMPARE(dragged->urls().constFirst().toLocalFile(), QStringLiteral("/images/scene2.jpg"));
    QCOMPARE(model.supportedDragActions(), Qt::CopyAction);

    ThumbnailView captionView;
    captionView.setModel(&model);
    captionView.setViewMode(QListView::IconMode);
    captionView.setIconSize({160, 120});
    captionView.setGridSize({194, 190});
    QImage paintedItem(194, 190, QImage::Format_ARGB32_Premultiplied);
    paintedItem.fill(Qt::white);
    QPainter itemPainter(&paintedItem);
    QStyleOptionViewItem itemOption;
    itemOption.rect = paintedItem.rect();
    itemOption.widget = &captionView;
    itemOption.palette = captionView.palette();
    itemOption.font = captionView.font();
    itemOption.fontMetrics = captionView.fontMetrics();
    captionView.itemDelegate()->paint(&itemPainter, itemOption, model.index(1));
    itemPainter.end();
    bool captionBandContainsInk = false;
    for (int y = 162; y < paintedItem.height() && !captionBandContainsInk; ++y) {
        for (int x = 8; x < paintedItem.width() - 8; ++x) {
            if (paintedItem.pixelColor(x, y) != QColor(Qt::white)) {
                captionBandContainsInk = true;
                break;
            }
        }
    }
    QVERIFY2(captionBandContainsInk, "icon-mode delegate must paint a filename caption");

    paintedItem.fill(Qt::white);
    itemOption.state = QStyle::State_Enabled | QStyle::State_Selected;
    itemPainter.begin(&paintedItem);
    captionView.itemDelegate()->paint(&itemPainter, itemOption, model.index(1));
    itemPainter.end();
    const QColor selectedCaptionColor = itemOption.palette.color(QPalette::Text);
    bool selectedCaptionContainsText = false;
    for (int y = 162; y < paintedItem.height() && !selectedCaptionContainsText; ++y) {
        for (int x = 8; x < paintedItem.width() - 8; ++x) {
            const QColor pixel = paintedItem.pixelColor(x, y);
            const int colorDistance = std::abs(pixel.red() - selectedCaptionColor.red()) +
                                      std::abs(pixel.green() - selectedCaptionColor.green()) +
                                      std::abs(pixel.blue() - selectedCaptionColor.blue());
            if (colorDistance < 24) {
                selectedCaptionContainsText = true;
                break;
            }
        }
    }
    QVERIFY2(selectedCaptionContainsText,
             "selected icon-mode item must paint a readable filename caption");

    // Selection is intentionally represented only by the rounded outer stroke. Keep the card
    // padding transparent instead of applying the platform item-selection background.
    QCOMPARE(paintedItem.pixelColor(12, 30), QColor(Qt::white));
    const QColor highlight = itemOption.palette.color(QPalette::Highlight);
    bool selectedOutlineContainsHighlight = false;
    for (int y = 4; y <= 159 && !selectedOutlineContainsHighlight; ++y) {
        for (int x = 7; x <= 11; ++x) {
            const QColor pixel = paintedItem.pixelColor(x, y);
            const int colorDistance = std::abs(pixel.red() - highlight.red()) +
                                      std::abs(pixel.green() - highlight.green()) +
                                      std::abs(pixel.blue() - highlight.blue());
            if (colorDistance < 40) {
                selectedOutlineContainsHighlight = true;
                break;
            }
        }
    }
    QVERIFY2(selectedOutlineContainsHighlight,
             "selected icon-mode item must use only a blue outer outline");

    // Folder cards use a proportionally scaled platform icon and deliberately leave the metadata
    // band empty: no separator and no synthetic FOLDER label.
    QImage paintedFolder(194, 190, QImage::Format_ARGB32_Premultiplied);
    paintedFolder.fill(Qt::white);
    QPainter folderPainter(&paintedFolder);
    itemOption.state = QStyle::State_Enabled;
    captionView.itemDelegate()->paint(&folderPainter, itemOption, model.index(0));
    folderPainter.end();
    bool folderMetadataBandIsClear = true;
    for (int y = 133; y <= 154 && folderMetadataBandIsClear; ++y) {
        for (int x = 16; x <= 175; ++x) {
            if (paintedFolder.pixelColor(x, y) != QColor(Qt::white)) {
                folderMetadataBandIsClear = false;
                break;
            }
        }
    }
    QVERIFY2(folderMetadataBandIsClear,
             "folder card must not paint an inner separator or FOLDER metadata label");

    ThumbnailFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QCOMPARE(proxy.supportedDragActions(), Qt::CopyAction);
    QCOMPARE(proxy.rowCount(), 3);
    QCOMPARE(proxy.index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("with-images"));
    QCOMPARE(proxy.index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("scene2.jpg"));
    QCOMPARE(proxy.index(2, 0).data(Qt::DisplayRole).toString(), QStringLiteral("scene10.png"));
    proxy.setSortMode(BrowserSortMode::Size);
    QCOMPARE(proxy.index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("with-images"));
    QCOMPARE(proxy.index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("scene2.jpg"));
    QCOMPARE(proxy.index(2, 0).data(Qt::DisplayRole).toString(), QStringLiteral("scene10.png"));
    proxy.setSortMode(BrowserSortMode::ModifiedTime);
    QCOMPARE(proxy.index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("scene2.jpg"));
    QCOMPARE(proxy.index(2, 0).data(Qt::DisplayRole).toString(), QStringLiteral("scene10.png"));
    proxy.setSortMode(BrowserSortMode::Type);
    QCOMPARE(proxy.index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("scene2.jpg"));
    QCOMPARE(proxy.index(2, 0).data(Qt::DisplayRole).toString(), QStringLiteral("scene10.png"));

    ThumbnailView dropView;
    QCOMPARE(dropView.dragDropMode(), QAbstractItemView::DragDrop);
    QVERIFY(dropView.dragEnabled());
    QVERIFY(dropView.acceptDrops());
    QVERIFY(dropView.viewport()->acceptDrops());
    QSignalSpy dropped(&dropView, &ThumbnailView::localPathsDropped);
    QSignalSpy dropEntered(&dropView, &ThumbnailView::externalDropEntered);
    QMimeData dropMime;
    dropMime.setUrls({QUrl::fromLocalFile(QStringLiteral("/images/scene2.jpg"))});
    QDragEnterEvent dragEnter(QPoint(5, 5), Qt::CopyAction, &dropMime, Qt::LeftButton,
                              Qt::NoModifier);
    QApplication::sendEvent(dropView.viewport(), &dragEnter);
    QVERIFY(dragEnter.isAccepted());
    QCOMPARE(dropEntered.size(), 1);
    QCOMPARE(dropEntered.constFirst().constFirst().toLongLong(), 1);
    QDropEvent dropEvent(QPointF(5, 5), Qt::CopyAction, &dropMime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(dropView.viewport(), &dropEvent);
    QVERIFY(dropEvent.isAccepted());
    QCOMPARE(dropped.size(), 1);
    QCOMPARE(dropped.constFirst().constFirst().toStringList(),
             QStringList{QStringLiteral("/images/scene2.jpg")});

    QSignalSpy rejectedDrop(&dropView, &ThumbnailView::externalDropRejected);
    QMimeData unsupportedMime;
    unsupportedMime.setData(QStringLiteral("application/x-ispview-test"), QByteArray("opaque"));
    QDragEnterEvent unsupportedEnter(QPoint(5, 5), Qt::CopyAction, &unsupportedMime, Qt::LeftButton,
                                     Qt::NoModifier);
    QApplication::sendEvent(dropView.viewport(), &unsupportedEnter);
    QVERIFY(!unsupportedEnter.isAccepted());
    QCOMPARE(rejectedDrop.size(), 1);
    QVERIFY(rejectedDrop.constFirst().constFirst().toString().contains(
        QStringLiteral("application/x-ispview-test")));

    QMimeData plainFileUrlMime;
    plainFileUrlMime.setText(QStringLiteral("file:///images/scene10.png"));
    QCOMPARE(localFileDropPaths(&plainFileUrlMime),
             QStringList{QStringLiteral("/images/scene10.png")});

    ThumbnailView shortcutView;
    QSignalSpy trashRequested(&shortcutView, &ThumbnailView::trashShortcutRequested);
    QTest::keyClick(&shortcutView, Qt::Key_Delete);
    QCOMPARE(trashRequested.size(), 1);
#if defined(Q_OS_MACOS)
    QTest::keyClick(&shortcutView, Qt::Key_Backspace, Qt::ControlModifier);
    QCOMPARE(trashRequested.size(), 2);
    QTest::keyClick(&shortcutView, Qt::Key_Delete, Qt::ControlModifier);
    QCOMPARE(trashRequested.size(), 3);
#endif

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    const QString aPath = directory.filePath(QStringLiteral("a.png"));
    const QString bPath = directory.filePath(QStringLiteral("b.png"));
    QVERIFY(image.save(aPath));
    QVERIFY(image.save(bPath));
    QFile aFile(aPath);
    QFile bFile(bPath);
    QVERIFY(aFile.open(QIODevice::ReadOnly));
    QVERIFY(bFile.open(QIODevice::ReadOnly));
    QVERIFY(
        aFile.setFileTime(QDateTime::fromMSecsSinceEpoch(2000), QFileDevice::FileModificationTime));
    QVERIFY(
        bFile.setFileTime(QDateTime::fromMSecsSinceEpoch(1000), QFileDevice::FileModificationTime));
    aFile.close();
    bFile.close();

    ThumbnailModel externalDragModel(&loader);
    externalDragModel.setFiles(
        {{directory.filePath(QStringLiteral("a.png")), QStringLiteral("a.png"), 16, earlier}});
    ThumbnailView externalDragView;
    externalDragView.setModel(&externalDragModel);
    externalDragView.setViewMode(QListView::IconMode);
    externalDragView.setIconSize({160, 120});
    externalDragView.setGridSize({200, 170});
    externalDragView.resize(260, 210);
    externalDragView.show();
    QCoreApplication::processEvents();
    const QModelIndex externalDragIndex = externalDragModel.index(0);
    externalDragView.setCurrentIndex(externalDragIndex);
    externalDragView.selectionModel()->select(
        externalDragIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QSignalSpy externalDragStarted(&externalDragView, &ThumbnailView::externalDragStarted);
    QSignalSpy externalDragFinished(&externalDragView, &ThumbnailView::externalDragFinished);
    QSignalSpy internalDropRejected(&externalDragView, &ThumbnailView::localPathsDropped);
    const QPoint dragStart = externalDragView.visualRect(externalDragIndex).center();
    QTest::mousePress(externalDragView.viewport(), Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(externalDragView.viewport(), dragStart + QPoint(80, 0), 30);
    QTest::mouseRelease(externalDragView.viewport(), Qt::LeftButton, Qt::NoModifier,
                        dragStart + QPoint(80, 0));
    QTRY_COMPARE_WITH_TIMEOUT(externalDragStarted.size(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(externalDragFinished.size(), 1, 2000);
    QCOMPARE(externalDragStarted.constFirst().constFirst().toLongLong(), 1);
    QCOMPARE(internalDropRejected.size(), 0);

    const QString imageFolder = directory.filePath(QStringLiteral("with-images"));
    const QString emptyFolder = directory.filePath(QStringLiteral("empty"));
    QVERIFY(QDir().mkpath(imageFolder));
    QVERIFY(QDir().mkpath(emptyFolder));
    QVERIFY(image.save(QDir(imageFolder).filePath(QStringLiteral("child.png"))));

    MainWindow window(createDefaultImageDecoder(), directory.path());
    auto* view = window.findChild<ThumbnailView*>(QStringLiteral("thumbnailView"));
    auto* trash = window.findChild<QAction*>(QStringLiteral("trashAction"));
    QVERIFY(view && trash);
    QVERIFY(window.findChild<QComboBox*>(QStringLiteral("browserSortCombo")) == nullptr);
    QVERIFY(window.findChild<QLineEdit*>(QStringLiteral("pathEdit")) == nullptr);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("roiSelectionAction")) == nullptr);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("clearRoiAction")) == nullptr);
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("batchRenameAction")));
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("batchConvertAction")));
    QVERIFY(!window.findChild<QMenu*>(QStringLiteral("quickCopyMenu")));
    QVERIFY(!window.findChild<QMenu*>(QStringLiteral("quickMoveMenu")));
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("manageQuickDestinationsAction")));
    QVERIFY(!window.findChild<QComboBox*>(QStringLiteral("ratingFilterCombo")));
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("taggedOnlyAction")));
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("setRating4Action")));
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("toggleTagAction")));
    auto* backAction = window.findChild<QAction*>(QStringLiteral("backAction"));
    auto* forwardAction = window.findChild<QAction*>(QStringLiteral("forwardAction"));
    auto* upAction = window.findChild<QAction*>(QStringLiteral("upAction"));
    auto* copyAction = window.findChild<QAction*>(QStringLiteral("copyAction"));
    auto* cutAction = window.findChild<QAction*>(QStringLiteral("cutAction"));
    auto* pasteAction = window.findChild<QAction*>(QStringLiteral("pasteAction"));
    auto* renameAction = window.findChild<QAction*>(QStringLiteral("renameAction"));
    auto* propertiesAction = window.findChild<QAction*>(QStringLiteral("propertiesAction"));
    QVERIFY(backAction && forwardAction && upAction && copyAction && cutAction && pasteAction &&
            renameAction && propertiesAction);
    QVERIFY(window.findChild<PathBreadcrumb*>(QStringLiteral("pathBreadcrumb")) == nullptr);
    auto* previewPanel = window.findChild<QWidget*>(QStringLiteral("previewPanel"));
    auto* previewToggle = window.findChild<QAction*>(QStringLiteral("previewToggleAction"));
    auto* leftSplitter = window.findChild<QSplitter*>(QStringLiteral("browserLeftSplitter"));
    QVERIFY(previewPanel && previewToggle && leftSplitter);
    QCOMPARE(leftSplitter->orientation(), Qt::Vertical);
    QVERIFY(window.findChild<QToolButton*>(QStringLiteral("previewActualButton")) != nullptr);
    QVERIFY(window.findChild<QToolButton*>(QStringLiteral("previewFitButton")) != nullptr);
    QVERIFY(window.findChild<QToolButton*>(QStringLiteral("previewFullScreenButton")) != nullptr);
    previewToggle->setChecked(false);
    QVERIFY(previewPanel->isHidden());
    previewToggle->setChecked(true);
    QVERIFY(!backAction->isEnabled());
    QVERIFY(!forwardAction->isEnabled());
    QVERIFY(upAction->isEnabled());
    auto* toolsMenu = window.findChild<QMenu*>(QStringLiteral("toolsMenu"));
    auto* multiFolderAction = window.findChild<QAction*>(QStringLiteral("multiFolderAction"));
    QVERIFY(toolsMenu && multiFolderAction);
    QVERIFY(toolsMenu->actions().contains(multiFolderAction));
    QCOMPARE(multiFolderAction->parent(), toolsMenu);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 4, 5000);
    QModelIndex actualSizeIndex;
    for (int row = 0; row < view->model()->rowCount(); ++row) {
        const QModelIndex candidate = view->model()->index(row, 0);
        if (candidate.data(ThumbnailModel::PathRole).toString() == aPath) {
            actualSizeIndex = candidate;
            break;
        }
    }
    QVERIFY(actualSizeIndex.isValid());
    (void)actualSizeIndex.data(Qt::DecorationRole);
    QTRY_COMPARE_WITH_TIMEOUT(actualSizeIndex.data(ThumbnailModel::DimensionsRole).toSize(),
                              QSize(2, 2), 5000);
    QCOMPARE(view->model()->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("empty"));
    QVERIFY(view->model()->index(0, 0).data(ThumbnailModel::DirectoryRole).toBool());
    QCOMPARE(view->model()->index(1, 0).data(Qt::DisplayRole).toString(),
             QStringLiteral("with-images"));
    for (int row = 0; row < view->model()->rowCount(); ++row) {
        QVERIFY(!view->model()->index(row, 0).data(Qt::DisplayRole).toString().isEmpty());
    }
    QVERIFY(trash->shortcuts().isEmpty());
#if defined(Q_OS_MACOS)
    QVERIFY(!window.findChild<QShortcut*>(QStringLiteral("trashWindowShortcut")));
    QVERIFY(!window.findChild<QShortcut*>(QStringLiteral("trashForwardDeleteShortcut")));
#endif

    window.show();
    window.activateWindow();
    QCoreApplication::processEvents();
    // The platform trash shortcut is a window-level image command. Keep it working when the
    // directory tree (rather than the thumbnail viewport) owns focus, while canceling the modal
    // confirmation so the temporary fixture is never moved to the real system Trash.
    QModelIndex selectedImage;
    for (int row = 0; row < view->model()->rowCount(); ++row) {
        const QModelIndex candidate = view->model()->index(row, 0);
        if (!candidate.data(ThumbnailModel::DirectoryRole).toBool()) {
            selectedImage = candidate;
            break;
        }
    }
    QVERIFY(selectedImage.isValid());
    view->setCurrentIndex(selectedImage);
    view->selectionModel()->select(selectedImage,
                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    auto* mainSplitter = window.findChild<QSplitter*>(QStringLiteral("browserMainSplitter"));
    auto* previewCanvas = window.findChild<ImageCanvas*>();
    auto* actualButton = window.findChild<QToolButton*>(QStringLiteral("previewActualButton"));
    auto* fitButton = window.findChild<QToolButton*>(QStringLiteral("previewFitButton"));
    QVERIFY(mainSplitter && previewCanvas && actualButton && fitButton);
    QTRY_VERIFY_WITH_TIMEOUT(previewCanvas->frame() != nullptr, 5000);
    const QList<int> stableMainSizes = mainSplitter->sizes();
    actualButton->click();
    fitButton->click();
    QCoreApplication::processEvents();
    QCOMPARE(mainSplitter->sizes(), stableMainSizes);
    QVERIFY(QMetaObject::invokeMethod(previewCanvas, "pixelHovered", Qt::DirectConnection,
                                      Q_ARG(QPoint, QPoint(1, 1)),
                                      Q_ARG(QColor, QColor(10, 20, 30, 40)), Q_ARG(bool, true)));
    QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("x:1 y:1  RGBA(10,20,30,40)"));
    auto* directoryTree = window.findChild<QTreeView*>(QStringLiteral("directoryTree"));
    QVERIFY(directoryTree);
    directoryTree->setFocus();
    QCoreApplication::processEvents();
    QSettings trashSettings;
    const QVariant previousTrashConfirmation = trashSettings.value("browser/confirmTrash");
    trashSettings.setValue("browser/confirmTrash", true);
    bool platformDeleteConfirmed = false;
    QTimer::singleShot(0, this, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!dialog) {
            return;
        }
        platformDeleteConfirmed = dialog->text().contains(QStringLiteral("system Trash"));
        dialog->reject();
    });
#if defined(Q_OS_MACOS)
    QTest::keyClick(directoryTree, Qt::Key_Backspace, Qt::ControlModifier);
#else
    QTest::keyClick(directoryTree, Qt::Key_Delete);
#endif
    QVERIFY(platformDeleteConfirmed);
    if (previousTrashConfirmation.isValid()) {
        trashSettings.setValue("browser/confirmTrash", previousTrashConfirmation);
    } else {
        trashSettings.remove("browser/confirmTrash");
    }
    const QPoint blankPosition(view->viewport()->width() - 8, view->viewport()->height() - 8);
    QVERIFY(!view->indexAt(blankPosition).isValid());
    bool blankMenuInspected = false;
    QTimer::singleShot(0, this, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        const auto actionByText = [](QMenu* source, const QString& text) -> QAction* {
            const auto actions = source->actions();
            const auto found =
                std::find_if(actions.cbegin(), actions.cend(),
                             [&text](QAction* item) { return item->text() == text; });
            return found == actions.cend() ? nullptr : *found;
        };
        QAction* newFolder = actionByText(menu, QStringLiteral("New Folder…"));
        QAction* refresh = actionByText(menu, QStringLiteral("Refresh"));
        QAction* viewMenuAction = actionByText(menu, QStringLiteral("View"));
        QAction* sortMenuAction = actionByText(menu, QStringLiteral("Sort By"));
        if (!newFolder || !refresh || viewMenuAction || !sortMenuAction ||
            !sortMenuAction->menu()) {
            menu->close();
            return;
        }
        const bool allSortModes =
            actionByText(sortMenuAction->menu(), QStringLiteral("Name")) &&
            actionByText(sortMenuAction->menu(), QStringLiteral("Date Modified")) &&
            actionByText(sortMenuAction->menu(), QStringLiteral("Size")) &&
            actionByText(sortMenuAction->menu(), QStringLiteral("Type"));
        if (!allSortModes) {
            menu->close();
            return;
        }
        blankMenuInspected = true;
        menu->close();
    });
    QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                      Q_ARG(QPoint, blankPosition)));
    QVERIFY(blankMenuInspected);
    QCOMPARE(view->viewMode(), QListView::IconMode);
    QCOMPARE(view->iconSize(), QSize(160, 120));
    QCOMPARE(view->gridSize(), QSize(194, 190));

    bool newFolderMenuHandled = false;
    bool newFolderDialogHandled = false;
    const QString createdFolderName = QStringLiteral("created-from-menu-test");
    QTimer::singleShot(0, this, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        QAction* newFolder = nullptr;
        for (QAction* action : menu->actions()) {
            if (action->text() == QStringLiteral("New Folder…")) {
                newFolder = action;
                break;
            }
        }
        if (!newFolder) {
            menu->close();
            return;
        }
        QTimer::singleShot(0, this, [&] {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (!dialog) {
                return;
            }
            dialog->setTextValue(createdFolderName);
            newFolderDialogHandled = true;
            dialog->accept();
        });
        newFolderMenuHandled = true;
        newFolder->trigger();
        menu->close();
    });
    QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                      Q_ARG(QPoint, blankPosition)));
    QVERIFY(newFolderMenuHandled);
    QVERIFY(newFolderDialogHandled);
    QVERIFY(QFileInfo::exists(directory.filePath(createdFolderName)));

    bool dateSortTriggered = false;
    QTimer::singleShot(0, this, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        QAction* sortMenuAction = nullptr;
        for (QAction* action : menu->actions()) {
            if (action->text() == QStringLiteral("Sort By")) {
                sortMenuAction = action;
                break;
            }
        }
        if (!sortMenuAction || !sortMenuAction->menu()) {
            menu->close();
            return;
        }
        for (QAction* action : sortMenuAction->menu()->actions()) {
            if (action->text() == QStringLiteral("Date Modified")) {
                dateSortTriggered = true;
                action->trigger();
                break;
            }
        }
        menu->close();
    });
    QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                      Q_ARG(QPoint, blankPosition)));
    QVERIFY(dateSortTriggered);
    QStringList sortedImageNames;
    for (int row = 0; row < view->model()->rowCount(); ++row) {
        const QModelIndex candidate = view->model()->index(row, 0);
        if (!candidate.data(ThumbnailModel::DirectoryRole).toBool()) {
            sortedImageNames.append(candidate.data(Qt::DisplayRole).toString());
        }
    }
    QCOMPARE(sortedImageNames, QStringList({QStringLiteral("b.png"), QStringLiteral("a.png")}));

    QVERIFY(image.save(directory.filePath(QStringLiteral("c.png"))));
    bool refreshTriggered = false;
    QTimer::singleShot(0, this, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        for (QAction* action : menu->actions()) {
            if (action->text() == QStringLiteral("Refresh")) {
                refreshTriggered = true;
                action->trigger();
                break;
            }
        }
        menu->close();
    });
    QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                      Q_ARG(QPoint, blankPosition)));
    QVERIFY(refreshTriggered);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 6, 5000);

    QModelIndex firstImage;
    QModelIndex secondImage;
    for (int row = 0; row < view->model()->rowCount(); ++row) {
        const QModelIndex candidate = view->model()->index(row, 0);
        if (candidate.data(ThumbnailModel::DirectoryRole).toBool()) {
            continue;
        }
        if (!firstImage.isValid()) {
            firstImage = candidate;
        } else {
            secondImage = candidate;
            break;
        }
    }
    QVERIFY(firstImage.isValid() && secondImage.isValid());
    view->setCurrentIndex(firstImage);
    view->selectionModel()->select(QItemSelection(firstImage, secondImage),
                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    auto* compareCommand = window.findChild<QAction*>(QStringLiteral("compareAction"));
    QVERIFY(compareCommand != nullptr);
    compareCommand->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<CompareWindow*>().size(), 1, 3000);
    auto* compareWindow = window.findChild<CompareWindow*>();
    QVERIFY(compareWindow != nullptr);
    QVERIFY(compareWindow->windowState().testFlag(Qt::WindowFullScreen));
    QCOMPARE(compareWindow->findChildren<ImageCanvas*>().size(), 2);
    compareWindow->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    view->setCurrentIndex(firstImage);
    view->selectionModel()->select(firstImage,
                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    auto* fullScreenCommand = window.findChild<QAction*>(QStringLiteral("fullScreenAction"));
    QVERIFY(fullScreenCommand != nullptr);
    fullScreenCommand->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<FullScreenWindow*>().size(), 1, 3000);
    auto* fullScreenWindow = window.findChild<FullScreenWindow*>();
    QVERIFY(fullScreenWindow != nullptr);
    QVERIFY(fullScreenWindow->windowState().testFlag(Qt::WindowFullScreen));
    fullScreenWindow->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QModelIndex imageFolderIndex;
    for (int row = 0; row < view->model()->rowCount(); ++row) {
        const QModelIndex candidate = view->model()->index(row, 0);
        if (candidate.data(ThumbnailModel::PathRole).toString() == imageFolder) {
            imageFolderIndex = candidate;
            break;
        }
    }
    QVERIFY(imageFolderIndex.isValid());
    view->activated(imageFolderIndex);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 1, 5000);
    QCOMPARE(view->model()->index(0, 0).data(Qt::DisplayRole).toString(),
             QStringLiteral("child.png"));

    QTemporaryDir droppedDirectory;
    QVERIFY(droppedDirectory.isValid());
    const QString droppedPath = droppedDirectory.filePath(QStringLiteral("dropped.png"));
    QVERIFY(image.save(droppedPath));
    auto* previewDropCanvas = window.findChild<ImageCanvas*>();
    QVERIFY(previewDropCanvas);
    QVERIFY(previewDropCanvas->acceptDrops());
    QMimeData previewDropMime;
    previewDropMime.setUrls({QUrl::fromLocalFile(droppedPath)});
    QDragEnterEvent previewDragEnter(QPoint(5, 5), Qt::CopyAction, &previewDropMime, Qt::LeftButton,
                                     Qt::NoModifier);
    QApplication::sendEvent(previewDropCanvas, &previewDragEnter);
    QVERIFY(previewDragEnter.isAccepted());
    QDropEvent previewDrop(QPointF(5, 5), Qt::CopyAction, &previewDropMime, Qt::LeftButton,
                           Qt::NoModifier);
    QApplication::sendEvent(previewDropCanvas, &previewDrop);
    QVERIFY(previewDrop.isAccepted());
    const QString copiedPath = QDir(imageFolder).filePath(QStringLiteral("dropped.png"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(copiedPath), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(view->currentIndex().data(ThumbnailModel::PathRole).toString(),
                              copiedPath, 5000);

    const QString incomingFolder = droppedDirectory.filePath(QStringLiteral("incoming-folder"));
    QVERIFY(QDir().mkpath(incomingFolder));
    QVERIFY(image.save(QDir(incomingFolder).filePath(QStringLiteral("folder-image.png"))));
    QMimeData previewFolderDropMime;
    previewFolderDropMime.setUrls({QUrl::fromLocalFile(incomingFolder)});
    QDragEnterEvent previewFolderDragEnter(QPoint(5, 5), Qt::CopyAction, &previewFolderDropMime,
                                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(previewDropCanvas, &previewFolderDragEnter);
    QVERIFY(previewFolderDragEnter.isAccepted());
    QDropEvent previewFolderDrop(QPointF(5, 5), Qt::CopyAction, &previewFolderDropMime,
                                 Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(previewDropCanvas, &previewFolderDrop);
    QVERIFY(previewFolderDrop.isAccepted());
    const QString copiedFolder = QDir(imageFolder).filePath(QStringLiteral("incoming-folder"));
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(QDir(copiedFolder).filePath(QStringLiteral("folder-image.png"))), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 3, 5000);

    // Exercise the application-level native-drag fallback over the thumbnail viewport. Plain
    // file-URL text models a platform pasteboard that has not been converted to hasUrls(). The
    // duplicate name must be resolved without overwriting the first dropped copy.
    QMimeData thumbnailDropMime;
    thumbnailDropMime.setText(QUrl::fromLocalFile(droppedPath).toString());
    QDragEnterEvent thumbnailDragEnter(QPoint(5, 5), Qt::CopyAction, &thumbnailDropMime,
                                       Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &thumbnailDragEnter);
    QVERIFY(thumbnailDragEnter.isAccepted());
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Drop to copy")));
    QDropEvent thumbnailDrop(QPointF(5, 5), Qt::CopyAction, &thumbnailDropMime, Qt::LeftButton,
                             Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &thumbnailDrop);
    QVERIFY(thumbnailDrop.isAccepted());
    const QString duplicateCopiedPath =
        QDir(imageFolder).filePath(QStringLiteral("dropped copy.png"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(duplicateCopiedPath), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(view->currentIndex().data(ThumbnailModel::PathRole).toString(),
                              duplicateCopiedPath, 5000);

    auto* navigationTree = window.findChild<QTreeView*>(QStringLiteral("directoryTree"));
    QVERIFY(navigationTree);
    auto* fileSystemModel = qobject_cast<QFileSystemModel*>(navigationTree->model());
    QVERIFY(fileSystemModel);
    QModelIndex droppedDirectoryIndex;
    QTRY_VERIFY_WITH_TIMEOUT(
        (droppedDirectoryIndex = fileSystemModel->index(droppedDirectory.path())).isValid(), 5000);
    navigationTree->clicked(droppedDirectoryIndex);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 2, 5000);
}

void UiTests::mainWindowProvidesExplorerStyleFileCommands() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    const QString imagePath = directory.filePath(QStringLiteral("source.png"));
    const QString albumPath = directory.filePath(QStringLiteral("album"));
    const QString destinationPath = directory.filePath(QStringLiteral("destination"));
    QVERIFY(image.save(imagePath));
    QVERIFY(QDir().mkdir(albumPath));
    QVERIFY(QDir().mkdir(destinationPath));
    QVERIFY(image.save(QDir(albumPath).filePath(QStringLiteral("inside.png"))));

    MainWindow window(createDefaultImageDecoder(), directory.path());
    window.show();
    window.activateWindow();
    auto* view = window.findChild<ThumbnailView*>(QStringLiteral("thumbnailView"));
    auto* copyAction = window.findChild<QAction*>(QStringLiteral("copyAction"));
    auto* cutAction = window.findChild<QAction*>(QStringLiteral("cutAction"));
    auto* pasteAction = window.findChild<QAction*>(QStringLiteral("pasteAction"));
    auto* backAction = window.findChild<QAction*>(QStringLiteral("backAction"));
    auto* forwardAction = window.findChild<QAction*>(QStringLiteral("forwardAction"));
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("browserSearchEdit"));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("directoryTree"));
    auto* treeModel = tree ? qobject_cast<QFileSystemModel*>(tree->model()) : nullptr;
    QVERIFY(view && copyAction && cutAction && pasteAction && backAction && forwardAction &&
            search && tree && treeModel);
    const auto currentDirectoryPath = [tree, treeModel] {
        return QDir::cleanPath(treeModel->filePath(tree->currentIndex()));
    };
    QCOMPARE(copyAction->shortcut(), QKeySequence::Copy);
    QCOMPARE(cutAction->shortcut(), QKeySequence::Cut);
    QCOMPARE(pasteAction->shortcut(), QKeySequence::Paste);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 3, 5000);

    const auto indexForPath = [view](const QString& path) {
        for (int row = 0; row < view->model()->rowCount(); ++row) {
            const QModelIndex candidate = view->model()->index(row, 0);
            if (candidate.data(ThumbnailModel::PathRole).toString() == path) {
                return candidate;
            }
        }
        return QModelIndex{};
    };
    const auto select = [view](const QModelIndex& index) {
        view->setCurrentIndex(index);
        view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect |
                                                  QItemSelectionModel::Rows);
        view->setFocus();
        QCoreApplication::processEvents();
    };

    // Copy/paste applies to both files and folders and never overwrites an existing item.
    select(indexForPath(imagePath));
    QVERIFY(copyAction->isEnabled());
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(localFileDropPaths(QGuiApplication::clipboard()->mimeData()), QStringList{imagePath});
    QVERIFY(pasteAction->isEnabled());
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
    const QString copiedImage = directory.filePath(QStringLiteral("source copy.png"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(copiedImage), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 4, 5000);

    // Cut a complete folder, navigate into the target, then paste. The source disappears only
    // after the destination tree has been created successfully.
    select(indexForPath(albumPath));
    QTest::keyClick(view, Qt::Key_X, Qt::ControlModifier);
    const QModelIndex destinationIndex = indexForPath(destinationPath);
    QVERIFY(destinationIndex.isValid());
    view->activated(destinationIndex);
    QTRY_COMPARE_WITH_TIMEOUT(currentDirectoryPath(), QDir::cleanPath(destinationPath), 5000);
    QVERIFY(backAction->isEnabled());
    view->setFocus();
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
    const QString movedAlbum = QDir(destinationPath).filePath(QStringLiteral("album"));
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo(QDir(movedAlbum).filePath(QStringLiteral("inside.png"))).isFile(), 5000);
    QVERIFY(!QFileInfo::exists(albumPath));
    QTRY_VERIFY_WITH_TIMEOUT(!pasteAction->isEnabled(), 2000);

    backAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(currentDirectoryPath(), QDir::cleanPath(directory.path()), 5000);
    QVERIFY(forwardAction->isEnabled());
    forwardAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(currentDirectoryPath(), QDir::cleanPath(destinationPath), 5000);
    backAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(currentDirectoryPath(), QDir::cleanPath(directory.path()), 5000);

    search->setText(QStringLiteral("source"));
    QTRY_COMPARE(view->model()->rowCount(), 2);
    search->clear();
    QTRY_COMPARE(view->model()->rowCount(), 3);

    // Folder context menus expose the same file commands as image context menus.
    const QModelIndex folderIndex = indexForPath(destinationPath);
    QVERIFY(folderIndex.isValid());
    select(indexForPath(destinationPath));
    bool menuVerified = false;
    QTimer::singleShot(0, this, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        QStringList labels;
        for (QAction* action : menu->actions()) {
            labels.append(action->text());
        }
        menuVerified = labels.contains(QStringLiteral("Open Folder")) &&
                       labels.contains(QStringLiteral("Cut")) &&
                       labels.contains(QStringLiteral("Copy")) &&
                       labels.contains(QStringLiteral("Paste Into Folder")) &&
                       labels.contains(QStringLiteral("Rename…")) &&
                       labels.contains(QStringLiteral("Move to Trash")) &&
                       labels.contains(QStringLiteral("Properties"));
        menu->close();
    });
    const QPoint folderPosition = view->visualRect(folderIndex).center();
    QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                      Q_ARG(QPoint, folderPosition)));
    QVERIFY(menuVerified);

    const QModelIndex copiedImageIndex = indexForPath(copiedImage);
    QVERIFY(copiedImageIndex.isValid());
    select(copiedImageIndex);
    bool imageMenuVerified = false;
    QTimer::singleShot(0, this, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        QStringList labels;
        for (QAction* action : menu->actions()) {
            labels.append(action->text());
        }
        imageMenuVerified = labels.contains(QStringLiteral("Open Full Screen")) &&
                            labels.contains(QStringLiteral("Cut")) &&
                            labels.contains(QStringLiteral("Copy")) &&
                            labels.contains(QStringLiteral("Rename…")) &&
                            labels.contains(QStringLiteral("Move to Trash")) &&
                            labels.contains(QStringLiteral("Properties"));
        menu->close();
    });
    const QPoint imagePosition = view->visualRect(copiedImageIndex).center();
    QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                      Q_ARG(QPoint, imagePosition)));
    QVERIFY(imageMenuVerified);

    const QString renamedImage = directory.filePath(QStringLiteral("renamed.png"));
    bool renameDialogHandled = false;
    QTimer::singleShot(0, this, [&] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (!dialog) {
            return;
        }
        dialog->setTextValue(QStringLiteral("renamed.png"));
        renameDialogHandled = true;
        dialog->accept();
    });
    QTest::keyClick(view, Qt::Key_F2);
    QVERIFY(renameDialogHandled);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(renamedImage), 5000);
    QVERIFY(!QFileInfo::exists(copiedImage));

    // The platform delete shortcut must also route a folder selection through the shared
    // confirmation path. Cancel so the fixture never enters the real system Trash.
    const QModelIndex refreshedFolderIndex = indexForPath(destinationPath);
    QVERIFY(refreshedFolderIndex.isValid());
    select(refreshedFolderIndex);
    QSettings settings;
    const QVariant previousConfirmation = settings.value(QStringLiteral("browser/confirmTrash"));
    settings.setValue(QStringLiteral("browser/confirmTrash"), true);
    bool folderDeleteConfirmed = false;
    QTimer::singleShot(0, this, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!dialog) {
            return;
        }
        folderDeleteConfirmed = dialog->text().contains(QStringLiteral("system Trash"));
        dialog->reject();
    });
#if defined(Q_OS_MACOS)
    QTest::keyClick(view, Qt::Key_Backspace, Qt::ControlModifier);
#else
    QTest::keyClick(view, Qt::Key_Delete);
#endif
    QVERIFY(folderDeleteConfirmed);
    if (previousConfirmation.isValid()) {
        settings.setValue(QStringLiteral("browser/confirmTrash"), previousConfirmation);
    } else {
        settings.remove(QStringLiteral("browser/confirmTrash"));
    }
}

void UiTests::trashConfirmationPersistsOnlyAffirmativeSuppression() {
    const QString key = QStringLiteral("browser/confirmTrash");
    QSettings settings;
    const bool originallyPresent = settings.contains(key);
    const QVariant originalValue = settings.value(key);
    const auto restoreSetting = [&] {
        if (originallyPresent) {
            settings.setValue(key, originalValue);
        } else {
            settings.remove(key);
        }
    };
    const auto settingGuard = qScopeGuard(restoreSetting);

    settings.setValue(key, true);
    bool canceledDialogHandled = false;
    QTimer::singleShot(0, this, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!dialog) {
            return;
        }
        auto* suppression = dialog->findChild<QCheckBox*>(QStringLiteral("trashDontAskAgain"));
        if (!suppression) {
            return;
        }
        suppression->setChecked(true);
        canceledDialogHandled = true;
        dialog->done(QMessageBox::No);
    });
    QVERIFY(!TrashConfirmation::request(nullptr, 2));
    QVERIFY(canceledDialogHandled);
    QVERIFY(settings.value(key).toBool());

    bool acceptedDialogHandled = false;
    QTimer::singleShot(0, this, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!dialog) {
            return;
        }
        auto* suppression = dialog->findChild<QCheckBox*>(QStringLiteral("trashDontAskAgain"));
        if (!suppression) {
            return;
        }
        suppression->setChecked(true);
        acceptedDialogHandled = true;
        dialog->done(QMessageBox::Yes);
    });
    QVERIFY(TrashConfirmation::request(nullptr, 2));
    QVERIFY(acceptedDialogHandled);
    QVERIFY(!settings.value(key).toBool());

    // Once suppressed, request() returns without opening another modal dialog.
    QVERIFY(TrashConfirmation::request(nullptr, 3));
}

void UiTests::rawThumbnailLoadsAutomaticallyFromSidecar() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("capture.yuv"));

    constexpr int width = 400;
    constexpr int height = 300;
    constexpr qsizetype frameBytes = width * height * 3 / 2;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray(frameBytes, static_cast<char>(128))), frameBytes);
    file.close();

    RawImageParameters parameters;
    parameters.size = {width, height};
    parameters.format = RawPixelFormat::NV12;
    parameters.rowStride = width;
    parameters.chromaStride = width;
    QString error;
    QVERIFY2(RawPresetStore::saveSidecar(path, parameters, &error), qPrintable(error));

    auto decoder = std::make_shared<RawImageDecoder>();
    ImageLoader loader(decoder);
    ThumbnailModel model(&loader);
    model.setFiles({{path, QFileInfo(path).fileName(), QFileInfo(path).size(),
                     QFileInfo(path).lastModified()}});

    const QPixmap initial = model.index(0).data(Qt::DecorationRole).value<QPixmap>();
    QCOMPARE(initial.size(), QSize(160, 120));
    QTRY_COMPARE_WITH_TIMEOUT(model.index(0).data(ThumbnailModel::DimensionsRole).toSize(),
                              QSize(width, height), 5000);
    QCOMPARE(model.index(0).data(Qt::DecorationRole).value<QPixmap>().size(), QSize(160, 120));
    QVERIFY(loader.rawParameters(path).has_value());
}

void UiTests::mainWindowUsesDockForUnconfiguredRawParameters() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("capture.yuv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray(24, static_cast<char>(128))), 24);
    file.close();
    QImage png(4, 4, QImage::Format_RGBA8888);
    png.fill(QColor(32, 64, 96, 255));
    QVERIFY(png.save(directory.filePath(QStringLiteral("photo.png"))));

    MainWindow window(createDefaultImageDecoder(), directory.path());
    window.show();
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("rawConfigurationDock"));
    auto* propertiesDock = window.findChild<QDockWidget*>(QStringLiteral("imageInfoDock"));
    auto* properties = window.findChild<ImagePropertiesPanel*>();
    auto* panel = window.findChild<RawParameterPanel*>(QStringLiteral("rawConfigurationPanel"));
    auto* action = window.findChild<QAction*>(QStringLiteral("rawParametersAction"));
    auto* canvas = window.findChild<ImageCanvas*>();
    auto* thumbnailView = window.findChild<QListView*>(QStringLiteral("thumbnailView"));
    QVERIFY(dock && propertiesDock && properties && panel && action && canvas && thumbnailView);
    QTRY_VERIFY_WITH_TIMEOUT(action->isEnabled(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(dock->isVisible(), 5000);
    QVERIFY(!dock->isFloating());
    QCOMPARE(window.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QVERIFY(propertiesDock->isHidden());
    QVERIFY(!QApplication::activeModalWidget());
    QCOMPARE(window.findChildren<QDialog*>().size(), 0);
    QVERIFY(panel->findChild<QLabel*>(QStringLiteral("rawPanelFile")) == nullptr);
    QVERIFY(panel->findChild<QLabel*>(QStringLiteral("rawPanelFrameIndex")) == nullptr);
    QVERIFY(window.findChildren<QSlider*>().isEmpty());
    for (QToolBar* toolbar : window.findChildren<QToolBar*>()) {
        QVERIFY(!toolbar->actions().contains(action));
    }
    QVERIFY(panel->findChild<QComboBox*>(QStringLiteral("rawConfigurationPreset")) != nullptr);
    QVERIFY(panel->findChild<QPushButton*>(QStringLiteral("saveRawConfiguration")) != nullptr);
    QVERIFY(panel->findChild<QPushButton*>(QStringLiteral("applyRawConfigurationToFolder")) !=
            nullptr);
    auto* deleteConfiguration =
        panel->findChild<QPushButton*>(QStringLiteral("deleteRawConfiguration"));
    QVERIFY(deleteConfiguration != nullptr);
    QVERIFY(!deleteConfiguration->isEnabled());

    const QModelIndex rawIndex = thumbnailView->model()->index(0, 0);
    QVERIFY(rawIndex.isValid());
    thumbnailView->selectionModel()->setCurrentIndex(rawIndex, QItemSelectionModel::ClearAndSelect |
                                                                   QItemSelectionModel::Current);
    bool rawMenuVerified = false;
    QTimer::singleShot(0, this, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        for (QAction* menuAction : menu->actions()) {
            rawMenuVerified =
                rawMenuVerified || menuAction->text() == QStringLiteral("RAW Parameters…");
        }
        menu->close();
    });
    QVERIFY(QMetaObject::invokeMethod(thumbnailView, "customContextMenuRequested",
                                      Qt::DirectConnection,
                                      Q_ARG(QPoint, thumbnailView->visualRect(rawIndex).center())));
    QVERIFY(rawMenuVerified);

    auto* width = panel->findChild<QSpinBox*>(QStringLiteral("rawPanelWidth"));
    auto* height = panel->findChild<QSpinBox*>(QStringLiteral("rawPanelHeight"));
    QVERIFY(width && height);
    width->setValue(4);
    height->setValue(4);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->frame() != nullptr, 5000);
    QCOMPARE(canvas->frame()->descriptor.size, QSize(4, 4));
    QVERIFY(canvas->frame()->rawParameters.has_value());
    QCOMPARE(canvas->frame()->rawParameters->frameIndex, 0);
    QVERIFY(dock->isVisible());
    auto* rawTable = properties->rawParametersTable();
    QVERIFY(rawTable != nullptr);
    QVERIFY(rawTable->topLevelItemCount() > 0);
    QCOMPARE(rawTable->topLevelItem(0)->text(0), QStringLiteral("Format"));
    QVERIFY(properties->findChild<QSpinBox*>(QStringLiteral("rawPanelWidth")) == nullptr);
    QVERIFY(!QApplication::activeModalWidget());

    QVERIFY(panel->isEnabled());
    QCOMPARE(panel->sourcePath(), path);
    QModelIndex pngIndex;
    for (int row = 0; row < thumbnailView->model()->rowCount(); ++row) {
        const QModelIndex candidate = thumbnailView->model()->index(row, 0);
        if (candidate.data(ThumbnailModel::PathRole).toString() ==
            directory.filePath(QStringLiteral("photo.png"))) {
            pngIndex = candidate;
            break;
        }
    }
    QVERIFY(pngIndex.isValid());
    thumbnailView->selectionModel()->setCurrentIndex(pngIndex, QItemSelectionModel::ClearAndSelect |
                                                                   QItemSelectionModel::Current);
    QTRY_COMPARE_WITH_TIMEOUT(panel->sourcePath(), QString{}, 5000);
    QVERIFY(!panel->isEnabled());
    QVERIFY(!action->isEnabled());
    QVERIFY(dock->isVisible());
}

void UiTests::mainWindowBuildsCurrentDisplayHistogram() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(4, 2, QImage::Format_RGBA8888);
    image.fill(QColor(0, 0, 0, 255));
    for (int y = 0; y < image.height(); ++y) {
        image.setPixelColor(2, y, QColor(255, 0, 0, 255));
        image.setPixelColor(3, y, QColor(255, 0, 0, 255));
    }
    QVERIFY(image.save(directory.filePath(QStringLiteral("red.png"))));

    MainWindow window(createDefaultImageDecoder(), directory.path());
    auto* properties = window.findChild<ImagePropertiesPanel*>();
    auto* propertiesDock = window.findChild<QDockWidget*>(QStringLiteral("imageInfoDock"));
    auto* propertiesToggle =
        window.findChild<QAction*>(QStringLiteral("imageInformationToggleAction"));
    auto* propertiesCommand = window.findChild<QAction*>(QStringLiteral("propertiesAction"));
    auto* panel = window.findChild<HistogramPanel*>(QStringLiteral("histogramPanel"));
    auto* summary = window.findChild<QLabel*>(QStringLiteral("histogramSummary"));
    auto* statistics = window.findChild<QTableWidget*>(QStringLiteral("histogramStatistics"));
    auto* channels = window.findChild<QComboBox*>(QStringLiteral("histogramChannelCombo"));
    auto* canvas = window.findChild<ImageCanvas*>();
    QVERIFY(properties != nullptr);
    QVERIFY(propertiesDock != nullptr);
    QVERIFY(propertiesToggle != nullptr);
    QVERIFY(propertiesCommand != nullptr);
    QVERIFY(panel != nullptr);
    QVERIFY(summary != nullptr);
    QVERIFY(canvas != nullptr);
    QVERIFY(statistics != nullptr);
    QVERIFY(channels != nullptr);
    QCOMPARE(properties->tabs()->count(), 3);
    QCOMPARE(properties->tabs()->tabText(0), QStringLiteral("EXIF"));
    QCOMPARE(properties->tabs()->tabText(1), QStringLiteral("Histogram"));
    QCOMPARE(properties->tabs()->tabText(2), QStringLiteral("RAW Parameters"));
    QVERIFY(propertiesDock->isFloating());
    QVERIFY(propertiesDock->isHidden());
    QTRY_VERIFY_WITH_TIMEOUT(propertiesCommand->isEnabled(), 5000);
    propertiesCommand->trigger();
    QVERIFY(!propertiesDock->isHidden());
    QCOMPARE(properties->tabs()->currentIndex(), 0);
    QCOMPARE(propertiesToggle->text(), QStringLiteral("Properties"));
    QTRY_VERIFY_WITH_TIMEOUT(panel->histogram().has_value(), 5000);
    QVERIFY(panel->histogram()->isValid());
    QCOMPARE(panel->histogram()->analyzedSize, QSize(4, 2));
    QCOMPARE(panel->histogram()->sampledPixelCount, 8);
    QCOMPARE(panel->histogram()->red.bins[255], 4);
    QCOMPARE(panel->histogram()->red.mean, 127.5);
    QVERIFY(summary->isHidden());
    QVERIFY(summary->text().contains(QStringLiteral("8 samples")));
    QCOMPARE(statistics->rowCount(), 4);
    QCOMPARE(statistics->horizontalHeaderItem(2)->text(), QStringLiteral("Variance"));
    QCOMPARE(statistics->item(1, 1)->text(), QStringLiteral("127.50"));
    QCOMPARE(statistics->item(1, 5)->text(), QStringLiteral("127.5"));
    QCOMPARE(statistics->columnCount(), 6);
    QCOMPARE(statistics->horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
    QCOMPARE(statistics->horizontalHeader()->sectionResizeMode(5), QHeaderView::Stretch);
    QVERIFY(statistics->horizontalHeaderItem(0)->font().bold());
    QCOMPARE(statistics->horizontalHeaderItem(0)->textAlignment(),
             static_cast<int>(Qt::AlignCenter));
    QCOMPARE(statistics->item(1, 1)->textAlignment(), static_cast<int>(Qt::AlignCenter));
    QCOMPARE(channels->count(), 4);
    QCOMPARE(channels->itemText(0), QStringLiteral("Luma"));
    channels->setCurrentIndex(channels->findData(static_cast<int>(HistogramChannelMode::Red)));
    QCOMPARE(channels->currentText(), QStringLiteral("Red"));
    QVERIFY(window.findChild<QToolButton*>(QStringLiteral("histogramZoomIn")) == nullptr);
    QVERIFY(window.findChild<QToolButton*>(QStringLiteral("histogramZoomOut")) == nullptr);

    auto* plot = window.findChild<QWidget*>(QStringLiteral("histogramPlot"));
    QVERIFY(plot != nullptr);
    plot->resize(320, 210);
    QImage rendered(plot->size(), QImage::Format_RGBA8888);
    rendered.fill(Qt::transparent);
    plot->render(&rendered);
    QCOMPARE(rendered.pixelColor(1, 1), QColor(Qt::white));
    bool containsRedFill = false;
    for (int y = 0; y < rendered.height() && !containsRedFill; ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            const QColor pixel = rendered.pixelColor(x, y);
            if (pixel.red() > 170 && pixel.red() > pixel.green() * 2 &&
                pixel.red() > pixel.blue() * 2) {
                containsRedFill = true;
                break;
            }
        }
    }
    QVERIFY(containsRedFill);

    channels->setCurrentIndex(channels->findData(static_cast<int>(HistogramChannelMode::Blue)));
    QImage blueRendered(plot->size(), QImage::Format_RGBA8888);
    blueRendered.fill(Qt::transparent);
    plot->render(&blueRendered);
    bool containsBlueFill = false;
    for (int y = 0; y < blueRendered.height() && !containsBlueFill; ++y) {
        for (int x = 0; x < blueRendered.width(); ++x) {
            const QColor pixel = blueRendered.pixelColor(x, y);
            if (pixel.blue() > 220 && pixel.red() < 30 && pixel.green() < 30) {
                containsBlueFill = true;
                break;
            }
        }
    }
    QVERIFY(containsBlueFill);

    ViewState state = canvas->viewState();
    state.normalizedRoi = QRectF(0.0, 0.0, 0.5, 1.0);
    canvas->setViewState(state, true);
    QTRY_VERIFY_WITH_TIMEOUT(
        panel->histogram().has_value() && panel->histogram()->isRegionLimited(), 3000);
    QCOMPARE(panel->histogram()->logicalRegion, QRect(0, 0, 2, 2));
    QCOMPARE(panel->histogram()->red.mean, 0.0);
    QCOMPARE(panel->histogram()->red.standardDeviation, 0.0);
    QVERIFY(summary->isHidden());
}

void UiTests::histogramPanelCoalescesRapidFrames() {
    const auto solidFrame = [](const QColor& color) {
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.fill(color);
        auto frame = std::make_shared<ImageFrame>();
        frame->descriptor.size = image.size();
        frame->storage = std::move(image);
        return ImageFramePtr(std::move(frame));
    };
    HistogramPanel panel;
    panel.setFrame(solidFrame(QColor(255, 0, 0)));
    QTest::qWait(50);
    panel.setFrame(solidFrame(QColor(0, 0, 255)));
    QTRY_VERIFY_WITH_TIMEOUT(panel.histogram().has_value(), 2000);
    QCOMPARE(panel.histogram()->red.bins[0], 4);
    QCOMPARE(panel.histogram()->blue.bins[255], 4);

    panel.setFrame(solidFrame(QColor(0, 255, 0)));
    panel.setFrame({});
    QTest::qWait(250);
    QVERIFY(!panel.histogram().has_value());
}

void UiTests::histogramPanelAnalyzesRawPlanesAndRoi() {
    RawImageParameters parameters;
    parameters.size = {4, 2};
    parameters.format = RawPixelFormat::NV12;
    auto planes = std::make_shared<PlaneBufferSet>();
    planes->storage =
        QByteArray::fromRawData("\x0A\x14\x1E\x28\x32\x3C\x46\x50\x64\x96\x6E\xA0", 12);
    planes->planes = {{0, 4, 8}, {8, 4, 4}};
    planes->displayImage = QImage(parameters.size, QImage::Format_RGBA8888);
    planes->displayImage.fill(Qt::gray);
    auto frame = std::make_shared<ImageFrame>();
    frame->descriptor.size = parameters.size;
    frame->rawParameters = parameters;
    frame->storage = std::shared_ptr<const PlaneBufferSet>(planes);

    HistogramPanel panel;
    auto* source = panel.findChild<QComboBox*>(QStringLiteral("histogramSourceCombo"));
    auto* summary = panel.findChild<QLabel*>(QStringLiteral("histogramSummary"));
    QVERIFY(source != nullptr);
    QVERIFY(summary != nullptr);
    panel.setSource(HistogramSource::SourcePlanes);
    QCOMPARE(panel.source(), HistogramSource::SourcePlanes);
    panel.setFrame(frame);
    QTRY_VERIFY_WITH_TIMEOUT(panel.rawHistogram().has_value(), 2000);
    QVERIFY(panel.rawHistogram()->isValid());
    QCOMPARE(panel.rawHistogram()->channels.size(), 3);
    QCOMPARE(panel.rawHistogram()->channels.at(0).mean, 45.0);
    QCOMPARE(panel.rawHistogram()->channels.at(1).sampledSampleCount, 2);
    QVERIFY(summary->isHidden());
    auto* statistics = panel.findChild<QTableWidget*>(QStringLiteral("histogramStatistics"));
    auto* channels = panel.findChild<QComboBox*>(QStringLiteral("histogramChannelCombo"));
    QVERIFY(statistics != nullptr);
    QVERIFY(channels != nullptr);
    QCOMPARE(statistics->rowCount(), 3);
    QCOMPARE(statistics->item(0, 0)->text(), QStringLiteral("Y"));
    QCOMPARE(statistics->item(0, 1)->text(), QStringLiteral("45.00"));
    QCOMPARE(channels->count(), 3);
    QCOMPARE(statistics->columnCount(), 6);

    panel.setNormalizedRegion(QRectF(0.0, 0.0, 0.5, 1.0));
    QTRY_VERIFY_WITH_TIMEOUT(
        panel.rawHistogram().has_value() && panel.rawHistogram()->isRegionLimited(), 2000);
    QCOMPARE(panel.rawHistogram()->logicalRegion, QRect(0, 0, 2, 2));
    QCOMPARE(panel.rawHistogram()->channels.at(0).mean, 35.0);
    QCOMPARE(panel.rawHistogram()->channels.at(1).sampledSampleCount, 1);
    QVERIFY(summary->isHidden());

    panel.setSource(HistogramSource::Display);
    QTRY_VERIFY_WITH_TIMEOUT(panel.histogram().has_value(), 2000);
    QVERIFY(!panel.rawHistogram().has_value());
    QVERIFY(summary->isHidden());
}

void UiTests::imageInfoPanelShowsTypedCameraAndRawMetadata() {
    auto frame = std::make_shared<ImageFrame>();
    frame->descriptor.size = {4000, 3000};
    frame->descriptor.storageBits = 16;
    frame->descriptor.validBits = 12;
    frame->metadata.fileName = QStringLiteral("capture.dng");
    frame->metadata.path = QStringLiteral("/images/capture.dng");
    frame->metadata.format = QStringLiteral("DNG");
    frame->metadata.fileSize = 4 * 1024 * 1024;
    frame->metadata.decoderName = QStringLiteral("LibRaw");
    frame->metadata.decoderVersion = QStringLiteral("0.21.1");
    frame->metadata.metadataReaderName = QStringLiteral("Exiv2");
    frame->metadata.metadataReaderVersion = QStringLiteral("0.28.7");
    frame->metadata.sourceOrientation = ImageMetadata::Orientation::Rotate90;
    frame->metadata.gpsMetadataPresent = true;
    ImageMetadata::Descriptive descriptive;
    descriptive.title = QStringLiteral("Tuning result");
    descriptive.creator = QStringLiteral("ISP Team");
    frame->metadata.descriptive = descriptive;
    ImageMetadata::ColorProfile colorProfile;
    colorProfile.sourceDescription = QStringLiteral("Linear sRGB");
    colorProfile.sourceFingerprint = QStringLiteral("0123456789abcdef");
    colorProfile.destinationColorSpace = QStringLiteral("sRGB");
    colorProfile.transformEngine = QStringLiteral("LittleCMS 2.17");
    colorProfile.renderingIntent = QStringLiteral("Relative colorimetric");
    colorProfile.converted = true;
    frame->metadata.colorProfile = colorProfile;
    ImageMetadata::Camera camera;
    camera.make = QStringLiteral("Example");
    camera.model = QStringLiteral("Camera X");
    camera.software = QStringLiteral("ISP Firmware 3");
    camera.lens = QStringLiteral("24 mm Prime");
    camera.exposureSeconds = 1.0 / 125.0;
    camera.aperture = 2.8;
    camera.iso = 200;
    camera.focalLengthMm = 24.0;
    camera.exposureProgram = QStringLiteral("Manual");
    camera.meteringMode = QStringLiteral("Pattern");
    camera.exposureCompensation = QStringLiteral("-0.3 EV");
    camera.flash = QStringLiteral("Did not fire");
    camera.gps = QStringLiteral("31° N, 121° E");
    camera.sensorSize = {4056, 3040};
    frame->metadata.camera = camera;
    frame->storage = QImage(1, 1, QImage::Format_RGBA8888);

    ImageInfoPanel panel;
    panel.setFrame(frame);
    QCOMPARE(panel.valueForField(QStringLiteral("Make")), QStringLiteral("Example"));
    QCOMPARE(panel.valueForField(QStringLiteral("Model")), QStringLiteral("Camera X"));
    QCOMPARE(panel.valueForField(QStringLiteral("Software")), QStringLiteral("ISP Firmware 3"));
    QCOMPARE(panel.valueForField(QStringLiteral("Exposure Time")), QStringLiteral("1/125 s"));
    QCOMPARE(panel.valueForField(QStringLiteral("Aperture")), QStringLiteral("f/2.8"));
    QCOMPARE(panel.valueForField(QStringLiteral("Sensor Size")), QStringLiteral("4056 × 3040"));
    QCOMPARE(panel.valueForField(QStringLiteral("Exposure Program")), QStringLiteral("Manual"));
    QCOMPARE(panel.valueForField(QStringLiteral("Metering Mode")), QStringLiteral("Pattern"));
    QCOMPARE(panel.valueForField(QStringLiteral("Exposure Compensation")),
             QStringLiteral("-0.3 EV"));
    QCOMPARE(panel.valueForField(QStringLiteral("Flash")), QStringLiteral("Did not fire"));
    QCOMPARE(panel.valueForField(QStringLiteral("Orientation")), QStringLiteral("Rotate 90° CW"));
    QCOMPARE(panel.valueForField(QStringLiteral("GPS")), QStringLiteral("31° N, 121° E"));
    QCOMPARE(panel.valueForField(QStringLiteral("Title")), QStringLiteral("Tuning result"));
    QVERIFY(panel.valueForField(QStringLiteral("Keywords")).isEmpty());
    QVERIFY(panel.valueForField(QStringLiteral("Rating")).isEmpty());
    QCOMPARE(panel.valueForField(QStringLiteral("File Name")), QStringLiteral("capture.dng"));
    QCOMPARE(panel.valueForField(QStringLiteral("Dimensions")), QStringLiteral("4000 × 3000"));

    panel.setFrame({});
    QVERIFY(panel.valueForField(QStringLiteral("Type")).isEmpty());

    auto noExif = std::make_shared<ImageFrame>();
    noExif->metadata.fileName = QStringLiteral("plain.png");
    ImageInfoPanel exif(nullptr, ImageInfoPanel::Section::Exif);
    exif.setFrame(noExif);
    auto* exifFields = exif.findChild<QTreeWidget*>(QStringLiteral("imageInfoFields"));
    QVERIFY(exifFields != nullptr);
    QVERIFY(exifFields->topLevelItemCount() >= 15);
    QCOMPARE(exifFields->topLevelItem(0)->text(0), QStringLiteral("Make"));
    QCOMPARE(exifFields->topLevelItem(0)->text(1), QString{});
}

void UiTests::mainWindowLoadsLocalDngIntoInformationPanel() {
#if ISPVIEW_HAS_LIBRAW
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        QSKIP("Large-image MainWindow coverage requires a native QRhi backend");
    }
    const QString source = QFINDTESTDATA("../test_images/img.dng");
    if (source.isEmpty()) {
        QSKIP("Optional local test_images/img.dng is not available");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString copiedPath = directory.filePath(QStringLiteral("img.dng"));
    QVERIFY2(QFile::copy(source, copiedPath), qPrintable(copiedPath));

    MainWindow window(createDefaultImageDecoder(), directory.path());
    auto* panel = window.findChild<ImageInfoPanel*>(QStringLiteral("imageInfoPanel"));
    QVERIFY(panel != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->valueForField(QStringLiteral("Make")).isEmpty(), 5000);
    auto* basic = window.findChild<ImageInfoPanel*>(QStringLiteral("basicInformationPanel"));
    QVERIFY(basic != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(basic->valueForField(QStringLiteral("File Name")),
                              QStringLiteral("img.dng"), 5000);
    QVERIFY(!panel->valueForField(QStringLiteral("Sensor Size")).isEmpty());
#else
    QSKIP("This build does not include LibRaw");
#endif
}

void UiTests::imageCanvasSelectsNormalizedRoi() {
    QImage image(100, 100, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    auto frame = std::make_shared<ImageFrame>();
    frame->descriptor.size = image.size();
    frame->storage = std::move(image);

    ImageCanvas canvas;
    canvas.resize(200, 200);
    canvas.setFrame(std::move(frame));
    canvas.setRoiSelectionEnabled(true);
    QSignalSpy changed(&canvas, &ImageCanvas::roiChanged);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTest::mouseMove(&canvas, QPoint(150, 150));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(150, 150));
    QCOMPARE(changed.size(), 1);
    QVERIFY(changed.constFirst().at(1).toBool());
    QVERIFY(canvas.normalizedRoi().has_value());
    const QRectF roi = *canvas.normalizedRoi();
    QVERIFY(std::abs(roi.x() - 0.25) < 0.001);
    QVERIFY(std::abs(roi.y() - 0.25) < 0.001);
    QVERIFY(std::abs(roi.width() - 0.5) < 0.001);
    QVERIFY(std::abs(roi.height() - 0.5) < 0.001);
    const QRectF widgetRoi = canvas.roiWidgetRect();
    QVERIFY(std::abs(widgetRoi.x() - 50.0) < 0.1);
    QVERIFY(std::abs(widgetRoi.y() - 50.0) < 0.1);
    QVERIFY(std::abs(widgetRoi.width() - 100.0) < 0.1);
    QVERIFY(std::abs(widgetRoi.height() - 100.0) < 0.1);

    canvas.clearRoi();
    QCOMPARE(changed.size(), 2);
    QVERIFY(!changed.constLast().at(1).toBool());
    QVERIFY(!canvas.normalizedRoi());
}

void UiTests::imageCanvasWheelPanFitAndResizePreserveFrame() {
    QImage image(100, 80, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    auto frame = std::make_shared<ImageFrame>();
    frame->descriptor.size = image.size();
    frame->storage = std::move(image);

    ImageCanvas canvas;
    canvas.resize(200, 160);
    canvas.setFrame(frame);
    QVERIFY(!canvas.navigationThumbnailEnabled());
    canvas.setNavigationThumbnailEnabled(true);
    auto* navigation =
        canvas.findChild<NavigationThumbnailOverlay*>(QStringLiteral("navigationThumbnailOverlay"));
    QVERIFY(navigation != nullptr);
    QVERIFY(navigation->isHidden());
    QCOMPARE(canvas.viewState().pixelsPerImagePixel, 2.0);
    canvas.actualPixels();
    QCOMPARE(canvas.viewState().fitMode, FitMode::Manual);
    QCOMPARE(canvas.viewState().pixelsPerImagePixel, 1.0);
    QVERIFY(!navigation->isHidden());
    QCOMPARE(navigation->zoomText(), QStringLiteral("100%"));
    canvas.fitImage();
    QCOMPARE(canvas.viewState().fitMode, FitMode::Fit);
    QCOMPARE(canvas.viewState().pixelsPerImagePixel, 2.0);
    QVERIFY(navigation->isHidden());

    const QPointF anchor(45.0, 55.0);
    const QPointF beforePoint = ViewTransform::widgetToImage(
        anchor, canvas.size(), canvas.logicalImageSize(), canvas.viewState());
    QSignalSpy changed(&canvas, &ImageCanvas::viewStateChanged);
    QWheelEvent wheel(anchor, anchor, {}, QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&canvas, &wheel);
    QVERIFY(wheel.isAccepted());
    QCOMPARE(changed.size(), 1);
    QCOMPARE(canvas.viewState().fitMode, FitMode::Manual);
    QVERIFY(canvas.viewState().pixelsPerImagePixel > 2.0);
    QVERIFY(!navigation->isHidden());
    const QPointF afterPoint = ViewTransform::widgetToImage(
        anchor, canvas.size(), canvas.logicalImageSize(), canvas.viewState());
    QVERIFY(QLineF(beforePoint, afterPoint).length() < 0.0001);

    const QPointF centerBeforePan = canvas.viewState().normalizedCenter;
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 80));
    QTest::mouseMove(&canvas, QPoint(120, 90));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(120, 90));
    QVERIFY(canvas.viewState().normalizedCenter != centerBeforePan);
    QVERIFY(changed.size() >= 2);

    ViewState overviewState = canvas.viewState();
    overviewState.pixelsPerImagePixel = 4.0;
    overviewState.normalizedCenter = {0.5, 0.5};
    canvas.setViewState(overviewState, false);
    QCOMPARE(navigation->normalizedViewportRect(), QRectF(0.25, 0.25, 0.5, 0.5));
    QCOMPARE(navigation->zoomText(), QStringLiteral("400%"));
    QCOMPARE(navigation->geometry().left(), 12);
    QCOMPARE(canvas.height() - navigation->geometry().bottom() - 1, 12);

    QImage navigationPaint(navigation->size(), QImage::Format_ARGB32_Premultiplied);
    navigationPaint.fill(Qt::transparent);
    navigation->render(&navigationPaint);
    bool hasTranslucentPixels = false;
    bool hasViewportStroke = false;
    const QRectF thumbnailRect = QRectF(navigation->rect()).adjusted(5.0, 5.0, -5.0, -5.0);
    const int expectedViewportLeft = qRound(
        thumbnailRect.left() + navigation->normalizedViewportRect().left() * thumbnailRect.width());
    for (int y = 0; y < navigationPaint.height(); ++y) {
        for (int x = 0; x < navigationPaint.width(); ++x) {
            const QColor pixel = navigationPaint.pixelColor(x, y);
            hasTranslucentPixels =
                hasTranslucentPixels || (pixel.alpha() > 0 && pixel.alpha() < 255);
            if (std::abs(x - expectedViewportLeft) <= 2 && y > thumbnailRect.top() + 4 &&
                y < thumbnailRect.bottom() - 4 && pixel.red() > 200 && pixel.green() > 200 &&
                pixel.blue() > 200) {
                hasViewportStroke = true;
            }
        }
    }
    QVERIFY(hasTranslucentPixels);
    QVERIFY(hasViewportStroke);

    canvas.resize(320, 240);
    QCoreApplication::processEvents();
    QCOMPARE(canvas.frame().get(), frame.get());
}

void UiTests::compareWindowProvidesImmersiveTwoImageControls() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage left(8, 6, QImage::Format_RGBA8888);
    left.fill(Qt::red);
    QImage right(8, 6, QImage::Format_RGBA8888);
    right.fill(Qt::blue);
    const QString leftPath = directory.filePath(QStringLiteral("left.png"));
    const QString rightPath = directory.filePath(QStringLiteral("right.png"));
    QVERIFY(left.save(leftPath));
    QVERIFY(right.save(rightPath));

    auto decoder = std::make_shared<QtImageDecoder>();
    ImageLoader loader(decoder);
    CompareWindow window(&loader, leftPath, rightPath);
    window.show();
    QCoreApplication::processEvents();
    auto* firstCanvas = window.findChild<ImageCanvas*>(QStringLiteral("compareCanvas0"));
    auto* secondCanvas = window.findChild<ImageCanvas*>(QStringLiteral("compareCanvas1"));
    QVERIFY(firstCanvas && secondCanvas);
    QVERIFY(firstCanvas->navigationThumbnailEnabled());
    QVERIFY(secondCanvas->navigationThumbnailEnabled());
    const QList<ImageCanvas*> canvases{firstCanvas, secondCanvas};
    QTRY_VERIFY_WITH_TIMEOUT(firstCanvas->frame() && secondCanvas->frame(), 5000);
    auto* firstNavigation = firstCanvas->findChild<NavigationThumbnailOverlay*>(
        QStringLiteral("navigationThumbnailOverlay"));
    auto* secondNavigation = secondCanvas->findChild<NavigationThumbnailOverlay*>(
        QStringLiteral("navigationThumbnailOverlay"));
    QVERIFY(firstNavigation && secondNavigation);
    QVERIFY(firstNavigation->isHidden());
    QVERIFY(secondNavigation->isHidden());

    ViewState state = canvases.at(0)->viewState();
    state.fitMode = FitMode::Manual;
    state.pixelsPerImagePixel = 3.0;
    state.normalizedCenter = {0.25, 0.75};
    canvases.at(0)->setViewState(state, true);
    QTRY_COMPARE_WITH_TIMEOUT(canvases.at(1)->viewState().pixelsPerImagePixel, 3.0, 2000);
    QCOMPARE(canvases.at(1)->viewState().normalizedCenter, QPointF(0.25, 0.75));
    QVERIFY(!firstNavigation->isHidden());
    QVERIFY(!secondNavigation->isHidden());
    QCOMPARE(firstNavigation->zoomText(), QStringLiteral("300%"));
    QCOMPARE(secondNavigation->zoomText(), QStringLiteral("300%"));

    auto* hold = window.findChild<QToolButton*>(QStringLiteral("holdComparisonButton"));
    auto* leftPixel = window.findChild<QLabel*>(QStringLiteral("comparePixelOverlay0"));
    auto* rightPixel = window.findChild<QLabel*>(QStringLiteral("comparePixelOverlay1"));
    auto* leftInfo = window.findChild<QLabel*>(QStringLiteral("compareFileOverlay0"));
    auto* leftExif = window.findChild<QLabel*>(QStringLiteral("compareExifOverlay0"));
    auto* leftHistogram = window.findChild<HistogramPanel*>(QStringLiteral("compareHistogram0"));
    auto* informationOverlay =
        window.findChild<QWidget*>(QStringLiteral("compareInformationOverlay0"));
    QVERIFY(hold && leftPixel && rightPixel && leftInfo && leftExif && leftHistogram &&
            informationOverlay);
    QCOMPARE(leftInfo->text(), QStringLiteral("left.png"));
    QVERIFY(!leftInfo->isHidden());
    QVERIFY(leftExif->isHidden());
    QVERIFY(leftHistogram->isHidden());
    QVERIFY(leftPixel->isHidden());
    QVERIFY(leftInfo->styleSheet().contains(QStringLiteral("background: transparent")));
    QVERIFY(leftInfo->graphicsEffect() != nullptr);
    auto* informationLayout = qobject_cast<QVBoxLayout*>(informationOverlay->layout());
    QVERIFY(informationLayout != nullptr);
    QVERIFY(informationLayout->indexOf(leftInfo) < informationLayout->indexOf(leftExif));
    QVERIFY(informationLayout->indexOf(leftExif) < informationLayout->indexOf(leftHistogram));
    auto* paneLayout = qobject_cast<QGridLayout*>(firstCanvas->parentWidget()->layout());
    QVERIFY(paneLayout != nullptr);
    const QLayoutItem* pixelItem = paneLayout->itemAt(paneLayout->indexOf(leftPixel));
    QVERIFY(pixelItem != nullptr);
    QVERIFY(pixelItem->alignment().testFlag(Qt::AlignRight));
    QVERIFY(pixelItem->alignment().testFlag(Qt::AlignBottom));

    QVERIFY(QMetaObject::invokeMethod(canvases.constFirst(), "pixelHovered", Qt::DirectConnection,
                                      Q_ARG(QPoint, QPoint(3, 2)), Q_ARG(QColor, QColor(Qt::red)),
                                      Q_ARG(bool, true)));
    QVERIFY(leftPixel->text().contains(QStringLiteral("(3, 2)")));
    QVERIFY(rightPixel->text().contains(QStringLiteral("(3, 2)")));

    auto* fileInfoToggle = window.findChild<QAction*>(QStringLiteral("compareFileInfoAction"));
    auto* exifToggle = window.findChild<QAction*>(QStringLiteral("compareExifAction"));
    auto* histogramToggle = window.findChild<QAction*>(QStringLiteral("compareHistogramAction"));
    auto* pixelToggle = window.findChild<QAction*>(QStringLiteral("comparePixelAction"));
    QVERIFY(fileInfoToggle && exifToggle && histogramToggle && pixelToggle);
    QCOMPARE(fileInfoToggle->text(), QStringLiteral("File"));
    QVERIFY(fileInfoToggle->isChecked());
    QVERIFY(!exifToggle->isChecked());
    QVERIFY(!histogramToggle->isChecked());
    QVERIFY(!pixelToggle->isChecked());
    fileInfoToggle->setChecked(false);
    QVERIFY(leftInfo->isHidden());
    exifToggle->setChecked(true);
    QVERIFY(!leftExif->isHidden());
    histogramToggle->setChecked(true);
    QVERIFY(!leftHistogram->isHidden());
    auto* histogramSource =
        leftHistogram->findChild<QComboBox*>(QStringLiteral("histogramSourceCombo"));
    auto* histogramChannel =
        leftHistogram->findChild<QComboBox*>(QStringLiteral("histogramChannelCombo"));
    auto* histogramStatistics =
        leftHistogram->findChild<QTableWidget*>(QStringLiteral("histogramStatistics"));
    QVERIFY(histogramSource && histogramChannel && histogramStatistics);
    QVERIFY(histogramSource->isHidden());
    QVERIFY(histogramChannel->isHidden());
    QVERIFY(histogramStatistics->isHidden());
    pixelToggle->setChecked(true);
    QVERIFY(!leftPixel->isHidden());
    QVERIFY(!rightPixel->isHidden());
    fileInfoToggle->setChecked(true);
    exifToggle->setChecked(false);
    histogramToggle->setChecked(false);

    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("compareToolbar"));
    auto* sideBySide = window.findChild<QAction*>(QStringLiteral("sideBySideAction"));
    auto* verticalSplit = window.findChild<QAction*>(QStringLiteral("verticalSplitAction"));
    auto* horizontalSplit = window.findChild<QAction*>(QStringLiteral("horizontalSplitAction"));
    auto* screenshotAction = window.findChild<QAction*>(QStringLiteral("compareScreenshotAction"));
    QVERIFY(toolbar && sideBySide && verticalSplit && horizontalSplit && screenshotAction);
    const QList<QAction*> toolbarActions = toolbar->actions();
    const qsizetype sideIndex = toolbarActions.indexOf(sideBySide);
    QVERIFY(sideIndex >= 0 && sideIndex + 1 < toolbarActions.size());
    QCOMPARE(toolbar->widgetForAction(toolbarActions.at(sideIndex + 1)),
             static_cast<QWidget*>(hold));
    for (QAction* action : toolbarActions) {
        QVERIFY(action->text() != QStringLiteral("Close"));
    }

    const QString screenshotPath = directory.filePath(QStringLiteral("chosen-screenshot.png"));
    QString suggestedScreenshotName;
    QTimer::singleShot(0, [&suggestedScreenshotName, screenshotPath] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QFileDialog*>(widget);
            if (!dialog || dialog->objectName() != QStringLiteral("compareScreenshotDialog")) {
                continue;
            }
            if (!dialog->selectedFiles().isEmpty()) {
                suggestedScreenshotName =
                    QFileInfo(dialog->selectedFiles().constFirst()).fileName();
            }
            dialog->selectFile(screenshotPath);
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            return;
        }
    });
    QSignalSpy screenshotSaved(&window, &CompareWindow::screenshotSaved);
    screenshotAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(screenshotSaved.size(), 1, 3000);
    QCOMPARE(screenshotSaved.constFirst().constFirst().toString(), screenshotPath);
    QVERIFY(suggestedScreenshotName.startsWith(QStringLiteral("screen_shot_")));
    QVERIFY(suggestedScreenshotName.endsWith(QStringLiteral(".png")));
    bool suggestedTimestampValid = false;
    QFileInfo(suggestedScreenshotName)
        .baseName()
        .mid(QStringLiteral("screen_shot_").size())
        .toLongLong(&suggestedTimestampValid);
    QVERIFY(suggestedTimestampValid);
    const QFileInfo screenshotInfo(screenshotPath);
    QCOMPARE(screenshotInfo.absolutePath(), QDir(directory.path()).absolutePath());
    const QImage savedScreenshot(screenshotPath);
    QVERIFY(!savedScreenshot.isNull());
    QVERIFY(savedScreenshot.height() < window.grab().toImage().height());
    QVERIFY(QFile::remove(screenshotPath));

    QTimer::singleShot(0, [] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* dialog = qobject_cast<QFileDialog*>(widget)) {
                QMetaObject::invokeMethod(dialog, "reject", Qt::DirectConnection);
                return;
            }
        }
    });
    screenshotAction->trigger();
    QCOMPARE(screenshotSaved.size(), 1);

    verticalSplit->trigger();
    QTRY_COMPARE(firstCanvas->compareMode(), ImageCompareMode::VerticalSplit);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("comparePane1"))->isHidden());
    const QSize logicalSize = firstCanvas->logicalImageSize();
    const QPointF verticalStart =
        ViewTransform::imageToWidget(QPointF(logicalSize.width() * 0.5, logicalSize.height() * 0.5),
                                     firstCanvas->size(), logicalSize, firstCanvas->viewState());
    const QPointF verticalTargetInCanvas = ViewTransform::imageToWidget(
        QPointF(logicalSize.width() * 0.75, logicalSize.height() * 0.5), firstCanvas->size(),
        logicalSize, firstCanvas->viewState());
    QMouseEvent verticalPress(QEvent::MouseButtonPress, verticalStart, verticalStart,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(firstCanvas, &verticalPress);
    QMouseEvent verticalMove(QEvent::MouseMove, verticalTargetInCanvas, verticalTargetInCanvas,
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(firstCanvas, &verticalMove);
    QMouseEvent verticalRelease(QEvent::MouseButtonRelease, verticalTargetInCanvas,
                                verticalTargetInCanvas, Qt::LeftButton, Qt::NoButton,
                                Qt::NoModifier);
    QCoreApplication::sendEvent(firstCanvas, &verticalRelease);
    QVERIFY(std::abs(firstCanvas->compareAmount() - 0.75F) < 0.02F);

    horizontalSplit->trigger();
    QTRY_COMPARE(firstCanvas->compareMode(), ImageCompareMode::HorizontalSplit);
    const QPointF horizontalStart =
        ViewTransform::imageToWidget(QPointF(logicalSize.width() * 0.5, logicalSize.height() * 0.5),
                                     firstCanvas->size(), logicalSize, firstCanvas->viewState());
    const QPointF horizontalTargetInCanvas = ViewTransform::imageToWidget(
        QPointF(logicalSize.width() * 0.5, logicalSize.height() * 0.25), firstCanvas->size(),
        logicalSize, firstCanvas->viewState());
    QMouseEvent horizontalPress(QEvent::MouseButtonPress, horizontalStart, horizontalStart,
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(firstCanvas, &horizontalPress);
    QMouseEvent horizontalMove(QEvent::MouseMove, horizontalTargetInCanvas,
                               horizontalTargetInCanvas, Qt::NoButton, Qt::LeftButton,
                               Qt::NoModifier);
    QCoreApplication::sendEvent(firstCanvas, &horizontalMove);
    QMouseEvent horizontalRelease(QEvent::MouseButtonRelease, horizontalTargetInCanvas,
                                  horizontalTargetInCanvas, Qt::LeftButton, Qt::NoButton,
                                  Qt::NoModifier);
    QCoreApplication::sendEvent(firstCanvas, &horizontalRelease);
    QVERIFY(std::abs(firstCanvas->compareAmount() - 0.25F) < 0.02F);

    sideBySide->trigger();
    QTRY_COMPARE(firstCanvas->compareMode(), ImageCompareMode::Single);
    QVERIFY(!window.findChild<QWidget*>(QStringLiteral("comparePane1"))->isHidden());

    QVERIFY(QMetaObject::invokeMethod(hold, "pressed", Qt::DirectConnection));
    QCOMPARE(canvases.at(0)->frame()->metadata.fileName, QStringLiteral("right.png"));
    QVERIFY(QMetaObject::invokeMethod(hold, "released", Qt::DirectConnection));
    QCOMPARE(canvases.at(0)->frame()->metadata.fileName, QStringLiteral("left.png"));

    canvases.at(0)->setFocus();
    QTest::keyPress(canvases.at(0), Qt::Key_B);
    QCOMPARE(canvases.at(0)->frame()->metadata.fileName, QStringLiteral("right.png"));
    QTest::keyRelease(canvases.at(0), Qt::Key_B);
    QCOMPARE(canvases.at(0)->frame()->metadata.fileName, QStringLiteral("left.png"));
    QTest::keyPress(canvases.at(0), Qt::Key_B);
    QCOMPARE(canvases.at(0)->frame()->metadata.fileName, QStringLiteral("right.png"));
    window.hide();
    QCoreApplication::processEvents();
    QEvent deactivated(QEvent::ActivationChange);
    QApplication::sendEvent(&window, &deactivated);
    QCOMPARE(canvases.at(0)->frame()->metadata.fileName, QStringLiteral("left.png"));

    QVERIFY(window.findChild<QAction*>(QStringLiteral("alphaAction")) == nullptr);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("checkerboardAction")) == nullptr);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("differenceAction")) == nullptr);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("autoAlignAction")) == nullptr);
    QVERIFY(window.findChild<QTableWidget*>(QStringLiteral("pixelComparisonTable")) == nullptr);

    QImage third(8, 6, QImage::Format_RGBA8888);
    third.fill(Qt::green);
    QImage fourth(8, 6, QImage::Format_RGBA8888);
    fourth.fill(Qt::yellow);
    const QString thirdPath = directory.filePath(QStringLiteral("third.png"));
    const QString fourthPath = directory.filePath(QStringLiteral("fourth.png"));
    QVERIFY(third.save(thirdPath));
    QVERIFY(fourth.save(fourthPath));

    CompareWindow threeImages(&loader, QStringList{leftPath, rightPath, thirdPath});
    const QList<ImageCanvas*> threeCanvases = threeImages.findChildren<ImageCanvas*>();
    QCOMPARE(threeCanvases.size(), 3);
    QTRY_VERIFY_WITH_TIMEOUT(threeCanvases.at(0)->frame() && threeCanvases.at(1)->frame() &&
                                 threeCanvases.at(2)->frame(),
                             5000);
    auto* threeGrid = qobject_cast<QGridLayout*>(threeImages.centralWidget()->layout());
    QVERIFY(threeGrid != nullptr);
    QCOMPARE(threeGrid->columnCount(), 3);
    QCOMPARE(threeGrid->rowCount(), 1);
    QVERIFY(!threeImages.findChild<QAction*>(QStringLiteral("verticalSplitAction"))->isEnabled());
    QVERIFY(!threeImages.findChild<QAction*>(QStringLiteral("horizontalSplitAction"))->isEnabled());
    ViewState threeState = threeCanvases.at(0)->viewState();
    threeState.fitMode = FitMode::Manual;
    threeState.pixelsPerImagePixel = 2.25;
    threeState.normalizedCenter = {0.2, 0.8};
    threeCanvases.at(0)->setViewState(threeState, true);
    for (int index = 1; index < threeCanvases.size(); ++index) {
        QTRY_COMPARE_WITH_TIMEOUT(threeCanvases.at(index)->viewState().pixelsPerImagePixel, 2.25,
                                  2000);
        QCOMPARE(threeCanvases.at(index)->viewState().normalizedCenter, QPointF(0.2, 0.8));
    }

    CompareWindow fourImages(&loader, QStringList{leftPath, rightPath, thirdPath, fourthPath});
    const QList<ImageCanvas*> fourCanvases = fourImages.findChildren<ImageCanvas*>();
    QCOMPARE(fourCanvases.size(), 4);
    QTRY_VERIFY_WITH_TIMEOUT(fourCanvases.at(0)->frame() && fourCanvases.at(1)->frame() &&
                                 fourCanvases.at(2)->frame() && fourCanvases.at(3)->frame(),
                             5000);
    auto* fourGrid = qobject_cast<QGridLayout*>(fourImages.centralWidget()->layout());
    QVERIFY(fourGrid != nullptr);
    QCOMPARE(fourGrid->columnCount(), 2);
    QCOMPARE(fourGrid->rowCount(), 2);
    QCOMPARE(fourGrid->horizontalSpacing(), 2);
    QCOMPARE(fourGrid->verticalSpacing(), 2);
    QVERIFY(fourImages.centralWidget()->styleSheet().contains(
        QStringLiteral("background-color: white")));
    ViewState fourState = fourCanvases.at(0)->viewState();
    fourState.fitMode = FitMode::Manual;
    fourState.pixelsPerImagePixel = 1.75;
    fourState.normalizedCenter = {0.7, 0.3};
    fourCanvases.at(0)->setViewState(fourState, true);
    for (int index = 1; index < fourCanvases.size(); ++index) {
        QTRY_COMPARE_WITH_TIMEOUT(fourCanvases.at(index)->viewState().pixelsPerImagePixel, 1.75,
                                  2000);
        QCOMPARE(fourCanvases.at(index)->viewState().normalizedCenter, QPointF(0.7, 0.3));
    }
}

void UiTests::multiFolderWindowStartsWithFourIndependentBrowsers() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(8, 6, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    for (int index = 0; index < 4; ++index) {
        QVERIFY(image.save(directory.filePath(QStringLiteral("scene%1.png").arg(index))));
    }
    const QString alternateDirectory = directory.filePath(QStringLiteral("alternate"));
    QVERIFY(QDir().mkpath(alternateDirectory));
    const QString alternatePath =
        QDir(alternateDirectory).filePath(QStringLiteral("alternate-only.png"));
    QVERIFY(image.save(alternatePath));
    const QString deepDirectory = directory.filePath(QStringLiteral("nested/deep-images"));
    QVERIFY(QDir().mkpath(deepDirectory));
    QVERIFY(image.save(QDir(deepDirectory).filePath(QStringLiteral("deep.png"))));
    QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("empty/deeper"))));
    const QString documentsDirectory = directory.filePath(QStringLiteral("documents"));
    QVERIFY(QDir().mkpath(documentsDirectory));
    QFile note(QDir(documentsDirectory).filePath(QStringLiteral("notes.txt")));
    QVERIFY(note.open(QIODevice::WriteOnly));
    note.write("not an image");
    note.close();
    auto decoder = std::make_shared<QtImageDecoder>();
    ImageLoader loader(decoder);
    MultiFolderWindow window(&loader, directory.path());
    window.show();
    QCoreApplication::processEvents();
    const QList<ThumbnailView*> views = window.findChildren<ThumbnailView*>();
    QCOMPARE(views.size(), 4);
    QCOMPARE(window.findChildren<QTreeView*>().size(), 0);
    QCOMPARE(window.findChildren<PathBreadcrumb*>().size(), 4);
    for (ThumbnailView* view : views) {
        QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 7, 5000);
        bool foundNestedFolder = false;
        bool foundDeepFolder = false;
        for (int row = 0; row < view->model()->rowCount(); ++row) {
            const QModelIndex candidate = view->model()->index(row, 0);
            if (candidate.data(ThumbnailModel::PathRole).toString() ==
                directory.filePath(QStringLiteral("nested"))) {
                QCOMPARE(candidate.data(Qt::DisplayRole).toString(), QStringLiteral("nested"));
                foundNestedFolder = true;
            }
            if (candidate.data(ThumbnailModel::PathRole).toString() == deepDirectory) {
                QCOMPARE(candidate.data(Qt::DisplayRole).toString(),
                         QStringLiteral("nested/deep-images"));
                foundDeepFolder = true;
            }
            QVERIFY(candidate.data(ThumbnailModel::PathRole).toString() !=
                    directory.filePath(QStringLiteral("empty")));
            QVERIFY(candidate.data(ThumbnailModel::PathRole).toString() != documentsDirectory);
        }
        QVERIFY(foundNestedFolder);
        QVERIFY(foundDeepFolder);
    }
    ThumbnailView* hierarchyView = views.at(2);
    QModelIndex nestedIndex;
    for (int row = 0; row < hierarchyView->model()->rowCount(); ++row) {
        const QModelIndex candidate = hierarchyView->model()->index(row, 0);
        if (candidate.data(ThumbnailModel::PathRole).toString() ==
            directory.filePath(QStringLiteral("nested"))) {
            nestedIndex = candidate;
            break;
        }
    }
    QVERIFY(nestedIndex.isValid());
    hierarchyView->activated(nestedIndex);
    QTRY_COMPARE_WITH_TIMEOUT(hierarchyView->model()->rowCount(), 1, 5000);
    QCOMPARE(hierarchyView->model()->index(0, 0).data(ThumbnailModel::PathRole).toString(),
             deepDirectory);
    QVERIFY(hierarchyView->model()->index(0, 0).data(ThumbnailModel::DirectoryRole).toBool());
    hierarchyView->activated(hierarchyView->model()->index(0, 0));
    QTRY_COMPARE_WITH_TIMEOUT(hierarchyView->model()->rowCount(), 1, 5000);
    QCOMPARE(hierarchyView->model()->index(0, 0).data(Qt::DisplayRole).toString(),
             QStringLiteral("deep.png"));
    QCOMPARE(window.findChildren<QPushButton*>(QStringLiteral("closeFolderPane")).size(), 4);

    auto* firstPane = window.findChild<QWidget*>(QStringLiteral("multiFolderPane0"));
    auto* secondPane = window.findChild<QWidget*>(QStringLiteral("multiFolderPane1"));
    QVERIFY(firstPane && secondPane);
    auto* firstThumbnails =
        firstPane->findChild<ThumbnailView*>(QStringLiteral("multiFolderThumbnails"));
    auto* secondThumbnails =
        secondPane->findChild<ThumbnailView*>(QStringLiteral("multiFolderThumbnails"));
    auto* firstPath =
        firstPane->findChild<PathBreadcrumb*>(QStringLiteral("multiFolderBreadcrumb"));
    auto* secondPath =
        secondPane->findChild<PathBreadcrumb*>(QStringLiteral("multiFolderBreadcrumb"));
    QVERIFY(firstThumbnails && secondThumbnails && firstPath && secondPath);
    QModelIndex alternateDirectoryIndex;
    for (int row = 0; row < firstThumbnails->model()->rowCount(); ++row) {
        const QModelIndex candidate = firstThumbnails->model()->index(row, 0);
        if (candidate.data(ThumbnailModel::PathRole).toString() == alternateDirectory) {
            alternateDirectoryIndex = candidate;
            break;
        }
    }
    QVERIFY(alternateDirectoryIndex.isValid());
    firstThumbnails->activated(alternateDirectoryIndex);
    QTRY_COMPARE_WITH_TIMEOUT(firstThumbnails->model()->rowCount(), 1, 5000);
    QCOMPARE(firstThumbnails->model()->index(0, 0).data(ThumbnailModel::PathRole).toString(),
             alternatePath);
    QCOMPARE(firstPath->path(), alternateDirectory);
    QCOMPARE(secondThumbnails->model()->rowCount(), 7);
    QCOMPARE(secondPath->path(), directory.path());

    QToolButton* parentSegment = nullptr;
    for (QToolButton* segment :
         firstPath->findChildren<QToolButton*>(QStringLiteral("pathBreadcrumbSegment"))) {
        if (segment->property("path").toString() == directory.path()) {
            parentSegment = segment;
            break;
        }
    }
    QVERIFY(parentSegment);
    parentSegment->click();
    QTRY_COMPARE_WITH_TIMEOUT(firstThumbnails->model()->rowCount(), 7, 5000);
    QCOMPARE(firstPath->path(), directory.path());
    // Return to the child folder so the cross-pane selection below continues to prove independent
    // navigation and selection.
    for (int row = 0; row < firstThumbnails->model()->rowCount(); ++row) {
        const QModelIndex candidate = firstThumbnails->model()->index(row, 0);
        if (candidate.data(ThumbnailModel::PathRole).toString() == alternateDirectory) {
            firstThumbnails->activated(candidate);
            break;
        }
    }
    QTRY_COMPARE_WITH_TIMEOUT(firstThumbnails->model()->rowCount(), 1, 5000);

    firstThumbnails->selectionModel()->select(firstThumbnails->model()->index(0, 0),
                                              QItemSelectionModel::ClearAndSelect |
                                                  QItemSelectionModel::Rows);
    QModelIndex secondImage;
    for (int row = 0; row < secondThumbnails->model()->rowCount(); ++row) {
        const QModelIndex candidate = secondThumbnails->model()->index(row, 0);
        if (!candidate.data(ThumbnailModel::DirectoryRole).toBool()) {
            secondImage = candidate;
            break;
        }
    }
    QVERIFY(secondImage.isValid());
    secondThumbnails->selectionModel()->select(secondImage, QItemSelectionModel::ClearAndSelect |
                                                                QItemSelectionModel::Rows);
    auto* compare = window.findChild<QAction*>(QStringLiteral("multiFolderCompareAction"));
    QVERIFY(compare != nullptr);
    compare->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<CompareWindow*>().size(), 1, 3000);
    auto* comparison = window.findChild<CompareWindow*>();
    QVERIFY(comparison != nullptr);
    QCOMPARE(comparison->findChildren<ImageCanvas*>().size(), 2);
    comparison->close();

    auto* firstClose = firstPane->findChild<QPushButton*>(QStringLiteral("closeFolderPane"));
    auto* restore = window.findChild<QAction*>(QStringLiteral("restoreFourPanesAction"));
    QVERIFY(firstPane && firstClose && restore);
    firstClose->click();
    QVERIFY(firstPane->isHidden());
    restore->trigger();
    QVERIFY(!firstPane->isHidden());
    QTRY_COMPARE_WITH_TIMEOUT(firstPane->findChild<ThumbnailView*>()->model()->rowCount(), 7, 5000);
}

void UiTests::rawParameterPanelEmitsDebouncedSingleFrameParameters() {
    RawParameterPanel panel;
    RawImageParameters initial;
    initial.size = {1920, 1080};
    initial.format = RawPixelFormat::NV12;
    initial.frameIndex = 9;
    panel.setSource(QStringLiteral("/tmp/frame.yuv"), initial);
    int changeCount = 0;
    QString changedPath;
    RawImageParameters changedParameters;
    connect(&panel, &RawParameterPanel::parametersChanged, &panel,
            [&](const QString& path, const RawImageParameters& parameters) {
                ++changeCount;
                changedPath = path;
                changedParameters = parameters;
            });
    auto* width = panel.findChild<QSpinBox*>(QStringLiteral("rawPanelWidth"));
    QVERIFY(width != nullptr);
    QVERIFY(panel.findChild<QLabel*>(QStringLiteral("rawPanelFile")) == nullptr);
    QVERIFY(panel.findChild<QLabel*>(QStringLiteral("rawPanelFrameIndex")) == nullptr);
    width->setValue(1280);
    width->setValue(640);
    QTRY_COMPARE_WITH_TIMEOUT(changeCount, 1, 1000);
    QCOMPARE(changedPath, QStringLiteral("/tmp/frame.yuv"));
    QCOMPARE(changedParameters.size, QSize(640, 1080));
    QCOMPARE(changedParameters.frameIndex, 0);
    const int widthBeforeWheel = width->value();
    const QPointF wheelPosition = width->rect().center();
    QWheelEvent spinWheel(wheelPosition, width->mapToGlobal(wheelPosition.toPoint()), {},
                          QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(width, &spinWheel);
    QCOMPARE(width->value(), widthBeforeWheel);

    RawImageParameters raw;
    raw.size = {4000, 3000};
    raw.format = RawPixelFormat::Raw16;
    raw.validBitsOverride = 12;
    raw.orientation = ImageOrientation::Rotate90Clockwise;
    raw.blackLevel = 64;
    raw.whiteLevel = 4095;
    raw.whiteBalanceGains = {2.0, 1.0, 1.5};
    raw.colorCorrectionMatrix = {1.1, -0.1, 0.0, -0.05, 1.05, 0.0, 0.0, -0.2, 1.2};
    raw.displayGamma = 2.4;
    panel.setSource(QStringLiteral("/tmp/frame.raw"), raw);
    auto* demosaic = panel.findChild<QCheckBox*>(QStringLiteral("rawPanelDemosaic"));
    QVERIFY(demosaic != nullptr);
    QVERIFY(!demosaic->isChecked());
    raw.demosaic = true;
    panel.setSource(QStringLiteral("/tmp/frame.raw"), raw);
    const RawImageParameters roundTrip = panel.parameters();
    QCOMPARE(roundTrip.orientation, ImageOrientation::Rotate90Clockwise);
    QVERIFY(roundTrip.demosaic);
    QCOMPARE(roundTrip.blackLevel, 64);
    QCOMPARE(roundTrip.whiteLevel, 4095);
    QCOMPARE(roundTrip.whiteBalanceGains, raw.whiteBalanceGains);
    QCOMPARE(roundTrip.colorCorrectionMatrix, raw.colorCorrectionMatrix);
    QCOMPARE(roundTrip.displayGamma, 2.4);
    QVERIFY(panel.findChild<QComboBox*>(QStringLiteral("rawPanelFormat")) != nullptr);
    QVERIFY(panel.findChild<QSpinBox*>(QStringLiteral("rawPanelBlackLevel")) != nullptr);
    QVERIFY(panel.findChild<QSpinBox*>(QStringLiteral("rawPanelWhiteLevel")) != nullptr);
    QVERIFY(panel.findChild<QDoubleSpinBox*>(QStringLiteral("rawPanelWhiteBalanceR")) != nullptr);
    QVERIFY(panel.findChild<QDoubleSpinBox*>(QStringLiteral("rawPanelCcm22")) != nullptr);
    QVERIFY(panel.findChild<QDoubleSpinBox*>(QStringLiteral("rawPanelGamma")) != nullptr);

    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("ISPViewTests"));
    QCoreApplication::setApplicationName(QStringLiteral("UiRawConfigurationTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    const QString presetName = QStringLiteral("ui-test-raw-configuration");
    (void)RawPresetStore::removeNamedPreset(presetName);
    RawImageParameters presetParameters;
    presetParameters.size = {4000, 3000};
    presetParameters.format = RawPixelFormat::Raw16;
    presetParameters.validBitsOverride = 12;
    presetParameters.whiteLevel = 4095;
    presetParameters.demosaic = true;
    QVERIFY(RawPresetStore::saveNamedPreset(presetName, presetParameters));
    panel.refreshPresets();
    auto* preset = panel.findChild<QComboBox*>(QStringLiteral("rawConfigurationPreset"));
    QVERIFY(preset != nullptr);
    QVERIFY(preset->findText(presetName) > 0);
    const int changesBeforePreset = changeCount;
    preset->setCurrentIndex(preset->findText(presetName));
    QCOMPARE(changeCount, changesBeforePreset + 1);
    QVERIFY(panel.parameters().demosaic);

    bool saveDialogHandled = false;
    QTimer::singleShot(0, this, [&] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (!dialog) {
            return;
        }
        QCOMPARE(dialog->textValue(), QStringLiteral("4000_3000_RAW16"));
        dialog->setTextValue(QStringLiteral("custom-raw-config"));
        saveDialogHandled = true;
        dialog->accept();
    });
    panel.findChild<QPushButton*>(QStringLiteral("saveRawConfiguration"))->click();
    QVERIFY(saveDialogHandled);
    QVERIFY(RawPresetStore::loadNamedPreset(QStringLiteral("custom-raw-config")).has_value());
    auto* deleteConfiguration =
        panel.findChild<QPushButton*>(QStringLiteral("deleteRawConfiguration"));
    QVERIFY(deleteConfiguration != nullptr);
    QVERIFY(deleteConfiguration->isEnabled());
    deleteConfiguration->click();
    QVERIFY(!RawPresetStore::loadNamedPreset(QStringLiteral("custom-raw-config")).has_value());
    QCOMPARE(preset->currentIndex(), 0);
    QVERIFY(!deleteConfiguration->isEnabled());

    QSignalSpy folderApplied(&panel, &RawParameterPanel::folderParametersApplied);
    auto* applyFolder =
        panel.findChild<QPushButton*>(QStringLiteral("applyRawConfigurationToFolder"));
    QVERIFY(applyFolder != nullptr);
    applyFolder->click();
    QCOMPARE(folderApplied.count(), 1);
    QVERIFY(RawPresetStore::loadForFile(QStringLiteral("/tmp/another.raw")).has_value());
    QVERIFY(RawPresetStore::removeNamedPreset(presetName));
}

void UiTests::fullScreenNavigatesEncodedImagesWithKeyboardAndButtons() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage red(8, 6, QImage::Format_RGBA8888);
    red.fill(Qt::red);
    QImage blue(8, 6, QImage::Format_RGBA8888);
    blue.fill(Qt::blue);
    const QString redPath = directory.filePath(QStringLiteral("01-red.png"));
    const QString bluePath = directory.filePath(QStringLiteral("02-blue.png"));
    QVERIFY(red.save(redPath));
    QVERIFY(blue.save(bluePath));

    auto decoder = std::make_shared<QtImageDecoder>();
    ImageLoader loader(decoder);
    FullScreenWindow window(&loader, {redPath, bluePath}, 0);
    auto* canvas = window.findChild<ImageCanvas*>();
    auto* navigation =
        window.findChild<NavigationThumbnailOverlay*>(QStringLiteral("navigationThumbnailOverlay"));
    auto* fileName = window.findChild<QLabel*>(QStringLiteral("fullScreenFileName"));
    auto* position = window.findChild<QLabel*>(QStringLiteral("fullScreenPosition"));
    auto* previous = window.findChild<QToolButton*>(QStringLiteral("fullScreenPreviousImage"));
    auto* next = window.findChild<QToolButton*>(QStringLiteral("fullScreenNextImage"));
    QVERIFY(canvas && navigation && fileName && position && previous && next);
    QVERIFY(canvas->navigationThumbnailEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(canvas->frame() != nullptr, 3000);
    QVERIFY(navigation->isHidden());
    canvas->actualPixels();
    QVERIFY(!navigation->isHidden());
    QCOMPARE(navigation->zoomText(), QStringLiteral("100%"));
    canvas->fitImage();
    QVERIFY(navigation->isHidden());
    QCOMPARE(canvas->frame()->metadata.fileName, QStringLiteral("01-red.png"));
    QCOMPARE(fileName->text(), QStringLiteral("01-red.png"));
    QCOMPARE(position->text(), QStringLiteral("1 / 2"));

    QTest::keyClick(&window, Qt::Key_Right);
    QTRY_COMPARE_WITH_TIMEOUT(canvas->frame()->metadata.fileName, QStringLiteral("02-blue.png"),
                              3000);
    QCOMPARE(fileName->text(), QStringLiteral("02-blue.png"));
    QCOMPARE(position->text(), QStringLiteral("2 / 2"));

    previous->click();
    QTRY_COMPARE_WITH_TIMEOUT(canvas->frame()->metadata.fileName, QStringLiteral("01-red.png"),
                              3000);
    QTest::keyClick(&window, Qt::Key_Left);
    QTest::qWait(100);
    QCOMPARE(canvas->frame()->metadata.fileName, QStringLiteral("01-red.png"));

    QTest::keyClick(&window, Qt::Key_Space);
    QTRY_COMPARE_WITH_TIMEOUT(canvas->frame()->metadata.fileName, QStringLiteral("02-blue.png"),
                              3000);
    next->click();
    QTest::qWait(100);
    QCOMPARE(canvas->frame()->metadata.fileName, QStringLiteral("02-blue.png"));
}

void UiTests::fullScreenUsesEdgePanelsContextActionsAndNoTimeline() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("fullscreen_sequence.yuv"));
    const QByteArray black = QByteArray::fromRawData("\x10\x10\x10\x10\x80\x80", 6);
    const QByteArray white = QByteArray::fromRawData("\xEB\xEB\xEB\xEB\x80\x80", 6);
    const QByteArray gray = QByteArray::fromRawData("\x80\x80\x80\x80\x80\x80", 6);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(black + white + gray), 18);
    file.close();

    auto decoder = std::make_shared<RawImageDecoder>();
    ImageLoader loader(decoder);
    RawImageParameters parameters;
    parameters.size = {2, 2};
    parameters.format = RawPixelFormat::NV12;
    loader.setRawParameters(path, parameters);
    FullScreenWindow window(&loader, {path}, 0);
    window.resize(800, 600);
    if (QGuiApplication::primaryScreen()) {
        window.show();
    }

    auto* top = window.findChild<QFrame*>(QStringLiteral("fullScreenTopPanel"));
    auto* left = window.findChild<QFrame*>(QStringLiteral("fullScreenLeftPanel"));
    auto* right = window.findChild<QFrame*>(QStringLiteral("fullScreenRightPanel"));
    auto* bottom = window.findChild<QFrame*>(QStringLiteral("fullScreenBottomPanel"));
    auto* properties =
        window.findChild<ImagePropertiesPanel*>(QStringLiteral("fullScreenImagePropertiesPanel"));
    auto* information = window.findChild<ImageInfoPanel*>(QStringLiteral("imageInfoPanel"));
    auto* basic = window.findChild<ImageInfoPanel*>(QStringLiteral("basicInformationPanel"));
    auto* histogram = window.findChild<HistogramPanel*>(QStringLiteral("histogramPanel"));
    auto* rawTable = window.findChild<QTreeWidget*>(QStringLiteral("rawParameterFields"));
    auto* contextMenu = window.findChild<QMenu*>(QStringLiteral("fullScreenContextMenu"));
    auto* reveal = window.findChild<QAction*>(QStringLiteral("fullScreenRevealAction"));
    auto* showInformation =
        window.findChild<QAction*>(QStringLiteral("fullScreenShowInformationAction"));
    auto* showHistogram =
        window.findChild<QAction*>(QStringLiteral("fullScreenShowHistogramAction"));
    auto* canvas = window.findChild<ImageCanvas*>();
    QVERIFY(top && left && right && bottom && properties && information && basic && histogram &&
            rawTable && contextMenu && reveal && showInformation && showHistogram && canvas);
    QVERIFY(
        right->styleSheet().contains(QStringLiteral("background-color: rgba(255,255,255,246)")));
    QVERIFY(right->styleSheet().contains(QStringLiteral("color: #202124")));
    QVERIFY(!right->styleSheet().contains(QStringLiteral("rgba(20, 20, 22, 225)")));
    QCOMPARE(showInformation->text(), QStringLiteral("Show Properties"));
    QVERIFY(!window.findChild<QWidget*>(QStringLiteral("fullScreenFrameControls")));
    QVERIFY(!window.findChild<QSlider*>(QStringLiteral("fullScreenFrameSlider")));
    QVERIFY(!window.findChild<QSpinBox*>(QStringLiteral("fullScreenFrameSpin")));
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("fullScreenPlayFrames")));
    QCOMPARE(canvas->contextMenuPolicy(), Qt::CustomContextMenu);
    QCOMPARE(contextMenu->actions().size(), 4);
    QVERIFY(top->isHidden());
    QVERIFY(left->isHidden());
    QVERIFY(right->isHidden());
    QVERIFY(bottom->isHidden());

    QTRY_VERIFY_WITH_TIMEOUT(canvas->frame() && canvas->frame()->rawParameters, 3000);
    QCOMPARE(canvas->frame()->rawParameters->frameIndex, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!basic->valueForField(QStringLiteral("File Name")).isEmpty(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(histogram->histogram().has_value(), 5000);
    QVERIFY(properties->tabs()->isTabEnabled(2));

    const auto moveTo = [canvas](const QPointF& position) {
        QMouseEvent move(QEvent::MouseMove, position, position, Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &move);
    };
    moveTo(QPointF(canvas->width() / 2.0, 1.0));
    QVERIFY(!top->isHidden());
    moveTo(QPointF(1.0, canvas->height() / 2.0));
    QVERIFY(!left->isHidden());
    moveTo(QPointF(canvas->width() / 2.0, canvas->height() - 1.0));
    QVERIFY(!bottom->isHidden());
    moveTo(QPointF(canvas->width() - 1.0, canvas->height() / 2.0));
    QVERIFY(!right->isHidden());
    QCOMPARE(properties->tabs()->currentWidget(), static_cast<QWidget*>(information));

    showHistogram->trigger();
    QVERIFY(showHistogram->isChecked());
    QVERIFY(!showInformation->isChecked());
    QVERIFY(!right->isHidden());
    QCOMPARE(properties->tabs()->currentWidget(), static_cast<QWidget*>(histogram));
    moveTo(QPointF(canvas->width() / 2.0, canvas->height() / 2.0));
    QTest::qWait(1100);
    QVERIFY(!right->isHidden());

    showInformation->trigger();
    QVERIFY(showInformation->isChecked());
    QVERIFY(!showHistogram->isChecked());
    QCOMPARE(properties->tabs()->currentWidget(), static_cast<QWidget*>(information));
    showInformation->trigger();
    QVERIFY(!showInformation->isChecked());
    QVERIFY(right->isHidden());

    QTest::keyClick(&window, Qt::Key_BracketRight);
    QTest::keyClick(&window, Qt::Key_P);
    QCOMPARE(loader.rawParameters(path)->frameIndex, 0);
}

} // namespace ispview

QTEST_MAIN(ispview::UiTests)
#include "ui_tests.moc"
