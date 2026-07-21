#include "ui/full_screen_window.h"

#include "core/raw_plane_access.h"
#include "io/image_loader.h"
#include "io/single_file_rename.h"
#include "platform/platform_services.h"
#include "render/image_canvas.h"
#include "ui/file_clipboard.h"
#include "ui/image_properties_panel.h"
#include "ui/trash_confirmation.h"

#include <QAction>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
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
    // Match the main-window inspection card so the same data has the same visual language in
    // both browse and full-screen modes.
    panel->setStyleSheet(QStringLiteral(
        "QFrame#fullScreenRightPanel { background: #F7F9FA; border: 1px solid #D7DEE3; "
        "border-radius: 10px; color: #33414B; }"
        "QFrame#fullScreenRightPanel QLabel { color: #33414B; }"
        "QFrame#fullScreenRightPanel QTreeWidget, QFrame#fullScreenRightPanel QTableWidget, "
        "QFrame#fullScreenRightPanel QComboBox, QFrame#fullScreenRightPanel QSpinBox, "
        "QFrame#fullScreenRightPanel QDoubleSpinBox { background: #FCFDFC; border: 1px solid #D7DEE3; "
        "border-radius: 6px; color: #33414B; selection-background-color: #E8F0F4; }"
        "QFrame#fullScreenRightPanel QTreeWidget::item { padding: 4px 6px; }"
        "QFrame#fullScreenRightPanel QHeaderView::section { background: #F1F4F5; color: #71808A; "
        "border: none; border-bottom: 1px solid #D7DEE3; padding: 6px; font-weight: 600; }"
        "QFrame#fullScreenRightPanel QTabWidget::pane { background: #FCFDFC; border: 1px solid #D7DEE3; "
        "border-radius: 8px; top: -1px; }"
        "QFrame#fullScreenRightPanel QTabBar::tab { background: transparent; color: #71808A; "
        "padding: 8px 12px; margin-right: 3px; }"
        "QFrame#fullScreenRightPanel QTabBar::tab:selected { color: #2E5269; border-bottom: 2px solid #6C8799; }"
        "QFrame#fullScreenRightPanel QToolButton { color: #33414B; background: #FCFDFC; "
        "padding: 6px 10px; border: 1px solid #C9D4DA; border-radius: 6px; }"
        "QFrame#fullScreenRightPanel QToolButton:hover { background: #EAF0F4; border-color: #9EB2BE; }"));
}

void applyContextMenuStyle(QMenu* menu) {
    menu->setStyleSheet(QStringLiteral(
        "QMenu { background: #FCFDFC; border: 1px solid #D7DEE3; border-radius: 8px; padding: 6px; }"
        "QMenu::item { color: #33414B; min-width: 200px; padding: 8px 28px 8px 10px; "
        "border-radius: 5px; margin: 0px; }"
        "QMenu::item:selected { background: #EAF0F4; }"
        "QMenu::item:disabled { color: #A9B2B8; }"
        "QMenu::separator { height: 1px; background: #E1E6E9; margin: 4px 8px; }"));
}

} // namespace

FullScreenWindow::FullScreenWindow(ImageLoader* loader, QStringList paths, int initialIndex,
                                   QWidget* parent)
    : QMainWindow(parent), loader_(loader), canvas_(new ImageCanvas(this)), topPanel_(nullptr),
      rightPanel_(nullptr), bottomPanel_(nullptr), fileNameLabel_(nullptr),
      positionLabel_(nullptr), statusLabel_(nullptr), rightTitleLabel_(nullptr),
      propertiesPanel_(nullptr), contextMenu_(new QMenu(this)), revealAction_(nullptr),
      showInformationAction_(nullptr), panelHideTimer_(new QTimer(this)),
      bottomHideTimer_(new QTimer(this)), paths_(std::move(paths)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setCentralWidget(canvas_);
    canvas_->setNavigationThumbnailEnabled(true);
    canvas_->setContextMenuPolicy(Qt::CustomContextMenu);
    canvas_->installEventFilter(this);
    buildEdgePanels();

    panelHideTimer_->setSingleShot(true);
    panelHideTimer_->setInterval(650);
    connect(panelHideTimer_, &QTimer::timeout, this, &FullScreenWindow::hideTransientPanels);
    bottomHideTimer_->setSingleShot(true);
    bottomHideTimer_->setInterval(5'000);
    connect(bottomHideTimer_, &QTimer::timeout, this, &FullScreenWindow::hideBottomPanel);

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

    statusLabel_ = new QLabel(bottomPanel_);
    statusLabel_->setObjectName(QStringLiteral("fullScreenStatus"));
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* bottomLayout = new QHBoxLayout(bottomPanel_);
    bottomLayout->setContentsMargins(12, 6, 12, 6);
    bottomLayout->addWidget(statusLabel_, 1);

    rightTitleLabel_ = new QLabel(QStringLiteral("IMAGE INSPECTION"), rightPanel_);
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
    rightLayout->setContentsMargins(14, 12, 14, 14);
    rightLayout->setSpacing(10);
    rightLayout->addLayout(rightHeader);
    rightLayout->addWidget(propertiesPanel_, 1);
    connect(hideRight, &QToolButton::clicked, this, &FullScreenWindow::hideRightOverlay);
    applyContextMenuStyle(contextMenu_);
    auto* cutAction = contextMenu_->addAction(QStringLiteral("Cut"));
    auto* copyAction = contextMenu_->addAction(QStringLiteral("Copy"));
    auto* renameAction = contextMenu_->addAction(QStringLiteral("Rename…"));
    auto* trashAction = contextMenu_->addAction(QStringLiteral("Move to Trash"));
    contextMenu_->addSeparator();
    revealAction_ = contextMenu_->addAction(
        QStringLiteral("Reveal in Finder / Explorer"));
    revealAction_->setObjectName(QStringLiteral("fullScreenRevealAction"));
    contextMenu_->addSeparator();
    auto* displayMenu = contextMenu_->addMenu(QStringLiteral("Display"));
    applyContextMenuStyle(displayMenu);
    auto* actualPixelsAction = displayMenu->addAction(QStringLiteral("1:1"));
    auto* fitAction = displayMenu->addAction(QStringLiteral("Fit"));
    contextMenu_->addSeparator();
    showInformationAction_ = contextMenu_->addAction(QStringLiteral("Properties"));
    showInformationAction_->setObjectName(QStringLiteral("fullScreenShowInformationAction"));
    contextMenu_->setObjectName(QStringLiteral("fullScreenContextMenu"));
    connect(cutAction, &QAction::triggered, this,
            [this] { FileClipboard::setPaths({currentPath()}, true); });
    connect(copyAction, &QAction::triggered, this,
            [this] { FileClipboard::setPaths({currentPath()}, false); });
    connect(renameAction, &QAction::triggered, this, &FullScreenWindow::renameCurrentFile);
    connect(trashAction, &QAction::triggered, this, &FullScreenWindow::moveCurrentFileToTrash);
    connect(revealAction_, &QAction::triggered, this, &FullScreenWindow::revealCurrentFile);
    connect(actualPixelsAction, &QAction::triggered, canvas_, &ImageCanvas::actualPixels);
    connect(fitAction, &QAction::triggered, canvas_, &ImageCanvas::fitImage);
    connect(showInformationAction_, &QAction::triggered, this,
            &FullScreenWindow::showPropertiesOverlay);

    for (QWidget* panel : std::array<QWidget*, 3>{topPanel_, rightPanel_, bottomPanel_}) {
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
        } else if (position.x() >= canvas_->width() - edgeTriggerWidth) {
            target = rightPanel_;
        }
        if (target) {
            showTransientPanel(target);
        } else if (topPanel_->isVisible() || rightPanel_->isVisible()) {
            panelHideTimer_->start();
            if (bottomPanel_->isVisible()) {
                bottomHideTimer_->start();
            }
        } else if (bottomPanel_->isVisible()) {
            bottomHideTimer_->start();
        }
    } else if (auto* widget = qobject_cast<QWidget*>(watched);
               widget && (widget == topPanel_ || topPanel_->isAncestorOf(widget) ||
                          widget == rightPanel_ || rightPanel_->isAncestorOf(widget) ||
                          widget == bottomPanel_ || bottomPanel_->isAncestorOf(widget))) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove ||
            event->type() == QEvent::MouseButtonPress) {
            if (widget == bottomPanel_ || bottomPanel_->isAncestorOf(widget)) {
                bottomHideTimer_->stop();
            } else {
                panelHideTimer_->stop();
            }
        } else if (event->type() == QEvent::Leave) {
            if (widget == bottomPanel_ || bottomPanel_->isAncestorOf(widget)) {
                bottomHideTimer_->start();
            } else {
                panelHideTimer_->start();
            }
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
    statusLabel_->setText(pixelText_.trimmed());
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
    const int rightWidth = std::min(600, std::max(1, width - 40));
    const int rightHeight = std::min(760, std::max(1, height - 40));
    rightPanel_->setGeometry(std::max(12, width - rightWidth - 14), (height - rightHeight) / 2,
                             rightWidth, rightHeight);
    for (QWidget* panel : std::array<QWidget*, 3>{topPanel_, rightPanel_, bottomPanel_}) {
        panel->raise();
    }
}

void FullScreenWindow::showTransientPanel(QWidget* panel) {
    if (panel == bottomPanel_) {
        bottomHideTimer_->stop();
    } else {
        panelHideTimer_->stop();
    }
    panel->show();
    panel->raise();
}

void FullScreenWindow::hideTransientPanels() {
    topPanel_->hide();
    rightPanel_->hide();
}

void FullScreenWindow::hideBottomPanel() { bottomPanel_->hide(); }

void FullScreenWindow::showPropertiesOverlay() {
    propertiesPanel_->showTab(ImagePropertiesPanel::Tab::Exif);
    rightTitleLabel_->setText(QStringLiteral("IMAGE INSPECTION"));
    rightPanel_->show();
    rightPanel_->raise();
    panelHideTimer_->stop();
}

void FullScreenWindow::hideRightOverlay() {
    rightPanel_->hide();
}

void FullScreenWindow::renameCurrentFile() {
    const QString sourcePath = currentPath();
    const QFileInfo source(sourcePath);
    if (sourcePath.isEmpty() || !source.exists()) {
        return;
    }
    bool accepted = false;
    const QString newName = QInputDialog::getText(this, QStringLiteral("Rename"),
                                                  QStringLiteral("Name"), QLineEdit::Normal,
                                                  source.fileName(), &accepted);
    if (!accepted || newName == source.fileName()) {
        return;
    }
    const QString destination = source.dir().filePath(newName);
    QString error;
    if (!SingleFileRename::execute(sourcePath, destination, &error)) {
        QMessageBox::warning(this, QStringLiteral("Rename"), error);
        return;
    }
    paths_[index_] = destination;
    showIndex(index_);
}

void FullScreenWindow::moveCurrentFileToTrash() {
    const QString path = currentPath();
    if (path.isEmpty() || !TrashConfirmation::request(this, 1)) {
        return;
    }
    if (!QFile::moveToTrash(path)) {
        QMessageBox::warning(this, QStringLiteral("Move to Trash"),
                             QStringLiteral("The file could not be moved to the system Trash."));
        return;
    }
    paths_.removeAt(index_);
    if (paths_.isEmpty()) {
        close();
        return;
    }
    showIndex(std::min(index_, static_cast<int>(paths_.size()) - 1));
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
