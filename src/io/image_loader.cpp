#include "io/image_loader.h"

#include "io/thumbnail_disk_cache.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

namespace ispview {

ImageLoader::ImageLoader(std::shared_ptr<const IImageDecoder> decoder, QObject* parent)
    : QObject(parent), decoder_(std::move(decoder)),
      diskCache_(std::make_shared<ThumbnailDiskCache>()) {
    Q_ASSERT(decoder_);
    pool_.setMaxThreadCount(qMax(2, QThread::idealThreadCount() - 1));
    pool_.setExpiryTimeout(10'000);
}

void ImageLoader::request(quint64 requestId, DecodeRequest request, Callback callback,
                          int priority) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (!request.rawParameters) {
        request.rawParameters = rawParameters(request.path);
    }
    const QString key = cacheKey(request, decoder_->cacheIdentity());
    if (auto cached = cache_.get(key)) {
        callback(requestId, {std::move(cached), {}});
        return;
    }
    if (auto found = inFlight_.find(key); found != inFlight_.end()) {
        found->push_back({requestId, std::move(callback)});
        return;
    }
    inFlight_.insert(key, {{requestId, std::move(callback)}});

    const QPointer<ImageLoader> self(this);
    const auto decoder = decoder_;
    const auto diskCache = diskCache_;
    pool_.start(
        [self, decoder, diskCache, request = std::move(request), key] {
            DecodeResult result;
            if (request.purpose == DecodePurpose::Thumbnail) {
                QImage cachedImage = diskCache->load(key);
                if (!cachedImage.isNull()) {
                    const QFileInfo info(request.path);
                    auto frame = std::make_shared<ImageFrame>();
                    frame->descriptor.size = cachedImage.size();
                    frame->metadata.path = info.absoluteFilePath();
                    frame->metadata.fileName = info.fileName();
                    frame->metadata.format = info.suffix().toUpper();
                    frame->metadata.fileSize = info.size();
                    frame->metadata.modifiedAt = info.lastModified();
                    if (request.rawParameters && request.rawParameters->size.isValid()) {
                        frame->metadata.sourceSize = request.rawParameters->size;
                    } else {
                        // The disk cache stores only preview pixels. Probe the original header on
                        // this worker thread so legacy cache entries still report source dimensions
                        // instead of the cached 160×120 bitmap size.
                        QImageReader sourceReader(request.path);
                        frame->metadata.sourceSize = sourceReader.size();
                    }
                    if (!frame->metadata.sourceSize.isValid()) {
                        frame->metadata.sourceSize = cachedImage.size();
                    }
                    frame->storage = std::move(cachedImage);
                    result.frame = std::move(frame);
                }
            }
            if (!result.frame) {
                result = decoder->decode(request);
                if (result.frame && request.purpose == DecodePurpose::Thumbnail) {
                    if (const QImage* image = result.frame->qImage()) {
                        (void)diskCache->store(key, *image);
                    }
                }
            }
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, result = std::move(result), key]() {
                    if (!self) {
                        return;
                    }
                    if (result.frame) {
                        self->cache_.put(key, result.frame, result.frame->byteSize());
                    }
                    const QVector<PendingRequest> pending = self->inFlight_.take(key);
                    for (const PendingRequest& request : pending) {
                        request.callback(request.requestId, result);
                        if (!self) {
                            break;
                        }
                    }
                },
                Qt::QueuedConnection);
        },
        priority);
}

void ImageLoader::prefetchAdjacentRawFrames(const QString& path, const RawImageParameters& current,
                                            const QSize& previewSize) {
    const int frameCount = availableFrameCount(QFileInfo(path).size(), current);
    const int adjacentCount =
        (current.frameIndex > 0 ? 1 : 0) + (current.frameIndex + 1 < frameCount ? 1 : 0);
    const qsizetype estimatedFrameCost = estimatedFullFrameBytes(current);
    // Keep one quarter of the cache available for thumbnails, encoded images, and active UI
    // state. Full prefetch is useful only when current and adjacent frames can coexist.
    const qsizetype fullPrefetchBudget = cache_.maximumCost() / 4 * 3;
    const bool prefetchFull = adjacentCount > 0 && estimatedFrameCost > 0 &&
                              estimatedFrameCost <= fullPrefetchBudget / (adjacentCount + 1);
    for (const int delta : {-1, 1}) {
        RawImageParameters adjacent = current;
        adjacent.frameIndex += delta;
        if (adjacent.frameIndex < 0 || adjacent.frameIndex >= frameCount) {
            continue;
        }
        request(
            0, {path, DecodePurpose::Preview, previewSize, adjacent},
            [](quint64, const DecodeResult&) {}, -1);
        if (prefetchFull) {
            request(
                0, {path, DecodePurpose::Full, {}, adjacent}, [](quint64, const DecodeResult&) {},
                -2);
        }
    }
}

void ImageLoader::clearCache() { cache_.clear(); }

void ImageLoader::setRawParameters(const QString& path, const RawImageParameters& parameters) {
    rawParameters_.insert(QFileInfo(path).absoluteFilePath(), parameters);
}

std::optional<RawImageParameters> ImageLoader::rawParameters(const QString& path) const {
    const auto found = rawParameters_.constFind(QFileInfo(path).absoluteFilePath());
    return found == rawParameters_.cend() ? std::nullopt
                                          : std::optional<RawImageParameters>(*found);
}

bool ImageLoader::isCached(DecodeRequest request) const {
    Q_ASSERT(thread() == QThread::currentThread());
    if (!request.rawParameters) {
        request.rawParameters = rawParameters(request.path);
    }
    return cache_.contains(cacheKey(request, decoder_->cacheIdentity()));
}

QString ImageLoader::cacheKey(const DecodeRequest& request, const QString& decoderIdentity) {
    const QFileInfo info(request.path);
    return QStringLiteral("%1|%2|%3|%4x%5|%6|%7|%8")
        .arg(info.absoluteFilePath())
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(request.maximumSize.width())
        .arg(request.maximumSize.height())
        .arg(static_cast<int>(request.purpose))
        .arg(request.rawParameters ? request.rawParameters->cacheKey() : QStringLiteral("encoded"))
        .arg(decoderIdentity);
}

} // namespace ispview
