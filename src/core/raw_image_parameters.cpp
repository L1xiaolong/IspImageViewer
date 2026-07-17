#include "core/raw_image_parameters.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ispview {
namespace {

qsizetype checkedMultiply(qsizetype left, qsizetype right) {
    if (left < 0 || right < 0 ||
        (left != 0 && right > std::numeric_limits<qsizetype>::max() / left)) {
        return -1;
    }
    return left * right;
}

qsizetype effectiveStride(qsizetype configured, qsizetype minimum) {
    if (minimum < 0 || (configured > 0 && configured < minimum)) {
        return -1;
    }
    return configured > 0 ? configured : minimum;
}

} // namespace

bool RawImageParameters::isYuv() const {
    return format == RawPixelFormat::NV12 || format == RawPixelFormat::NV21 ||
           format == RawPixelFormat::I420 || format == RawPixelFormat::P010;
}

int RawImageParameters::validBits() const {
    switch (format) {
    case RawPixelFormat::P010:
    case RawPixelFormat::MipiRaw10:
        return 10;
    case RawPixelFormat::MipiRaw12:
        return 12;
    case RawPixelFormat::Raw16:
        return validBitsOverride > 0 ? validBitsOverride : 16;
    default:
        return 8;
    }
}

bool RawImageParameters::hasValidBitLayout() const {
    if (format != RawPixelFormat::Raw16) {
        return validBitsOverride == 0;
    }
    return validBitsOverride >= 0 && validBitsOverride <= 16;
}

int RawImageParameters::maximumSampleValue() const { return (1 << validBits()) - 1; }

bool RawImageParameters::hasValidDisplayTransform() const {
    const bool validGains =
        std::all_of(whiteBalanceGains.cbegin(), whiteBalanceGains.cend(),
                    [](double gain) { return std::isfinite(gain) && gain > 0.0 && gain <= 64.0; });
    const bool validMatrix = std::all_of(
        colorCorrectionMatrix.cbegin(), colorCorrectionMatrix.cend(), [](double coefficient) {
            return std::isfinite(coefficient) && std::abs(coefficient) <= 64.0;
        });
    return validGains && validMatrix && std::isfinite(displayGamma) && displayGamma >= 0.1 &&
           displayGamma <= 10.0;
}

bool RawImageParameters::hasValidOrientation() const {
    const int value = static_cast<int>(orientation);
    return value >= static_cast<int>(ImageOrientation::Normal) &&
           value <= static_cast<int>(ImageOrientation::Rotate270Clockwise);
}

QString RawImageParameters::cacheKey() const {
    QString result = QStringLiteral("%1x%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16")
                         .arg(size.width())
                         .arg(size.height())
                         .arg(static_cast<int>(format))
                         .arg(headerOffset)
                         .arg(rowStride)
                         .arg(chromaStride)
                         .arg(frameIndex)
                         .arg(littleEndian)
                         .arg(msbAligned)
                         .arg(validBitsOverride)
                         .arg(static_cast<int>(bayerPattern))
                         .arg(static_cast<int>(yuvMatrix))
                         .arg(static_cast<int>(range))
                         .arg(blackLevel)
                         .arg(whiteLevel)
                         .arg(static_cast<int>(orientation));
    if (isYuv()) {
        return result;
    }
    result += QLatin1Char('|') + QString::number(demosaic);
    for (double gain : whiteBalanceGains) {
        result += QLatin1Char('|') + QString::number(gain, 'g', 17);
    }
    for (double coefficient : colorCorrectionMatrix) {
        result += QLatin1Char('|') + QString::number(coefficient, 'g', 17);
    }
    result += QLatin1Char('|') + QString::number(displayGamma, 'g', 17);
    return result;
}

QString rawPixelFormatName(RawPixelFormat format) {
    switch (format) {
    case RawPixelFormat::NV12:
        return QStringLiteral("NV12");
    case RawPixelFormat::NV21:
        return QStringLiteral("NV21");
    case RawPixelFormat::I420:
        return QStringLiteral("I420");
    case RawPixelFormat::P010:
        return QStringLiteral("P010");
    case RawPixelFormat::MipiRaw10:
        return QStringLiteral("MIPI RAW10");
    case RawPixelFormat::MipiRaw12:
        return QStringLiteral("MIPI RAW12");
    case RawPixelFormat::Raw16:
        return QStringLiteral("RAW16");
    }
    return {};
}

QString bayerPatternName(BayerPattern pattern) {
    switch (pattern) {
    case BayerPattern::RGGB:
        return QStringLiteral("RGGB");
    case BayerPattern::GRBG:
        return QStringLiteral("GRBG");
    case BayerPattern::GBRG:
        return QStringLiteral("GBRG");
    case BayerPattern::BGGR:
        return QStringLiteral("BGGR");
    }
    return {};
}

QString yuvMatrixName(YuvMatrix matrix) {
    switch (matrix) {
    case YuvMatrix::BT601:
        return QStringLiteral("BT.601");
    case YuvMatrix::BT709:
        return QStringLiteral("BT.709");
    case YuvMatrix::BT2020:
        return QStringLiteral("BT.2020");
    }
    return {};
}

qsizetype minimumRowStride(const RawImageParameters& parameters) {
    const qsizetype width = parameters.size.width();
    switch (parameters.format) {
    case RawPixelFormat::NV12:
    case RawPixelFormat::NV21:
    case RawPixelFormat::I420:
        return width;
    case RawPixelFormat::P010:
    case RawPixelFormat::Raw16:
        return checkedMultiply(width, 2);
    case RawPixelFormat::MipiRaw10:
        return checkedMultiply((width + 3) / 4, 5);
    case RawPixelFormat::MipiRaw12:
        return checkedMultiply((width + 1) / 2, 3);
    }
    return -1;
}

qsizetype minimumChromaRowStride(const RawImageParameters& parameters) {
    const qsizetype chromaWidth = (static_cast<qsizetype>(parameters.size.width()) + 1) / 2;
    switch (parameters.format) {
    case RawPixelFormat::NV12:
    case RawPixelFormat::NV21:
        return checkedMultiply(chromaWidth, 2);
    case RawPixelFormat::I420:
        return chromaWidth;
    case RawPixelFormat::P010:
        return checkedMultiply(chromaWidth, 4);
    default:
        return 0;
    }
}

qsizetype frameByteSize(const RawImageParameters& parameters) {
    if (parameters.size.isEmpty()) {
        return -1;
    }
    const qsizetype height = parameters.size.height();
    const qsizetype rowStride = effectiveStride(parameters.rowStride, minimumRowStride(parameters));
    if (rowStride < 0) {
        return -1;
    }
    const qsizetype primary = checkedMultiply(rowStride, height);
    if (primary < 0 || !parameters.isYuv()) {
        return primary;
    }

    const qsizetype chromaHeight = (height + 1) / 2;
    const qsizetype minimumChromaStride = minimumChromaRowStride(parameters);
    int planeCount = 1;
    if (parameters.format == RawPixelFormat::I420) {
        planeCount = 2;
    }
    const qsizetype chromaStride = effectiveStride(parameters.chromaStride, minimumChromaStride);
    if (chromaStride < 0) {
        return -1;
    }
    const qsizetype chroma = checkedMultiply(chromaStride, chromaHeight);
    if (chroma < 0 || chroma > std::numeric_limits<qsizetype>::max() / planeCount ||
        primary > std::numeric_limits<qsizetype>::max() - chroma * planeCount) {
        return -1;
    }
    return primary + chroma * planeCount;
}

qsizetype estimatedFullFrameBytes(const RawImageParameters& parameters) {
    const qsizetype sourceBytes = frameByteSize(parameters);
    const qsizetype pixels = checkedMultiply(parameters.size.width(), parameters.size.height());
    const qsizetype displayBytes = checkedMultiply(pixels, 4);
    if (sourceBytes <= 0 || displayBytes < 0 ||
        sourceBytes > std::numeric_limits<qsizetype>::max() - displayBytes) {
        return -1;
    }
    return sourceBytes + displayBytes;
}

int availableFrameCount(qint64 fileSize, const RawImageParameters& parameters) {
    const qsizetype frameSize = frameByteSize(parameters);
    if (frameSize <= 0 || parameters.headerOffset < 0 || fileSize < parameters.headerOffset) {
        return 0;
    }
    const qint64 count = (fileSize - parameters.headerOffset) / frameSize;
    return static_cast<int>(std::min<qint64>(count, std::numeric_limits<int>::max()));
}

QSize orientedImageSize(const QSize& sourceSize, ImageOrientation orientation) {
    if (orientation == ImageOrientation::Rotate90Clockwise ||
        orientation == ImageOrientation::Rotate270Clockwise) {
        return {sourceSize.height(), sourceSize.width()};
    }
    return sourceSize;
}

QPoint displayToSourcePixel(const QPoint& displayPixel, const QSize& sourceSize,
                            ImageOrientation orientation) {
    switch (orientation) {
    case ImageOrientation::Normal:
        return displayPixel;
    case ImageOrientation::Rotate90Clockwise:
        return {displayPixel.y(), sourceSize.height() - 1 - displayPixel.x()};
    case ImageOrientation::Rotate180:
        return {sourceSize.width() - 1 - displayPixel.x(),
                sourceSize.height() - 1 - displayPixel.y()};
    case ImageOrientation::Rotate270Clockwise:
        return {sourceSize.width() - 1 - displayPixel.y(), displayPixel.x()};
    }
    return displayPixel;
}

} // namespace ispview
