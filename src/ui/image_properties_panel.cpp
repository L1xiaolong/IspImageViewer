#include "ui/image_properties_panel.h"

#include "ui/histogram_panel.h"
#include "ui/image_info_panel.h"

#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace ispview {
namespace {

QString orientationName(ImageOrientation orientation) {
    switch (orientation) {
    case ImageOrientation::Normal:
        return QStringLiteral("Normal");
    case ImageOrientation::Rotate90Clockwise:
        return QStringLiteral("Rotate 90° clockwise");
    case ImageOrientation::Rotate180:
        return QStringLiteral("Rotate 180°");
    case ImageOrientation::Rotate270Clockwise:
        return QStringLiteral("Rotate 270° clockwise");
    }
    return {};
}

QString rangeName(QuantizationRange range) {
    return range == QuantizationRange::Full ? QStringLiteral("Full") : QStringLiteral("Limited");
}

QString matrixText(const std::array<double, 9>& matrix) {
    QStringList rows;
    for (int row = 0; row < 3; ++row) {
        QStringList values;
        for (int column = 0; column < 3; ++column) {
            values.append(
                QString::number(matrix.at(static_cast<std::size_t>(row * 3 + column)), 'g', 6));
        }
        rows.append(values.join(QStringLiteral(", ")));
    }
    return rows.join(QStringLiteral("  |  "));
}

void addRawField(QTreeWidget* table, const QString& field, const QString& value) {
    auto* item = new QTreeWidgetItem(table, {field, value});
    item->setToolTip(1, value);
}

void populateRawTable(QTreeWidget* table, const RawImageParameters& parameters) {
    table->clear();
    addRawField(table, QStringLiteral("Format"), rawPixelFormatName(parameters.format));
    addRawField(
        table, QStringLiteral("Dimensions"),
        QStringLiteral("%1 × %2").arg(parameters.size.width()).arg(parameters.size.height()));
    addRawField(table, QStringLiteral("Header Offset"), QString::number(parameters.headerOffset));
    addRawField(table, QStringLiteral("Row Stride"), QString::number(parameters.rowStride));
    addRawField(table, QStringLiteral("Orientation"), orientationName(parameters.orientation));
    if (parameters.isYuv()) {
        addRawField(table, QStringLiteral("Chroma Stride"),
                    QString::number(parameters.chromaStride));
        addRawField(table, QStringLiteral("YUV Matrix"), yuvMatrixName(parameters.yuvMatrix));
        addRawField(table, QStringLiteral("YUV Range"), rangeName(parameters.range));
    } else {
        addRawField(table, QStringLiteral("Valid Bits"), QString::number(parameters.validBits()));
        addRawField(table, QStringLiteral("Bayer Pattern"),
                    bayerPatternName(parameters.bayerPattern));
        addRawField(table, QStringLiteral("Demosaic"),
                    parameters.demosaic ? QStringLiteral("Yes") : QStringLiteral("No"));
        addRawField(table, QStringLiteral("Black Level"), QString::number(parameters.blackLevel));
        addRawField(table, QStringLiteral("White Level"), QString::number(parameters.whiteLevel));
        addRawField(table, QStringLiteral("White Balance"),
                    QStringLiteral("R %1  G %2  B %3")
                        .arg(parameters.whiteBalanceGains.at(0), 0, 'g', 6)
                        .arg(parameters.whiteBalanceGains.at(1), 0, 'g', 6)
                        .arg(parameters.whiteBalanceGains.at(2), 0, 'g', 6));
        addRawField(table, QStringLiteral("Color Matrix"),
                    matrixText(parameters.colorCorrectionMatrix));
        addRawField(table, QStringLiteral("Display Gamma"),
                    QString::number(parameters.displayGamma, 'g', 6));
    }
    if (parameters.format == RawPixelFormat::P010 || parameters.format == RawPixelFormat::Raw16) {
        addRawField(table, QStringLiteral("Byte Order"),
                    parameters.littleEndian ? QStringLiteral("Little endian")
                                            : QStringLiteral("Big endian"));
        addRawField(table, QStringLiteral("Bit Alignment"),
                    parameters.msbAligned ? QStringLiteral("MSB aligned")
                                          : QStringLiteral("LSB aligned"));
    }
}

} // namespace

ImagePropertiesPanel::ImagePropertiesPanel(QWidget* parent)
    : QWidget(parent), basicInformation_(new ImageInfoPanel(this, ImageInfoPanel::Section::Basic)),
      exifInformation_(new ImageInfoPanel(this, ImageInfoPanel::Section::Exif)),
      histogramPanel_(new HistogramPanel(this)), rawParametersTable_(new QTreeWidget(this)),
      tabs_(new QTabWidget(this)) {
    setObjectName(QStringLiteral("imagePropertiesPanel"));
    basicInformation_->setObjectName(QStringLiteral("basicInformationPanel"));
    exifInformation_->setObjectName(QStringLiteral("imageInfoPanel"));
    histogramPanel_->setObjectName(QStringLiteral("histogramPanel"));
    rawParametersTable_->setObjectName(QStringLiteral("rawParameterFields"));
    rawParametersTable_->setColumnCount(2);
    rawParametersTable_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    rawParametersTable_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    rawParametersTable_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    rawParametersTable_->setRootIsDecorated(false);
    rawParametersTable_->setAlternatingRowColors(true);
    rawParametersTable_->setTextElideMode(Qt::ElideMiddle);
    tabs_->setObjectName(QStringLiteral("imagePropertiesTabs"));

    auto* heading = new QLabel(QStringLiteral("File Properties"), this);
    heading->setObjectName(QStringLiteral("filePropertiesHeading"));
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    basicInformation_->setMinimumHeight(190);
    basicInformation_->setMaximumHeight(245);
    tabs_->addTab(exifInformation_, QStringLiteral("EXIF"));
    tabs_->addTab(histogramPanel_, QStringLiteral("Histogram"));
    rawTabIndex_ = tabs_->addTab(rawParametersTable_, QStringLiteral("RAW Parameters"));
    tabs_->setTabEnabled(rawTabIndex_, false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(heading);
    layout->addWidget(basicInformation_);
    layout->addWidget(tabs_, 1);
}

void ImagePropertiesPanel::setFrame(ImageFramePtr frame) {
    basicInformation_->setFrame(frame);
    exifInformation_->setFrame(frame);
    histogramPanel_->setFrame(frame);
    const bool hasRawParameters = frame && frame->rawParameters.has_value();
    tabs_->setTabEnabled(rawTabIndex_, hasRawParameters);
    if (hasRawParameters) {
        populateRawTable(rawParametersTable_, *frame->rawParameters);
    } else {
        rawParametersTable_->clear();
        if (tabs_->currentIndex() == rawTabIndex_) {
            tabs_->setCurrentIndex(0);
        }
    }
}

void ImagePropertiesPanel::clearFramePreservingRawParameters() {
    basicInformation_->setFrame({});
    exifInformation_->setFrame({});
    histogramPanel_->setFrame({});
}

void ImagePropertiesPanel::setNormalizedRegion(std::optional<QRectF> normalizedRegion) {
    histogramPanel_->setNormalizedRegion(std::move(normalizedRegion));
}

void ImagePropertiesPanel::setRawParameters(const QString& path,
                                            const RawImageParameters& parameters) {
    Q_UNUSED(path);
    populateRawTable(rawParametersTable_, parameters);
    tabs_->setTabEnabled(rawTabIndex_, true);
}

void ImagePropertiesPanel::showTab(Tab tab) {
    int index = 0;
    switch (tab) {
    case Tab::Exif:
        index = 0;
        break;
    case Tab::Histogram:
        index = 1;
        break;
    case Tab::RawParameters:
        index = rawTabIndex_;
        break;
    }
    if (index >= 0 && tabs_->isTabEnabled(index)) {
        tabs_->setCurrentIndex(index);
    }
}

} // namespace ispview
