#pragma once

#include <QImage>
#include <QQuickImageProvider>

#include <memory>

namespace ispview {

class IImageDecoder;
class ImageLoader;

// Cross-platform image provider for QML thumbnails. The URL contains only an encoded local path;
// decoding and RAW/YUV parameter inference stay in the existing IO layer.
class ThumbnailImageProvider final : public QQuickAsyncImageProvider {
  public:
    ThumbnailImageProvider(std::shared_ptr<const IImageDecoder> decoder, ImageLoader* loader);

    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;
    // Synchronous compatibility helper used by deterministic unit tests only.
    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;
    [[nodiscard]] static QSize bucketedSize(const QSize& requestedSize);

  private:
    ImageLoader* loader_ = nullptr;
};

} // namespace ispview
