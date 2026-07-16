#include "io/thumbnail_disk_cache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>

namespace ispview {

ThumbnailDiskCache::ThumbnailDiskCache(QString rootDirectory)
    : rootDirectory_(std::move(rootDirectory)) {
    if (rootDirectory_.isEmpty()) {
        rootDirectory_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                         QStringLiteral("/thumbnails-v1");
    }
    QDir().mkpath(rootDirectory_);
}

QImage ThumbnailDiskCache::load(const QString& key) const {
    QImage image;
    image.load(pathForKey(key), "PNG");
    return image;
}

bool ThumbnailDiskCache::store(const QString& key, const QImage& image) const {
    if (image.isNull()) {
        return false;
    }
    QSaveFile file(pathForKey(key));
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QString ThumbnailDiskCache::pathForKey(const QString& key) const {
    const QByteArray digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    return rootDirectory_ + QLatin1Char('/') + QString::fromLatin1(digest) + QStringLiteral(".png");
}

} // namespace ispview
