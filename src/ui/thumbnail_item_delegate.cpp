#include "ui/thumbnail_item_delegate.h"

#include "ui/thumbnail_model.h"

#include <QApplication>
#include <QIcon>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QStyleOptionViewItem>

#include <algorithm>

namespace ispview {

ThumbnailItemDelegate::ThumbnailItemDelegate(const QListView* view)
    : QStyledItemDelegate(const_cast<QListView*>(view)), view_(view) {}

void ThumbnailItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    if (view_->viewMode() == QListView::ListMode) {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const QStyle* style = item.widget ? item.widget->style() : QApplication::style();
        item.text.clear();
        item.icon = {};
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &item, painter, item.widget);

        const QRect row = option.rect.adjusted(8, 4, -10, -4);
        const QRect iconRect(row.left(), row.top(), 48, 48);
        QPixmap icon = index.data(Qt::DecorationRole).value<QPixmap>();
        if (!icon.isNull()) {
            icon = icon.scaled(iconRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QSize size = icon.deviceIndependentSize().toSize();
            painter->drawPixmap(QRect(QPoint(iconRect.center().x() - size.width() / 2,
                                             iconRect.center().y() - size.height() / 2),
                                      size),
                                icon);
        }
        const QRect textRect(iconRect.right() + 12, row.top(),
                             std::max(1, row.right() - iconRect.right() - 12), row.height());
        const QColor foreground = option.state.testFlag(QStyle::State_Selected)
                                      ? option.palette.color(QPalette::HighlightedText)
                                      : option.palette.color(QPalette::Text);
        const QColor secondary = option.state.testFlag(QStyle::State_Selected)
                                     ? foreground
                                     : option.palette.color(QPalette::PlaceholderText);
        painter->save();
        painter->setPen(foreground);
        const QString name = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                           Qt::ElideMiddle, textRect.width());
        painter->drawText(textRect.adjusted(0, 1, 0, -textRect.height() / 2),
                          Qt::AlignLeft | Qt::AlignVCenter, name);
        painter->setPen(secondary);
        const QString detail =
            index.data(ThumbnailModel::DirectoryRole).toBool()
                ? QStringLiteral("Folder")
                : QStringLiteral("%1  •  %2 KB")
                      .arg(index.data(ThumbnailModel::TypeRole).toString().toUpper())
                      .arg(index.data(ThumbnailModel::SizeRole).toLongLong() / 1024);
        painter->drawText(textRect.adjusted(0, textRect.height() / 2, 0, -1),
                          Qt::AlignLeft | Qt::AlignVCenter, detail);
        painter->restore();
        return;
    }

    const bool selected = option.state.testFlag(QStyle::State_Selected);
    constexpr int imageWidth = 160;
    constexpr int imageHeight = 120;
    constexpr int cardInset = 8;
    const bool directory = index.data(ThumbnailModel::DirectoryRole).toBool();
    const QRect card(option.rect.center().x() - 89, option.rect.top() + 4, 178, 156);
    const QRect imageArea(card.left() + cardInset, card.top() + cardInset, imageWidth, imageHeight);
    const QRect metadataArea(card.left() + cardInset, imageArea.bottom() + 2, imageWidth, 22);
    const QRect captionArea(option.rect.left() + 4, card.bottom() + 3, option.rect.width() - 8, 22);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPainterPath cardPath;
    cardPath.addRoundedRect(QRectF(card), 7.0, 7.0);
    // Cards intentionally have no fill. Selection is expressed only by the outer stroke.
    painter->setPen(
        QPen(selected ? option.palette.highlight().color() : option.palette.color(QPalette::Mid),
             selected ? 2.0 : 1.0));
    painter->drawPath(cardPath);
    painter->setClipPath(cardPath);

    const QVariant decoration = index.data(Qt::DecorationRole);
    QPixmap pixmap = decoration.value<QPixmap>();
    if (pixmap.isNull() && decoration.canConvert<QIcon>()) {
        pixmap = decoration.value<QIcon>().pixmap(view_->iconSize());
    }
    if (!pixmap.isNull()) {
        if (directory) {
            const QRect folderArea = imageArea.adjusted(24, 12, -24, -12);
            const QPixmap scaled =
                pixmap.scaled(folderArea.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QSize logicalSize = scaled.deviceIndependentSize().toSize();
            const QRect target(QPoint(folderArea.center().x() - logicalSize.width() / 2,
                                      folderArea.center().y() - logicalSize.height() / 2),
                               logicalSize);
            painter->drawPixmap(target, scaled);
        } else {
            // Product behavior intentionally forces a uniform 160x120 bitmap.
            const QPixmap scaled =
                pixmap.scaled(imageArea.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            painter->drawPixmap(imageArea, scaled);
        }
    }
    painter->setClipping(false);
    painter->setRenderHint(QPainter::Antialiasing, false);
    const QSize dimensions = index.data(ThumbnailModel::DimensionsRole).toSize();
    const QString dimensionsText =
        dimensions.isValid()
            ? QStringLiteral("%1×%2").arg(dimensions.width()).arg(dimensions.height())
            : QString{};
    const QString type =
        directory ? QString{} : index.data(ThumbnailModel::TypeRole).toString().toUpper();
    painter->setFont(option.font);
    painter->setPen(option.palette.color(QPalette::Text));
    if (!directory) {
        painter->drawText(metadataArea, Qt::AlignLeft | Qt::AlignVCenter, dimensionsText);
        painter->drawText(metadataArea, Qt::AlignRight | Qt::AlignVCenter, type);
    }
    // The caption keeps normal text color; highlighted text may be white and disappear.
    painter->setPen(option.palette.color(QPalette::Text));
    const QString visibleCaption = option.fontMetrics.elidedText(
        index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, captionArea.width());
    painter->drawText(captionArea, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine,
                      visibleCaption);
    painter->restore();
}

QSize ThumbnailItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    if (view_->viewMode() == QListView::ListMode) {
        Q_UNUSED(index);
        return {std::max(240, option.rect.width()), 58};
    }
    Q_UNUSED(option);
    Q_UNUSED(index);
    return {194, 190};
}

} // namespace ispview
