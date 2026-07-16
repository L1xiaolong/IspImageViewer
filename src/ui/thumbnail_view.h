#pragma once

#include <QListView>
#include <QPersistentModelIndex>
#include <QPoint>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QKeyEvent;
class QMimeData;
class QMouseEvent;
QT_END_NAMESPACE

namespace ispview {

class ThumbnailView final : public QListView {
    Q_OBJECT

  public:
    explicit ThumbnailView(QWidget* parent = nullptr);

  signals:
    void localPathsDropped(const QStringList& paths);
    void trashShortcutRequested();
    void renameShortcutRequested();
    void externalDragStarted(qsizetype itemCount);
    void externalDragFinished(Qt::DropAction result);
    void externalDropEntered(qsizetype pathCount);
    void externalDropRejected(const QString& formats);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

  private:
    void resetPendingDrag();
    QPersistentModelIndex dragCandidate_;
    QPoint dragStartPosition_;
    bool dragPending_ = false;
};

} // namespace ispview
