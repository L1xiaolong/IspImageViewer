#pragma once

#include "core/image_types.h"
#include "core/sync_group.h"

#include <QMainWindow>
#include <QStringList>
#include <QVector>

QT_BEGIN_NAMESPACE
class QAction;
class QEvent;
class QKeyEvent;
class QLabel;
QT_END_NAMESPACE

namespace ispview {

class HistogramPanel;
class ImageCanvas;
class ImageLoader;

class CompareWindow final : public QMainWindow {
    Q_OBJECT

  public:
    CompareWindow(ImageLoader* loader, const QStringList& paths, QWidget* parent = nullptr);
    CompareWindow(ImageLoader* loader, const QString& leftPath, const QString& rightPath,
                  QWidget* parent = nullptr);

  signals:
    void screenshotSaved(const QString& path);

  protected:
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

  private:
    enum class PresentationMode { SideBySide, VerticalSplit, HorizontalSplit };

    void load(int slot, const QString& path);
    void propagate(ImageCanvas* source, const ViewState& state);
    void updatePixelOverlays(int sourceSlot, const QPoint& pixel);
    void updateInformationOverlay(int slot);
    void setPresentationMode(PresentationMode mode);
    void setHoldComparison(bool active);
    void saveScreenshot();

    ImageLoader* loader_;
    QVector<ImageCanvas*> canvases_;
    QVector<QWidget*> panes_;
    QVector<QLabel*> fileLabels_;
    QVector<QLabel*> exifLabels_;
    QVector<QLabel*> pixelLabels_;
    QVector<HistogramPanel*> histograms_;
    QVector<ImageFramePtr> frames_;
    QVector<quint64> generations_;
    SyncGroup syncGroup_;
    QAction* fileInfoAction_ = nullptr;
    QAction* exifAction_ = nullptr;
    QAction* histogramAction_ = nullptr;
    QAction* pixelAction_ = nullptr;
    PresentationMode presentationMode_ = PresentationMode::SideBySide;
    bool applyingSync_ = false;
    bool synchronized_ = true;
    bool holdActive_ = false;
};

} // namespace ispview
