#include "ui/histogram_panel.h"

#include "core/view_state.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

namespace ispview {

template <std::size_t Size> qsizetype histogramBinCount(const std::array<quint64, Size>&) {
    return static_cast<qsizetype>(Size);
}

qsizetype histogramBinCount(const QVector<quint64>& bins) { return bins.size(); }

template <std::size_t Size>
quint64 histogramBinValue(const std::array<quint64, Size>& bins, qsizetype index) {
    return bins.at(static_cast<std::size_t>(index));
}

quint64 histogramBinValue(const QVector<quint64>& bins, qsizetype index) { return bins.at(index); }

template <typename Bins> double histogramMedian(const Bins& bins, quint64 samples) {
    if (samples == 0) {
        return 0.0;
    }
    const quint64 firstTarget = (samples - 1) / 2;
    const quint64 secondTarget = samples / 2;
    quint64 cumulative = 0;
    int first = -1;
    int second = -1;
    for (qsizetype index = 0; index < histogramBinCount(bins); ++index) {
        cumulative += histogramBinValue(bins, index);
        if (first < 0 && cumulative > firstTarget) {
            first = static_cast<int>(index);
        }
        if (cumulative > secondTarget) {
            second = static_cast<int>(index);
            break;
        }
    }
    return (std::max(0, first) + std::max(0, second)) / 2.0;
}

bool rawChannelMatches(HistogramChannelMode mode, RawHistogramChannelId id) {
    if (mode == HistogramChannelMode::All) {
        return true;
    }
    switch (id) {
    case RawHistogramChannelId::Y:
        return mode == HistogramChannelMode::Y || mode == HistogramChannelMode::Luma;
    case RawHistogramChannelId::U:
        return mode == HistogramChannelMode::U;
    case RawHistogramChannelId::V:
        return mode == HistogramChannelMode::V;
    case RawHistogramChannelId::Red:
        return mode == HistogramChannelMode::Red;
    case RawHistogramChannelId::GreenRedRow:
        return mode == HistogramChannelMode::GreenRedRow;
    case RawHistogramChannelId::GreenBlueRow:
        return mode == HistogramChannelMode::GreenBlueRow;
    case RawHistogramChannelId::Blue:
        return mode == HistogramChannelMode::Blue;
    }
    return false;
}

class HistogramPlot final : public QWidget {
  public:
    explicit HistogramPlot(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("histogramPlot"));
        setMinimumSize(240, 190);
        setMouseTracking(true);
        setFocusPolicy(Qt::WheelFocus);
    }

    void setDisplayHistogram(std::optional<DisplayHistogram> histogram) {
        displayHistogram_ = std::move(histogram);
        rawHistogram_.reset();
        maximumBinValue_ = 255;
        update();
    }

    void setRawHistogram(std::optional<RawPlaneHistogram> histogram) {
        rawHistogram_ = std::move(histogram);
        displayHistogram_.reset();
        maximumBinValue_ = rawHistogram_ ? rawHistogram_->maximumValue : 0;
        update();
    }

    void setChannelMode(HistogramChannelMode mode) {
        channelMode_ = mode;
        update();
    }

    void setOverlayMode(bool enabled) {
        overlayMode_ = enabled;
        setAttribute(Qt::WA_TranslucentBackground, enabled);
        setAttribute(Qt::WA_NoSystemBackground, enabled);
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (!overlayMode_) {
            painter.fillRect(rect(), Qt::white);
        }
        const QRectF graph = graphRect();
        painter.setPen(overlayMode_ ? QColor(255, 255, 255, 95) : QColor(225, 225, 225));
        for (int division = 0; division <= 4; ++division) {
            const qreal y = graph.top() + graph.height() * division / 4.0;
            painter.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
        }
        painter.setPen(overlayMode_ ? QColor(Qt::white) : QColor(70, 70, 70));
        for (int division = 0; division <= 4; ++division) {
            const int value = maximumBinValue_ * division / 4;
            const qreal x = graph.left() + graph.width() * division / 4.0;
            const QRectF label =
                division == 0   ? QRectF(graph.left(), graph.bottom() + 4.0, 68.0, 16.0)
                : division == 4 ? QRectF(graph.right() - 68.0, graph.bottom() + 4.0, 68.0, 16.0)
                                : QRectF(x - 34.0, graph.bottom() + 4.0, 68.0, 16.0);
            painter.drawText(label,
                             division == 0   ? Qt::AlignLeft
                             : division == 4 ? Qt::AlignRight
                                             : Qt::AlignHCenter,
                             QString::number(value));
        }
        if ((!displayHistogram_ || !displayHistogram_->isValid()) &&
            (!rawHistogram_ || !rawHistogram_->isValid())) {
            painter.drawText(graph, Qt::AlignCenter, QStringLiteral("No histogram"));
            return;
        }

        quint64 maximum = 0;
        if (displayHistogram_) {
            QVector<const HistogramChannel*> channels;
            if (channelMode_ == HistogramChannelMode::All ||
                channelMode_ == HistogramChannelMode::Red) {
                channels.append(&displayHistogram_->red);
            }
            if (channelMode_ == HistogramChannelMode::All ||
                channelMode_ == HistogramChannelMode::Green) {
                channels.append(&displayHistogram_->green);
            }
            if (channelMode_ == HistogramChannelMode::All ||
                channelMode_ == HistogramChannelMode::Blue) {
                channels.append(&displayHistogram_->blue);
            }
            if (channelMode_ == HistogramChannelMode::All ||
                channelMode_ == HistogramChannelMode::Luma) {
                channels.append(&displayHistogram_->luma);
            }
            for (const HistogramChannel* channel : channels) {
                maximum = std::max(maximum,
                                   *std::max_element(channel->bins.cbegin(), channel->bins.cend()));
            }
        } else {
            for (const RawHistogramChannel& channel : rawHistogram_->channels) {
                if (rawChannelMatches(channelMode_, channel.id) && !channel.bins.isEmpty()) {
                    maximum = std::max(
                        maximum, *std::max_element(channel.bins.cbegin(), channel.bins.cend()));
                }
            }
        }
        if (maximum == 0) {
            return;
        }
        const double logMaximum = std::log1p(static_cast<double>(maximum));
        const auto drawBins = [&](const auto& bins, const QColor& color) {
            const qsizetype binCount = histogramBinCount(bins);
            if (binCount < 2) {
                return;
            }
            const qsizetype pointCount =
                std::min(binCount, static_cast<qsizetype>(std::max(2.0, graph.width())));
            QPainterPath path;
            for (qsizetype point = 0; point < pointCount; ++point) {
                const qsizetype firstBin = point * binCount / pointCount;
                const qsizetype endBin = (point + 1) * binCount / pointCount;
                quint64 peak = 0;
                for (qsizetype bin = firstBin; bin < endBin; ++bin) {
                    peak = std::max(peak, histogramBinValue(bins, bin));
                }
                const qreal x = graph.left() + graph.width() * point / (pointCount - 1);
                const double normalized =
                    std::min(1.0, std::log1p(static_cast<double>(peak)) / logMaximum);
                const qreal y = graph.bottom() - graph.height() * normalized;
                if (point == 0) {
                    path.moveTo(x, y);
                } else {
                    path.lineTo(x, y);
                }
            }
            path.lineTo(graph.right(), graph.bottom());
            path.lineTo(graph.left(), graph.bottom());
            path.closeSubpath();
            painter.setPen(QPen(color == QColor(Qt::white) ? QColor(115, 115, 115) : color, 1.0));
            painter.setBrush(color);
            painter.drawPath(path);
        };
        if (displayHistogram_) {
            if (channelMode_ == HistogramChannelMode::Luma) {
                drawBins(displayHistogram_->luma.bins,
                         overlayMode_ ? QColor(255, 255, 255, 205) : QColor(25, 25, 25));
            }
            if (channelMode_ == HistogramChannelMode::Red) {
                drawBins(displayHistogram_->red.bins, QColor(225, 45, 45));
            }
            if (channelMode_ == HistogramChannelMode::Green) {
                drawBins(displayHistogram_->green.bins, QColor(35, 175, 70));
            }
            if (channelMode_ == HistogramChannelMode::Blue) {
                drawBins(displayHistogram_->blue.bins, QColor(0, 0, 255));
            }
        } else {
            const auto channelColor = [](RawHistogramChannelId id) {
                switch (id) {
                case RawHistogramChannelId::Y:
                    return QColor(25, 25, 25);
                case RawHistogramChannelId::U:
                    return QColor(70, 220, 230, 220);
                case RawHistogramChannelId::V:
                    return QColor(230, 90, 210, 220);
                case RawHistogramChannelId::Red:
                    return QColor(225, 45, 45);
                case RawHistogramChannelId::GreenRedRow:
                    return QColor(35, 175, 70);
                case RawHistogramChannelId::GreenBlueRow:
                    return QColor(30, 135, 55);
                case RawHistogramChannelId::Blue:
                    return QColor(0, 0, 255);
                }
                return QColor(Qt::white);
            };
            for (const RawHistogramChannel& channel : rawHistogram_->channels) {
                if (rawChannelMatches(channelMode_, channel.id)) {
                    drawBins(channel.bins, channelColor(channel.id));
                }
            }
        }
        if (graph.contains(hoverPosition_)) {
            painter.setPen(QPen(QColor(80, 80, 80, 150), 1.0, Qt::DashLine));
            painter.drawLine(QPointF(hoverPosition_.x(), graph.top()),
                             QPointF(hoverPosition_.x(), graph.bottom()));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        hoverPosition_ = event->position();
        const QRectF graph = graphRect();
        if (!graph.contains(hoverPosition_) || maximumBinValue_ <= 0) {
            QToolTip::hideText();
            update();
            return;
        }
        const int value =
            std::clamp(static_cast<int>(std::round((hoverPosition_.x() - graph.left()) /
                                                   graph.width() * maximumBinValue_)),
                       0, maximumBinValue_);
        QStringList counts;
        const auto appendCount = [&counts, value, this](const QString& name, const auto& bins,
                                                        bool visible) {
            if (!visible || histogramBinCount(bins) <= 0) {
                return;
            }
            const qsizetype index = std::clamp<qsizetype>(
                static_cast<qsizetype>(
                    std::llround(static_cast<double>(value) * (histogramBinCount(bins) - 1) /
                                 std::max(1, maximumBinValue_))),
                0, histogramBinCount(bins) - 1);
            counts << QStringLiteral("%1: %2").arg(
                name, QLocale().toString(histogramBinValue(bins, index)));
        };
        if (displayHistogram_) {
            appendCount(QStringLiteral("Luma"), displayHistogram_->luma.bins,
                        channelMode_ == HistogramChannelMode::All ||
                            channelMode_ == HistogramChannelMode::Luma);
            appendCount(QStringLiteral("R"), displayHistogram_->red.bins,
                        channelMode_ == HistogramChannelMode::All ||
                            channelMode_ == HistogramChannelMode::Red);
            appendCount(QStringLiteral("G"), displayHistogram_->green.bins,
                        channelMode_ == HistogramChannelMode::All ||
                            channelMode_ == HistogramChannelMode::Green);
            appendCount(QStringLiteral("B"), displayHistogram_->blue.bins,
                        channelMode_ == HistogramChannelMode::All ||
                            channelMode_ == HistogramChannelMode::Blue);
        } else if (rawHistogram_) {
            for (const RawHistogramChannel& channel : rawHistogram_->channels) {
                appendCount(rawHistogramChannelName(channel.id), channel.bins,
                            rawChannelMatches(channelMode_, channel.id));
            }
        }
        QToolTip::showText(event->globalPosition().toPoint(),
                           QStringLiteral("Value: %1\n%2").arg(value).arg(counts.join('\n')), this);
        update();
    }

    void leaveEvent(QEvent* event) override {
        hoverPosition_ = {-1.0, -1.0};
        QToolTip::hideText();
        update();
        QWidget::leaveEvent(event);
    }

  private:
    [[nodiscard]] QRectF graphRect() const {
        return QRectF(rect()).adjusted(10.0, 10.0, -10.0, -28.0);
    }

    std::optional<DisplayHistogram> displayHistogram_;
    std::optional<RawPlaneHistogram> rawHistogram_;
    int maximumBinValue_ = 255;
    HistogramChannelMode channelMode_ = HistogramChannelMode::Luma;
    bool overlayMode_ = false;
    QPointF hoverPosition_{-1.0, -1.0};
};

HistogramPanel::HistogramPanel(QWidget* parent)
    : QWidget(parent), plot_(new HistogramPlot(this)), sourceCombo_(new QComboBox(this)),
      channelCombo_(new QComboBox(this)), summary_(new QLabel(this)),
      statistics_(new QTableWidget(this)), debounce_(new QTimer(this)) {
    setObjectName(QStringLiteral("histogramPanel"));
    pool_.setMaxThreadCount(1);
    pool_.setExpiryTimeout(5'000);
    summary_->setObjectName(QStringLiteral("histogramSummary"));
    summary_->setWordWrap(true);
    summary_->setText(QStringLiteral("No image"));
    sourceCombo_->setObjectName(QStringLiteral("histogramSourceCombo"));
    channelCombo_->setObjectName(QStringLiteral("histogramChannelCombo"));
    sourceCombo_->addItem(QStringLiteral("Display"), static_cast<int>(HistogramSource::Display));
    sourceCombo_->addItem(QStringLiteral("Source RAW/YUV planes"),
                          static_cast<int>(HistogramSource::SourcePlanes));
    populateDisplayChannels();
    statistics_->setObjectName(QStringLiteral("histogramStatistics"));
    statistics_->setColumnCount(6);
    statistics_->setHorizontalHeaderLabels({QStringLiteral("Channel"), QStringLiteral("Mean"),
                                            QStringLiteral("Variance"), QStringLiteral("Min"),
                                            QStringLiteral("Max"), QStringLiteral("Median")});
    statistics_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    statistics_->horizontalHeader()->setStretchLastSection(false);
    for (int column = 0; column < statistics_->columnCount(); ++column) {
        QTableWidgetItem* header = statistics_->horizontalHeaderItem(column);
        QFont font = header->font();
        font.setBold(true);
        header->setFont(font);
        header->setTextAlignment(Qt::AlignCenter);
    }
    statistics_->verticalHeader()->hide();
    statistics_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    statistics_->setSelectionMode(QAbstractItemView::NoSelection);
    statistics_->setAlternatingRowColors(true);
    statistics_->setMaximumHeight(170);
    debounce_->setSingleShot(true);
    debounce_->setInterval(200);
    connect(debounce_, &QTimer::timeout, this, &HistogramPanel::startPendingAnalysis);
    connect(sourceCombo_, &QComboBox::currentIndexChanged, this, [this] { scheduleAnalysis(); });
    connect(channelCombo_, &QComboBox::currentIndexChanged, this, [this] {
        plot_->setChannelMode(
            static_cast<HistogramChannelMode>(channelCombo_->currentData().toInt()));
    });
    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->addWidget(sourceCombo_, 1);
    controls->addWidget(channelCombo_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(controls);
    layout->addWidget(plot_, 1);
    summary_->hide();
    layout->addWidget(statistics_);
}

HistogramPanel::~HistogramPanel() {
    ++generation_;
    debounce_->stop();
    pendingFrame_.reset();
    currentFrame_.reset();
    pool_.clear();
    pool_.waitForDone();
}

void HistogramPanel::setFrame(ImageFramePtr frame) {
    currentFrame_ = std::move(frame);
    scheduleAnalysis();
}

void HistogramPanel::setNormalizedRegion(std::optional<QRectF> normalizedRegion) {
    if (normalizedRegion) {
        normalizedRegion = ViewTransform::clampedNormalizedRoi(*normalizedRegion);
    }
    if (normalizedRegion_ == normalizedRegion) {
        return;
    }
    normalizedRegion_ = std::move(normalizedRegion);
    scheduleAnalysis();
}

void HistogramPanel::setSource(HistogramSource source) {
    const int index = sourceCombo_->findData(static_cast<int>(source));
    if (index >= 0) {
        sourceCombo_->setCurrentIndex(index);
    }
}

HistogramSource HistogramPanel::source() const {
    return static_cast<HistogramSource>(sourceCombo_->currentData().toInt());
}

void HistogramPanel::setCompactLumaOnly(bool enabled) {
    if (enabled) {
        setSource(HistogramSource::Display);
        populateDisplayChannels();
        channelCombo_->setCurrentIndex(
            channelCombo_->findData(static_cast<int>(HistogramChannelMode::Luma)));
        plot_->setChannelMode(HistogramChannelMode::Luma);
    }
    sourceCombo_->setVisible(!enabled);
    channelCombo_->setVisible(!enabled);
    statistics_->setVisible(!enabled);
    plot_->setOverlayMode(enabled);
    plot_->setMinimumSize(enabled ? QSize(160, 80) : QSize(240, 190));
    layout()->setContentsMargins(enabled ? QMargins(0, 0, 0, 0) : QMargins(6, 6, 6, 6));
    setAttribute(Qt::WA_TranslucentBackground, enabled);
    setAttribute(Qt::WA_NoSystemBackground, enabled);
    setStyleSheet(enabled ? QStringLiteral("background: transparent;") : QString{});
}

void HistogramPanel::scheduleAnalysis() {
    ++generation_;
    debounce_->stop();
    pool_.clear();
    pendingFrame_.reset();
    histogram_.reset();
    rawHistogram_.reset();
    statistics_->setRowCount(0);
    if (source() == HistogramSource::Display) {
        plot_->setDisplayHistogram(std::nullopt);
    } else {
        plot_->setRawHistogram(std::nullopt);
    }
    if (!currentFrame_) {
        summary_->setText(QStringLiteral("No image"));
        return;
    }
    if (source() == HistogramSource::Display && !currentFrame_->qImage()) {
        summary_->setText(QStringLiteral("Display histogram unavailable"));
        return;
    }
    if (source() == HistogramSource::SourcePlanes && !RawPlaneAccessor(*currentFrame_).isValid()) {
        summary_->setText(
            QStringLiteral("Source planes unavailable; waiting for a Full RAW/YUV frame"));
        return;
    }
    pendingFrame_ = currentFrame_;
    summary_->setText(source() == HistogramSource::Display
                          ? QStringLiteral("Waiting to analyze display histogram…")
                          : QStringLiteral("Waiting to analyze source planes…"));
    debounce_->start();
}

void HistogramPanel::startPendingAnalysis() {
    if (!pendingFrame_) {
        return;
    }
    const quint64 generation = generation_;
    ImageFramePtr frame = std::move(pendingFrame_);
    const std::optional<QRectF> normalizedRegion = normalizedRegion_;
    const HistogramSource requestedSource = source();
    summary_->setText(requestedSource == HistogramSource::Display
                          ? QStringLiteral("Calculating display histogram…")
                          : QStringLiteral("Calculating source-plane histogram…"));
    const QPointer<HistogramPanel> self(this);
    pool_.start([self, frame = std::move(frame), normalizedRegion, requestedSource, generation] {
        if (requestedSource == HistogramSource::Display) {
            DisplayHistogram histogram =
                normalizedRegion
                    ? DisplayHistogramAnalyzer::analyzeRegion(*frame, *normalizedRegion)
                    : DisplayHistogramAnalyzer::analyze(*frame);
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self.data(),
                [self, generation, histogram = std::move(histogram)]() mutable {
                    if (self) {
                        self->applyDisplayHistogram(generation, std::move(histogram));
                    }
                },
                Qt::QueuedConnection);
            return;
        }
        RawPlaneHistogram histogram =
            normalizedRegion ? RawPlaneHistogramAnalyzer::analyzeRegion(*frame, *normalizedRegion)
                             : RawPlaneHistogramAnalyzer::analyze(*frame);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, generation, histogram = std::move(histogram)]() mutable {
                if (self) {
                    self->applyRawHistogram(generation, std::move(histogram));
                }
            },
            Qt::QueuedConnection);
    });
}

void HistogramPanel::applyDisplayHistogram(quint64 generation, DisplayHistogram histogram) {
    if (generation != generation_) {
        return;
    }
    if (!histogram.isValid()) {
        histogram_.reset();
        plot_->setDisplayHistogram(std::nullopt);
        summary_->setText(QStringLiteral("Histogram unavailable"));
        return;
    }
    histogram_ = std::move(histogram);
    rawHistogram_.reset();
    plot_->setDisplayHistogram(histogram_);
    populateDisplayChannels();
    updateDisplayStatistics(*histogram_);
    const DisplayHistogram& value = *histogram_;
    QString geometry =
        QStringLiteral("%1×%2").arg(value.analyzedSize.width()).arg(value.analyzedSize.height());
    if (value.usesDisplayProxy()) {
        geometry += QStringLiteral(" proxy for %1×%2")
                        .arg(value.logicalSize.width())
                        .arg(value.logicalSize.height());
    }
    QString samples = QLocale().toString(value.sampledPixelCount) + QStringLiteral(" samples");
    if (value.isSubsampled()) {
        samples += QStringLiteral(" of %1").arg(QLocale().toString(value.availablePixelCount));
    }
    QString scope = QStringLiteral("Full display");
    if (value.isRegionLimited()) {
        scope = QStringLiteral("ROI x:%1 y:%2  %3×%4")
                    .arg(value.logicalRegion.x())
                    .arg(value.logicalRegion.y())
                    .arg(value.logicalRegion.width())
                    .arg(value.logicalRegion.height());
    }
    summary_->setText(QStringLiteral("%1 · %2 · %3").arg(scope, geometry, samples));
}

void HistogramPanel::applyRawHistogram(quint64 generation, RawPlaneHistogram histogram) {
    if (generation != generation_) {
        return;
    }
    if (!histogram.isValid()) {
        rawHistogram_.reset();
        plot_->setRawHistogram(std::nullopt);
        summary_->setText(QStringLiteral("Source-plane histogram unavailable"));
        return;
    }
    rawHistogram_ = std::move(histogram);
    histogram_.reset();
    plot_->setRawHistogram(rawHistogram_);
    populateRawChannels(*rawHistogram_);
    updateRawStatistics(*rawHistogram_);
    const RawPlaneHistogram& value = *rawHistogram_;
    QString scope = QStringLiteral("Full source");
    if (value.isRegionLimited()) {
        scope = QStringLiteral("ROI x:%1 y:%2  %3×%4")
                    .arg(value.logicalRegion.x())
                    .arg(value.logicalRegion.y())
                    .arg(value.logicalRegion.width())
                    .arg(value.logicalRegion.height());
    }
    QString text = QStringLiteral("Source %1 · %2-bit · %3\nSource region x:%4 y:%5  %6×%7")
                       .arg(value.domain == RawHistogramDomain::Yuv ? QStringLiteral("YUV")
                                                                    : QStringLiteral("Bayer"))
                       .arg(value.validBits)
                       .arg(scope)
                       .arg(value.sourceRegion.x())
                       .arg(value.sourceRegion.y())
                       .arg(value.sourceRegion.width())
                       .arg(value.sourceRegion.height());
    for (const RawHistogramChannel& channel : value.channels) {
        if (!channel.isValid()) {
            continue;
        }
        QString samples = QLocale().toString(channel.sampledSampleCount);
        if (channel.isSubsampled()) {
            samples += QStringLiteral("/%1").arg(QLocale().toString(channel.availableSampleCount));
        }
        text += QStringLiteral("\n%1: %2 samples · Mean %3 · StdDev %4 · Range %5–%6")
                    .arg(rawHistogramChannelName(channel.id), samples)
                    .arg(channel.mean, 0, 'f', 1)
                    .arg(channel.standardDeviation, 0, 'f', 1)
                    .arg(channel.minimum)
                    .arg(channel.maximum);
    }
    summary_->setText(text);
}

void HistogramPanel::populateDisplayChannels() {
    const QSignalBlocker blocker(channelCombo_);
    channelCombo_->clear();
    channelCombo_->addItem(QStringLiteral("Luma"), static_cast<int>(HistogramChannelMode::Luma));
    channelCombo_->addItem(QStringLiteral("Red"), static_cast<int>(HistogramChannelMode::Red));
    channelCombo_->addItem(QStringLiteral("Green"), static_cast<int>(HistogramChannelMode::Green));
    channelCombo_->addItem(QStringLiteral("Blue"), static_cast<int>(HistogramChannelMode::Blue));
    channelCombo_->setCurrentIndex(0);
    plot_->setChannelMode(HistogramChannelMode::Luma);
}

void HistogramPanel::populateRawChannels(const RawPlaneHistogram& histogram) {
    const QSignalBlocker blocker(channelCombo_);
    channelCombo_->clear();
    for (const RawHistogramChannel& channel : histogram.channels) {
        HistogramChannelMode mode = HistogramChannelMode::All;
        switch (channel.id) {
        case RawHistogramChannelId::Y:
            mode = HistogramChannelMode::Y;
            break;
        case RawHistogramChannelId::U:
            mode = HistogramChannelMode::U;
            break;
        case RawHistogramChannelId::V:
            mode = HistogramChannelMode::V;
            break;
        case RawHistogramChannelId::Red:
            mode = HistogramChannelMode::Red;
            break;
        case RawHistogramChannelId::GreenRedRow:
            mode = HistogramChannelMode::GreenRedRow;
            break;
        case RawHistogramChannelId::GreenBlueRow:
            mode = HistogramChannelMode::GreenBlueRow;
            break;
        case RawHistogramChannelId::Blue:
            mode = HistogramChannelMode::Blue;
            break;
        }
        channelCombo_->addItem(rawHistogramChannelName(channel.id), static_cast<int>(mode));
    }
    if (channelCombo_->count() > 0) {
        channelCombo_->setCurrentIndex(0);
        plot_->setChannelMode(
            static_cast<HistogramChannelMode>(channelCombo_->currentData().toInt()));
    }
}

void HistogramPanel::updateDisplayStatistics(const DisplayHistogram& histogram) {
    struct Row {
        QString name;
        const HistogramChannel* channel;
    };
    const std::array<Row, 4> rows{{{QStringLiteral("Luma"), &histogram.luma},
                                   {QStringLiteral("R"), &histogram.red},
                                   {QStringLiteral("G"), &histogram.green},
                                   {QStringLiteral("B"), &histogram.blue}}};
    statistics_->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const HistogramChannel& channel = *rows.at(static_cast<std::size_t>(row)).channel;
        const QStringList values{
            rows.at(static_cast<std::size_t>(row)).name,
            QString::number(channel.mean, 'f', 2),
            QString::number(channel.standardDeviation * channel.standardDeviation, 'f', 2),
            QString::number(channel.minimum),
            QString::number(channel.maximum),
            QString::number(
                histogramMedian(channel.bins, static_cast<quint64>(histogram.sampledPixelCount)),
                'f', 1)};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values.at(column));
            item->setTextAlignment(Qt::AlignCenter);
            statistics_->setItem(row, column, item);
        }
    }
}

void HistogramPanel::updateRawStatistics(const RawPlaneHistogram& histogram) {
    statistics_->setRowCount(static_cast<int>(histogram.channels.size()));
    int row = 0;
    for (const RawHistogramChannel& channel : histogram.channels) {
        const QStringList values{
            rawHistogramChannelName(channel.id),
            QString::number(channel.mean, 'f', 2),
            QString::number(channel.standardDeviation * channel.standardDeviation, 'f', 2),
            QString::number(channel.minimum),
            QString::number(channel.maximum),
            QString::number(
                histogramMedian(channel.bins, static_cast<quint64>(channel.sampledSampleCount)),
                'f', 1)};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values.at(column));
            item->setTextAlignment(Qt::AlignCenter);
            statistics_->setItem(row, column, item);
        }
        ++row;
    }
}

} // namespace ispview
