#pragma once

#include "core/display_histogram.h"
#include "core/raw_plane_histogram.h"

#include <QThreadPool>
#include <QWidget>

#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QLabel;
class QComboBox;
class QTableWidget;
class QTimer;
QT_END_NAMESPACE

namespace ispview {

class HistogramPlot;

enum class HistogramSource { Display, SourcePlanes };
enum class HistogramChannelMode { All, Luma, Red, Green, Blue, Y, U, V, GreenRedRow, GreenBlueRow };

class HistogramPanel final : public QWidget {
    Q_OBJECT

  public:
    explicit HistogramPanel(QWidget* parent = nullptr);
    ~HistogramPanel() override;

    void setFrame(ImageFramePtr frame);
    void setNormalizedRegion(std::optional<QRectF> normalizedRegion);
    void setSource(HistogramSource source);
    void setCompactLumaOnly(bool enabled);
    [[nodiscard]] HistogramSource source() const;
    [[nodiscard]] const std::optional<QRectF>& normalizedRegion() const {
        return normalizedRegion_;
    }
    [[nodiscard]] const std::optional<DisplayHistogram>& histogram() const { return histogram_; }
    [[nodiscard]] const std::optional<RawPlaneHistogram>& rawHistogram() const {
        return rawHistogram_;
    }

  private:
    void scheduleAnalysis();
    void startPendingAnalysis();
    void applyDisplayHistogram(quint64 generation, DisplayHistogram histogram);
    void applyRawHistogram(quint64 generation, RawPlaneHistogram histogram);
    void populateDisplayChannels();
    void populateRawChannels(const RawPlaneHistogram& histogram);
    void updateDisplayStatistics(const DisplayHistogram& histogram);
    void updateRawStatistics(const RawPlaneHistogram& histogram);

    HistogramPlot* plot_;
    QComboBox* sourceCombo_;
    QComboBox* channelCombo_;
    QLabel* summary_;
    QTableWidget* statistics_;
    QTimer* debounce_;
    QThreadPool pool_;
    ImageFramePtr currentFrame_;
    ImageFramePtr pendingFrame_;
    std::optional<QRectF> normalizedRegion_;
    std::optional<DisplayHistogram> histogram_;
    std::optional<RawPlaneHistogram> rawHistogram_;
    quint64 generation_ = 0;
};

} // namespace ispview
