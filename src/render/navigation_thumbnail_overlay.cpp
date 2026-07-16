#include "render/navigation_thumbnail_overlay.h"

#include <QFont>
#include <QPaintEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace ispview {
namespace {

constexpr int overlayMargin = 12;
constexpr int contentPadding = 5;
constexpr int maximumThumbnailWidth = 180;
constexpr int maximumThumbnailHeight = 130;

QString formattedZoom(double scale) {
    const double percent = scale * 100.0;
    return std::abs(percent - std::round(percent)) < 0.05
               ? QStringLiteral("%1%").arg(qRound(percent))
               : QStringLiteral("%1%").arg(QString::number(percent, 'f', 1));
}

} // namespace

NavigationThumbnailOverlay::NavigationThumbnailOverlay(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("navigationThumbnailOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void NavigationThumbnailOverlay::setFrame(const ImageFramePtr& frame) {
    if (!frame) {
        framePath_.clear();
        thumbnail_ = {};
        update();
        return;
    }

    // Preview and Full frames have the same path. Retaining the bounded Preview thumbnail avoids
    // resampling a 48 MP Full image on the UI thread when progressive loading completes.
    if (!thumbnail_.isNull() && !frame->metadata.path.isEmpty() &&
        framePath_ == frame->metadata.path) {
        return;
    }

    framePath_ = frame->metadata.path;
    const QImage* source = frame->qImage();
    thumbnail_ = source && !source->isNull()
                     ? source->scaled(maximumThumbnailWidth, maximumThumbnailHeight,
                                      Qt::KeepAspectRatio, Qt::SmoothTransformation)
                     : QImage{};
    update();
}

void NavigationThumbnailOverlay::setView(const ViewState& state, const QSize& viewportSize,
                                         const QSize& imageSize) {
    normalizedViewportRect_ = ViewTransform::visibleNormalizedRect(viewportSize, imageSize, state);
    zoomText_ = formattedZoom(state.pixelsPerImagePixel);
    update();
}

void NavigationThumbnailOverlay::layoutWithin(const QRect& parentRect, const QSize& imageSize) {
    if (parentRect.isEmpty() || imageSize.isEmpty()) {
        hide();
        return;
    }

    const int availableWidth =
        std::max(48, std::min(maximumThumbnailWidth, parentRect.width() / 3));
    const int availableHeight =
        std::max(36, std::min(maximumThumbnailHeight, parentRect.height() / 3));
    const QSize contentSize =
        imageSize.scaled(availableWidth, availableHeight, Qt::KeepAspectRatio);
    const QSize overlaySize(contentSize.width() + contentPadding * 2,
                            contentSize.height() + contentPadding * 2);
    setGeometry(parentRect.left() + overlayMargin,
                parentRect.bottom() - overlayMargin - overlaySize.height() + 1, overlaySize.width(),
                overlaySize.height());
    raise();
}

QRectF NavigationThumbnailOverlay::imageRect() const {
    return QRectF(rect()).adjusted(contentPadding, contentPadding, -contentPadding,
                                   -contentPadding);
}

void NavigationThumbnailOverlay::paintEvent(QPaintEvent*) {
    const QRectF target = imageRect();
    if (target.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    painter.setPen(QPen(QColor(255, 255, 255, 170), 1.0));
    painter.setBrush(QColor(12, 12, 14, 105));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);

    if (!thumbnail_.isNull()) {
        painter.save();
        painter.setOpacity(0.70);
        painter.drawImage(target, thumbnail_);
        painter.restore();
    }

    if (!normalizedViewportRect_.isEmpty()) {
        const QRectF viewport(target.left() + normalizedViewportRect_.left() * target.width(),
                              target.top() + normalizedViewportRect_.top() * target.height(),
                              normalizedViewportRect_.width() * target.width(),
                              normalizedViewportRect_.height() * target.height());
        painter.setBrush(QColor(255, 255, 255, 24));
        painter.setPen(QPen(QColor(0, 0, 0, 190), 4.0));
        painter.drawRect(viewport);
        painter.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
        painter.drawRect(viewport);
    }

    QFont font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);
    const QPointF textOrigin(target.left() + 5.0, target.top() + 14.0);
    painter.setPen(QPen(QColor(0, 0, 0, 230), 3.0));
    painter.drawText(textOrigin, zoomText_);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(textOrigin, zoomText_);
}

} // namespace ispview
