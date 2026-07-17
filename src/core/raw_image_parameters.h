#pragma once

#include <QPoint>
#include <QSize>
#include <QString>

#include <array>

namespace ispview {

enum class RawPixelFormat { NV12, NV21, I420, P010, MipiRaw10, MipiRaw12, Raw16 };
enum class BayerPattern { RGGB, GRBG, GBRG, BGGR };
enum class YuvMatrix { BT601, BT709, BT2020 };
enum class QuantizationRange { Full, Limited };
enum class ImageOrientation { Normal, Rotate90Clockwise, Rotate180, Rotate270Clockwise };

struct RawImageParameters {
    QSize size;
    RawPixelFormat format = RawPixelFormat::NV12;
    qsizetype headerOffset = 0;
    qsizetype rowStride = 0;
    qsizetype chromaStride = 0;
    int frameIndex = 0;
    bool littleEndian = true;
    bool msbAligned = false;
    // Zero selects the pixel-format default. A non-zero override is valid only for
    // Raw16 containers, where the sensor samples may use fewer than 16 bits.
    int validBitsOverride = 0;
    BayerPattern bayerPattern = BayerPattern::RGGB;
    YuvMatrix yuvMatrix = YuvMatrix::BT709;
    QuantizationRange range = QuantizationRange::Limited;
    ImageOrientation orientation = ImageOrientation::Normal;
    int blackLevel = 0;
    int whiteLevel = 0;
    bool demosaic = false;
    std::array<double, 3> whiteBalanceGains{1.0, 1.0, 1.0};
    std::array<double, 9> colorCorrectionMatrix{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    double displayGamma = 2.2;

    [[nodiscard]] bool isYuv() const;
    [[nodiscard]] int validBits() const;
    [[nodiscard]] bool hasValidBitLayout() const;
    [[nodiscard]] int maximumSampleValue() const;
    [[nodiscard]] bool hasValidDisplayTransform() const;
    [[nodiscard]] bool hasValidOrientation() const;
    [[nodiscard]] QString cacheKey() const;
};

[[nodiscard]] QString rawPixelFormatName(RawPixelFormat format);
[[nodiscard]] QString bayerPatternName(BayerPattern pattern);
[[nodiscard]] QString yuvMatrixName(YuvMatrix matrix);
[[nodiscard]] qsizetype minimumRowStride(const RawImageParameters& parameters);
[[nodiscard]] qsizetype minimumChromaRowStride(const RawImageParameters& parameters);
[[nodiscard]] qsizetype frameByteSize(const RawImageParameters& parameters);
[[nodiscard]] qsizetype estimatedFullFrameBytes(const RawImageParameters& parameters);
[[nodiscard]] int availableFrameCount(qint64 fileSize, const RawImageParameters& parameters);
[[nodiscard]] QSize orientedImageSize(const QSize& sourceSize, ImageOrientation orientation);
[[nodiscard]] QPoint displayToSourcePixel(const QPoint& displayPixel, const QSize& sourceSize,
                                          ImageOrientation orientation);

} // namespace ispview
