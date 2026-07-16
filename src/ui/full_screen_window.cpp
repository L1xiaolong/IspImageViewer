#include "ui/full_screen_window.h"

#include "core/raw_plane_access.h"
#include "io/image_loader.h"
#include "platform/platform_services.h"
#include "render/image_canvas.h"
#include "ui/image_properties_panel.h"

#include <QAction>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace ispview {
namespace {

constexpr int edgeTriggerWidth = 28;

QFrame* makeOverlayPanel(const QString& objectName, QWidget* parent) {
    auto* panel = new QFrame(parent);
    panel->setObjectName(objectName);
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setStyleSheet(
        QStringLiteral(
            "QFrame#%1 { background-color: rgba(20, 20, 22, 225); border: 1px solid "
            "rgba(255,255,255,55); border-radius: 8px; color: white; }"
            "QLabel { color: white; }"
            "QToolButton { color: white; background-color: rgba(255,255,255,24); padding: 7px; "
            "border: 1px solid rgba(255,255,255,35); border-radius: 5px; }"
            "QToolButton:hover, QToolButton:checked { background-color: rgba(90,145,230,150); }")
            .arg(objectName));
    panel->setMouseTracking(true);
    panel->hide();
    return panel;
}

QToolButton* makeTextButton(const QString& text, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setAutoRaise(false);
    return button;
}

void applyPropertiesOverlayStyle(QFrame* panel) {
    // Properties contains dense tables and charts, so it intentionally uses a substantially more
    // opaque surface than the lightweight navigation overlays. The white surface also prevents
    // the image underneath from changing table and label contrast.
    panel->setStyleSheet(QStringLiteral(
        "QFrame#fullScreenRightPanel { background-color: rgba(255,255,255,246); "
        "border: 1px solid rgba(40,40,45,120); border-radius: 8px; color: #202124; }"
        "QFrame#fullScreenRightPanel QLabel { color: #202124; }"
        "QFrame#fullScreenRightPanel QToolButton { color: #202124; "
        "background-color: rgba(235,235,238,235); padding: 7px; "
        "border: 1px solid rgba(70,70,75,90); border-radius: 5px; }"
        "QFrame#fullScreenRightPanel QToolButton:hover, "
        "QFrame#fullScreenRightPanel QToolButton:checked { "
        "background-color: rgba(185,215,250,245); }"
        "QFrame#fullScreenRightPanel QTreeWidget, "
        "QFrame#fullScreenRightPanel QTableWidget, "
        "QFrame#fullScreenRightPanel QComboBox { "
        "background-color: rgba(255,255,255,242); color: #202124; "
        "alternate-background-color: rgba(242,242,244,242); }"
        "QFrame#fullScreenRightPanel QHeaderView::section { "
        "background-color: rgba(232,232,235,245); color: #202124; }"
        "QFrame#fullScreenRightPanel QTabWidget::pane { "
        "background-color: rgba(255,255,255,230); "
        "border: 1px solid rgba(70,70,75,65); }"
        "QFrame#fullScreenRightPanel QTabBar::tab { "
        "background-color: rgba(232,232,235,235); color: #202124; padding: 6px 10px; }"
        "QFrame#fullScreenRightPanel QTabBar::tab:selected { "
        "background-color: rgba(90,145,230,235); color: white; }"));
}

} // namespace

FullScreenWindow::FullScreenWindow(ImageLoader* loader, QStringList paths, int initialIndex,
                                   QWidget* parent)
    : QMainWindow(parent), loader_(loader), canvas_(new ImageCanvas(this)), topPanel_(nullptr),
      leftPanel_(nullptr), rightPanel_(nullptr), bottomPanel_(nullptr), fileNameLabel_(nullptr),
      positionLabel_(nullptr), statusLabel_(nullptr), rightTitleLabel_(nullptr),
      propertiesPanel_(nullptr), contextMenu_(new QMenu(this)), revealAction_(nullptr),
      showInformationAction_(nullptr), showHistogramAction_(nullptr),
      panelHideTimer_(new QTimer(this)), paths_(std::move(paths)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setCentralWidget(canvas_);
    canvas_->setNavigationThumbnailEnabled(true);
    canvas_->setContextMenuPolicy(Qt::CustomContextMenu);
    canvas_->installEventFilter(this);
    buildEdgePanels();

    panelHideTimer_->setSingleShot(true);
    panelHideTimer_->setInterval(900);
    connect(panelHideTimer_, &QTimer::timeout, this, &FullScreenWindow::hideTransientPanels);

    connect(canvas_, &ImageCanvas::customContextMenuRequested, this,
            [this](const QPoint& position) {
                revealAction_->setEnabled(!currentPath().isEmpty());
                contextMenu_->popup(canvas_->mapToGlobal(position));
            });
    connect(canvas_, &ImageCanvas::viewStateChanged, this,
            [this](const ViewState&) { updateStatus(); });
    connect(canvas_, &ImageCanvas::pixelHovered, this,
            [this](const QPoint& pixel, const QColor& color, bool valid) {
                pixelText_ = valid ? QStringLiteral("  x:%1 y:%2  RGBA(%3,%4,%5,%6)")
                                         .arg(pixel.x())
                                         .arg(pixel.y())
                                         .arg(color.red())
                                         .arg(color.green())
                                         .arg(color.blue())
                                         .arg(color.alpha())
                                   : QString{};
                if (valid && canvas_->frame() && canvas_->frame()->rawParameters) {
                    pixelText_ +=
                        QStringLiteral("  ") +
                        RawPlaneAccessor(*canvas_->frame()).pixelDescriptionAtDisplayPixel(pixel);
                }
                updateStatus();
            });
    showIndex(initialIndex);
}

void FullScreenWindow::buildEdgePanels() {
    topPanel_ = makeOverlayPanel(QStringLiteral("fullScreenTopPanel"), canvas_);
    leftPanel_ = makeOverlayPanel(QStringLiteral("fullScreenLeftPanel"), canvas_);
    rightPanel_ = makeOverlayPanel(QStringLiteral("fullScreenRightPanel"), canvas_);
    applyPropertiesOverlayStyle(rightPanel_);
    bottomPanel_ = makeOverlayPanel(QStringLiteral("fullScreenBottomPanel"), canvas_);

    auto* previous = makeTextButton(QStringLiteral("Previous"), topPanel_);
    previous->setObjectName(QStringLiteral("fullScreenPreviousImage"));
    auto* next = makeTextButton(QStringLiteral("Next"), topPanel_);
    next->setObjectName(QStringLiteral("fullScreenNextImage"));
    fileNameLabel_ = new QLabel(topPanel_);
    fileNameLabel_->setObjectName(QStringLiteral("fullScreenFileName"));
    fileNameLabel_->setAlignment(Qt::AlignCenter);
    fileNameLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    positionLabel_ = new QLabel(topPanel_);
    positionLabel_->setObjectName(QStringLiteral("fullScreenPosition"));
    auto* topLayout = new QHBoxLayout(topPanel_);
    topLayout->setContentsMargins(8, 6, 8, 6);
    topLayout->addWidget(previous);
    topLayout->addWidget(fileNameLabel_, 1);
    topLayout->addWidget(positionLabel_);
    topLayout->addWidget(next);
    connect(previous, &QToolButton::clicked, this, [this] { showNeighbor(-1); });
    connect(next, &QToolButton::clicked, this, [this] { showNeighbor(1); });

    auto* fit = makeTextButton(QStringLiteral("Fit"), leftPanel_);
    fit->setObjectName(QStringLiteral("fullScreenFit"));
    auto* actual = makeTextButton(QStringLiteral("100%"), leftPanel_);
    actual->setObjectName(QStringLiteral("fullScreenActualPixels"));
    auto* reveal = makeTextButton(QStringLiteral("Reveal"), leftPanel_);
    reveal->setObjectName(QStringLiteral("fullScreenRevealButton"));
    auto* close = makeTextButton(QStringLiteral("Close"), leftPanel_);
    close->setObjectName(QStringLiteral("fullScreenClose"));
    auto* leftLayout = new QVBoxLayout(leftPanel_);
    leftLayout->setContentsMargins(7, 7, 7, 7);
    leftLayout->addWidget(fit);
    leftLayout->addWidget(actual);
    leftLayout->addWidget(reveal);
    leftLayout->addStretch(1);
    leftLayout->addWidget(close);
    connect(fit, &QToolButton::clicked, canvas_, &ImageCanvas::fitImage);
    connect(actual, &QToolButton::clicked, canvas_, &ImageCanvas::actualPixels);
    connect(reveal, &QToolButton::clicked, this, &FullScreenWindow::revealCurrentFile);
    connect(close, &QToolButton::clicked, this, &QWidget::close);

    statusLabel_ = new QLabel(bottomPanel_);
    statusLabel_->setObjectName(QStringLiteral("fullScreenStatus"));
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* bottomLayout = new QHBoxLayout(bottomPanel_);
    bottomLayout->setContentsMargins(12, 6, 12, 6);
    bottomLayout->addWidget(statusLabel_, 1);

    rightTitleLabel_ = new QLabel(QStringLiteral("Properties"), rightPanel_);
    rightTitleLabel_->setObjectName(QStringLiteral("fullScreenRightTitle"));
    auto* hideRight = makeTextButton(QStringLiteral("×"), rightPanel_);
    hideRight->setObjectName(QStringLiteral("fullScreenHideRightPanel"));
    hideRight->setFixedWidth(34);
    propertiesPanel_ = new ImagePropertiesPanel(rightPanel_);
    propertiesPanel_->setObjectName(QStringLiteral("fullScreenImagePropertiesPanel"));
    auto* rightHeader = new QHBoxLayout;
    rightHeader->addWidget(rightTitleLabel_, 1);
    rightHeader->addWidget(hideRight);
    auto* rightLayout = new QVBoxLayout(rightPanel_);
    rightLayout->setContentsMargins(8, 6, 8, 8);
    rightLayout->addLayout(rightHeader);
    rightLayout->addWidget(propertiesPanel_, 1);
    connect(hideRight, &QToolButton::clicked, this, &FullScreenWindow::hideRightOverlay);
    revealAction_ = contextMenu_->addAction(QStringLiteral("Show in File Manager"));
    revealAction_->setObjectName(QStringLiteral("fullScreenRevealAction"));
    contextMenu_->addSeparator();
    showInformationAction_ = contextMenu_->addAction(QStringLiteral("Show Properties"));
    showInformationAction_->setObjectName(QStringLiteral("fullScreenShowInformationAction"));
    showInformationAction_->setCheckable(true);
    showHistogramAction_ = contextMenu_->addAction(QStringLiteral("Show Histogram"));
    showHistogramAction_->setObjectName(QStringLiteral("fullScreenShowHistogramAction"));
    showHistogramAction_->setCheckable(true);
    contextMenu_->setObjectName(QStringLiteral("fullScreenContextMenu"));
    connect(revealAction_, &QAction::triggered, this, &FullScreenWindow::revealCurrentFile);
    connect(showInformationAction_, &QAction::toggled, this, [this](bool checked) {
        if (checked) {
            const QSignalBlocker blocker(showHistogramAction_);
            showHistogramAction_->setChecked(false);
            setRightOverlay(RightOverlay::Information, true);
        } else if (!showHistogramAction_->isChecked()) {
            hideRightOverlay();
        }
    });
    connect(showHistogramAction_, &QAction::toggled, this, [this](bool checked) {
        if (checked) {
            const QSignalBlocker blocker(showInformationAction_);
            showInformationAction_->setChecked(false);
            setRightOverlay(RightOverlay::Histogram, true);
        } else if (!showInformationAction_->isChecked()) {
            hideRightOverlay();
        }
    });

    for (QWidget* panel :
         std::array<QWidget*, 4>{topPanel_, leftPanel_, rightPanel_, bottomPanel_}) {
        panel->installEventFilter(this);
        for (QWidget* child : panel->findChildren<QWidget*>()) {
            child->installEventFilter(this);
        }
    }
}

bool FullScreenWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == canvas_ && event->type() == QEvent::MouseMove) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const QPointF position = mouse->position();
        QWidget* target = nullptr;
        if (position.y() <= edgeTriggerWidth) {
            target = topPanel_;
        } else if (position.y() >= canvas_->height() - edgeTriggerWidth) {
            target = bottomPanel_;
        } else if (position.x() <= edgeTriggerWidth) {
            target = leftPanel_;
        } else if (position.x() >= canvas_->width() - edgeTriggerWidth) {
            target = rightPanel_;
        }
        if (target) {
            showTransientPanel(target);
        } else if (topPanel_->isVisible() || leftPanel_->isVisible() || bottomPanel_->isVisible() ||
                   (rightPanel_->isVisible() && !rightPanelPinned_)) {
            panelHideTimer_->start();
        }
    } else if (auto* widget = qobject_cast<QWidget*>(watched);
               widget && (widget == topPanel_ || topPanel_->isAncestorOf(widget) ||
                          widget == leftPanel_ || leftPanel_->isAncestorOf(widget) ||
                          widget == rightPanel_ || rightPanel_->isAncestorOf(widget) ||
                          widget == bottomPanel_ || bottomPanel_->isAncestorOf(widget))) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove ||
            event->type() == QEvent::MouseButtonPress) {
            panelHideTimer_->stop();
        } else if (event->type() == QEvent::Leave) {
            panelHideTimer_->start();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void FullScreenWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        close();
        return;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
        showNeighbor(-1);
        return;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        showNeighbor(1);
        return;
    case Qt::Key_F:
        canvas_->fitImage();
        return;
    case Qt::Key_1:
        canvas_->actualPixels();
        return;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

void FullScreenWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    layoutEdgePanels();
}

void FullScreenWindow::showIndex(int index) {
    if (index < 0 || index >= paths_.size()) {
        return;
    }
    index_ = index;
    const quint64 request = ++generation_;
    const QString path = paths_.at(index_);
    fileNameLabel_->setText(QFileInfo(path).fileName());
    fileNameLabel_->setToolTip(path);
    positionLabel_->setText(QStringLiteral("%1 / %2").arg(index_ + 1).arg(paths_.size()));
    statusLabel_->setText(QStringLiteral("Loading %1…").arg(QFileInfo(path).fileName()));
    propertiesPanel_->setFrame({});
    if (const auto raw = loader_->rawParameters(path)) {
        propertiesPanel_->setRawParameters(path, *raw);
    }
    const auto rawParameters = loader_->rawParameters(path);
    const QSize previewSize =
        rawParameters && !rawParameters->isYuv() ? QSize(1280, 800) : QSize(2560, 1600);
    const QPointer<FullScreenWindow> self(this);
    loader_->request(
        request, {path, DecodePurpose::Preview, previewSize},
        [self, request, path](quint64 id, const DecodeResult& result) {
            if (!self || id != self->generation_ || id != request) {
                return;
            }
            if (!result.frame) {
                self->statusLabel_->setText(result.error);
                self->showTransientPanel(self->bottomPanel_);
                return;
            }
            self->canvas_->setFrame(result.frame, true);
            self->propertiesPanel_->setFrame(result.frame);
            self->updateStatus();
            if (request == self->generation_) {
                self->requestFullFrame(path, request);
            }
        },
        3);
}

void FullScreenWindow::requestFullFrame(const QString& path, quint64 generation) {
    const QPointer<FullScreenWindow> self(this);
    loader_->request(
        generation, {path, DecodePurpose::Full, {}},
        [self, generation, path](quint64 id, const DecodeResult& full) {
            if (!self || id != self->generation_ || id != generation ||
                self->paths_.value(self->index_) != path) {
                return;
            }
            if (!full.frame) {
                self->statusLabel_->setText(full.error);
                self->showTransientPanel(self->bottomPanel_);
                return;
            }
            self->canvas_->setFrame(full.frame, false);
            self->propertiesPanel_->setFrame(full.frame);
            self->updateStatus();
        },
        1);
}

void FullScreenWindow::showNeighbor(int delta) { showIndex(index_ + delta); }

void FullScreenWindow::updateStatus() {
    if (!canvas_->frame()) {
        return;
    }
    const auto& frame = *canvas_->frame();
    statusLabel_->setText(
        QStringLiteral("%1  %2×%3  %4%")
            .arg(frame.metadata.fileName)
            .arg(frame.descriptor.size.width())
            .arg(frame.descriptor.size.height())
            .arg(QString::number(canvas_->viewState().pixelsPerImagePixel * 100.0, 'f', 1)) +
        pixelText_);
}

void FullScreenWindow::layoutEdgePanels() {
    if (!canvas_) {
        return;
    }
    const int width = canvas_->width();
    const int height = canvas_->height();
    const int horizontalWidth = std::min(920, std::max(1, width - 32));
    topPanel_->setGeometry((width - horizontalWidth) / 2, 12, horizontalWidth, 54);
    bottomPanel_->setGeometry((width - horizontalWidth) / 2, std::max(12, height - 66),
                              horizontalWidth, 54);
    const int leftHeight = std::min(360, std::max(1, height - 40));
    leftPanel_->setGeometry(12, (height - leftHeight) / 2, 92, leftHeight);
    const int rightWidth = std::min(560, std::max(1, width - 40));
    const int rightHeight = std::min(760, std::max(1, height - 40));
    rightPanel_->setGeometry(std::max(12, width - rightWidth - 14), (height - rightHeight) / 2,
                             rightWidth, rightHeight);
    for (QWidget* panel :
         std::array<QWidget*, 4>{topPanel_, leftPanel_, rightPanel_, bottomPanel_}) {
        panel->raise();
    }
}

void FullScreenWindow::showTransientPanel(QWidget* panel) {
    panelHideTimer_->stop();
    for (QWidget* candidate : std::array<QWidget*, 3>{topPanel_, leftPanel_, bottomPanel_}) {
        if (candidate != panel) {
            candidate->hide();
        }
    }
    if (panel != rightPanel_ && !rightPanelPinned_) {
        rightPanel_->hide();
    }
    panel->show();
    panel->raise();
}

void FullScreenWindow::hideTransientPanels() {
    topPanel_->hide();
    leftPanel_->hide();
    bottomPanel_->hide();
    if (!rightPanelPinned_) {
        rightPanel_->hide();
    }
}

void FullScreenWindow::setRightOverlay(RightOverlay overlay, bool pinned) {
    rightPanelPinned_ = pinned;
    const bool information = overlay == RightOverlay::Information;
    propertiesPanel_->showTab(information ? ImagePropertiesPanel::Tab::Exif
                                          : ImagePropertiesPanel::Tab::Histogram);
    rightTitleLabel_->setText(QStringLiteral("Properties"));
    rightPanel_->show();
    rightPanel_->raise();
    panelHideTimer_->stop();
}

void FullScreenWindow::hideRightOverlay() {
    const QSignalBlocker informationBlocker(showInformationAction_);
    const QSignalBlocker histogramBlocker(showHistogramAction_);
    showInformationAction_->setChecked(false);
    showHistogramAction_->setChecked(false);
    rightPanelPinned_ = false;
    rightPanel_->hide();
}

void FullScreenWindow::revealCurrentFile() {
    const QString path = currentPath();
    if (!path.isEmpty() && !PlatformServices::revealInFileManager(path)) {
        statusLabel_->setText(QStringLiteral("Could not open the system file manager"));
        showTransientPanel(bottomPanel_);
    }
}

QString FullScreenWindow::currentPath() const {
    return index_ >= 0 && index_ < paths_.size() ? paths_.at(index_) : QString{};
}

} // namespace ispview
