#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QHBoxLayout;
class QScrollArea;
QT_END_NAMESPACE

namespace ispview {

// Compact, cross-platform filesystem hierarchy used above image grids. Each segment represents
// an ancestor of the current directory and can be activated without reserving space for a tree.
class PathBreadcrumb final : public QWidget {
    Q_OBJECT

  public:
    explicit PathBreadcrumb(QWidget* parent = nullptr);

    void setPath(const QString& path);
    [[nodiscard]] QString path() const { return path_; }

  signals:
    void pathActivated(const QString& path);

  private:
    void clearSegments();
    void addSegment(const QString& label, const QString& path, bool drive);

    QScrollArea* scrollArea_;
    QWidget* segmentContainer_;
    QHBoxLayout* segmentLayout_;
    QString path_;
};

} // namespace ispview
