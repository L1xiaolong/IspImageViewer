#pragma once

#include "core/image_types.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QTreeWidget;
QT_END_NAMESPACE

namespace ispview {

class ImageInfoPanel final : public QWidget {
    Q_OBJECT

  public:
    enum class Section { All, Basic, Exif };

    explicit ImageInfoPanel(QWidget* parent = nullptr, Section section = Section::All);

    void setFrame(ImageFramePtr frame);
    [[nodiscard]] QString valueForField(const QString& field) const;

  private:
    void addField(const QString& field, const QString& value, bool keepEmpty = false);
    void populateEmptyExifFields();

    QTreeWidget* fields_;
    ImageFramePtr frame_;
    Section section_;
};

} // namespace ispview
