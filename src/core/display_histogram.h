#pragma once

#include "core/image_types.h"

#include <QSize>
#include <QRect>
#include <QRectF>

#include <array>

namespace ispview {

struct HistogramChannel {
    std::array<quint64, 256> bins{};
    int minimum = 0;
    int maximum = 0;
    double mean = 0.0;
    double standardDeviation = 0.0;
};

struct DisplayHistogram {
    HistogramChannel red;
    HistogramChannel green;
    HistogramChannel blue;
    HistogramChannel luma;
    QSize analyzedSize;
    QSize logicalSize;
    QRect analyzedRegion;
    QRect logicalRegion;
    qint64 availablePixelCount = 0;
    qint64 sampledPixelCount = 0;

    [[nodiscard]] bool isValid() const { return sampledPixelCount > 0; }
    [[nodiscard]] bool usesDisplayProxy() const { return analyzedSize != logicalSize; }
    [[nodiscard]] bool isSubsampled() const {
        return sampledPixelCount > 0 && sampledPixelCount < availablePixelCount;
    }
    [[nodiscard]] bool isRegionLimited() const {
        return isValid() && logicalRegion != QRect(QPoint{}, logicalSize);
    }
};

class DisplayHistogramAnalyzer final {
  public:
    static constexpr qint64 kDefaultMaximumSamples = 262'144;

    [[nodiscard]] static DisplayHistogram analyze(
        const ImageFrame& frame, qint64 maximumSamples = kDefaultMaximumSamples);
    [[nodiscard]] static DisplayHistogram analyzeRegion(
        const ImageFrame& frame, const QRectF& normalizedRegion,
        qint64 maximumSamples = kDefaultMaximumSamples);
};

} // namespace ispview
