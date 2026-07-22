#include "qml/thumbnail_image_provider.h"

#include "core/raw_image_parameters.h"
#include "io/image_decoder.h"
#include "io/image_loader.h"
#include "io/raw_preset_store.h"

#include <QFileInfo>
#include <QMutexLocker>
#include <QPainter>
#include <QUrl>

namespace ispview {

ThumbnailImageProvider::ThumbnailImageProvider(std::shared_ptr<const IImageDecoder> decoder,
                                               ImageLoader* loader)
    : QQuickImageProvider(QQuickImageProvider::Image), decoder_(std::move(decoder)),
      loader_(loader) {}

QImage ThumbnailImageProvider::requestImage(const QString& id, QSize* size,
                                            const QSize& requestedSize) {
    const QString path = QUrl::fromPercentEncoding(id.section(QLatin1Char('?'), 0, 0).toUtf8());
    const QSize target = requestedSize.isValid() ? requestedSize : QSize(320, 240);
    const QString key = QStringLiteral("%1|%2x%3").arg(id).arg(target.width()).arg(target.height());
    {
        const QMutexLocker lock(&mutex_);
        if (const auto it = cache_.constFind(key); it != cache_.cend()) {
            if (size) {
                *size = it->size();
            }
            return *it;
        }
    }

    QSize sourceSize;
    QImage image = decode(path, &sourceSize, target);
    if (image.isNull()) {
        image = placeholder(QStringLiteral("Preview unavailable"), target);
    }
    if (size) {
        *size = sourceSize.isValid() ? sourceSize : image.size();
    }
    {
        const QMutexLocker lock(&mutex_);
        if (cache_.size() > 256) {
            cache_.clear();
        }
        cache_.insert(key, image);
    }
    return image;
}

QImage ThumbnailImageProvider::decode(const QString& path, QSize* sourceSize,
                                      const QSize& requestedSize) const {
    if (!decoder_ || !QFileInfo::exists(path)) {
        return {};
    }
    std::optional<RawImageParameters> parameters;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")) {
        if (loader_) parameters = loader_->rawParameters(path);
        if (!parameters) parameters = RawPresetStore::loadForFile(path);
        if (!parameters) {
            parameters = RawPresetStore::inferFromFileName(path);
        }
    }
    const DecodeResult decoded =
        decoder_->decode({path, DecodePurpose::Thumbnail, requestedSize, parameters});
    if (!decoded.frame || !decoded.frame->qImage()) {
        return {};
    }
    *sourceSize = decoded.frame->metadata.sourceSize.isValid()
                      ? decoded.frame->metadata.sourceSize
                      : decoded.frame->descriptor.size;
    return decoded.frame->qImage()->scaled(requestedSize, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGBA8888);
}

QImage ThumbnailImageProvider::placeholder(const QString& text, const QSize& size) {
    QImage image(size.expandedTo(QSize(160, 120)), QImage::Format_RGBA8888);
    image.fill(QColor(QStringLiteral("#E8EBEB")));
    QPainter painter(&image);
    painter.setPen(QColor(QStringLiteral("#69747D")));
    painter.drawText(image.rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                     text);
    return image;
}

} // namespace ispview
