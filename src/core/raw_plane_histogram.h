#pragma once

#include "core/raw_plane_access.h"

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QVector>

namespace ispview {

enum class RawHistogramDomain { Yuv, Bayer };
enum class RawHistogramChannelId { Y, U, V, Red, GreenRedRow, GreenBlueRow, Blue };

struct RawHistogramChannel {
    RawHistogramChannelId id = RawHistogramChannelId::Y;
    QVector<quint64> bins;
    qint64 availableSampleCount = 0;
    qint64 sampledSampleCount = 0;
    int minimum = 0;
    int maximum = 0;
    double mean = 0.0;
    double standardDeviation = 0.0;

    [[nodiscard]] bool isValid() const { return sampledSampleCount > 0; }
    [[nodiscard]] bool isSubsampled() const {
        return sampledSampleCount > 0 && sampledSampleCount < availableSampleCount;
    }
};

struct RawPlaneHistogram {
    RawHistogramDomain domain = RawHistogramDomain::Yuv;
    QSize logicalSize;
    QRect logicalRegion;
    QRect sourceRegion;
    int validBits = 0;
    // The visible histogram domain. Bayer data honors an explicit sensor white level;
    // samples above it are accumulated into the last (saturated) bin.
    int maximumValue = 0;
    QVector<RawHistogramChannel> channels;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isRegionLimited() const {
        return isValid() && logicalRegion != QRect(QPoint{}, logicalSize);
    }
};

class RawPlaneHistogramAnalyzer final {
  public:
    // The cap is applied independently to each engineering channel. Chroma and Bayer channels
    // have different native sample grids, so a single total-pixel count would hide that fact.
    static constexpr qint64 kDefaultMaximumSamplesPerChannel = 262'144;

    [[nodiscard]] static RawPlaneHistogram
    analyze(const ImageFrame& frame,
            qint64 maximumSamplesPerChannel = kDefaultMaximumSamplesPerChannel);
    [[nodiscard]] static RawPlaneHistogram
    analyzeRegion(const ImageFrame& frame, const QRectF& normalizedRegion,
                  qint64 maximumSamplesPerChannel = kDefaultMaximumSamplesPerChannel);
};

[[nodiscard]] QString rawHistogramChannelName(RawHistogramChannelId id);

} // namespace ispview
