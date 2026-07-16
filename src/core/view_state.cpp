#include "core/view_state.h"

#include <algorithm>
#include <cmath>

namespace ispview {

double ViewTransform::fitScale(const QSize& imageSize, const QSize& viewportSize) {
    if (imageSize.isEmpty() || viewportSize.isEmpty()) {
        return 1.0;
    }
    const double horizontal = static_cast<double>(viewportSize.width()) / imageSize.width();
    const double vertical = static_cast<double>(viewportSize.height()) / imageSize.height();
    return std::max(0.0001, std::min(horizontal, vertical));
}

QPointF ViewTransform::widgetToImage(const QPointF& widgetPoint, const QSize& viewportSize,
                                     const QSize& imageSize, const ViewState& state) {
    if (imageSize.isEmpty() || state.pixelsPerImagePixel <= 0.0) {
        return {};
    }
    const QPointF viewportCenter(viewportSize.width() * 0.5, viewportSize.height() * 0.5);
    const QPointF imageCenter(state.normalizedCenter.x() * imageSize.width(),
                              state.normalizedCenter.y() * imageSize.height());
    return imageCenter + (widgetPoint - viewportCenter) / state.pixelsPerImagePixel;
}

QPointF ViewTransform::imageToWidget(const QPointF& imagePoint, const QSize& viewportSize,
                                     const QSize& imageSize, const ViewState& state) {
    const QPointF viewportCenter(viewportSize.width() * 0.5, viewportSize.height() * 0.5);
    const QPointF imageCenter(state.normalizedCenter.x() * imageSize.width(),
                              state.normalizedCenter.y() * imageSize.height());
    return viewportCenter + (imagePoint - imageCenter) * state.pixelsPerImagePixel;
}

ViewState ViewTransform::zoomAt(const ViewState& state, double newScale,
                                const QPointF& anchorInWidget, const QSize& viewportSize,
                                const QSize& imageSize) {
    if (imageSize.isEmpty()) {
        return state;
    }

    ViewState result = state;
    newScale = std::clamp(newScale, 0.005, 64.0);
    const QPointF anchorImage = widgetToImage(anchorInWidget, viewportSize, imageSize, state);
    const QPointF viewportCenter(viewportSize.width() * 0.5, viewportSize.height() * 0.5);
    const QPointF newCenterImage = anchorImage - (anchorInWidget - viewportCenter) / newScale;

    result.pixelsPerImagePixel = newScale;
    result.normalizedCenter = clampedCenter(
        {newCenterImage.x() / imageSize.width(), newCenterImage.y() / imageSize.height()});
    result.fitMode = FitMode::Manual;
    return result;
}

ViewState ViewTransform::panBy(const ViewState& state, const QPointF& widgetDelta,
                               const QSize& imageSize) {
    if (imageSize.isEmpty() || state.pixelsPerImagePixel <= 0.0) {
        return state;
    }
    ViewState result = state;
    result.normalizedCenter -=
        QPointF(widgetDelta.x() / (imageSize.width() * state.pixelsPerImagePixel),
                widgetDelta.y() / (imageSize.height() * state.pixelsPerImagePixel));
    result.normalizedCenter = clampedCenter(result.normalizedCenter);
    result.fitMode = FitMode::Manual;
    return result;
}

QPointF ViewTransform::clampedCenter(QPointF center) {
    center.setX(std::clamp(center.x(), 0.0, 1.0));
    center.setY(std::clamp(center.y(), 0.0, 1.0));
    return center;
}

std::optional<QRectF> ViewTransform::clampedNormalizedRoi(QRectF roi) {
    roi = roi.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (roi.width() <= 0.0 || roi.height() <= 0.0) {
        return std::nullopt;
    }
    return roi;
}

} // namespace ispview
