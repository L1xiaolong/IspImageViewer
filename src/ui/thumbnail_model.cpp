#include "ui/thumbnail_model.h"

#include "io/image_loader.h"
#include "io/raw_preset_store.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QStyle>
#include <QUrl>

namespace ispview {
namespace {

QPixmap textPlaceholder(const QString& text) {
    QPixmap result(160, 120);
    result.fill(QColor(48, 51, 57));
    QPainter painter(&result);
    painter.setPen(QColor(150, 154, 162));
    painter.drawText(result.rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                     text);
    return result;
}

} // namespace

ThumbnailModel::ThumbnailModel(ImageLoader* loader, QObject* parent)
    : QAbstractListModel(parent), loader_(loader), placeholder_(textPlaceholder("Loading…")),
      unavailablePlaceholder_(textPlaceholder("Parameters required")),
      folderPlaceholder_(QApplication::style()->standardIcon(QStyle::SP_DirIcon).pixmap(120, 96)) {}

int ThumbnailModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(files_.size());
}

QVariant ThumbnailModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= files_.size()) {
        return {};
    }
    const auto& file = files_.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return file.fileName;
    case Qt::DecorationRole:
        if (file.isDirectory) {
            return folderPlaceholder_;
        }
        if (const auto it = thumbnails_.constFind(file.path); it != thumbnails_.cend()) {
            return *it;
        }
        requestThumbnail(index.row());
        return placeholder_;
    case Qt::ToolTipRole:
        return file.isDirectory ? QStringLiteral("%1\nFolder").arg(file.path)
                                : QStringLiteral("%1\n%2 KB\nModified %3")
                                      .arg(file.path)
                                      .arg(QString::number(file.fileSize / 1024))
                                      .arg(file.modifiedAt.toString(Qt::ISODate));
    case PathRole:
        return file.path;
    case SizeRole:
        return file.fileSize;
    case ModifiedRole:
        return file.modifiedAt;
    case DirectoryRole:
        return file.isDirectory;
    case TypeRole:
        return file.isDirectory ? QStringLiteral("Folder")
                                : QFileInfo(file.fileName).suffix().toCaseFolded();
    case DimensionsRole:
        return dimensions_.value(file.path);
    default:
        return {};
    }
}

QHash<int, QByteArray> ThumbnailModel::roleNames() const {
    auto roles = QAbstractListModel::roleNames();
    roles.insert(PathRole, "path");
    roles.insert(SizeRole, "fileSize");
    roles.insert(ModifiedRole, "modifiedAt");
    roles.insert(DirectoryRole, "isDirectory");
    roles.insert(TypeRole, "fileType");
    roles.insert(DimensionsRole, "dimensions");
    return roles;
}

Qt::ItemFlags ThumbnailModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags result = QAbstractListModel::flags(index);
    if (index.isValid()) {
        result |= Qt::ItemIsDragEnabled;
    }
    return result;
}

QStringList ThumbnailModel::mimeTypes() const { return {QStringLiteral("text/uri-list")}; }

QMimeData* ThumbnailModel::mimeData(const QModelIndexList& indexes) const {
    QList<QUrl> urls;
    QSet<QString> paths;
    for (const QModelIndex& index : indexes) {
        if (!index.isValid()) {
            continue;
        }
        const QString path = index.data(PathRole).toString();
        if (!path.isEmpty() && !paths.contains(path)) {
            paths.insert(path);
            urls.append(QUrl::fromLocalFile(path));
        }
    }
    auto* result = new QMimeData;
    result->setUrls(urls);
    return result;
}

Qt::DropActions ThumbnailModel::supportedDragActions() const { return Qt::CopyAction; }

void ThumbnailModel::setFiles(QVector<ImageFileRecord> files) {
    beginResetModel();
    files_ = std::move(files);
    thumbnails_.clear();
    dimensions_.clear();
    pending_.clear();
    pendingRequestIds_.clear();
    rebuildPathIndex();
    endResetModel();
}

void ThumbnailModel::updateFiles(const QVector<ImageFileRecord>& files) {
    QSet<QString> incomingPaths;
    incomingPaths.reserve(files.size());
    for (const auto& file : files) {
        incomingPaths.insert(file.path);
    }

    // Remove backwards so every existing persistent index before the removed row stays valid.
    for (qsizetype row = files_.size(); row-- > 0;) {
        const QString path = files_.at(row).path;
        if (incomingPaths.contains(path)) {
            continue;
        }
        beginRemoveRows({}, static_cast<int>(row), static_cast<int>(row));
        files_.removeAt(row);
        thumbnails_.remove(path);
        dimensions_.remove(path);
        pending_.remove(path);
        pendingRequestIds_.remove(path);
        endRemoveRows();
    }

    // The scanner output is naturally sorted. Files that were renamed have already been
    // removed above, so additions can be inserted directly at their final positions.
    for (int row = 0; row < files.size(); ++row) {
        const ImageFileRecord& incoming = files.at(row);
        if (row >= files_.size() || files_.at(row).path != incoming.path) {
            beginInsertRows({}, row, row);
            files_.insert(row, incoming);
            endInsertRows();
            continue;
        }

        ImageFileRecord& existing = files_[row];
        const bool contentChanged =
            existing.fileSize != incoming.fileSize || existing.modifiedAt != incoming.modifiedAt;
        if (existing.fileName != incoming.fileName || contentChanged) {
            existing = incoming;
            if (contentChanged) {
                thumbnails_.remove(incoming.path);
                dimensions_.remove(incoming.path);
                pending_.remove(incoming.path);
                pendingRequestIds_.remove(incoming.path);
            }
            const QModelIndex changed = index(row);
            emit dataChanged(changed, changed);
        }
    }

    if (files_.size() > files.size()) {
        const int first = static_cast<int>(files.size());
        const int last = static_cast<int>(files_.size()) - 1;
        beginRemoveRows({}, first, last);
        files_.remove(first, last - first + 1);
        endRemoveRows();
    }
    rebuildPathIndex();
}

QString ThumbnailModel::pathAt(int row) const {
    return row >= 0 && row < files_.size() ? files_.at(row).path : QString{};
}

void ThumbnailModel::invalidateThumbnail(const QString& path) {
    thumbnails_.remove(path);
    dimensions_.remove(path);
    pending_.remove(path);
    pendingRequestIds_.remove(path);
    const int row = pathToRow_.value(path, -1);
    if (row >= 0) {
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed, {Qt::DecorationRole, DimensionsRole});
    }
}

void ThumbnailModel::rebuildPathIndex() {
    pathToRow_.clear();
    pathToRow_.reserve(files_.size());
    for (int row = 0; row < files_.size(); ++row) {
        pathToRow_.insert(files_.at(row).path, row);
    }
}

void ThumbnailModel::requestThumbnail(int row) const {
    if (row < 0 || row >= files_.size()) {
        return;
    }
    const QString path = files_.at(row).path;
    if (files_.at(row).isDirectory) {
        return;
    }
    if (pending_.contains(path)) {
        return;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    if ((suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")) &&
        !loader_->rawParameters(path)) {
        std::optional<RawImageParameters> parameters = RawPresetStore::loadForFile(path);
        if (!parameters) {
            const RawImageParameters inferred = RawPresetStore::inferFromFileName(path);
            if (availableFrameCount(QFileInfo(path).size(), inferred) > 0) {
                parameters = inferred;
            }
        }
        if (!parameters || availableFrameCount(QFileInfo(path).size(), *parameters) <= 0) {
            thumbnails_.insert(path, unavailablePlaceholder_);
            const QModelIndex changed = index(row);
            emit const_cast<ThumbnailModel*>(this)->dataChanged(changed, changed,
                                                                {Qt::DecorationRole});
            return;
        }
        loader_->setRawParameters(path, *parameters);
    }
    pending_.insert(path);
    const quint64 requestId = ++requestCounter_;
    pendingRequestIds_.insert(path, requestId);
    const QPointer<ThumbnailModel> self(const_cast<ThumbnailModel*>(this));
    loader_->request(
        requestId, {path, DecodePurpose::Thumbnail, {160, 120}},
        [self, path](quint64 requestId, const DecodeResult& result) {
            if (!self) {
                return;
            }
            auto* model = self.data();
            if (model->pendingRequestIds_.value(path) != requestId) {
                return;
            }
            model->pending_.remove(path);
            model->pendingRequestIds_.remove(path);
            if (!result.frame || !result.frame->qImage()) {
                model->thumbnails_.insert(path, model->unavailablePlaceholder_);
                const int failedRow = model->pathToRow_.value(path, -1);
                if (failedRow >= 0) {
                    const QModelIndex changed = model->index(failedRow);
                    emit model->dataChanged(changed, changed, {Qt::DecorationRole});
                }
                return;
            }
            model->thumbnails_.insert(path, QPixmap::fromImage(*result.frame->qImage()));
            const QSize dimensions = result.frame->metadata.sourceSize.isValid()
                                         ? result.frame->metadata.sourceSize
                                         : result.frame->descriptor.size;
            model->dimensions_.insert(path, dimensions);
            const int row = model->pathToRow_.value(path, -1);
            if (row >= 0) {
                const QModelIndex changed = model->index(row);
                emit model->dataChanged(changed, changed,
                                        {Qt::DecorationRole, ThumbnailModel::DimensionsRole});
            }
        },
        -1);
}

} // namespace ispview
