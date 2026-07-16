#pragma once

#include <QMainWindow>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QAction;
class QEvent;
class QFrame;
class QLabel;
class QMenu;
class QResizeEvent;
class QTimer;
QT_END_NAMESPACE

namespace ispview {

class ImageCanvas;
class ImagePropertiesPanel;
class ImageLoader;

class FullScreenWindow final : public QMainWindow {
    Q_OBJECT

  public:
    FullScreenWindow(ImageLoader* loader, QStringList paths, int initialIndex,
                     QWidget* parent = nullptr);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    enum class RightOverlay { Information, Histogram };

    void buildEdgePanels();
    void showIndex(int index);
    void showNeighbor(int delta);
    void requestFullFrame(const QString& path, quint64 generation);
    void updateStatus();
    void layoutEdgePanels();
    void showTransientPanel(QWidget* panel);
    void hideTransientPanels();
    void setRightOverlay(RightOverlay overlay, bool pinned);
    void hideRightOverlay();
    void revealCurrentFile();
    [[nodiscard]] QString currentPath() const;

    ImageLoader* loader_;
    ImageCanvas* canvas_;
    QFrame* topPanel_;
    QFrame* leftPanel_;
    QFrame* rightPanel_;
    QFrame* bottomPanel_;
    QLabel* fileNameLabel_;
    QLabel* positionLabel_;
    QLabel* statusLabel_;
    QLabel* rightTitleLabel_;
    ImagePropertiesPanel* propertiesPanel_;
    QMenu* contextMenu_;
    QAction* revealAction_;
    QAction* showInformationAction_;
    QAction* showHistogramAction_;
    QTimer* panelHideTimer_;
    QStringList paths_;
    int index_ = -1;
    quint64 generation_ = 0;
    QString pixelText_;
    bool rightPanelPinned_ = false;
};

} // namespace ispview
