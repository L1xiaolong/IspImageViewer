#include "ui/main_window.h"

#include "core/raw_plane_access.h"
#include "io/directory_scanner.h"
#include "io/drop_copy_operation.h"
#include "io/image_loader.h"
#include "io/raw_preset_store.h"
#include "io/single_file_rename.h"
#include "platform/platform_services.h"
#include "platform/platform_shortcuts.h"
#include "render/image_canvas.h"
#include "ui/compare_window.h"
#include "ui/file_clipboard.h"
#include "ui/full_screen_window.h"
#include "ui/image_properties_panel.h"
#include "ui/local_file_drop.h"
#include "ui/multi_folder_window.h"
#include "ui/raw_parameter_panel.h"
#include "ui/thumbnail_filter_proxy_model.h"
#include "ui/thumbnail_model.h"
#include "ui/thumbnail_view.h"
#include "ui/trash_confirmation.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QModelIndex>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTextEdit>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <optional>
#include <utility>

namespace ispview {
namespace {

bool isTextEditingWidget(const QWidget* widget) {
    if (!widget) {
        return false;
    }
    const auto* comboBox = qobject_cast<const QComboBox*>(widget);
    return qobject_cast<const QLineEdit*>(widget) || qobject_cast<const QTextEdit*>(widget) ||
           qobject_cast<const QPlainTextEdit*>(widget) ||
           qobject_cast<const QAbstractSpinBox*>(widget) || (comboBox && comboBox->isEditable());
}

} // namespace

MainWindow::MainWindow(std::shared_ptr<const IImageDecoder> decoder,
                       const QString& initialDirectory, QWidget* parent)
    : QMainWindow(parent), loader_(new ImageLoader(std::move(decoder), this)),
      scanner_(new DirectoryScanner(this)), thumbnailModel_(new ThumbnailModel(loader_, this)),
      filterModel_(new ThumbnailFilterProxyModel(this)),
      fileSystemModel_(new QFileSystemModel(this)), directoryTree_(new QTreeView(this)),
      thumbnailView_(new ThumbnailView(this)), previewCanvas_(new ImageCanvas(this)),
      previewPanel_(new QWidget(this)), previewInfoLabel_(new QLabel(previewPanel_)),
      previewZoomLabel_(new QLabel(previewPanel_)),
      imagePropertiesPanel_(new ImagePropertiesPanel(this)), imagePropertiesDock_(nullptr),
      rawConfigurationPanel_(new RawParameterPanel(this)), rawConfigurationDock_(nullptr),
      sortMode_(BrowserSortMode::Name), directoryWatcher_(new QFileSystemWatcher(this)),
      refreshTimer_(new QTimer(this)) {
    buildInterface();
    connectInterface();
    QSettings settings;
    const int savedSortMode = settings.value(QStringLiteral("browser/sortMode"), 0).toInt();
    if (savedSortMode >= static_cast<int>(BrowserSortMode::Name) &&
        savedSortMode <= static_cast<int>(BrowserSortMode::Type)) {
        sortMode_ = static_cast<BrowserSortMode>(savedSortMode);
    }
    filterModel_->setSortMode(sortMode_);
    const bool previewVisible =
        settings.value(QStringLiteral("browser/previewVisible"), true).toBool();
    previewToggleAction_->setChecked(previewVisible);
    previewPanel_->setVisible(previewVisible);
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    const QString lastDirectory =
        initialDirectory.isEmpty()
            ? settings.value(QStringLiteral("browser/lastDirectory"), QDir::homePath()).toString()
            : initialDirectory;
    openDirectory(lastDirectory);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("browser/lastDirectory"), currentDirectory_);
    settings.setValue(QStringLiteral("browser/sortMode"), static_cast<int>(sortMode_));
    settings.setValue(QStringLiteral("browser/previewVisible"), previewPanel_->isVisible());
    QMainWindow::closeEvent(event);
}

void MainWindow::buildInterface() {
    setWindowTitle(QStringLiteral("ISP Image Viewer"));
    resize(1400, 850);

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    fileMenu->setObjectName(QStringLiteral("fileMenu"));
    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    editMenu->setObjectName(QStringLiteral("editMenu"));
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    viewMenu->setObjectName(QStringLiteral("viewMenu"));
    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
    toolsMenu->setObjectName(QStringLiteral("toolsMenu"));

    auto* toolbar = addToolBar(QStringLiteral("Browser"));
    toolbar->setObjectName(QStringLiteral("browserToolbar"));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    backAction_ =
        toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowBack), QStringLiteral("Back"));
    backAction_->setObjectName(QStringLiteral("backAction"));
    backAction_->setShortcut(QKeySequence::Back);
    forwardAction_ = toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowForward),
                                        QStringLiteral("Forward"));
    forwardAction_->setObjectName(QStringLiteral("forwardAction"));
    forwardAction_->setShortcut(QKeySequence::Forward);
    upAction_ = toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowUp), QStringLiteral("Up"));
    upAction_->setObjectName(QStringLiteral("upAction"));
    upAction_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    toolbar->addSeparator();

    QAction* openAction =
        toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), QStringLiteral("Open"));
    openAction->setObjectName(QStringLiteral("openAction"));
    openAction->setShortcut(QKeySequence::Open);
    fileMenu->addAction(openAction);
    newFolderAction_ =
        new QAction(style()->standardIcon(QStyle::SP_DirIcon), QStringLiteral("New Folder…"), this);
    newFolderAction_->setObjectName(QStringLiteral("newFolderAction"));
    newFolderAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    toolbar->addAction(newFolderAction_);
    fileMenu->addAction(newFolderAction_);

    copyAction_ = new QAction(QStringLiteral("Copy"), this);
    copyAction_->setObjectName(QStringLiteral("copyAction"));
    copyAction_->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    copyAction_->setShortcut(QKeySequence::Copy);
    copyAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    cutAction_ = new QAction(QStringLiteral("Cut"), this);
    cutAction_->setObjectName(QStringLiteral("cutAction"));
    cutAction_->setIcon(QIcon::fromTheme(QStringLiteral("edit-cut")));
    cutAction_->setShortcut(QKeySequence::Cut);
    cutAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    pasteAction_ = new QAction(QStringLiteral("Paste"), this);
    pasteAction_->setObjectName(QStringLiteral("pasteAction"));
    pasteAction_->setIcon(QIcon::fromTheme(QStringLiteral("edit-paste")));
    pasteAction_->setShortcut(QKeySequence::Paste);
    pasteAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    renameAction_ = new QAction(QStringLiteral("Rename…"), this);
    renameAction_->setObjectName(QStringLiteral("renameAction"));
    renameAction_->setShortcut(QKeySequence(Qt::Key_F2));
    renameAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    propertiesAction_ = new QAction(QStringLiteral("Properties"), this);
    propertiesAction_->setObjectName(QStringLiteral("propertiesAction"));
    propertiesAction_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
    propertiesAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    copyAction_->setEnabled(false);
    cutAction_->setEnabled(false);
    renameAction_->setEnabled(false);
    propertiesAction_->setEnabled(false);
    editMenu->addActions({cutAction_, copyAction_, pasteAction_});
    editMenu->addSeparator();
    editMenu->addAction(renameAction_);
    editMenu->addAction(propertiesAction_);
    toolbar->addActions({cutAction_, copyAction_, pasteAction_});
    toolbar->addSeparator();
    compareAction_ = toolbar->addAction(QStringLiteral("Compare"));
    compareAction_->setObjectName(QStringLiteral("compareAction"));
    QAction* multiFolderAction = toolsMenu->addAction(QStringLiteral("Multi-Folder Browser"));
    multiFolderAction->setObjectName(QStringLiteral("multiFolderAction"));
    connect(multiFolderAction, &QAction::triggered, this, [this] {
        auto* window = new MultiFolderWindow(loader_, currentDirectory_, this);
        window->showMaximized();
    });
    QAction* fullScreenAction = new QAction(QStringLiteral("Full Screen"), this);
    fullScreenAction->setObjectName(QStringLiteral("fullScreenAction"));
    connect(fullScreenAction, &QAction::triggered, this, &MainWindow::openFullScreen);
    QAction* fitAction = new QAction(QStringLiteral("Fit"), this);
    fitAction->setObjectName(QStringLiteral("fitAction"));
    QAction* actualAction = new QAction(QStringLiteral("1:1"), this);
    actualAction->setObjectName(QStringLiteral("actualAction"));
    rawParametersAction_ = new QAction(QStringLiteral("RAW Parameters…"), this);
    rawParametersAction_->setObjectName(QStringLiteral("rawParametersAction"));
    rawParametersAction_->setEnabled(false);

    fileSystemModel_->setFilter(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Drives);
    fileSystemModel_->setRootPath(QDir::rootPath());
    directoryTree_->setModel(fileSystemModel_);
    directoryTree_->setObjectName(QStringLiteral("directoryTree"));
    directoryTree_->setHeaderHidden(true);
    for (int column = 1; column < fileSystemModel_->columnCount(); ++column) {
        directoryTree_->hideColumn(column);
    }

    filterModel_->setSourceModel(thumbnailModel_);
    thumbnailView_->setModel(filterModel_);
    thumbnailView_->setObjectName(QStringLiteral("thumbnailView"));
    thumbnailView_->setViewMode(QListView::IconMode);
    thumbnailView_->setResizeMode(QListView::Adjust);
    thumbnailView_->setMovement(QListView::Static);
    thumbnailView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    thumbnailView_->setIconSize({160, 120});
    thumbnailView_->setGridSize({194, 190});
    thumbnailView_->setSpacing(2);
    thumbnailView_->setWordWrap(true);
    thumbnailView_->setTextElideMode(Qt::ElideMiddle);
    thumbnailView_->setUniformItemSizes(true);
    thumbnailView_->setContextMenuPolicy(Qt::CustomContextMenu);
    // The large preview is the natural system drop target.  MainWindow owns the file-navigation
    // behavior through an event filter so the render-only ImageCanvas remains unaware of paths.
    previewCanvas_->setAcceptDrops(true);
    previewCanvas_->installEventFilter(this);

    trashAction_ = new QAction(QStringLiteral("Move to Trash"), this);
    trashAction_->setObjectName(QStringLiteral("trashAction"));
    trashAction_->setEnabled(false);
    editMenu->addSeparator();
    editMenu->addAction(trashAction_);
    thumbnailView_->addActions(
        {cutAction_, copyAction_, pasteAction_, renameAction_, trashAction_, propertiesAction_});
    connect(trashAction_, &QAction::triggered, this, &MainWindow::trashSelectedFiles);
    connect(thumbnailView_, &ThumbnailView::trashShortcutRequested, this,
            &MainWindow::trashSelectedFiles);
    connect(thumbnailView_, &ThumbnailView::renameShortcutRequested, this,
            &MainWindow::renameSelectedFile);
    connect(thumbnailView_, &ThumbnailView::externalDragStarted, this, [this](qsizetype fileCount) {
        statusBar()->showMessage(
            QStringLiteral("Dragging %1 item(s) to Finder / Explorer…").arg(fileCount));
    });
    connect(thumbnailView_, &ThumbnailView::externalDragFinished, this,
            [this](Qt::DropAction result) {
                statusBar()->showMessage(
                    result == Qt::CopyAction
                        ? QStringLiteral("Copy handed to Finder / Explorer")
                        : QStringLiteral("Drag canceled or the target did not accept the copy"),
                    4000);
            });
    connect(thumbnailView_, &ThumbnailView::externalDropEntered, this, [this](qsizetype pathCount) {
        statusBar()->showMessage(
            QStringLiteral("Drop to copy %1 item(s) into the current folder").arg(pathCount));
    });
    connect(thumbnailView_, &ThumbnailView::externalDropRejected, this,
            [this](const QString& formats) {
                statusBar()->showMessage(QStringLiteral("Unsupported drop data (%1)").arg(formats),
                                         5000);
            });

    // Platform delete commands are window-wide. Install one application event filter instead of
    // relying on the focus widget's shortcut resolution; text editors are explicitly excluded in
    // eventFilter() so normal editing keeps working.
    qApp->installEventFilter(this);

    auto* compareShortcut = new QShortcut(QKeySequence(Qt::Key_C), thumbnailView_);
    compareShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(compareShortcut, &QShortcut::activated, this, &MainWindow::openComparison);

    auto* toolbarSpacer = new QWidget(toolbar);
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(toolbarSpacer);
    auto* searchEdit = new QLineEdit(toolbar);
    searchEdit->setObjectName(QStringLiteral("browserSearchEdit"));
    searchEdit->setPlaceholderText(QStringLiteral("Search this folder"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMaximumWidth(260);
    connect(searchEdit, &QLineEdit::textChanged, filterModel_,
            &QSortFilterProxyModel::setFilterFixedString);
    toolbar->addWidget(searchEdit);

    previewPanel_->setObjectName(QStringLiteral("previewPanel"));
    auto* previewLayout = new QVBoxLayout(previewPanel_);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(0);
    previewLayout->addWidget(previewCanvas_, 1);
    auto* previewInfoBar = new QWidget(previewPanel_);
    previewInfoBar->setObjectName(QStringLiteral("previewInfoBar"));
    auto* previewInfoLayout = new QHBoxLayout(previewInfoBar);
    previewInfoLayout->setContentsMargins(6, 3, 4, 3);
    previewInfoLayout->setSpacing(5);
    previewInfoLabel_->setObjectName(QStringLiteral("previewInfoLabel"));
    previewInfoLabel_->setText(QStringLiteral("No image selected"));
    // Ignore changing text width when computing the splitter's minimum size. Otherwise selecting
    // files with different metadata can make the entire main layout move by a few pixels.
    previewInfoLabel_->setMinimumWidth(0);
    previewInfoLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    previewZoomLabel_->setObjectName(QStringLiteral("previewZoomLabel"));
    previewZoomLabel_->setText(QStringLiteral("—"));
    previewZoomLabel_->setFixedWidth(52);
    previewZoomLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto makePreviewButton = [previewInfoBar, previewInfoLayout](QAction* action,
                                                                 const QString& name) {
        auto* button = new QToolButton(previewInfoBar);
        button->setObjectName(name);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        previewInfoLayout->addWidget(button);
    };
    previewInfoLayout->addWidget(previewInfoLabel_, 1);
    previewInfoLayout->addWidget(previewZoomLabel_);
    makePreviewButton(actualAction, QStringLiteral("previewActualButton"));
    makePreviewButton(fitAction, QStringLiteral("previewFitButton"));
    makePreviewButton(fullScreenAction, QStringLiteral("previewFullScreenButton"));
    previewInfoBar->setFixedHeight(previewInfoBar->sizeHint().height());
    previewLayout->addWidget(previewInfoBar);

    auto* leftSplitter = new QSplitter(Qt::Vertical, this);
    leftSplitter->setObjectName(QStringLiteral("browserLeftSplitter"));
    leftSplitter->addWidget(directoryTree_);
    leftSplitter->addWidget(previewPanel_);
    leftSplitter->setSizes({500, 300});
    leftSplitter->setChildrenCollapsible(false);
    leftSplitter->setStretchFactor(0, 1);
    leftSplitter->setStretchFactor(1, 1);
    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setObjectName(QStringLiteral("browserMainSplitter"));
    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(thumbnailView_);
    mainSplitter->setSizes({380, 1020});
    mainSplitter->setChildrenCollapsible(false);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    setCentralWidget(mainSplitter);

    imagePropertiesDock_ = new QDockWidget(QStringLiteral("Properties"), this);
    imagePropertiesDock_->setObjectName(QStringLiteral("imageInfoDock"));
    imagePropertiesDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    imagePropertiesDock_->setFeatures(QDockWidget::DockWidgetClosable |
                                      QDockWidget::DockWidgetMovable |
                                      QDockWidget::DockWidgetFloatable);
    imagePropertiesDock_->setWidget(imagePropertiesPanel_);
    addDockWidget(Qt::RightDockWidgetArea, imagePropertiesDock_);
    imagePropertiesDock_->setFloating(true);
    imagePropertiesDock_->resize(500, 740);
    imagePropertiesDock_->hide();
    QAction* imageInformationToggle = imagePropertiesDock_->toggleViewAction();
    imageInformationToggle->setObjectName(QStringLiteral("imageInformationToggleAction"));
    imageInformationToggle->setText(QStringLiteral("Properties"));
    viewMenu->addAction(imageInformationToggle);

    rawConfigurationDock_ = new QDockWidget(QStringLiteral("RAW/YUV Configuration"), this);
    rawConfigurationDock_->setObjectName(QStringLiteral("rawConfigurationDock"));
    rawConfigurationPanel_->setObjectName(QStringLiteral("rawConfigurationPanel"));
    rawConfigurationDock_->setAllowedAreas(Qt::RightDockWidgetArea);
    rawConfigurationDock_->setFeatures(QDockWidget::DockWidgetClosable |
                                       QDockWidget::DockWidgetMovable);
    rawConfigurationDock_->setWidget(rawConfigurationPanel_);
    addDockWidget(Qt::RightDockWidgetArea, rawConfigurationDock_);
    rawConfigurationDock_->setMinimumWidth(360);
    rawConfigurationDock_->hide();

    connect(openAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Open Folder"),
                                                               currentDirectory_);
        if (!path.isEmpty()) {
            openDirectory(path);
        }
    });
    connect(backAction_, &QAction::triggered, this, &MainWindow::navigateBack);
    connect(forwardAction_, &QAction::triggered, this, &MainWindow::navigateForward);
    connect(upAction_, &QAction::triggered, this, &MainWindow::navigateUp);
    connect(newFolderAction_, &QAction::triggered, this, &MainWindow::createFolder);
    connect(copyAction_, &QAction::triggered, this, [this] { copySelectedItems(false); });
    connect(cutAction_, &QAction::triggered, this, [this] { copySelectedItems(true); });
    connect(pasteAction_, &QAction::triggered, this, [this] { pasteItems(); });
    connect(renameAction_, &QAction::triggered, this, &MainWindow::renameSelectedFile);
    connect(propertiesAction_, &QAction::triggered, this, &MainWindow::showSelectedProperties);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
            [this] { pasteAction_->setEnabled(FileClipboard::hasFiles()); });
    pasteAction_->setEnabled(FileClipboard::hasFiles());

    QAction* selectAllAction = editMenu->addAction(QStringLiteral("Select All"));
    selectAllAction->setObjectName(QStringLiteral("selectAllAction"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, thumbnailView_, &QListView::selectAll);

    previewToggleAction_ = viewMenu->addAction(QStringLiteral("Preview"));
    previewToggleAction_->setObjectName(QStringLiteral("previewToggleAction"));
    previewToggleAction_->setCheckable(true);
    previewToggleAction_->setChecked(true);
    connect(previewToggleAction_, &QAction::toggled, previewPanel_, &QWidget::setVisible);
    connect(fitAction, &QAction::triggered, previewCanvas_, &ImageCanvas::fitImage);
    connect(actualAction, &QAction::triggered, previewCanvas_, &ImageCanvas::actualPixels);
    connect(rawParametersAction_, &QAction::triggered, this, &MainWindow::editRawParameters);
    connect(rawConfigurationPanel_, &RawParameterPanel::parametersChanged, this,
            [this](const QString& path, RawImageParameters parameters) {
                parameters.frameIndex = 0;
                if (availableFrameCount(QFileInfo(path).size(), parameters) <= 0) {
                    statusBar()->showMessage(
                        QStringLiteral("RAW/YUV parameters do not describe one complete frame"),
                        3500);
                    return;
                }
                loader_->setRawParameters(path, parameters);
                thumbnailModel_->invalidateThumbnail(path);
                if (path == currentPath_) {
                    loadPreview(path, false);
                }
            });
    connect(rawConfigurationPanel_, &RawParameterPanel::folderParametersApplied, this,
            [this](const QString& sourcePath, RawImageParameters parameters) {
                parameters.frameIndex = 0;
                const QFileInfo source(sourcePath);
                for (const QString& path : allPaths()) {
                    const QFileInfo candidate(path);
                    if (candidate.absolutePath() == source.absolutePath() &&
                        candidate.suffix().compare(source.suffix(), Qt::CaseInsensitive) == 0) {
                        loader_->setRawParameters(path, parameters);
                        thumbnailModel_->invalidateThumbnail(path);
                    }
                }
                const QFileInfo current(currentPath_);
                if (current.absolutePath() == source.absolutePath() &&
                    current.suffix().compare(source.suffix(), Qt::CaseInsensitive) == 0) {
                    loadPreview(currentPath_, false);
                }
                rescanCurrentDirectory();
            });
    connect(rawConfigurationPanel_, &RawParameterPanel::notificationRequested, this,
            [this](const QString& message, bool error) {
                if (error) {
                    QMessageBox::warning(this, QStringLiteral("RAW/YUV Configuration"), message);
                } else {
                    statusBar()->showMessage(message, 3500);
                }
            });
}

void MainWindow::connectInterface() {
    connect(scanner_, &DirectoryScanner::scanFinished, this,
            [this](const QString& directory, const QVector<ImageFileRecord>& files,
                   quint64 generation) {
                if (generation != scanGeneration_ || directory != currentDirectory_) {
                    return;
                }
                const bool incremental = generation == incrementalScanGeneration_;
                const QString pathToRestore = currentPath_;
                if (incremental) {
                    thumbnailModel_->updateFiles(files);
                } else {
                    thumbnailModel_->setFiles(files);
                }
                const qsizetype folderCount =
                    std::count_if(files.cbegin(), files.cend(),
                                  [](const ImageFileRecord& item) { return item.isDirectory; });
                statusBar()->showMessage(QStringLiteral("%1 folders, %2 images — %3")
                                             .arg(folderCount)
                                             .arg(files.size() - folderCount)
                                             .arg(currentDirectory_));
                if (!files.isEmpty()) {
                    QModelIndex target;
                    const QString preferredPath =
                        !pendingSelectionPath_.isEmpty() ? pendingSelectionPath_ : pathToRestore;
                    for (int row = 0; row < filterModel_->rowCount(); ++row) {
                        const QModelIndex candidate = filterModel_->index(row, 0);
                        if (!preferredPath.isEmpty() &&
                            candidate.data(ThumbnailModel::PathRole).toString() == preferredPath) {
                            target = candidate;
                            break;
                        }
                    }
                    if (!target.isValid()) {
                        for (int row = 0; row < filterModel_->rowCount(); ++row) {
                            const QModelIndex candidate = filterModel_->index(row, 0);
                            if (!candidate.data(ThumbnailModel::DirectoryRole).toBool()) {
                                target = candidate;
                                break;
                            }
                        }
                    }
                    pendingSelectionPath_.clear();
                    if (target.isValid()) {
                        thumbnailView_->setCurrentIndex(target);
                        if (!target.data(ThumbnailModel::DirectoryRole).toBool()) {
                            loadPreview(target.data(ThumbnailModel::PathRole).toString());
                        }
                    }
                } else {
                    currentPath_.clear();
                    previewCanvas_->setFrame({});
                    imagePropertiesPanel_->setFrame({});
                }
            });

    connect(directoryTree_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        const QString path = fileSystemModel_->filePath(index);
        if (QFileInfo(path).isDir()) {
            openDirectory(path);
        }
    });
    connect(compareAction_, &QAction::triggered, this, &MainWindow::openComparison);
    connect(thumbnailView_, &QListView::customContextMenuRequested, this,
            &MainWindow::showThumbnailContextMenu);
    connect(thumbnailView_, &ThumbnailView::localPathsDropped, this, &MainWindow::copyDroppedPaths);

    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(250);
    connect(directoryWatcher_, &QFileSystemWatcher::directoryChanged, refreshTimer_,
            qOverload<>(&QTimer::start));
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::rescanCurrentDirectory);

    connect(thumbnailView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (current.isValid()) {
                    if (current.data(ThumbnailModel::DirectoryRole).toBool()) {
                        currentPath_.clear();
                        rawParametersAction_->setEnabled(false);
                        rawConfigurationPanel_->clearSource();
                        previewCanvas_->setFrame({});
                        imagePropertiesPanel_->setFrame({});
                        return;
                    }
                    loadPreview(current.data(ThumbnailModel::PathRole).toString());
                    prefetchNeighbors(current.row());
                }
            });
    connect(thumbnailView_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        const QStringList items = selectedItemPaths();
        copyAction_->setEnabled(!items.isEmpty());
        cutAction_->setEnabled(!items.isEmpty());
        trashAction_->setEnabled(!items.isEmpty());
        renameAction_->setEnabled(items.size() == 1);
        propertiesAction_->setEnabled(items.size() == 1);
        compareAction_->setEnabled(selectedPaths().size() >= 2 && selectedPaths().size() <= 4);
    });
    connect(thumbnailView_, &QListView::activated, this, [this](const QModelIndex& index) {
        if (index.data(ThumbnailModel::DirectoryRole).toBool()) {
            openDirectory(index.data(ThumbnailModel::PathRole).toString());
        } else {
            openFullScreen();
        }
    });
    connect(previewCanvas_, &ImageCanvas::activated, this, &MainWindow::openFullScreen);
    connect(previewCanvas_, &ImageCanvas::viewStateChanged, this,
            [this](const ViewState&) { updateStatusBase(); });
    connect(previewCanvas_, &ImageCanvas::pixelHovered, this,
            [this](const QPoint& pixel, const QColor& color, bool valid) {
                pixelStatus_ = valid ? QStringLiteral("x:%1 y:%2  RGBA(%3,%4,%5,%6)")
                                           .arg(pixel.x())
                                           .arg(pixel.y())
                                           .arg(color.red())
                                           .arg(color.green())
                                           .arg(color.blue())
                                           .arg(color.alpha())
                                     : QString{};
                if (valid && previewCanvas_->frame() && previewCanvas_->frame()->rawParameters) {
                    pixelStatus_ +=
                        QStringLiteral("  ") + RawPlaneAccessor(*previewCanvas_->frame())
                                                   .pixelDescriptionAtDisplayPixel(pixel);
                }
                statusBar()->showMessage(pixelStatus_);
            });
    connect(previewCanvas_, &ImageCanvas::roiChanged, this, [this](const QRectF& roi, bool valid) {
        imagePropertiesPanel_->setNormalizedRegion(valid ? std::optional<QRectF>(roi)
                                                         : std::nullopt);
    });
}

void MainWindow::openDirectory(const QString& path, bool addToHistory) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        statusBar()->showMessage(QStringLiteral("Folder does not exist: %1").arg(path), 4000);
        return;
    }
    currentDirectory_ = info.absoluteFilePath();
    if (addToHistory && (navigationHistoryIndex_ < 0 ||
                         navigationHistory_.value(navigationHistoryIndex_) != currentDirectory_)) {
        while (navigationHistory_.size() > navigationHistoryIndex_ + 1) {
            navigationHistory_.removeLast();
        }
        navigationHistory_.append(currentDirectory_);
        navigationHistoryIndex_ = static_cast<int>(navigationHistory_.size()) - 1;
    }
    updateNavigationActions();
    directoryTree_->setCurrentIndex(fileSystemModel_->index(currentDirectory_));

    if (!directoryWatcher_->directories().isEmpty()) {
        directoryWatcher_->removePaths(directoryWatcher_->directories());
    }
    directoryWatcher_->addPath(currentDirectory_);

    currentPath_.clear();
    rawParametersAction_->setEnabled(false);
    rawConfigurationPanel_->clearSource();
    previewCanvas_->setFrame({});
    imagePropertiesPanel_->setFrame({});
    thumbnailModel_->setFiles({});
    statusBar()->showMessage(QStringLiteral("Scanning %1…").arg(currentDirectory_));
    incrementalScanGeneration_ = 0;
    scanGeneration_ = scanner_->scanAsync(currentDirectory_);
}

void MainWindow::navigateBack() {
    if (navigationHistoryIndex_ <= 0) {
        return;
    }
    --navigationHistoryIndex_;
    openDirectory(navigationHistory_.at(navigationHistoryIndex_), false);
}

void MainWindow::navigateForward() {
    if (navigationHistoryIndex_ < 0 || navigationHistoryIndex_ + 1 >= navigationHistory_.size()) {
        return;
    }
    ++navigationHistoryIndex_;
    openDirectory(navigationHistory_.at(navigationHistoryIndex_), false);
}

void MainWindow::navigateUp() {
    QDir directory(currentDirectory_);
    if (directory.cdUp()) {
        openDirectory(directory.absolutePath());
    }
}

void MainWindow::updateNavigationActions() {
    backAction_->setEnabled(navigationHistoryIndex_ > 0);
    forwardAction_->setEnabled(navigationHistoryIndex_ >= 0 &&
                               navigationHistoryIndex_ + 1 < navigationHistory_.size());
    QDir directory(currentDirectory_);
    upAction_->setEnabled(!currentDirectory_.isEmpty() && directory.cdUp());
}

void MainWindow::createFolder() {
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("New Folder"), QStringLiteral("Folder name:"),
                              QLineEdit::Normal, QStringLiteral("New folder"), &accepted)
            .trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
        name == QStringLiteral(".") || name == QStringLiteral("..")) {
        QMessageBox::warning(this, QStringLiteral("New Folder Failed"),
                             QStringLiteral("The folder name is not valid."));
        return;
    }
    const QString path = QDir(currentDirectory_).filePath(name);
    if (QFileInfo::exists(path) || !QDir().mkdir(path)) {
        QMessageBox::warning(this, QStringLiteral("New Folder Failed"),
                             QStringLiteral("Could not create “%1”.").arg(name));
        return;
    }
    pendingSelectionPath_ = path;
    rescanCurrentDirectory();
}

void MainWindow::copySelectedItems(bool cut) {
    const QStringList paths = selectedItemPaths();
    if (paths.isEmpty()) {
        return;
    }
    FileClipboard::setPaths(paths, cut);
    statusBar()->showMessage(QStringLiteral("%1 %2 item(s) to the clipboard")
                                 .arg(cut ? QStringLiteral("Cut") : QStringLiteral("Copied"))
                                 .arg(paths.size()),
                             3000);
}

void MainWindow::pasteItems(const QString& targetDirectory) {
    const FileClipboardContents clipboard = FileClipboard::contents();
    if (clipboard.paths.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("The clipboard does not contain files"), 3000);
        return;
    }
    const QString target = targetDirectory.isEmpty() ? currentDirectory_ : targetDirectory;
    transferPaths(clipboard.paths, target,
                  clipboard.cut ? FileTransferMode::Move : FileTransferMode::Copy, clipboard.cut);
}

void MainWindow::showSelectedProperties() {
    const QString path = primarySelectedItemPath();
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
    if (info.isFile()) {
        if (path != currentPath_) {
            loadPreview(path);
        }
        imagePropertiesDock_->show();
        imagePropertiesDock_->raise();
        imagePropertiesPanel_->showTab(ImagePropertiesPanel::Tab::Exif);
        return;
    }
    const QString type = info.isDir() ? QStringLiteral("Folder")
                                      : QStringLiteral("%1 file").arg(info.suffix().toUpper());
    QString details = QStringLiteral("Name: %1\nType: %2\nLocation: %3\nModified: %4")
                          .arg(info.fileName(), type, QDir::toNativeSeparators(info.absolutePath()),
                               QLocale().toString(info.lastModified(), QLocale::LongFormat));
    QMessageBox::information(this, QStringLiteral("Properties"), details);
}

void MainWindow::copyDroppedPaths(const QStringList& paths) {
    if (paths.isEmpty() || currentDirectory_.isEmpty()) {
        return;
    }

    transferPaths(paths, currentDirectory_, FileTransferMode::Copy, false);
}

void MainWindow::transferPaths(const QStringList& paths, const QString& targetDirectory,
                               FileTransferMode mode, bool clearCutClipboard) {
    if (paths.isEmpty() || targetDirectory.isEmpty()) {
        return;
    }

    const bool move = mode == FileTransferMode::Move;
    statusBar()->showMessage(QStringLiteral("%1 %2 item(s) into %3…")
                                 .arg(move ? QStringLiteral("Moving") : QStringLiteral("Copying"))
                                 .arg(paths.size())
                                 .arg(QFileInfo(targetDirectory).fileName()));

    const QPointer<MainWindow> self(this);
    QThreadPool::globalInstance()->start([self, paths, targetDirectory, mode, move,
                                          clearCutClipboard] {
        FileTransferResult result = FileTransferOperation::execute(paths, targetDirectory, mode);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self,
            [self, paths, targetDirectory, move, clearCutClipboard, result = std::move(result)] {
                if (!self) {
                    return;
                }
                const bool sourceWasCurrent =
                    std::any_of(paths.cbegin(), paths.cend(), [self](const QString& path) {
                        return QFileInfo(path).absolutePath() == self->currentDirectory_;
                    });
                if (self->currentDirectory_ == targetDirectory || sourceWasCurrent) {
                    const bool destinationIsCurrent = self->currentDirectory_ == targetDirectory;
                    self->pendingSelectionPath_ =
                        destinationIsCurrent && !result.destinationPaths.isEmpty()
                            ? result.destinationPaths.first()
                            : QString{};
                    self->rescanCurrentDirectory();
                }
                if (clearCutClipboard && result.errors.isEmpty()) {
                    FileClipboard::clear();
                }
                if (!result.errors.isEmpty()) {
                    self->statusBar()->showMessage(
                        QStringLiteral("%1 %2 item(s); %3 error(s): %4")
                            .arg(move ? QStringLiteral("Moved") : QStringLiteral("Copied"))
                            .arg(result.destinationPaths.size())
                            .arg(result.errors.size())
                            .arg(result.errors.first()),
                        7000);
                } else {
                    self->statusBar()->showMessage(
                        QStringLiteral("%1 %2 item(s)")
                            .arg(move ? QStringLiteral("Moved") : QStringLiteral("Copied"))
                            .arg(result.destinationPaths.size()),
                        4000);
                }
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::loadPreview(const QString& path, bool resetView) {
    if (path.isEmpty()) {
        return;
    }
    currentPath_ = path;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if ((suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")) &&
        !ensureRawParameters(path)) {
        previewCanvas_->setFrame({});
        imagePropertiesPanel_->clearFramePreservingRawParameters();
        return;
    }
    if (suffix != QStringLiteral("raw") && suffix != QStringLiteral("yuv")) {
        rawParametersAction_->setEnabled(false);
        rawConfigurationPanel_->clearSource();
    }
    imagePropertiesPanel_->setFrame({});
    const quint64 generation = ++previewGeneration_;
    const auto rawParameters = loader_->rawParameters(path);
    const QSize previewSize =
        rawParameters && !rawParameters->isYuv() ? QSize(640, 480) : QSize(1920, 1200);
    loader_->request(
        generation, {path, DecodePurpose::Preview, previewSize},
        [this, generation, path, resetView](quint64 id, const DecodeResult& preview) {
            if (id != previewGeneration_ || id != generation || currentPath_ != path) {
                return;
            }
            if (!preview.frame) {
                statusBar()->showMessage(preview.error);
                return;
            }
            previewCanvas_->setFrame(preview.frame, resetView);
            imagePropertiesPanel_->setFrame(preview.frame);
            updateStatusBase();
            requestFullFrame(path, generation);
        },
        4);
}

void MainWindow::requestFullFrame(const QString& path, quint64 generation) {
    loader_->request(
        generation, {path, DecodePurpose::Full, {}},
        [this, generation, path](quint64 id, const DecodeResult& full) {
            if (id != previewGeneration_ || id != generation || currentPath_ != path) {
                return;
            }
            if (!full.frame) {
                statusBar()->showMessage(full.error);
                return;
            }
            previewCanvas_->setFrame(full.frame, false);
            imagePropertiesPanel_->setFrame(full.frame);
            updateStatusBase();
        },
        1);
}

bool MainWindow::ensureRawParameters(const QString& path, bool showPanel) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QStringLiteral("raw") && suffix != QStringLiteral("yuv")) {
        rawParametersAction_->setEnabled(false);
        rawConfigurationPanel_->clearSource();
        return true;
    }
    rawParametersAction_->setEnabled(true);
    if (const auto active = loader_->rawParameters(path)) {
        imagePropertiesPanel_->setRawParameters(path, *active);
        rawConfigurationPanel_->setSource(path, *active);
        if (showPanel) {
            rawConfigurationDock_->show();
            rawConfigurationDock_->raise();
        }
        return true;
    }

    RawImageParameters initial = RawPresetStore::inferFromFileName(path);
    if (const auto saved = RawPresetStore::loadForFile(path)) {
        initial = *saved;
    }
    initial.frameIndex = 0;
    imagePropertiesPanel_->setRawParameters(path, initial);
    rawConfigurationPanel_->setSource(path, initial);
    rawConfigurationDock_->show();
    rawConfigurationDock_->raise();
    if (availableFrameCount(QFileInfo(path).size(), initial) <= 0) {
        statusBar()->showMessage(
            QStringLiteral("Set the RAW/YUV interpretation in the right-hand configuration panel"),
            5000);
        return false;
    }
    loader_->setRawParameters(path, initial);
    thumbnailModel_->invalidateThumbnail(path);
    return true;
}

void MainWindow::editRawParameters() {
    if (currentPath_.isEmpty()) {
        return;
    }
    (void)ensureRawParameters(currentPath_, true);
}

void MainWindow::prefetchNeighbors(int row) {
    for (const int direction : {-1, 1}) {
        int candidate = row + direction;
        while (candidate >= 0 && candidate < filterModel_->rowCount() &&
               filterModel_->index(candidate, 0).data(ThumbnailModel::DirectoryRole).toBool()) {
            candidate += direction;
        }
        if (candidate < 0 || candidate >= filterModel_->rowCount()) {
            continue;
        }
        const QString path =
            filterModel_->index(candidate, 0).data(ThumbnailModel::PathRole).toString();
        const auto rawParameters = loader_->rawParameters(path);
        const QSize previewSize =
            rawParameters && !rawParameters->isYuv() ? QSize(640, 480) : QSize(1920, 1200);
        loader_->request(
            0, {path, DecodePurpose::Preview, previewSize}, [](quint64, const DecodeResult&) {}, 2);
    }
}

void MainWindow::openFullScreen() {
    const int row = currentSourceRow();
    if (row < 0) {
        return;
    }
    auto* viewer = new FullScreenWindow(loader_, allPaths(), row, this);
    viewer->showFullScreen();
}

void MainWindow::openComparison() {
    const QStringList paths = selectedPaths();
    if (paths.size() < 2 || paths.size() > 4) {
        statusBar()->showMessage(QStringLiteral("Select 2 to 4 images to compare"), 3500);
        return;
    }
    auto* comparison = new CompareWindow(loader_, paths, this);
    comparison->showFullScreen();
}

void MainWindow::updateStatusBase() {
    const auto frame = previewCanvas_->frame();
    if (!frame) {
        updatePreviewDetails();
        statusBar()->showMessage(pixelStatus_);
        return;
    }
    updatePreviewDetails();
    statusBar()->showMessage(pixelStatus_);
}

void MainWindow::updatePreviewDetails() {
    const auto frame = previewCanvas_->frame();
    if (!frame) {
        previewInfoLabel_->setText(QStringLiteral("No image selected"));
        previewZoomLabel_->setText(QStringLiteral("—"));
        return;
    }
    const int bits = std::max(1, frame->descriptor.validBits);
    previewInfoLabel_->setText(QStringLiteral("%1 × %2  %3-bit  %4  %5")
                                   .arg(frame->descriptor.size.width())
                                   .arg(frame->descriptor.size.height())
                                   .arg(bits)
                                   .arg(frame->metadata.format.toUpper())
                                   .arg(QLocale().formattedDataSize(frame->metadata.fileSize)));
    previewZoomLabel_->setText(QStringLiteral("%1%").arg(
        QString::number(previewCanvas_->viewState().pixelsPerImagePixel * 100.0, 'f', 1)));
}

void MainWindow::rescanCurrentDirectory() {
    if (currentDirectory_.isEmpty()) {
        return;
    }
    scanGeneration_ = scanner_->scanAsync(currentDirectory_);
    incrementalScanGeneration_ = scanGeneration_;
}

void MainWindow::showThumbnailContextMenu(const QPoint& position) {
    const QModelIndex clicked = thumbnailView_->indexAt(position);
    QMenu menu(this);
    if (!clicked.isValid()) {
        menu.addAction(newFolderAction_);
        menu.addAction(pasteAction_);
        menu.addSeparator();
        menu.addAction(QStringLiteral("Refresh"), this, &MainWindow::rescanCurrentDirectory);
        QMenu* sort = menu.addMenu(QStringLiteral("Sort By"));
        const auto addSort = [this, sort](const QString& text, BrowserSortMode mode) {
            QAction* action = sort->addAction(text);
            action->setCheckable(true);
            action->setChecked(sortMode_ == mode);
            connect(action, &QAction::triggered, this, [this, mode] {
                sortMode_ = mode;
                filterModel_->setSortMode(mode);
            });
        };
        addSort(QStringLiteral("Name"), BrowserSortMode::Name);
        addSort(QStringLiteral("Date Modified"), BrowserSortMode::ModifiedTime);
        addSort(QStringLiteral("Size"), BrowserSortMode::Size);
        addSort(QStringLiteral("Type"), BrowserSortMode::Type);
        menu.exec(thumbnailView_->viewport()->mapToGlobal(position));
        return;
    }
    if (!thumbnailView_->selectionModel()->isSelected(clicked)) {
        thumbnailView_->selectionModel()->select(clicked, QItemSelectionModel::ClearAndSelect |
                                                              QItemSelectionModel::Rows);
        thumbnailView_->setCurrentIndex(clicked);
    }
    const bool directory = clicked.data(ThumbnailModel::DirectoryRole).toBool();
    const bool singleSelection = selectedItemPaths().size() == 1;
    if (directory && singleSelection) {
        const QString folderPath = clicked.data(ThumbnailModel::PathRole).toString();
        menu.addAction(QStringLiteral("Open Folder"), this,
                       [this, folderPath] { openDirectory(folderPath); });
        QAction* pasteInto = menu.addAction(QStringLiteral("Paste Into Folder"));
        pasteInto->setEnabled(FileClipboard::hasFiles());
        connect(pasteInto, &QAction::triggered, this,
                [this, folderPath] { pasteItems(folderPath); });
        menu.addSeparator();
    } else if (!directory) {
        menu.addAction(QStringLiteral("Open Full Screen"), this, &MainWindow::openFullScreen);
        QAction* compare =
            menu.addAction(QStringLiteral("Compare Selected"), this, &MainWindow::openComparison);
        compare->setEnabled(selectedPaths().size() >= 2 && selectedPaths().size() <= 4);
        const QString suffix = clicked.data(ThumbnailModel::PathRole)
                                   .toString()
                                   .section(QLatin1Char('.'), -1)
                                   .toLower();
        if (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")) {
            menu.addAction(rawParametersAction_);
        }
        menu.addSeparator();
    }
    menu.addAction(cutAction_);
    menu.addAction(copyAction_);
    menu.addSeparator();
    if (singleSelection) {
        menu.addAction(renameAction_);
    }
    menu.addAction(trashAction_);
    menu.addSeparator();
    if (singleSelection) {
        menu.addAction(QStringLiteral("Reveal in Finder / Explorer"), this,
                       &MainWindow::revealSelectedFile);
        menu.addAction(propertiesAction_);
    }
    menu.exec(thumbnailView_->viewport()->mapToGlobal(position));
}

void MainWindow::renameSelectedFile() {
    const QString path = primarySelectedItemPath();
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo source(path);
    bool accepted = false;
    const QString newName =
        QInputDialog::getText(this, QStringLiteral("Rename Item"), QStringLiteral("New name:"),
                              QLineEdit::Normal, source.fileName(), &accepted)
            .trimmed();
    if (!accepted || newName.isEmpty() || newName == source.fileName()) {
        return;
    }
    const QString destination = source.dir().filePath(newName);
    QString renameError;
    if (!SingleFileRename::execute(path, destination, &renameError)) {
        QMessageBox::warning(this, QStringLiteral("Rename Failed"), renameError);
        return;
    }
    pendingSelectionPath_ = destination;
    currentPath_ = source.isFile() ? destination : QString{};
    rescanCurrentDirectory();
}

void MainWindow::trashSelectedFiles() {
    QStringList paths = selectedItemPaths();
    if (paths.isEmpty() && !primarySelectedItemPath().isEmpty()) {
        paths.append(primarySelectedItemPath());
    }
    if (paths.isEmpty()) {
        return;
    }

    if (!TrashConfirmation::request(this, paths.size())) {
        return;
    }
    QStringList failures;
    for (const QString& path : paths) {
        if (!QFile::moveToTrash(path)) {
            failures.append(QFileInfo(path).fileName());
        }
    }
    if (!failures.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Trash Incomplete"),
                             failures.join(QLatin1Char('\n')));
    }
    rescanCurrentDirectory();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // Native drag routing differs between Cocoa and Windows. Catch an external file drag at the
    // application boundary whenever its target belongs to this window, so child viewport routing
    // cannot make Finder/Explorer drops disappear silently. Internal Qt drags retain their normal
    // widget-specific behavior.
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
        event->type() == QEvent::Drop) {
        auto* dropEvent = static_cast<QDropEvent*>(event);
        auto* targetWidget = qobject_cast<QWidget*>(watched);
        const bool belongsToWindow =
            targetWidget && (targetWidget == this || targetWidget->window() == this);
        if (belongsToWindow && !dropEvent->source()) {
            const QStringList paths = localFileDropPaths(dropEvent->mimeData());
            if (event->type() == QEvent::DragEnter) {
                statusBar()->showMessage(
                    paths.isEmpty()
                        ? QStringLiteral("Unsupported drop data (%1)")
                              .arg(localFileDropFormats(dropEvent->mimeData()))
                        : QStringLiteral("Drop to copy %1 item(s) into the current folder")
                              .arg(paths.size()));
            }
            if (!paths.isEmpty()) {
                if (event->type() == QEvent::Drop) {
                    copyDroppedPaths(paths);
                }
                dropEvent->setDropAction(Qt::CopyAction);
                dropEvent->accept();
                return true;
            }
        }
    }
    if (watched == previewCanvas_) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDropEvent*>(event);
            if (!localFileDropPaths(dragEvent->mimeData()).isEmpty()) {
                dragEvent->setDropAction(Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            const QStringList paths = localFileDropPaths(dropEvent->mimeData());
            if (!paths.isEmpty()) {
                copyDroppedPaths(paths);
                dropEvent->setDropAction(Qt::CopyAction);
                dropEvent->accept();
                return true;
            }
        }
    }
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) &&
        QApplication::activeWindow() == this && !QApplication::activeModalWidget()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (isPlatformTrashShortcut(keyEvent)) {
            if (!isTextEditingWidget(QApplication::focusWidget()) &&
                !selectedItemPaths().isEmpty()) {
                event->accept();
                if (event->type() == QEvent::KeyPress && !keyEvent->isAutoRepeat()) {
                    trashSelectedFiles();
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::revealSelectedFile() {
    const QString path = primarySelectedItemPath();
    if (!path.isEmpty() && !PlatformServices::revealInFileManager(path)) {
        statusBar()->showMessage(QStringLiteral("Could not open the system file manager"), 3500);
    }
}

QString MainWindow::primarySelectedPath() const {
    const QModelIndex current = thumbnailView_->currentIndex();
    return current.isValid() && !current.data(ThumbnailModel::DirectoryRole).toBool()
               ? current.data(ThumbnailModel::PathRole).toString()
               : QString{};
}

QString MainWindow::primarySelectedItemPath() const {
    const QModelIndex current = thumbnailView_->currentIndex();
    return current.isValid() ? current.data(ThumbnailModel::PathRole).toString() : QString{};
}

QStringList MainWindow::allPaths() const {
    QStringList result;
    result.reserve(filterModel_->rowCount());
    for (int row = 0; row < filterModel_->rowCount(); ++row) {
        const QModelIndex index = filterModel_->index(row, 0);
        if (!index.data(ThumbnailModel::DirectoryRole).toBool()) {
            result.append(index.data(ThumbnailModel::PathRole).toString());
        }
    }
    return result;
}

QStringList MainWindow::selectedPaths() const {
    QStringList result;
    QModelIndexList indexes = thumbnailView_->selectionModel()->selectedIndexes();
    std::sort(
        indexes.begin(), indexes.end(),
        [](const QModelIndex& left, const QModelIndex& right) { return left.row() < right.row(); });
    for (const QModelIndex& index : indexes) {
        if (!index.data(ThumbnailModel::DirectoryRole).toBool()) {
            result.append(index.data(ThumbnailModel::PathRole).toString());
        }
    }
    return result;
}

QStringList MainWindow::selectedItemPaths() const {
    QStringList result;
    QModelIndexList indexes = thumbnailView_->selectionModel()->selectedIndexes();
    std::sort(
        indexes.begin(), indexes.end(),
        [](const QModelIndex& left, const QModelIndex& right) { return left.row() < right.row(); });
    for (const QModelIndex& index : indexes) {
        const QString path = index.data(ThumbnailModel::PathRole).toString();
        if (!path.isEmpty()) {
            result.append(path);
        }
    }
    result.removeDuplicates();
    return result;
}

int MainWindow::currentSourceRow() const {
    const QModelIndex current = thumbnailView_->currentIndex();
    if (!current.isValid() || current.data(ThumbnailModel::DirectoryRole).toBool()) {
        return -1;
    }
    int imageRow = 0;
    for (int row = 0; row < filterModel_->rowCount(); ++row) {
        const QModelIndex index = filterModel_->index(row, 0);
        if (index.data(ThumbnailModel::DirectoryRole).toBool()) {
            continue;
        }
        if (index == current) {
            return imageRow;
        }
        ++imageRow;
    }
    return -1;
}

} // namespace ispview
