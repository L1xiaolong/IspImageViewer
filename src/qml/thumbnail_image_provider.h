#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

#include <memory>

namespace ispview {

class IImageDecoder;

// Cross-platform image provider for QML thumbnails. The URL contains only an encoded local path;
// decoding and RAW/YUV parameter inference stay in the existing IO layer.
class ThumbnailImageProvider final : public QQuickImageProvider {
  public:
    explicit ThumbnailImageProvider(std::shared_ptr<const IImageDecoder> decoder);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

  private:
    [[nodiscard]] QImage decode(const QString& path, QSize* sourceSize,
                                const QSize& requestedSize) const;
    [[nodiscard]] static QImage placeholder(const QString& text, const QSize& size);

    std::shared_ptr<const IImageDecoder> decoder_;
    mutable QMutex mutex_;
    QHash<QString, QImage> cache_;
};

} // namespace ispview
