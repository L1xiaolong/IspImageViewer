#include "ui/thumbnail_view.h"

#include "platform/platform_shortcuts.h"
#include "ui/local_file_drop.h"
#include "ui/thumbnail_item_delegate.h"
#include "ui/thumbnail_model.h"

#include <algorithm>

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QUrl>

namespace ispview {
ThumbnailView::ThumbnailView(QWidget* parent) : QListView(parent) {
    setDragEnabled(true);
    // QAbstractItemView gates native platform DragEnter before the virtual dragEnterEvent() when
    // it remains in DragOnly mode.  Use DragDrop so Finder/Explorer URLs reach our explicit
    // handlers; self drags are still rejected in dragEnter/move/drop below, so this does not enable
    // internal item rearrangement.
    setDragDropMode(QAbstractItemView::DragDrop);
    setAcceptDrops(true);
    viewport()->installEventFilter(this);
    setDragDropOverwriteMode(false);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::CopyAction);
    setItemDelegate(new ThumbnailItemDelegate(this));
}

bool ThumbnailView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            const bool selfDrag = dropEvent->source() == this || dropEvent->source() == viewport();
            const QStringList paths =
                selfDrag ? QStringList{} : localFileDropPaths(dropEvent->mimeData());
            if (!paths.isEmpty()) {
                dropEvent->setDropAction(Qt::CopyAction);
                dropEvent->accept();
                if (event->type() == QEvent::DragEnter) {
                    emit externalDropEntered(paths.size());
                }
                return true;
            }
            if (event->type() == QEvent::DragEnter) {
                emit externalDropRejected(localFileDropFormats(dropEvent->mimeData()));
            }
            if (selfDrag) {
                dropEvent->ignore();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            if (dropEvent->source() == this || dropEvent->source() == viewport()) {
                dropEvent->ignore();
                return true;
            }
            const QStringList paths = localFileDropPaths(dropEvent->mimeData());
            if (!paths.isEmpty()) {
                emit localPathsDropped(paths);
                dropEvent->setDropAction(Qt::CopyAction);
                dropEvent->accept();
                return true;
            }
        }
    }
    return QListView::eventFilter(watched, event);
}

void ThumbnailView::startDrag(Qt::DropActions supportedActions) {
    if (!(supportedActions & Qt::CopyAction) || !selectionModel()) {
        return;
    }

    QList<QUrl> urls;
    for (const QModelIndex& index : selectionModel()->selectedIndexes()) {
        const QString path = index.data(ThumbnailModel::PathRole).toString();
        const QFileInfo file(path);
        if (file.exists() && (file.isFile() || file.isDir())) {
            urls.append(QUrl::fromLocalFile(file.absoluteFilePath()));
        }
    }
    if (urls.isEmpty()) {
        return;
    }

    emit externalDragStarted(urls.size());

    auto* mimeData = new QMimeData;
    // Keep the URL payload in the strongly typed form created by setUrls().  In particular, the
    // Cocoa platform plugin converts that QList<QUrl> to the native file-url pasteboard type used
    // by Finder.  Replacing text/uri-list afterwards with a QByteArray loses that typed payload
    // and can make a drag look valid to another Qt widget while Finder rejects it.
    mimeData->setUrls(urls);

    // Match QAbstractItemView's native drag implementation: the item view is the logical source
    // while mouse events still originate from its viewport.  Keeping one stable source widget is
    // important for native platform drag managers and also lets incoming-event code distinguish a
    // self drag from a Finder/Explorer drag.
    QDrag drag(this);
    drag.setMimeData(mimeData);
    const QPixmap thumbnail = currentIndex().data(Qt::DecorationRole).value<QPixmap>();
    if (!thumbnail.isNull()) {
        const QPixmap preview =
            thumbnail.scaled(96, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        drag.setPixmap(preview);
        drag.setHotSpot(preview.rect().center());
    }
    emit externalDragFinished(drag.exec(Qt::CopyAction, Qt::CopyAction));
}

void ThumbnailView::dragEnterEvent(QDragEnterEvent* event) {
    const bool selfDrag = event->source() == this || event->source() == viewport();
    const QStringList paths = selfDrag ? QStringList{} : localFileDropPaths(event->mimeData());
    if (!paths.isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        emit externalDropEntered(paths.size());
        return;
    }
    QListView::dragEnterEvent(event);
}

void ThumbnailView::dragMoveEvent(QDragMoveEvent* event) {
    const bool selfDrag = event->source() == this || event->source() == viewport();
    if (!selfDrag && !localFileDropPaths(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    QListView::dragMoveEvent(event);
}

void ThumbnailView::dropEvent(QDropEvent* event) {
    if (event->source() == this || event->source() == viewport()) {
        event->ignore();
        return;
    }
    const QStringList paths = localFileDropPaths(event->mimeData());
    if (paths.isEmpty()) {
        QListView::dropEvent(event);
        return;
    }
    emit localPathsDropped(paths);
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void ThumbnailView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F2 && event->modifiers() == Qt::NoModifier) {
        event->accept();
        emit renameShortcutRequested();
        return;
    }
    if (isItemViewTrashShortcut(event)) {
        event->accept();
        emit trashShortcutRequested();
        return;
    }
    QListView::keyPressEvent(event);
}

void ThumbnailView::mousePressEvent(QMouseEvent* event) {
    // Let QListView update currentIndex/selection first.  The explicit state below only replaces
    // its drag initiation; selection semantics continue to come from the standard item view.
    QListView::mousePressEvent(event);
    resetPendingDrag();
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const QModelIndex pressedIndex = indexAt(event->position().toPoint());
    if (!pressedIndex.isValid()) {
        return;
    }
    const QFileInfo file(pressedIndex.data(ThumbnailModel::PathRole).toString());
    if (!file.exists() || (!file.isFile() && !file.isDir())) {
        return;
    }

    dragCandidate_ = pressedIndex;
    dragStartPosition_ = event->position().toPoint();
    dragPending_ = true;
}

void ThumbnailView::mouseMoveEvent(QMouseEvent* event) {
    if (dragPending_ && event->buttons().testFlag(Qt::LeftButton)) {
        const int distance = (event->position().toPoint() - dragStartPosition_).manhattanLength();
        if (distance >= QApplication::startDragDistance()) {
            const QPersistentModelIndex candidate = dragCandidate_;
            resetPendingDrag();
            if (candidate.isValid()) {
                // Calling startDrag explicitly avoids depending on QAbstractItemView's private
                // DraggingState transition, which did not occur for real macOS gestures even
                // though the same path passed synthetic Qt tests.
                startDrag(model() ? model()->supportedDragActions() : Qt::CopyAction);
                event->accept();
                return;
            }
        }
    } else if (dragPending_) {
        resetPendingDrag();
    }
    QListView::mouseMoveEvent(event);
}

void ThumbnailView::mouseReleaseEvent(QMouseEvent* event) {
    resetPendingDrag();
    QListView::mouseReleaseEvent(event);
}

void ThumbnailView::resetPendingDrag() {
    dragCandidate_ = QPersistentModelIndex();
    dragStartPosition_ = {};
    dragPending_ = false;
}

} // namespace ispview
