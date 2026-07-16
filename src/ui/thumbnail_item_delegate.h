#pragma once

#include <QStyledItemDelegate>

QT_BEGIN_NAMESPACE
class QListView;
QT_END_NAMESPACE

namespace ispview {

// Owns thumbnail presentation only. ThumbnailView remains responsible for selection, keyboard and
// drag/drop behavior, so a future UI redesign can replace styling without touching file transfer.
class ThumbnailItemDelegate final : public QStyledItemDelegate {
  public:
    explicit ThumbnailItemDelegate(const QListView* view);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

  private:
    const QListView* view_;
};

} // namespace ispview
