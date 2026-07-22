#pragma once

#include "io/directory_scanner.h"

#include <QAbstractListModel>
#include <QHash>
#include <QMimeData>
#include <QPixmap>
#include <QSet>

namespace ispview {

class ImageLoader;

class ThumbnailModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        SizeRole,
        ModifiedRole,
        DirectoryRole,
        TypeRole,
        DimensionsRole,
        ThumbnailUrlRole,
        FileNameRole,
        TechnicalLabelRole,
        SelectedRole,
        SelectionOrdinalRole
    };

    explicit ThumbnailModel(ImageLoader* loader, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;

    void setFiles(QVector<ImageFileRecord> files);
    void updateFiles(const QVector<ImageFileRecord>& files);
    void invalidateThumbnail(const QString& path);
    void setSelectedPaths(const QStringList& paths);
    [[nodiscard]] QString pathAt(int row) const;
    [[nodiscard]] const QVector<ImageFileRecord>& files() const { return files_; }

  private:
    void requestThumbnail(int row) const;
    void rebuildPathIndex();

    ImageLoader* loader_;
    QVector<ImageFileRecord> files_;
    QHash<QString, int> pathToRow_;
    mutable QHash<QString, QPixmap> thumbnails_;
    mutable QHash<QString, QSize> dimensions_;
    mutable QSet<QString> pending_;
    mutable QHash<QString, quint64> pendingRequestIds_;
    mutable QString initializingRawParametersPath_;
    QStringList selectedPaths_;
    mutable quint64 requestCounter_ = 0;
    QPixmap placeholder_;
    QPixmap unavailablePlaceholder_;
    QPixmap folderPlaceholder_;
};

} // namespace ispview
