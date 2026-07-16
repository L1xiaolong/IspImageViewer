#pragma once

#include "core/image_types.h"
#include "core/view_state.h"

#include <QImage>
#include <QRectF>
#include <QWidget>

namespace ispview {

class NavigationThumbnailOverlay final : public QWidget {
    Q_OBJECT

  public:
    explicit NavigationThumbnailOverlay(QWidget* parent = nullptr);

    void setFrame(const ImageFramePtr& frame);
    void setView(const ViewState& state, const QSize& viewportSize, const QSize& imageSize);
    void layoutWithin(const QRect& parentRect, const QSize& imageSize);

    [[nodiscard]] QRectF normalizedViewportRect() const { return normalizedViewportRect_; }
    [[nodiscard]] QString zoomText() const { return zoomText_; }

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    [[nodiscard]] QRectF imageRect() const;

    QString framePath_;
    QImage thumbnail_;
    QRectF normalizedViewportRect_;
    QString zoomText_;
};

} // namespace ispview
