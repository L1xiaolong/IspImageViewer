#include "ui/multi_folder_window.h"

#include "io/directory_scanner.h"
#include "io/image_loader.h"
#include "ui/compare_window.h"
#include "ui/full_screen_window.h"
#include "ui/path_breadcrumb.h"
#include "ui/thumbnail_model.h"
#include "ui/thumbnail_view.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace ispview {

class FolderBrowserPane final : public QWidget {
  public:
    FolderBrowserPane(ImageLoader* loader, const QString& initialDirectory,
                      QWidget* parent = nullptr)
        : QWidget(parent), loader_(loader), scanner_(new DirectoryScanner(this)),
          model_(new ThumbnailModel(loader, this)), breadcrumb_(new PathBreadcrumb(this)),
          thumbnails_(new ThumbnailView(this)) {
        setObjectName(QStringLiteral("multiFolderPane"));
        breadcrumb_->setObjectName(QStringLiteral("multiFolderBreadcrumb"));
        thumbnails_->setObjectName(QStringLiteral("multiFolderThumbnails"));
        auto* header = new QWidget(this);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(6, 3, 3, 3);
        auto* close = new QPushButton(QStringLiteral("×"), header);
        close->setObjectName(QStringLiteral("closeFolderPane"));
        close->setFixedSize(24, 24);
        headerLayout->addWidget(breadcrumb_, 1);
        headerLayout->addWidget(close);

        thumbnails_->setModel(model_);
        thumbnails_->setViewMode(QListView::IconMode);
        thumbnails_->setResizeMode(QListView::Adjust);
        thumbnails_->setMovement(QListView::Static);
        thumbnails_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        thumbnails_->setIconSize({160, 120});
        thumbnails_->setGridSize({194, 190});
        thumbnails_->setWordWrap(true);
        thumbnails_->setUniformItemSizes(true);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(2);
        layout->addWidget(header);
        layout->addWidget(thumbnails_, 1);

        connect(close, &QPushButton::clicked, this, &QWidget::hide);
        connect(breadcrumb_, &PathBreadcrumb::pathActivated, this,
                [this](const QString& path) { openDirectory(path); });
        connect(scanner_, &DirectoryScanner::scanFinished, this,
                [this](const QString& directory, const QVector<ImageFileRecord>& files,
                       quint64 generation) {
                    if (generation == generation_ && directory == directory_) {
                        model_->setFiles(files);
                    }
                });
        connect(thumbnails_, &QListView::activated, this, [this](const QModelIndex& index) {
            const QString path = index.data(ThumbnailModel::PathRole).toString();
            if (index.data(ThumbnailModel::DirectoryRole).toBool()) {
                openDirectory(path);
                return;
            }
            QStringList paths;
            for (int row = 0; row < model_->rowCount(); ++row) {
                const QModelIndex candidate = model_->index(row, 0);
                if (!candidate.data(ThumbnailModel::DirectoryRole).toBool()) {
                    paths.append(candidate.data(ThumbnailModel::PathRole).toString());
                }
            }
            const qsizetype current = paths.indexOf(path);
            if (current >= 0) {
                auto* viewer =
                    new FullScreenWindow(loader_, paths, static_cast<int>(current), this);
                viewer->showFullScreen();
            }
        });
        openDirectory(initialDirectory);
    }

    QStringList selectedPaths() const {
        QStringList paths;
        if (!thumbnails_->selectionModel()) {
            return paths;
        }
        for (const QModelIndex& index : thumbnails_->selectionModel()->selectedIndexes()) {
            if (!index.data(ThumbnailModel::DirectoryRole).toBool()) {
                paths.append(index.data(ThumbnailModel::PathRole).toString());
            }
        }
        return paths;
    }

    void reset(const QString& directory) {
        show();
        openDirectory(directory);
    }

  private:
    void openDirectory(const QString& path) {
        const QFileInfo info(path);
        if (!info.isDir()) {
            return;
        }
        directory_ = info.absoluteFilePath();
        breadcrumb_->setPath(directory_);
        model_->setFiles({});
        generation_ = scanner_->scanImageFoldersAsync(directory_);
    }

    ImageLoader* loader_;
    DirectoryScanner* scanner_;
    ThumbnailModel* model_;
    PathBreadcrumb* breadcrumb_;
    ThumbnailView* thumbnails_;
    QString directory_;
    quint64 generation_ = 0;
};

MultiFolderWindow::MultiFolderWindow(ImageLoader* loader, const QString& initialDirectory,
                                     QWidget* parent)
    : QMainWindow(parent), loader_(loader) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("Multi-Folder Browser — ISP Image Viewer"));
    resize(1500, 950);
    auto* toolbar = addToolBar(QStringLiteral("Multi-folder"));
    toolbar->setMovable(false);
    QAction* compare = toolbar->addAction(QStringLiteral("Compare Selected"));
    compare->setObjectName(QStringLiteral("multiFolderCompareAction"));
    compare->setShortcut(QKeySequence(Qt::Key_C));
    connect(compare, &QAction::triggered, this, &MultiFolderWindow::compareSelected);
    QAction* restore = toolbar->addAction(QStringLiteral("Restore 4 Panes"));
    restore->setObjectName(QStringLiteral("restoreFourPanesAction"));
    connect(restore, &QAction::triggered, this, [this, initialDirectory] {
        for (FolderBrowserPane* pane : panes_) {
            pane->reset(initialDirectory);
        }
    });
    QAction* close = toolbar->addAction(QStringLiteral("Close"));
    close->setObjectName(QStringLiteral("closeMultiFolderAction"));
    close->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(close, &QAction::triggered, this, &QWidget::close);

    auto* central = new QWidget(this);
    auto* grid = new QGridLayout(central);
    grid->setContentsMargins(3, 3, 3, 3);
    grid->setSpacing(5);
    for (int slot = 0; slot < 4; ++slot) {
        auto* pane = new FolderBrowserPane(loader_, initialDirectory, central);
        pane->setObjectName(QStringLiteral("multiFolderPane%1").arg(slot));
        grid->addWidget(pane, slot / 2, slot % 2);
        panes_.append(pane);
    }
    setCentralWidget(central);
}

void MultiFolderWindow::compareSelected() {
    QStringList paths;
    for (FolderBrowserPane* pane : panes_) {
        if (pane->isVisible()) {
            paths.append(pane->selectedPaths());
        }
    }
    paths.removeDuplicates();
    if (paths.size() < 2 || paths.size() > 4) {
        statusBar()->showMessage(QStringLiteral("Select 2 to 4 images across the panes"), 3500);
        return;
    }
    auto* comparison = new CompareWindow(loader_, paths, this);
    comparison->showFullScreen();
}

} // namespace ispview
