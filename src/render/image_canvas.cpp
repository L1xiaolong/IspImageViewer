#include "render/image_canvas.h"
#include "render/bayer_render_parameters.h"
#include "render/navigation_thumbnail_overlay.h"
#include "render/yuv_render_parameters.h"

#include <QFile>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtGui/qrgbafloat.h>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ispview {

class RoiOverlay final : public QWidget {
  public:
    explicit RoiOverlay(ImageCanvas* canvas) : QWidget(canvas), canvas_(canvas) {
        setObjectName(QStringLiteral("roiOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        const QRectF roi = canvas_->roiWidgetRect();
        if (roi.isEmpty()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(60, 160, 255, 35));
        painter.setPen(QPen(QColor(100, 190, 255), 1.5, Qt::DashLine));
        painter.drawRect(roi);
        painter.setPen(QColor(235, 245, 255));
        painter.drawText(roi.adjusted(5.0, 3.0, -5.0, -3.0), Qt::AlignLeft | Qt::AlignTop,
                         QStringLiteral("ROI"));
    }

  private:
    ImageCanvas* canvas_;
};

namespace {

constexpr std::array<float, 24> kQuadVertices{
    -1.0F, -1.0F, 0.0F, 1.0F, 1.0F, -1.0F, 1.0F, 1.0F, -1.0F, 1.0F, 0.0F, 0.0F,
    -1.0F, 1.0F,  0.0F, 0.0F, 1.0F, -1.0F, 1.0F, 1.0F, 1.0F,  1.0F, 1.0F, 0.0F,
};

QShader loadShader(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
}

QRhiTextureSubresourceUploadDescription planeUpload(const PlaneBufferSet& storage, int planeIndex,
                                                    const QSize& size) {
    const PlaneBuffer& plane = storage.planes.at(planeIndex);
    const QByteArray view =
        QByteArray::fromRawData(storage.storage.constData() + plane.offset, plane.byteSize);
    QRhiTextureSubresourceUploadDescription upload(view);
    upload.setDataStride(static_cast<quint32>(plane.stride));
    upload.setSourceSize(size);
    return upload;
}

QRhiTextureSubresourceUploadDescription p010PlaneUpload(const PlaneBufferSet& storage,
                                                        int planeIndex, const QSize& size,
                                                        bool littleEndian) {
    if (littleEndian) {
        return planeUpload(storage, planeIndex, size);
    }

    const PlaneBuffer& plane = storage.planes.at(planeIndex);
    QByteArray normalized(plane.byteSize, Qt::Uninitialized);
    const char* source = storage.storage.constData() + plane.offset;
    char* destination = normalized.data();

    // R16/RG16 uploads on the supported Windows and macOS targets consume
    // little-endian component bytes. Preserve the immutable source Plane and
    // normalize only the upload payload for uncommon big-endian P010 files.
    for (qsizetype offset = 0; offset < plane.byteSize; offset += 2) {
        destination[offset] = source[offset + 1];
        destination[offset + 1] = source[offset];
    }

    QRhiTextureSubresourceUploadDescription upload(std::move(normalized));
    upload.setDataStride(static_cast<quint32>(plane.stride));
    upload.setSourceSize(size);
    return upload;
}

struct EncodedTextureUpload {
    QRhiTexture::Format format = QRhiTexture::RGBA8;
    QRhiTextureSubresourceUploadDescription description;
};

EncodedTextureUpload encodedTextureUpload(const QImage& source) {
    if (source.format() == QImage::Format_RGBA16FPx4) {
        const QByteArray bytes = QByteArray::fromRawData(
            reinterpret_cast<const char*>(source.constBits()), source.sizeInBytes());
        QRhiTextureSubresourceUploadDescription upload(bytes);
        upload.setDataStride(static_cast<quint32>(source.bytesPerLine()));
        upload.setSourceSize(source.size());
        return {QRhiTexture::RGBA16F, std::move(upload)};
    }
    if (source.format() == QImage::Format_RGBA32FPx4) {
        const QByteArray bytes = QByteArray::fromRawData(
            reinterpret_cast<const char*>(source.constBits()), source.sizeInBytes());
        QRhiTextureSubresourceUploadDescription upload(bytes);
        upload.setDataStride(static_cast<quint32>(source.bytesPerLine()));
        upload.setSourceSize(source.size());
        return {QRhiTexture::RGBA32F, std::move(upload)};
    }
    if (source.format() == QImage::Format_RGBA64) {
        const qsizetype pixelStride = qsizetype(sizeof(QRgbaFloat16));
        const qsizetype rowStride = qsizetype(source.width()) * pixelStride;
        QByteArray bytes(rowStride * qsizetype(source.height()), Qt::Uninitialized);
        for (int y = 0; y < source.height(); ++y) {
            const auto* input = reinterpret_cast<const QRgba64*>(source.constScanLine(y));
            auto* output = reinterpret_cast<QRgbaFloat16*>(bytes.data() + qsizetype(y) * rowStride);
            for (int x = 0; x < source.width(); ++x) {
                output[x] = QRgbaFloat16::fromRgba64(input[x].red(), input[x].green(),
                                                     input[x].blue(), input[x].alpha());
            }
        }
        QRhiTextureSubresourceUploadDescription upload(std::move(bytes));
        upload.setDataStride(static_cast<quint32>(rowStride));
        upload.setSourceSize(source.size());
        return {QRhiTexture::RGBA16F, std::move(upload)};
    }
    QImage normalized = source;
    if (normalized.format() != QImage::Format_RGBA8888) {
        normalized = normalized.convertToFormat(QImage::Format_RGBA8888);
    }
    return {QRhiTexture::RGBA8, QRhiTextureSubresourceUploadDescription(std::move(normalized))};
}

} // namespace

ImageCanvas::ImageCanvas(QWidget* parent) : QRhiWidget(parent) {
#if defined(Q_OS_MACOS)
    setApi(QRhiWidget::Api::Metal);
#elif defined(Q_OS_WIN)
    setApi(QRhiWidget::Api::Direct3D11);
#endif
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(120, 90);
    setAutoFillBackground(false);
    roiOverlay_ = new RoiOverlay(this);
    roiOverlay_->setGeometry(rect());
    roiOverlay_->show();
    roiOverlay_->raise();
}

ImageCanvas::~ImageCanvas() = default;

void ImageCanvas::setFrame(ImageFramePtr frame, bool resetView) {
    const bool hadRoi = state_.normalizedRoi.has_value();
    frame_ = std::move(frame);
    image_ = {};
    if (frame_) {
        if (const QImage* image = frame_->qImage()) {
            image_ = *image;
        }
    }
    textureDirty_ = true;
    if (resetView) {
        state_ = {};
        state_.fitMode = FitMode::Fit;
        state_.normalizedCenter = {0.5, 0.5};
    }
    if (navigationThumbnailOverlay_) {
        navigationThumbnailOverlay_->setFrame(frame_);
    }
    roiOverlay_->update();
    if (hadRoi && !state_.normalizedRoi) {
        emit roiChanged({}, false);
    }
    updateNavigationThumbnail();
    update();
}

void ImageCanvas::setComparisonFrame(ImageFramePtr frame) {
    comparisonFrame_ = std::move(frame);
    comparisonImage_ = {};
    if (comparisonFrame_) {
        if (const QImage* image = comparisonFrame_->qImage()) {
            comparisonImage_ = *image;
        }
    }
    textureDirty_ = true;
    update();
}

void ImageCanvas::setCompareMode(ImageCompareMode mode) {
    if (compareMode_ == mode) {
        return;
    }
    const bool changesCompositeResidency =
        (compareMode_ == ImageCompareMode::Single) != (mode == ImageCompareMode::Single);
    const bool rawPrimary = frameSupportsGpuYuv(frame_) || frameSupportsGpuBayer(frame_);
    compareMode_ = mode;
    if (changesCompositeResidency && rawPrimary) {
        textureDirty_ = true;
    }
    if (mode == ImageCompareMode::Single && !roiSelectionEnabled_) {
        unsetCursor();
    }
    update();
}

void ImageCanvas::setCompareAmount(float amount) {
    compareAmount_ = std::clamp(amount, 0.0F, 1.0F);
    update();
}

void ImageCanvas::fitImage() {
    if (!hasDisplayableFrame()) {
        return;
    }
    state_.fitMode = FitMode::Fit;
    state_.normalizedCenter = {0.5, 0.5};
    state_.pixelsPerImagePixel = ViewTransform::fitScale(image_.size(), size());
    notifyStateChanged();
}

void ImageCanvas::actualPixels() {
    if (!hasDisplayableFrame()) {
        return;
    }
    state_.fitMode = FitMode::Manual;
    state_.pixelsPerImagePixel = 1.0;
    notifyStateChanged();
}

void ImageCanvas::setNavigationThumbnailEnabled(bool enabled) {
    navigationThumbnailEnabled_ = enabled;
    if (enabled && !navigationThumbnailOverlay_) {
        navigationThumbnailOverlay_ = new NavigationThumbnailOverlay(this);
        navigationThumbnailOverlay_->setFrame(frame_);
    }
    updateNavigationThumbnail();
}

void ImageCanvas::setRoiSelectionEnabled(bool enabled) {
    roiSelectionEnabled_ = enabled;
    roiSelecting_ = false;
    if (enabled) {
        setCursor(Qt::CrossCursor);
    } else {
        unsetCursor();
    }
}

void ImageCanvas::clearRoi() {
    if (!state_.normalizedRoi) {
        return;
    }
    state_.normalizedRoi.reset();
    roiOverlay_->update();
    emit roiChanged({}, false);
    emit viewStateChanged(effectiveViewState());
}

QRectF ImageCanvas::roiWidgetRect() const {
    if (!state_.normalizedRoi || !hasDisplayableFrame()) {
        return {};
    }
    const QSize imageSize = logicalImageSize();
    const QRectF roi = *state_.normalizedRoi;
    const QPointF topLeft = ViewTransform::imageToWidget(
        {roi.left() * imageSize.width(), roi.top() * imageSize.height()}, size(), imageSize,
        effectiveViewState());
    const QPointF bottomRight = ViewTransform::imageToWidget(
        {roi.right() * imageSize.width(), roi.bottom() * imageSize.height()}, size(), imageSize,
        effectiveViewState());
    return QRectF(topLeft, bottomRight).normalized();
}

void ImageCanvas::setViewState(const ViewState& state, bool notify) {
    const auto previousRoi = state_.normalizedRoi;
    state_ = state;
    state_.normalizedCenter = ViewTransform::clampedCenter(state_.normalizedCenter);
    if (state_.normalizedRoi) {
        state_.normalizedRoi = ViewTransform::clampedNormalizedRoi(*state_.normalizedRoi);
    }
    roiOverlay_->update();
    if (previousRoi != state_.normalizedRoi) {
        emit roiChanged(state_.normalizedRoi.value_or(QRectF{}), state_.normalizedRoi.has_value());
    }
    updateNavigationThumbnail();
    if (notify) {
        notifyStateChanged();
    } else {
        update();
    }
}

ViewState ImageCanvas::viewState() const { return effectiveViewState(); }

void ImageCanvas::initialize(QRhiCommandBuffer* commandBuffer) {
    if (rhi_ != rhi()) {
        resetRhiResources();
        rhi_ = rhi();
        textureDirty_ = true;
    }

    if (!vertexBuffer_) {
        vertexBuffer_.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                            sizeof(kQuadVertices)));
        vertexBuffer_->create();
        uniformBuffer_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
        uniformBuffer_->create();
        compareUniformBuffer_.reset(
            rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
        compareUniformBuffer_->create();
        yuvUniformBuffer_.reset(
            rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 48));
        yuvUniformBuffer_->create();
        comparisonYuvUniformBuffer_.reset(
            rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 48));
        comparisonYuvUniformBuffer_->create();
        bayerUniformBuffer_.reset(
            rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 112));
        bayerUniformBuffer_->create();
        comparisonBayerUniformBuffer_.reset(
            rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 112));
        comparisonBayerUniformBuffer_->create();
        sampler_.reset(rhi_->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        sampler_->create();

        auto* updates = rhi_->nextResourceUpdateBatch();
        updates->uploadStaticBuffer(vertexBuffer_.get(), kQuadVertices.data());
        commandBuffer->resourceUpdate(updates);
    }

    if (textureDirty_ || !texture_) {
        rebuildTexture(commandBuffer);
    }

    if (!pipeline_) {
        pipeline_.reset(rhi_->newGraphicsPipeline());
        pipeline_->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/image.vert.qsb"))},
            {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/image.frag.qsb"))},
        });

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{4 * sizeof(float)}});
        inputLayout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float2, 0},
            {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
        });
        pipeline_->setVertexInputLayout(inputLayout);
        pipeline_->setShaderResourceBindings(bindings_.get());
        pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        pipeline_->create();

        yuvPipeline_.reset(rhi_->newGraphicsPipeline());
        yuvPipeline_->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/image.vert.qsb"))},
            {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/yuv.frag.qsb"))},
        });
        yuvPipeline_->setVertexInputLayout(inputLayout);
        yuvPipeline_->setShaderResourceBindings(yuvBindings_.get());
        yuvPipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        yuvPipeline_->create();

        bayerPipeline_.reset(rhi_->newGraphicsPipeline());
        bayerPipeline_->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/image.vert.qsb"))},
            {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/bayer.frag.qsb"))},
        });
        bayerPipeline_->setVertexInputLayout(inputLayout);
        bayerPipeline_->setShaderResourceBindings(bayerBindings_.get());
        bayerPipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        bayerPipeline_->create();

        yuvComparePipeline_.reset(rhi_->newGraphicsPipeline());
        yuvComparePipeline_->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/image.vert.qsb"))},
            {QRhiShaderStage::Fragment,
             loadShader(QStringLiteral(":/shaders/yuv_compare.frag.qsb"))},
        });
        yuvComparePipeline_->setVertexInputLayout(inputLayout);
        yuvComparePipeline_->setShaderResourceBindings(yuvCompareBindings_.get());
        yuvComparePipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        yuvComparePipeline_->create();

        bayerComparePipeline_.reset(rhi_->newGraphicsPipeline());
        bayerComparePipeline_->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/image.vert.qsb"))},
            {QRhiShaderStage::Fragment,
             loadShader(QStringLiteral(":/shaders/bayer_compare.frag.qsb"))},
        });
        bayerComparePipeline_->setVertexInputLayout(inputLayout);
        bayerComparePipeline_->setShaderResourceBindings(bayerCompareBindings_.get());
        bayerComparePipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        bayerComparePipeline_->create();
    }
}

void ImageCanvas::render(QRhiCommandBuffer* commandBuffer) {
    if (textureDirty_) {
        rebuildTexture(commandBuffer);
    }

    const QSize outputSize = renderTarget()->pixelSize();
    auto* updates = rhi_->nextResourceUpdateBatch();

    QMatrix4x4 matrix = rhi_->clipSpaceCorrMatrix();
    const QSize imageSize = logicalImageSize();
    if (hasDisplayableFrame() && !outputSize.isEmpty()) {
        const ViewState state = effectiveViewState();
        const double scale = state.pixelsPerImagePixel;
        const double imageWidth = imageSize.width() * scale;
        const double imageHeight = imageSize.height() * scale;
        const double centerX = width() * 0.5 + (0.5 - state.normalizedCenter.x()) * imageWidth;
        const double centerY = height() * 0.5 + (0.5 - state.normalizedCenter.y()) * imageHeight;
        const float ndcX = static_cast<float>(2.0 * centerX / width() - 1.0);
        const float ndcY = static_cast<float>(1.0 - 2.0 * centerY / height());
        const float extentX = static_cast<float>(imageWidth / width());
        const float extentY = static_cast<float>(imageHeight / height());
        QMatrix4x4 model;
        model.translate(ndcX, ndcY);
        model.scale(extentX, extentY);
        matrix *= model;
    } else {
        matrix.scale(0.0F);
    }
    updates->updateDynamicBuffer(uniformBuffer_.get(), 0, 64, matrix.constData());
    const std::array<float, 8> compareParameters{
        static_cast<float>(compareMode_), compareAmount_, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    updates->updateDynamicBuffer(compareUniformBuffer_.get(), 0, 32, compareParameters.data());
    const bool comparisonRequested = compareMode_ != ImageCompareMode::Single;
    const bool useYuvComparison = comparisonRequested && gpuYuvReady_ && comparisonGpuYuvReady_;
    const bool useBayerComparison =
        comparisonRequested && gpuBayerReady_ && comparisonGpuBayerReady_;
    const bool useProxyComparison = comparisonRequested && !comparisonImage_.isNull() &&
                                    !useYuvComparison && !useBayerComparison;
    const bool useYuv = gpuYuvReady_ && !useYuvComparison && !useProxyComparison;
    const bool useBayer = gpuBayerReady_ && !useBayerComparison && !useProxyComparison;
    if (useYuv || useYuvComparison) {
        const auto parameters = makeYuvRenderUniformData(*frame_->rawParameters);
        updates->updateDynamicBuffer(yuvUniformBuffer_.get(), 0,
                                     static_cast<quint32>(sizeof(parameters)), parameters.data());
        if (useYuvComparison) {
            const auto candidateParameters =
                makeYuvRenderUniformData(*comparisonFrame_->rawParameters);
            updates->updateDynamicBuffer(comparisonYuvUniformBuffer_.get(), 0,
                                         static_cast<quint32>(sizeof(candidateParameters)),
                                         candidateParameters.data());
        }
    } else if (useBayer || useBayerComparison) {
        const auto parameters = makeBayerRenderUniformData(*frame_->rawParameters);
        updates->updateDynamicBuffer(bayerUniformBuffer_.get(), 0,
                                     static_cast<quint32>(sizeof(parameters)), parameters.data());
        if (useBayerComparison) {
            const auto candidateParameters =
                makeBayerRenderUniformData(*comparisonFrame_->rawParameters);
            updates->updateDynamicBuffer(comparisonBayerUniformBuffer_.get(), 0,
                                         static_cast<quint32>(sizeof(candidateParameters)),
                                         candidateParameters.data());
        }
    }

    commandBuffer->beginPass(renderTarget(), QColor(30, 32, 36), {1.0F, 0}, updates);
    QRhiGraphicsPipeline* activePipeline = pipeline_.get();
    QRhiShaderResourceBindings* activeBindings = bindings_.get();
    if (useYuv) {
        activePipeline = yuvPipeline_.get();
        activeBindings = yuvBindings_.get();
    } else if (useBayer) {
        activePipeline = bayerPipeline_.get();
        activeBindings = bayerBindings_.get();
    } else if (useYuvComparison) {
        activePipeline = yuvComparePipeline_.get();
        activeBindings = yuvCompareBindings_.get();
    } else if (useBayerComparison) {
        activePipeline = bayerComparePipeline_.get();
        activeBindings = bayerCompareBindings_.get();
    }
    commandBuffer->setGraphicsPipeline(activePipeline);
    commandBuffer->setViewport(QRhiViewport(0, 0, static_cast<float>(outputSize.width()),
                                            static_cast<float>(outputSize.height())));
    commandBuffer->setShaderResources(activeBindings);
    const QRhiCommandBuffer::VertexInput binding(vertexBuffer_.get(), 0);
    commandBuffer->setVertexInput(0, 1, &binding);
    commandBuffer->draw(6);
    commandBuffer->endPass();
}

void ImageCanvas::releaseResources() {
    resetRhiResources();
    rhi_ = nullptr;
}

void ImageCanvas::wheelEvent(QWheelEvent* event) {
    if (!hasDisplayableFrame()) {
        event->ignore();
        return;
    }
    const ViewState current = effectiveViewState();
    const double steps = event->angleDelta().y() / 120.0;
    const double newScale = current.pixelsPerImagePixel * std::pow(1.2, steps);
    state_ =
        ViewTransform::zoomAt(current, newScale, event->position(), size(), logicalImageSize());
    notifyStateChanged();
    event->accept();
}

void ImageCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && hasDisplayableFrame()) {
        if (isNearCompareDivider(event->position())) {
            compareDividerDragging_ = true;
            setCursor(compareMode_ == ImageCompareMode::VerticalSplit ? Qt::SplitHCursor
                                                                      : Qt::SplitVCursor);
            event->accept();
            return;
        }
        if (roiSelectionEnabled_) {
            roiSelecting_ = true;
            roiStartWidgetPosition_ = event->position();
            roiStartNormalized_ = normalizedImagePoint(event->position());
            state_.normalizedRoi.reset();
            roiOverlay_->update();
            event->accept();
            return;
        }
        dragging_ = true;
        lastMousePosition_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QRhiWidget::mousePressEvent(event);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (compareDividerDragging_) {
        updateCompareAmount(event->position());
    } else if (roiSelecting_) {
        updateRoiSelection(event->position());
        emit viewStateChanged(effectiveViewState());
    } else if (dragging_) {
        const QPointF delta = event->position() - lastMousePosition_;
        lastMousePosition_ = event->position();
        state_ = ViewTransform::panBy(effectiveViewState(), delta, logicalImageSize());
        notifyStateChanged();
    }
    if (!compareDividerDragging_ && !dragging_ && !roiSelecting_ && !roiSelectionEnabled_) {
        if (isNearCompareDivider(event->position())) {
            setCursor(compareMode_ == ImageCompareMode::VerticalSplit ? Qt::SplitHCursor
                                                                      : Qt::SplitVCursor);
        } else {
            unsetCursor();
        }
    }
    updatePixelProbe(event->position());
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && compareDividerDragging_) {
        updateCompareAmount(event->position());
        compareDividerDragging_ = false;
        if (!roiSelectionEnabled_) {
            unsetCursor();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && roiSelecting_) {
        updateRoiSelection(event->position());
        roiSelecting_ = false;
        const QPointF delta = event->position() - roiStartWidgetPosition_;
        if (std::abs(delta.x()) < 3.0 || std::abs(delta.y()) < 3.0 || !state_.normalizedRoi) {
            state_.normalizedRoi.reset();
            roiOverlay_->update();
            emit roiChanged({}, false);
        } else {
            emit roiChanged(*state_.normalizedRoi, true);
        }
        emit viewStateChanged(effectiveViewState());
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        if (roiSelectionEnabled_) {
            setCursor(Qt::CrossCursor);
        } else {
            unsetCursor();
        }
        event->accept();
        return;
    }
    QRhiWidget::mouseReleaseEvent(event);
}

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (roiSelectionEnabled_) {
            clearRoi();
            event->accept();
            return;
        }
        emit activated();
        event->accept();
        return;
    }
    QRhiWidget::mouseDoubleClickEvent(event);
}

void ImageCanvas::resizeEvent(QResizeEvent* event) {
    QRhiWidget::resizeEvent(event);
    roiOverlay_->setGeometry(rect());
    roiOverlay_->raise();
    updateNavigationThumbnail();
}

void ImageCanvas::resetRhiResources() {
    gpuYuvReady_ = false;
    gpuBayerReady_ = false;
    comparisonGpuYuvReady_ = false;
    comparisonGpuBayerReady_ = false;
    bayerComparePipeline_.reset();
    yuvComparePipeline_.reset();
    bayerPipeline_.reset();
    yuvPipeline_.reset();
    pipeline_.reset();
    yuvBindings_.reset();
    bayerBindings_.reset();
    yuvCompareBindings_.reset();
    bayerCompareBindings_.reset();
    bindings_.reset();
    sampler_.reset();
    vTexture_.reset();
    uTexture_.reset();
    yTexture_.reset();
    rawTexture_.reset();
    comparisonVTexture_.reset();
    comparisonUTexture_.reset();
    comparisonYTexture_.reset();
    comparisonRawTexture_.reset();
    comparisonTexture_.reset();
    texture_.reset();
    yuvUniformBuffer_.reset();
    bayerUniformBuffer_.reset();
    comparisonYuvUniformBuffer_.reset();
    comparisonBayerUniformBuffer_.reset();
    compareUniformBuffer_.reset();
    uniformBuffer_.reset();
    vertexBuffer_.reset();
}

void ImageCanvas::rebuildTexture(QRhiCommandBuffer* commandBuffer) {
    gpuYuvReady_ = false;
    gpuBayerReady_ = false;
    comparisonGpuYuvReady_ = false;
    comparisonGpuBayerReady_ = false;
    comparisonTexture_.reset();
    texture_.reset();
    yTexture_.reset();
    uTexture_.reset();
    vTexture_.reset();
    rawTexture_.reset();
    comparisonYTexture_.reset();
    comparisonUTexture_.reset();
    comparisonVTexture_.reset();
    comparisonRawTexture_.reset();
    auto* updates = rhi_->nextResourceUpdateBatch();

    const auto uploadYuvFrame = [&](const ImageFramePtr& frame,
                                    std::unique_ptr<QRhiTexture>& yTexture,
                                    std::unique_ptr<QRhiTexture>& uTexture,
                                    std::unique_ptr<QRhiTexture>& vTexture) {
        if (!frameSupportsGpuYuv(frame)) {
            return false;
        }
        const auto* planeStorage =
            std::get_if<std::shared_ptr<const PlaneBufferSet>>(&frame->storage);
        const PlaneBufferSet& storage = **planeStorage;
        const RawImageParameters& parameters = *frame->rawParameters;
        const bool highBitDepth = parameters.format == RawPixelFormat::P010;
        const QSize chromaSize((parameters.size.width() + 1) / 2,
                               (parameters.size.height() + 1) / 2);
        yTexture.reset(rhi_->newTexture(highBitDepth ? QRhiTexture::R16 : QRhiTexture::R8,
                                        parameters.size, 1));
        const bool planar = parameters.format == RawPixelFormat::I420;
        uTexture.reset(rhi_->newTexture(
            planar ? QRhiTexture::R8 : (highBitDepth ? QRhiTexture::RG16 : QRhiTexture::RG8),
            chromaSize, 1));
        vTexture.reset(rhi_->newTexture(QRhiTexture::R8, planar ? chromaSize : QSize(1, 1), 1));
        if (!yTexture->create() || !uTexture->create() || !vTexture->create()) {
            yTexture.reset();
            uTexture.reset();
            vTexture.reset();
            return false;
        }
        const auto uploadPlane = [&](int planeIndex, const QSize& size) {
            return highBitDepth
                       ? p010PlaneUpload(storage, planeIndex, size, parameters.littleEndian)
                       : planeUpload(storage, planeIndex, size);
        };
        updates->uploadTexture(yTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                   0, 0, uploadPlane(0, parameters.size))));
        updates->uploadTexture(uTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                   0, 0, uploadPlane(1, chromaSize))));
        if (planar) {
            updates->uploadTexture(vTexture.get(),
                                   QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                       0, 0, planeUpload(storage, 2, chromaSize))));
        }
        return true;
    };

    const auto uploadBayerFrame = [&](const ImageFramePtr& frame,
                                      std::unique_ptr<QRhiTexture>& rawTexture) {
        if (!frameSupportsGpuBayer(frame)) {
            return false;
        }
        const auto* planeStorage =
            std::get_if<std::shared_ptr<const PlaneBufferSet>>(&frame->storage);
        const PlaneBufferSet& storage = **planeStorage;
        const RawImageParameters& parameters = *frame->rawParameters;
        const int stride = static_cast<int>(storage.planes.constFirst().stride);
        const QSize storageSize(stride, parameters.size.height());
        rawTexture.reset(rhi_->newTexture(QRhiTexture::R8, storageSize, 1));
        if (!rawTexture->create()) {
            rawTexture.reset();
            return false;
        }
        updates->uploadTexture(rawTexture.get(),
                               QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                   0, 0, planeUpload(storage, 0, storageSize))));
        return true;
    };

    gpuYuvReady_ = uploadYuvFrame(frame_, yTexture_, uTexture_, vTexture_);
    if (gpuYuvReady_ && compareMode_ != ImageCompareMode::Single) {
        comparisonGpuYuvReady_ = uploadYuvFrame(comparisonFrame_, comparisonYTexture_,
                                                comparisonUTexture_, comparisonVTexture_);
    }
    gpuBayerReady_ = uploadBayerFrame(frame_, rawTexture_);
    if (gpuBayerReady_ && compareMode_ != ImageCompareMode::Single) {
        comparisonGpuBayerReady_ = uploadBayerFrame(comparisonFrame_, comparisonRawTexture_);
    }

    // The encoded pipeline is also the guaranteed fallback when the platform
    // cannot create one of the raw plane texture formats. A failed GPU RAW/YUV
    // setup must never replace the valid CPU reference image with transparency.
    QImage encodedImage = image_;
    const bool compositeRequested =
        compareMode_ != ImageCompareMode::Single && !comparisonImage_.isNull();
    if (encodedImage.isNull() || ((gpuYuvReady_ || gpuBayerReady_) && !compositeRequested)) {
        encodedImage = QImage(1, 1, QImage::Format_RGBA8888);
        encodedImage.fill(Qt::transparent);
    }
    auto primaryUpload = encodedTextureUpload(encodedImage);
    texture_.reset(rhi_->newTexture(primaryUpload.format, encodedImage.size(), 1));
    texture_->create();
    updates->uploadTexture(texture_.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                               0, 0, primaryUpload.description)));

    QImage secondary = comparisonImage_;
    if (secondary.isNull()) {
        secondary = QImage(1, 1, QImage::Format_RGBA8888);
        secondary.fill(Qt::transparent);
    }
    auto comparisonUpload = encodedTextureUpload(secondary);
    comparisonTexture_.reset(rhi_->newTexture(comparisonUpload.format, secondary.size(), 1));
    comparisonTexture_->create();
    updates->uploadTexture(
        comparisonTexture_.get(),
        QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, comparisonUpload.description)));

    if (!gpuYuvReady_) {
        yTexture_.reset();
        uTexture_.reset();
        vTexture_.reset();
        yTexture_.reset(rhi_->newTexture(QRhiTexture::R8, {1, 1}, 1));
        uTexture_.reset(rhi_->newTexture(QRhiTexture::RG8, {1, 1}, 1));
        vTexture_.reset(rhi_->newTexture(QRhiTexture::R8, {1, 1}, 1));
        yTexture_->create();
        uTexture_->create();
        vTexture_->create();
    }
    if (!gpuBayerReady_) {
        rawTexture_.reset(rhi_->newTexture(QRhiTexture::R8, {1, 1}, 1));
        rawTexture_->create();
    }
    if (!comparisonGpuYuvReady_) {
        comparisonYTexture_.reset(rhi_->newTexture(QRhiTexture::R8, {1, 1}, 1));
        comparisonUTexture_.reset(rhi_->newTexture(QRhiTexture::RG8, {1, 1}, 1));
        comparisonVTexture_.reset(rhi_->newTexture(QRhiTexture::R8, {1, 1}, 1));
        comparisonYTexture_->create();
        comparisonUTexture_->create();
        comparisonVTexture_->create();
    }
    if (!comparisonGpuBayerReady_) {
        comparisonRawTexture_.reset(rhi_->newTexture(QRhiTexture::R8, {1, 1}, 1));
        comparisonRawTexture_->create();
    }
    commandBuffer->resourceUpdate(updates);
    rebuildBindings();
    textureDirty_ = false;
}

void ImageCanvas::rebuildBindings() {
    bindings_.reset(rhi_->newShaderResourceBindings());
    bindings_->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 uniformBuffer_.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  texture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  comparisonTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::FragmentStage,
                                                 compareUniformBuffer_.get()),
    });
    bindings_->create();

    yuvBindings_.reset(rhi_->newShaderResourceBindings());
    yuvBindings_->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 uniformBuffer_.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  yTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  uTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage,
                                                  vTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage,
                                                 yuvUniformBuffer_.get()),
    });
    yuvBindings_->create();

    yuvCompareBindings_.reset(rhi_->newShaderResourceBindings());
    yuvCompareBindings_->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 uniformBuffer_.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  yTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  uTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage,
                                                  vTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::sampledTexture(4, QRhiShaderResourceBinding::FragmentStage,
                                                  comparisonYTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::sampledTexture(5, QRhiShaderResourceBinding::FragmentStage,
                                                  comparisonUTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::sampledTexture(6, QRhiShaderResourceBinding::FragmentStage,
                                                  comparisonVTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::uniformBuffer(7, QRhiShaderResourceBinding::FragmentStage,
                                                 yuvUniformBuffer_.get()),
        QRhiShaderResourceBinding::uniformBuffer(8, QRhiShaderResourceBinding::FragmentStage,
                                                 comparisonYuvUniformBuffer_.get()),
        QRhiShaderResourceBinding::uniformBuffer(9, QRhiShaderResourceBinding::FragmentStage,
                                                 compareUniformBuffer_.get()),
    });
    yuvCompareBindings_->create();

    bayerBindings_.reset(rhi_->newShaderResourceBindings());
    bayerBindings_->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 uniformBuffer_.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  rawTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage,
                                                 bayerUniformBuffer_.get()),
    });
    bayerBindings_->create();

    bayerCompareBindings_.reset(rhi_->newShaderResourceBindings());
    bayerCompareBindings_->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 uniformBuffer_.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  rawTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  comparisonRawTexture_.get(), sampler_.get()),
        QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::FragmentStage,
                                                 bayerUniformBuffer_.get()),
        QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage,
                                                 comparisonBayerUniformBuffer_.get()),
        QRhiShaderResourceBinding::uniformBuffer(5, QRhiShaderResourceBinding::FragmentStage,
                                                 compareUniformBuffer_.get()),
    });
    bayerCompareBindings_->create();
}

bool ImageCanvas::hasGpuYuvFrame() const { return frameSupportsGpuYuv(frame_); }

bool ImageCanvas::frameSupportsGpuYuv(const ImageFramePtr& frame) const {
    if (!frame || !frame->rawParameters || !frame->rawParameters->isYuv()) {
        return false;
    }
    const auto* storage = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&frame->storage);
    if (!storage || !*storage) {
        return false;
    }
    const bool planar = frame->rawParameters->format == RawPixelFormat::I420;
    const int requiredPlanes = planar ? 3 : 2;
    if ((*storage)->planes.size() < requiredPlanes) {
        return false;
    }
    if (frame->rawParameters->format == RawPixelFormat::P010) {
        for (int index = 0; index < requiredPlanes; ++index) {
            const PlaneBuffer& plane = (*storage)->planes.at(index);
            if (plane.stride <= 0 || (plane.stride & 1) != 0 || (plane.byteSize & 1) != 0) {
                return false;
            }
        }
    }
    return true;
}

bool ImageCanvas::hasGpuBayerFrame() const { return frameSupportsGpuBayer(frame_); }

bool ImageCanvas::frameSupportsGpuBayer(const ImageFramePtr& frame) const {
    if (!frame || !frame->rawParameters || frame->rawParameters->isYuv()) {
        return false;
    }
    const auto* storage = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&frame->storage);
    if (!storage || !*storage || (*storage)->planes.isEmpty()) {
        return false;
    }
    const PlaneBuffer& plane = (*storage)->planes.constFirst();
    return plane.stride > 0 && plane.stride <= std::numeric_limits<int>::max() &&
           plane.byteSize >= plane.stride * frame->rawParameters->size.height();
}

QSize ImageCanvas::logicalImageSize() const {
    if (frame_ && frame_->rawParameters) {
        return orientedImageSize(frame_->rawParameters->size, frame_->rawParameters->orientation);
    }
    if (frame_ && !frame_->descriptor.size.isEmpty()) {
        return frame_->descriptor.size;
    }
    return image_.size();
}

bool ImageCanvas::hasDisplayableFrame() const {
    return !logicalImageSize().isEmpty() &&
           (!image_.isNull() || hasGpuYuvFrame() || hasGpuBayerFrame());
}

ViewState ImageCanvas::effectiveViewState() const {
    ViewState result = state_;
    if (result.fitMode == FitMode::Fit && hasDisplayableFrame()) {
        result.pixelsPerImagePixel = ViewTransform::fitScale(logicalImageSize(), size());
    }
    return result;
}

void ImageCanvas::notifyStateChanged() {
    roiOverlay_->update();
    updateNavigationThumbnail();
    update();
    emit viewStateChanged(effectiveViewState());
}

void ImageCanvas::updateNavigationThumbnail() {
    if (!navigationThumbnailOverlay_) {
        return;
    }

    const ViewState state = effectiveViewState();
    const QSize imageSize = logicalImageSize();
    const bool visible =
        navigationThumbnailEnabled_ && hasDisplayableFrame() && state.fitMode != FitMode::Fit;
    if (!visible) {
        navigationThumbnailOverlay_->hide();
        return;
    }

    navigationThumbnailOverlay_->setView(state, size(), imageSize);
    navigationThumbnailOverlay_->layoutWithin(rect(), imageSize);
    navigationThumbnailOverlay_->show();
    navigationThumbnailOverlay_->raise();
}

QPointF ImageCanvas::normalizedImagePoint(const QPointF& widgetPosition) const {
    const QSize imageSize = logicalImageSize();
    if (imageSize.isEmpty()) {
        return {};
    }
    const QPointF imagePosition =
        ViewTransform::widgetToImage(widgetPosition, size(), imageSize, effectiveViewState());
    return {std::clamp(imagePosition.x() / imageSize.width(), 0.0, 1.0),
            std::clamp(imagePosition.y() / imageSize.height(), 0.0, 1.0)};
}

bool ImageCanvas::isNearCompareDivider(const QPointF& widgetPosition) const {
    if (compareMode_ == ImageCompareMode::Single || logicalImageSize().isEmpty()) {
        return false;
    }
    constexpr qreal hitRadius = 7.0;
    const QSize imageSize = logicalImageSize();
    const QPointF seamImagePoint =
        compareMode_ == ImageCompareMode::VerticalSplit
            ? QPointF(compareAmount_ * imageSize.width(), imageSize.height() * 0.5)
            : QPointF(imageSize.width() * 0.5, compareAmount_ * imageSize.height());
    const QPointF seamWidgetPoint =
        ViewTransform::imageToWidget(seamImagePoint, size(), imageSize, effectiveViewState());
    return compareMode_ == ImageCompareMode::VerticalSplit
               ? std::abs(widgetPosition.x() - seamWidgetPoint.x()) <= hitRadius
               : std::abs(widgetPosition.y() - seamWidgetPoint.y()) <= hitRadius;
}

void ImageCanvas::updateCompareAmount(const QPointF& widgetPosition) {
    const QPointF normalized = normalizedImagePoint(widgetPosition);
    setCompareAmount(static_cast<float>(
        compareMode_ == ImageCompareMode::VerticalSplit ? normalized.x() : normalized.y()));
}

void ImageCanvas::updateRoiSelection(const QPointF& widgetPosition) {
    const QPointF current = normalizedImagePoint(widgetPosition);
    state_.normalizedRoi =
        ViewTransform::clampedNormalizedRoi(QRectF(roiStartNormalized_, current).normalized());
    roiOverlay_->update();
}

void ImageCanvas::updatePixelProbe(const QPointF& widgetPosition) {
    if (!hasDisplayableFrame()) {
        emit pixelHovered({}, {}, false);
        return;
    }
    const QSize imageSize = logicalImageSize();
    const QPointF imagePosition =
        ViewTransform::widgetToImage(widgetPosition, size(), imageSize, effectiveViewState());
    const QPoint pixel(static_cast<int>(std::floor(imagePosition.x())),
                       static_cast<int>(std::floor(imagePosition.y())));
    if (!QRect(QPoint{}, imageSize).contains(pixel)) {
        emit pixelHovered(pixel, {}, false);
        return;
    }
    QColor color;
    if (!image_.isNull()) {
        const int sampleX =
            std::clamp(static_cast<int>((pixel.x() + 0.5) * image_.width() / imageSize.width()), 0,
                       image_.width() - 1);
        const int sampleY =
            std::clamp(static_cast<int>((pixel.y() + 0.5) * image_.height() / imageSize.height()),
                       0, image_.height() - 1);
        color = image_.pixelColor(sampleX, sampleY);
    }
    emit pixelHovered(pixel, color, true);
}

} // namespace ispview
