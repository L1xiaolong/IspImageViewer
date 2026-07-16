#include "io/camera_raw_decoder.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>

#if ISPVIEW_HAS_LIBRAW
#include <libraw.h>
#endif

namespace ispview {
namespace {

QStringList cameraRawSuffixes() {
    return {QStringLiteral("dng"), QStringLiteral("cr2"), QStringLiteral("cr3"),
            QStringLiteral("crw"), QStringLiteral("nef"), QStringLiteral("nrw"),
            QStringLiteral("arw"), QStringLiteral("sr2"), QStringLiteral("srf"),
            QStringLiteral("raf"), QStringLiteral("rw2"), QStringLiteral("orf"),
            QStringLiteral("pef"), QStringLiteral("srw"), QStringLiteral("x3f"),
            QStringLiteral("rwl")};
}

#if ISPVIEW_HAS_LIBRAW

struct ProcessedImageDeleter {
    void operator()(libraw_processed_image_t* image) const {
        if (image) {
            LibRaw::dcraw_clear_mem(image);
        }
    }
};

using ProcessedImage = std::unique_ptr<libraw_processed_image_t, ProcessedImageDeleter>;

QString libRawError(int code) {
    const char* message = libraw_strerror(code);
    return message ? QString::fromLocal8Bit(message) : QStringLiteral("Unknown LibRaw error");
}

int openFile(LibRaw& processor, const QString& path) {
#if defined(Q_OS_WIN)
    return processor.open_file(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
    const QByteArray nativePath = QFile::encodeName(path);
    return processor.open_file(nativePath.constData());
#endif
}

QImage processedImageToQImage(const libraw_processed_image_t& processed, QString* error) {
    if (processed.type == LIBRAW_IMAGE_JPEG) {
        if (processed.data_size > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
            if (error) {
                *error = QStringLiteral("LibRaw embedded JPEG is too large");
            }
            return {};
        }
        QByteArray encoded = QByteArray::fromRawData(
            reinterpret_cast<const char*>(processed.data), static_cast<int>(processed.data_size));
        QBuffer buffer(&encoded);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "JPEG");
        reader.setAutoTransform(true);
        QImage image = reader.read();
        if (image.isNull() && error) {
            *error = QStringLiteral("Invalid LibRaw embedded JPEG preview: %1")
                         .arg(reader.errorString());
        }
        return image;
    }
    if (processed.type != LIBRAW_IMAGE_BITMAP || processed.bits != 8 ||
        (processed.colors != 3 && processed.colors != 4)) {
        if (error) {
            *error = QStringLiteral("Unsupported LibRaw output: type %1, %2-bit, %3 channels")
                         .arg(static_cast<int>(processed.type))
                         .arg(processed.bits)
                         .arg(processed.colors);
        }
        return {};
    }

    const qsizetype bytesPerPixel = processed.colors;
    const qsizetype expected = static_cast<qsizetype>(processed.width) * processed.height *
                               bytesPerPixel;
    if (expected <= 0 || expected > processed.data_size) {
        if (error) {
            *error = QStringLiteral("LibRaw returned a truncated bitmap");
        }
        return {};
    }

    const QImage::Format sourceFormat = processed.colors == 3 ? QImage::Format_RGB888
                                                               : QImage::Format_RGBA8888;
    const qsizetype sourceStride = static_cast<qsizetype>(processed.width) * bytesPerPixel;
    const QImage source(processed.data, processed.width, processed.height,
                        static_cast<qsizetype>(sourceStride), sourceFormat);
    return source.convertToFormat(QImage::Format_RGBA8888);
}

ImageMetadata metadataFor(const QString& path, const LibRaw& processor) {
    const QFileInfo fileInfo(path);
    ImageMetadata metadata;
    metadata.path = fileInfo.absoluteFilePath();
    metadata.fileName = fileInfo.fileName();
    metadata.format = fileInfo.suffix().toUpper();
    metadata.fileSize = fileInfo.size();
    metadata.modifiedAt = fileInfo.lastModified();
    metadata.decoderName = QStringLiteral("LibRaw");
    metadata.decoderVersion = QString::fromLatin1(libraw_version());

    ImageMetadata::Camera camera;
    camera.make = QString::fromLocal8Bit(processor.imgdata.idata.make).trimmed();
    camera.model = QString::fromLocal8Bit(processor.imgdata.idata.model).trimmed();
    camera.lens = QString::fromLocal8Bit(processor.imgdata.lens.Lens).trimmed();
    if (camera.lens.isEmpty()) {
        camera.lens = QString::fromLocal8Bit(processor.imgdata.lens.makernotes.Lens).trimmed();
    }
    camera.exposureSeconds = processor.imgdata.other.shutter;
    camera.aperture = processor.imgdata.other.aperture;
    camera.focalLengthMm = processor.imgdata.other.focal_len;
    const double iso = processor.imgdata.other.iso_speed;
    if (std::isfinite(iso) && iso > 0.0 && iso <= std::numeric_limits<int>::max()) {
        camera.iso = static_cast<int>(std::lround(iso));
    }
    if (processor.imgdata.other.timestamp > 0) {
        camera.capturedAt = QDateTime::fromSecsSinceEpoch(processor.imgdata.other.timestamp);
    }
    camera.sensorSize = {processor.imgdata.sizes.raw_width, processor.imgdata.sizes.raw_height};
    metadata.camera = std::move(camera);
    return metadata;
}

DecodeResult frameFromImage(QImage image, ImageMetadata metadata, const QSize& maximumSize) {
    if (image.isNull()) {
        return {{}, QStringLiteral("LibRaw produced an empty image")};
    }
    if (!maximumSize.isEmpty() &&
        (image.width() > maximumSize.width() || image.height() > maximumSize.height())) {
        image = image.scaled(maximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }

    auto frame = std::make_shared<ImageFrame>();
    frame->descriptor.size = image.size();
    frame->descriptor.layout = PixelLayout::Interleaved;
    frame->descriptor.sampleType = SampleType::UInt;
    frame->descriptor.channelOrder = ChannelOrder::RGBA;
    frame->descriptor.storageBits = 8;
    frame->descriptor.validBits = 8;
    frame->metadata = std::move(metadata);
    frame->storage = std::move(image);
    return {std::move(frame), {}};
}

DecodeResult decodeWithLibRaw(const DecodeRequest& request) {
    // LibRaw is a large object (roughly 800 KiB in supported releases). Keep it
    // off the smaller platform worker-thread stack while retaining RAII cleanup.
    auto processor = std::make_unique<LibRaw>();
    int code = openFile(*processor, request.path);
    if (code != LIBRAW_SUCCESS) {
        return {{}, QStringLiteral("LibRaw could not open %1: %2")
                        .arg(QFileInfo(request.path).fileName(), libRawError(code))};
    }
    ImageMetadata metadata = metadataFor(request.path, *processor);

    if (request.purpose != DecodePurpose::Full) {
        code = processor->unpack_thumb();
        if (code == LIBRAW_SUCCESS) {
            int imageError = LIBRAW_SUCCESS;
            ProcessedImage processed(processor->dcraw_make_mem_thumb(&imageError));
            if (processed && imageError == LIBRAW_SUCCESS) {
                QString conversionError;
                QImage image = processedImageToQImage(*processed, &conversionError);
                if (!image.isNull()) {
                    return frameFromImage(std::move(image), std::move(metadata),
                                          request.maximumSize);
                }
            }
        }

        // A RAW without an embedded preview still remains viewable. Reopen because
        // failed thumbnail extraction can leave the processor in a progressed state.
        processor->recycle();
        code = openFile(*processor, request.path);
        if (code != LIBRAW_SUCCESS) {
            return {{}, QStringLiteral("LibRaw could not reopen the RAW file: %1")
                            .arg(libRawError(code))};
        }
        metadata = metadataFor(request.path, *processor);
        processor->imgdata.params.half_size = 1;
    }

    processor->imgdata.params.output_bps = 8;
    processor->imgdata.params.output_color = 1;
    processor->imgdata.params.use_camera_wb = 1;
    code = processor->unpack();
    if (code == LIBRAW_SUCCESS) {
        code = processor->dcraw_process();
    }
    if (code != LIBRAW_SUCCESS) {
        return {{}, QStringLiteral("LibRaw decode failed for %1: %2")
                        .arg(QFileInfo(request.path).fileName(), libRawError(code))};
    }

    int imageError = LIBRAW_SUCCESS;
    ProcessedImage processed(processor->dcraw_make_mem_image(&imageError));
    if (!processed || imageError != LIBRAW_SUCCESS) {
        return {{}, QStringLiteral("LibRaw image conversion failed: %1")
                        .arg(libRawError(imageError))};
    }
    QString conversionError;
    QImage image = processedImageToQImage(*processed, &conversionError);
    if (image.isNull()) {
        return {{}, conversionError};
    }
    return frameFromImage(std::move(image), std::move(metadata), request.maximumSize);
}

#endif

} // namespace

bool CameraRawDecoder::isAvailable() {
#if ISPVIEW_HAS_LIBRAW
    return true;
#else
    return false;
#endif
}

QStringList CameraRawDecoder::supportedSuffixes() {
    return isAvailable() ? cameraRawSuffixes() : QStringList{};
}

QString CameraRawDecoder::cacheIdentity() const {
#if ISPVIEW_HAS_LIBRAW
    return QStringLiteral("camera-raw-v1|libraw-%1")
        .arg(QString::fromLatin1(libraw_version()));
#else
    return QStringLiteral("camera-raw-disabled");
#endif
}

bool CameraRawDecoder::canDecode(const QString& path) const {
    return isAvailable() && cameraRawSuffixes().contains(QFileInfo(path).suffix().toLower());
}

DecodeResult CameraRawDecoder::decode(const DecodeRequest& request) const {
    if (!canDecode(request.path)) {
        return {{},
                QStringLiteral("Camera RAW support is unavailable or the format is unsupported")};
    }
#if ISPVIEW_HAS_LIBRAW
    // Even the reentrant LibRaw build can delegate to optional codec dependencies
    // with shared process state. Serializing this adapter also bounds peak memory
    // when thumbnail, preview, and full requests for a large RAW overlap.
    static std::mutex decoderMutex;
    const std::scoped_lock lock(decoderMutex);
    try {
        return decodeWithLibRaw(request);
    } catch (const std::bad_alloc&) {
        return {{}, QStringLiteral("LibRaw could not allocate enough memory")};
    } catch (const std::exception& exception) {
        return {{}, QStringLiteral("LibRaw exception: %1")
                        .arg(QString::fromLocal8Bit(exception.what()))};
    } catch (...) {
        return {{}, QStringLiteral("Unknown LibRaw exception")};
    }
#else
    return {{}, QStringLiteral("This build does not include LibRaw")};
#endif
}

} // namespace ispview
