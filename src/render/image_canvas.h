#pragma once

#include "core/image_types.h"
#include "core/view_state.h"

#include <QRhiWidget>

#include <QColor>
#include <QPoint>

#include <memory>

QT_BEGIN_NAMESPACE
class QMouseEvent;
class QResizeEvent;
class QWheelEvent;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
QT_END_NAMESPACE

namespace ispview {

class RoiOverlay;
class NavigationThumbnailOverlay;

enum class ImageCompareMode {
    Single = 0,
    VerticalSplit = 1,
    HorizontalSplit = 2,
};

class ImageCanvas final : public QRhiWidget {
    Q_OBJECT

  public:
    explicit ImageCanvas(QWidget* parent = nullptr);
    ~ImageCanvas() override;

    void setFrame(ImageFramePtr frame, bool resetView = true);
    [[nodiscard]] ImageFramePtr frame() const { return frame_; }
    void setComparisonFrame(ImageFramePtr frame);
    [[nodiscard]] ImageFramePtr comparisonFrame() const { return comparisonFrame_; }
    void setCompareMode(ImageCompareMode mode);
    [[nodiscard]] ImageCompareMode compareMode() const { return compareMode_; }
    void setCompareAmount(float amount);
    [[nodiscard]] float compareAmount() const { return compareAmount_; }

    void fitImage();
    void actualPixels();
    void setNavigationThumbnailEnabled(bool enabled);
    [[nodiscard]] bool navigationThumbnailEnabled() const { return navigationThumbnailEnabled_; }
    void setRoiSelectionEnabled(bool enabled);
    [[nodiscard]] bool roiSelectionEnabled() const { return roiSelectionEnabled_; }
    void clearRoi();
    [[nodiscard]] std::optional<QRectF> normalizedRoi() const { return state_.normalizedRoi; }
    [[nodiscard]] QRectF roiWidgetRect() const;
    [[nodiscard]] QSize logicalImageSize() const;
    void setViewState(const ViewState& state, bool notify = false);
    [[nodiscard]] ViewState viewState() const;
    [[nodiscard]] bool usingGpuYuvPlanes() const { return gpuYuvReady_; }
    [[nodiscard]] bool usingGpuBayerPlane() const { return gpuBayerReady_; }
    [[nodiscard]] bool usingGpuYuvComparison() const { return comparisonGpuYuvReady_; }
    [[nodiscard]] bool usingGpuBayerComparison() const { return comparisonGpuBayerReady_; }

  signals:
    void viewStateChanged(const ispview::ViewState& state);
    void pixelHovered(const QPoint& pixel, const QColor& color, bool valid);
    void roiChanged(const QRectF& normalizedRoi, bool valid);
    void activated();

  protected:
    void initialize(QRhiCommandBuffer* commandBuffer) override;
    void render(QRhiCommandBuffer* commandBuffer) override;
    void releaseResources() override;

    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void resetRhiResources();
    void rebuildTexture(QRhiCommandBuffer* commandBuffer);
    void rebuildBindings();
    [[nodiscard]] bool hasGpuYuvFrame() const;
    [[nodiscard]] bool hasGpuBayerFrame() const;
    [[nodiscard]] bool frameSupportsGpuYuv(const ImageFramePtr& frame) const;
    [[nodiscard]] bool frameSupportsGpuBayer(const ImageFramePtr& frame) const;
    [[nodiscard]] bool hasDisplayableFrame() const;
    [[nodiscard]] ViewState effectiveViewState() const;
    void notifyStateChanged();
    void updateNavigationThumbnail();
    void updatePixelProbe(const QPointF& widgetPosition);
    [[nodiscard]] QPointF normalizedImagePoint(const QPointF& widgetPosition) const;
    [[nodiscard]] bool isNearCompareDivider(const QPointF& widgetPosition) const;
    void updateCompareAmount(const QPointF& widgetPosition);
    void updateRoiSelection(const QPointF& widgetPosition);

    ImageFramePtr frame_;
    ImageFramePtr comparisonFrame_;
    QImage image_;
    QImage comparisonImage_;
    ViewState state_;
    ImageCompareMode compareMode_ = ImageCompareMode::Single;
    float compareAmount_ = 0.5F;
    bool textureDirty_ = true;
    bool gpuYuvReady_ = false;
    bool gpuBayerReady_ = false;
    bool comparisonGpuYuvReady_ = false;
    bool comparisonGpuBayerReady_ = false;
    bool dragging_ = false;
    bool compareDividerDragging_ = false;
    bool roiSelectionEnabled_ = false;
    bool roiSelecting_ = false;
    bool navigationThumbnailEnabled_ = false;
    QPointF lastMousePosition_;
    QPointF roiStartWidgetPosition_;
    QPointF roiStartNormalized_;
    RoiOverlay* roiOverlay_ = nullptr;
    NavigationThumbnailOverlay* navigationThumbnailOverlay_ = nullptr;

    QRhi* rhi_ = nullptr;
    std::unique_ptr<QRhiBuffer> vertexBuffer_;
    std::unique_ptr<QRhiBuffer> uniformBuffer_;
    std::unique_ptr<QRhiBuffer> compareUniformBuffer_;
    std::unique_ptr<QRhiBuffer> yuvUniformBuffer_;
    std::unique_ptr<QRhiBuffer> comparisonYuvUniformBuffer_;
    std::unique_ptr<QRhiBuffer> bayerUniformBuffer_;
    std::unique_ptr<QRhiBuffer> comparisonBayerUniformBuffer_;
    std::unique_ptr<QRhiTexture> texture_;
    std::unique_ptr<QRhiTexture> comparisonTexture_;
    std::unique_ptr<QRhiTexture> yTexture_;
    std::unique_ptr<QRhiTexture> uTexture_;
    std::unique_ptr<QRhiTexture> vTexture_;
    std::unique_ptr<QRhiTexture> rawTexture_;
    std::unique_ptr<QRhiTexture> comparisonYTexture_;
    std::unique_ptr<QRhiTexture> comparisonUTexture_;
    std::unique_ptr<QRhiTexture> comparisonVTexture_;
    std::unique_ptr<QRhiTexture> comparisonRawTexture_;
    std::unique_ptr<QRhiSampler> sampler_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiShaderResourceBindings> yuvBindings_;
    std::unique_ptr<QRhiShaderResourceBindings> bayerBindings_;
    std::unique_ptr<QRhiShaderResourceBindings> yuvCompareBindings_;
    std::unique_ptr<QRhiShaderResourceBindings> bayerCompareBindings_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> yuvPipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> bayerPipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> yuvComparePipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> bayerComparePipeline_;
};

} // namespace ispview
