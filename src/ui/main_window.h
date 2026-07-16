#pragma once

#include <QMainWindow>
#include <QStringList>

#include <memory>

QT_BEGIN_NAMESPACE
class QAction;
class QCloseEvent;
class QDockWidget;
class QEvent;
class QFileSystemModel;
class QFileSystemWatcher;
class QModelIndex;
class QTimer;
class QTreeView;
class QLabel;
class QWidget;
QT_END_NAMESPACE

namespace ispview {

class DirectoryScanner;
class ImageCanvas;
class ImagePropertiesPanel;
class RawParameterPanel;
class ImageLoader;
class IImageDecoder;
class ThumbnailModel;
class ThumbnailFilterProxyModel;
class ThumbnailView;
enum class BrowserSortMode;
enum class FileTransferMode;

class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    MainWindow(std::shared_ptr<const IImageDecoder> decoder, const QString& initialDirectory = {},
               QWidget* parent = nullptr);
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void buildInterface();
    void connectInterface();
    void openDirectory(const QString& path, bool addToHistory = true);
    void navigateBack();
    void navigateForward();
    void navigateUp();
    void updateNavigationActions();
    void createFolder();
    void copySelectedItems(bool cut);
    void pasteItems(const QString& targetDirectory = {});
    void showSelectedProperties();
    void updatePreviewDetails();
    void copyDroppedPaths(const QStringList& paths);
    void transferPaths(const QStringList& paths, const QString& targetDirectory,
                       FileTransferMode mode, bool clearCutClipboard);
    void loadPreview(const QString& path, bool resetView = true);
    void requestFullFrame(const QString& path, quint64 generation);
    void prefetchNeighbors(int row);
    void openFullScreen();
    void openComparison();
    void updateStatusBase();
    void rescanCurrentDirectory();
    void showThumbnailContextMenu(const QPoint& position);
    void renameSelectedFile();
    void trashSelectedFiles();
    void revealSelectedFile();
    [[nodiscard]] bool ensureRawParameters(const QString& path, bool showPanel = false);
    void editRawParameters();
    [[nodiscard]] QString primarySelectedPath() const;
    [[nodiscard]] QString primarySelectedItemPath() const;
    [[nodiscard]] QStringList allPaths() const;
    [[nodiscard]] QStringList selectedPaths() const;
    [[nodiscard]] QStringList selectedItemPaths() const;
    [[nodiscard]] int currentSourceRow() const;

    ImageLoader* loader_;
    DirectoryScanner* scanner_;
    ThumbnailModel* thumbnailModel_;
    ThumbnailFilterProxyModel* filterModel_;

    QFileSystemModel* fileSystemModel_;
    QTreeView* directoryTree_;
    ThumbnailView* thumbnailView_;
    ImageCanvas* previewCanvas_;
    QWidget* previewPanel_;
    QLabel* previewInfoLabel_;
    QLabel* previewZoomLabel_;
    ImagePropertiesPanel* imagePropertiesPanel_;
    QDockWidget* imagePropertiesDock_;
    RawParameterPanel* rawConfigurationPanel_;
    QDockWidget* rawConfigurationDock_;
    BrowserSortMode sortMode_;
    QAction* compareAction_;
    QAction* rawParametersAction_;
    QAction* trashAction_ = nullptr;
    QAction* backAction_ = nullptr;
    QAction* forwardAction_ = nullptr;
    QAction* upAction_ = nullptr;
    QAction* newFolderAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* cutAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* renameAction_ = nullptr;
    QAction* propertiesAction_ = nullptr;
    QAction* previewToggleAction_ = nullptr;
    QFileSystemWatcher* directoryWatcher_;
    QTimer* refreshTimer_;

    QString currentDirectory_;
    QString currentPath_;
    QString pixelStatus_;
    QString pendingSelectionPath_;
    QStringList navigationHistory_;
    int navigationHistoryIndex_ = -1;
    quint64 scanGeneration_ = 0;
    quint64 incrementalScanGeneration_ = 0;
    quint64 previewGeneration_ = 0;
};

} // namespace ispview
