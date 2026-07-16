#include "core/comparison_pixel_probe.h"

#include <algorithm>
#include <cmath>

namespace ispview {
namespace {

QPoint pixelAtNormalizedPoint(const QPointF& normalizedPoint, const QSize& size) {
    if (size.isEmpty() || !std::isfinite(normalizedPoint.x()) ||
        !std::isfinite(normalizedPoint.y()) || normalizedPoint.x() < 0.0 ||
        normalizedPoint.x() > 1.0 || normalizedPoint.y() < 0.0 || normalizedPoint.y() > 1.0) {
        return {-1, -1};
    }
    const double u = normalizedPoint.x();
    const double v = normalizedPoint.y();
    return {std::min(static_cast<int>(std::floor(u * size.width())), size.width() - 1),
            std::min(static_cast<int>(std::floor(v * size.height())), size.height() - 1)};
}

QString rgbaText(const QColor& color) {
    return color.isValid() ? QStringLiteral("RGBA(%1,%2,%3,%4)")
                                 .arg(color.red())
                                 .arg(color.green())
                                 .arg(color.blue())
                                 .arg(color.alpha())
                           : QString{};
}

} // namespace

QString ComparisonPixelSample::sourceValueText() const {
    if (yuv) {
        return QStringLiteral("YUV(%1,%2,%3)").arg(yuv->y).arg(yuv->u).arg(yuv->v);
    }
    if (bayer) {
        return QStringLiteral("RAW(%1, %2)")
            .arg(bayer->value)
            .arg(RawPlaneAccessor::channelName(bayer->channel));
    }
    return rgbaText(displayColor);
}

QString ComparisonPixelSample::displayValueText() const { return rgbaText(displayColor); }

QPointF ComparisonPixelProbe::normalizedPixelCenter(const QPoint& pixel, const QSize& imageSize) {
    if (imageSize.isEmpty() || !QRect(QPoint{}, imageSize).contains(pixel)) {
        return {};
    }
    return {(pixel.x() + 0.5) / imageSize.width(), (pixel.y() + 0.5) / imageSize.height()};
}

ComparisonPixelSample ComparisonPixelProbe::sample(const ImageFrame& frame,
                                                   const QPointF& normalizedPoint) {
    const RawPlaneAccessor rawAccessor(frame);
    const QSize logicalSize =
        rawAccessor.isValid() ? rawAccessor.displaySize() : frame.descriptor.size;
    const QPoint displayPixel = pixelAtNormalizedPoint(normalizedPoint, logicalSize);
    if (!QRect(QPoint{}, logicalSize).contains(displayPixel)) {
        return {};
    }

    ComparisonPixelSample result;
    result.valid = true;
    result.displayPixel = displayPixel;
    result.sourcePixel = displayPixel;
    if (rawAccessor.isValid()) {
        if (rawAccessor.isYuv()) {
            result.yuv = rawAccessor.yuvAtDisplayPixel(displayPixel);
            if (result.yuv) {
                result.sourcePixel = result.yuv->sourcePixel;
            }
        } else {
            result.bayer = rawAccessor.bayerAtDisplayPixel(displayPixel);
            if (result.bayer) {
                result.sourcePixel = result.bayer->sourcePixel;
            }
        }
    }

    const QImage* display = frame.qImage();
    if (display && !display->isNull()) {
        const int sampleX = std::clamp(
            static_cast<int>((displayPixel.x() + 0.5) * display->width() / logicalSize.width()), 0,
            display->width() - 1);
        const int sampleY = std::clamp(
            static_cast<int>((displayPixel.y() + 0.5) * display->height() / logicalSize.height()),
            0, display->height() - 1);
        result.displayColor = display->pixelColor(sampleX, sampleY);
    }
    return result;
}

} // namespace ispview
