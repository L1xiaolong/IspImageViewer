#pragma once

#include "core/image_types.h"
#include "core/raw_image_parameters.h"

#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QTabWidget;
class QTreeWidget;
QT_END_NAMESPACE

namespace ispview {

class HistogramPanel;
class ImageInfoPanel;

class ImagePropertiesPanel final : public QWidget {
    Q_OBJECT

  public:
    enum class Tab { Exif, Histogram, RawParameters };

    explicit ImagePropertiesPanel(QWidget* parent = nullptr);

    void setFrame(ImageFramePtr frame);
    void clearFramePreservingRawParameters();
    void setNormalizedRegion(std::optional<QRectF> normalizedRegion);
    void setRawParameters(const QString& path, const RawImageParameters& parameters);
    void showTab(Tab tab);

    [[nodiscard]] ImageInfoPanel* basicInformation() const { return basicInformation_; }
    [[nodiscard]] ImageInfoPanel* exifInformation() const { return exifInformation_; }
    [[nodiscard]] HistogramPanel* histogramPanel() const { return histogramPanel_; }
    [[nodiscard]] QTreeWidget* rawParametersTable() const { return rawParametersTable_; }
    [[nodiscard]] QTabWidget* tabs() const { return tabs_; }

  private:
    ImageInfoPanel* basicInformation_;
    ImageInfoPanel* exifInformation_;
    HistogramPanel* histogramPanel_;
    QTreeWidget* rawParametersTable_;
    QTabWidget* tabs_;
    int rawTabIndex_ = -1;
};

} // namespace ispview
