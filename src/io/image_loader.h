#pragma once

#include "core/weighted_lru_cache.h"
#include "io/image_decoder.h"

#include <QObject>
#include <QHash>
#include <QReadWriteLock>
#include <QThreadPool>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

namespace ispview {

class ThumbnailDiskCache;

class ImageLoader final : public QObject {
    Q_OBJECT

  public:
    using Callback = std::function<void(quint64, const DecodeResult&)>;

    explicit ImageLoader(std::shared_ptr<const IImageDecoder> decoder, QObject* parent = nullptr);

    void request(quint64 requestId, DecodeRequest request, Callback callback, int priority = 0);
    void prefetchAdjacentRawFrames(const QString& path, const RawImageParameters& current,
                                   const QSize& previewSize);
    void setRawParameters(const QString& path, const RawImageParameters& parameters);
    [[nodiscard]] std::optional<RawImageParameters> rawParameters(const QString& path) const;
    [[nodiscard]] bool isCached(DecodeRequest request) const;
    void clearCache();

    [[nodiscard]] static QString cacheKey(const DecodeRequest& request,
                                          const QString& decoderIdentity = {});

  signals:
    void rawParametersChanged(const QString& path);

  private:
    struct PendingRequest {
        quint64 requestId = 0;
        Callback callback;
    };

    std::shared_ptr<const IImageDecoder> decoder_;
    std::shared_ptr<ThumbnailDiskCache> diskCache_;
    WeightedLruCache<ImageFrame> cache_{512LL * 1024 * 1024};
    QThreadPool pool_;
    mutable QReadWriteLock rawParametersLock_;
    QHash<QString, RawImageParameters> rawParameters_;
    QHash<QString, QVector<PendingRequest>> inFlight_;
};

} // namespace ispview
