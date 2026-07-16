#include "ui/image_info_panel.h"

#include <QHeaderView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <cmath>

namespace ispview {
namespace {

QString byteSizeText(qint64 bytes) {
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kibibytes = bytes / 1024.0;
    if (kibibytes < 1024.0) {
        return QStringLiteral("%1 KiB").arg(kibibytes, 0, 'f', 1);
    }
    return QStringLiteral("%1 MiB").arg(kibibytes / 1024.0, 0, 'f', 2);
}

QString exposureText(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) {
        return {};
    }
    if (seconds >= 1.0) {
        return QStringLiteral("%1 s").arg(seconds, 0, 'g', 4);
    }
    const double denominator = 1.0 / seconds;
    const double rounded = std::round(denominator);
    if (std::abs(denominator - rounded) < 0.05 && rounded <= 100000.0) {
        return QStringLiteral("1/%1 s").arg(static_cast<int>(rounded));
    }
    return QStringLiteral("%1 s").arg(seconds, 0, 'g', 4);
}

QString orientationText(ImageMetadata::Orientation orientation) {
    switch (orientation) {
    case ImageMetadata::Orientation::Normal:
        return QStringLiteral("Normal");
    case ImageMetadata::Orientation::MirrorHorizontal:
        return QStringLiteral("Mirror horizontal");
    case ImageMetadata::Orientation::Rotate180:
        return QStringLiteral("Rotate 180°");
    case ImageMetadata::Orientation::MirrorVertical:
        return QStringLiteral("Mirror vertical");
    case ImageMetadata::Orientation::MirrorHorizontalRotate270:
        return QStringLiteral("Mirror horizontal, rotate 270° CW");
    case ImageMetadata::Orientation::Rotate90:
        return QStringLiteral("Rotate 90° CW");
    case ImageMetadata::Orientation::MirrorHorizontalRotate90:
        return QStringLiteral("Mirror horizontal, rotate 90° CW");
    case ImageMetadata::Orientation::Rotate270:
        return QStringLiteral("Rotate 270° CW");
    case ImageMetadata::Orientation::Unspecified:
        return {};
    }
    return {};
}

} // namespace

ImageInfoPanel::ImageInfoPanel(QWidget* parent, Section section)
    : QWidget(parent), fields_(new QTreeWidget(this)), section_(section) {
    setObjectName(QStringLiteral("imageInfoPanel"));
    fields_->setObjectName(QStringLiteral("imageInfoFields"));
    fields_->setColumnCount(2);
    fields_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    fields_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    fields_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    fields_->setRootIsDecorated(false);
    fields_->setAlternatingRowColors(true);
    fields_->setTextElideMode(Qt::ElideMiddle);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(fields_);
}

void ImageInfoPanel::setFrame(ImageFramePtr frame) {
    frame_ = std::move(frame);
    fields_->clear();
    if (!frame_) {
        if (section_ == Section::Exif) {
            populateEmptyExifFields();
        }
        return;
    }

    const ImageMetadata& metadata = frame_->metadata;
    if (section_ == Section::All || section_ == Section::Basic) {
        const QSize dimensions =
            metadata.sourceSize.isValid() ? metadata.sourceSize : frame_->descriptor.size;
        addField(QStringLiteral("File Name"), metadata.fileName);
        addField(QStringLiteral("Location"), metadata.path);
        addField(QStringLiteral("Type"), metadata.format);
        addField(QStringLiteral("File Size"), byteSizeText(metadata.fileSize));
        addField(QStringLiteral("Date / Time"), metadata.modifiedAt.toString(Qt::ISODate));
        addField(QStringLiteral("Dimensions"),
                 QStringLiteral("%1 × %2").arg(dimensions.width()).arg(dimensions.height()));
        addField(QStringLiteral("Bit Depth"), QStringLiteral("%1-bit valid in %2-bit storage")
                                                  .arg(frame_->descriptor.validBits)
                                                  .arg(frame_->descriptor.storageBits));
    }

    if (section_ != Section::All && section_ != Section::Exif) {
        return;
    }

    const bool keepEmpty = section_ == Section::Exif;
    if (metadata.camera) {
        const ImageMetadata::Camera& camera = *metadata.camera;
        addField(QStringLiteral("Make"), camera.make, keepEmpty);
        addField(QStringLiteral("Model"), camera.model, keepEmpty);
        addField(QStringLiteral("Software"), camera.software, keepEmpty);
        addField(QStringLiteral("Captured At"), camera.capturedAt.toString(Qt::ISODate), keepEmpty);
        addField(QStringLiteral("Exposure Time"), exposureText(camera.exposureSeconds), keepEmpty);
        addField(QStringLiteral("Aperture"),
                 camera.aperture > 0.0 ? QStringLiteral("f/%1").arg(camera.aperture, 0, 'g', 3)
                                       : QString{},
                 keepEmpty);
        addField(QStringLiteral("ISO"), camera.iso > 0 ? QString::number(camera.iso) : QString{},
                 keepEmpty);
        addField(QStringLiteral("Exposure Program"), camera.exposureProgram, keepEmpty);
        addField(QStringLiteral("Metering Mode"), camera.meteringMode, keepEmpty);
        addField(QStringLiteral("Exposure Compensation"), camera.exposureCompensation, keepEmpty);
        addField(QStringLiteral("Flash"), camera.flash, keepEmpty);
        addField(QStringLiteral("Focal Length"),
                 camera.focalLengthMm > 0.0
                     ? QStringLiteral("%1 mm").arg(camera.focalLengthMm, 0, 'g', 4)
                     : QString{},
                 keepEmpty);
        addField(QStringLiteral("Lens"), camera.lens, keepEmpty);
        addField(QStringLiteral("GPS"), camera.gps, keepEmpty);
        addField(QStringLiteral("Sensor Size"),
                 camera.sensorSize.isValid() ? QStringLiteral("%1 × %2")
                                                   .arg(camera.sensorSize.width())
                                                   .arg(camera.sensorSize.height())
                                             : QString{},
                 keepEmpty);
    } else {
        populateEmptyExifFields();
    }

    addField(QStringLiteral("Orientation"), orientationText(metadata.sourceOrientation), keepEmpty);
    if (metadata.descriptive) {
        const ImageMetadata::Descriptive& descriptive = *metadata.descriptive;
        addField(QStringLiteral("Title"), descriptive.title);
        addField(QStringLiteral("Description"), descriptive.description);
        addField(QStringLiteral("Creator"), descriptive.creator);
        addField(QStringLiteral("Copyright"), descriptive.copyright);
    }
    addField(QStringLiteral("Metadata Warning"), metadata.metadataWarning, keepEmpty);
}

QString ImageInfoPanel::valueForField(const QString& field) const {
    for (int index = 0; index < fields_->topLevelItemCount(); ++index) {
        const QTreeWidgetItem* item = fields_->topLevelItem(index);
        if (item->text(0) == field) {
            return item->text(1);
        }
    }
    return {};
}

void ImageInfoPanel::addField(const QString& field, const QString& value, bool keepEmpty) {
    if (value.isEmpty() && !keepEmpty) {
        return;
    }
    auto* item = new QTreeWidgetItem(fields_, {field, value});
    item->setToolTip(1, value);
}

void ImageInfoPanel::populateEmptyExifFields() {
    const QStringList fields{
        QStringLiteral("Make"),          QStringLiteral("Model"),
        QStringLiteral("Software"),      QStringLiteral("Captured At"),
        QStringLiteral("Exposure Time"), QStringLiteral("Aperture"),
        QStringLiteral("ISO"),           QStringLiteral("Exposure Program"),
        QStringLiteral("Metering Mode"), QStringLiteral("Exposure Compensation"),
        QStringLiteral("Flash"),         QStringLiteral("Focal Length"),
        QStringLiteral("Lens"),          QStringLiteral("GPS"),
        QStringLiteral("Sensor Size")};
    for (const QString& field : fields) {
        addField(field, {}, true);
    }
}

} // namespace ispview
