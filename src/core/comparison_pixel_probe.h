#pragma once

#include "core/raw_plane_access.h"

#include <QColor>
#include <QPointF>
#include <QString>

namespace ispview {

struct ComparisonPixelSample {
    bool valid = false;
    QPoint displayPixel;
    QPoint sourcePixel;
    QColor displayColor;
    std::optional<YuvPlaneSample> yuv;
    std::optional<BayerPlaneSample> bayer;

    [[nodiscard]] QString sourceValueText() const;
    [[nodiscard]] QString displayValueText() const;
};

class ComparisonPixelProbe final {
  public:
    [[nodiscard]] static QPointF normalizedPixelCenter(const QPoint& pixel, const QSize& imageSize);
    [[nodiscard]] static ComparisonPixelSample sample(const ImageFrame& frame,
                                                      const QPointF& normalizedPoint);
};

} // namespace ispview
