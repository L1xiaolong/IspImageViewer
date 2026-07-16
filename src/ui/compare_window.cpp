#include "ui/compare_window.h"

#include "core/comparison_pixel_probe.h"
#include "io/image_loader.h"
#include "render/image_canvas.h"
#include "ui/histogram_panel.h"

#include <QAction>
#include <QActionGroup>
#include <QCursor>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

namespace ispview {
namespace {

QString overlayStyle() {
    return QStringLiteral(
        "QLabel { color: white; background: transparent; padding: 3px; font-weight: 600; }");
}

void applyOutlinedText(QLabel* label) {
    label->setStyleSheet(overlayStyle());
    auto* outline = new QGraphicsDropShadowEffect(label);
    outline->setColor(Qt::black);
    outline->setBlurRadius(4.0);
    outline->setOffset(0.0, 0.0);
    label->setGraphicsEffect(outline);
}

QString cameraText(const ImageMetadata& metadata) {
    if (!metadata.camera) {
        return QStringLiteral("No EXIF camera data");
    }
    const ImageMetadata::Camera& camera = *metadata.camera;
    QStringList values;
    const QString model = QStringLiteral("%1 %2").arg(camera.make, camera.model).trimmed();
    if (!model.isEmpty()) {
        values.append(model);
    }
    if (camera.exposureSeconds > 0.0) {
        values.append(camera.exposureSeconds < 1.0
                          ? QStringLiteral("1/%1 s").arg(qRound(1.0 / camera.exposureSeconds))
                          : QStringLiteral("%1 s").arg(camera.exposureSeconds, 0, 'g', 4));
    }
    if (camera.aperture > 0.0) {
        values.append(QStringLiteral("f/%1").arg(camera.aperture, 0, 'g', 3));
    }
    if (camera.iso > 0) {
        values.append(QStringLiteral("ISO %1").arg(camera.iso));
    }
    if (camera.focalLengthMm > 0.0) {
        values.append(QStringLiteral("%1 mm").arg(camera.focalLengthMm, 0, 'g', 4));
    }
    return values.isEmpty() ? QStringLiteral("No EXIF camera data") : values.join("  •  ");
}

} // namespace

CompareWindow::CompareWindow(ImageLoader* loader, const QStringList& paths, QWidget* parent)
    : QMainWindow(parent), loader_(loader) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("Compare Images — ISP Image Viewer"));
    resize(1440, 900);

    const QStringList boundedPaths = paths.mid(0, 4);
    if (boundedPaths.size() < 2) {
        return;
    }
    frames_.fill({}, boundedPaths.size());
    generations_.fill(0, boundedPaths.size());

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("compareCentral"));
    auto* layout = new QGridLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(2);
    layout->setVerticalSpacing(2);
    central->setStyleSheet(QStringLiteral("QWidget#compareCentral { background-color: white; }"));
    const int columns = boundedPaths.size() == 4 ? 2 : static_cast<int>(boundedPaths.size());
    for (int slot = 0; slot < boundedPaths.size(); ++slot) {
        auto* pane = new QFrame(central);
        pane->setObjectName(QStringLiteral("comparePane%1").arg(slot));
        pane->setStyleSheet(
            QStringLiteral("QFrame#comparePane%1 { background: black; }").arg(slot));
        auto* paneLayout = new QGridLayout(pane);
        paneLayout->setContentsMargins(0, 0, 0, 0);

        auto* canvas = new ImageCanvas(pane);
        canvas->setObjectName(QStringLiteral("compareCanvas%1").arg(slot));
        canvas->setNavigationThumbnailEnabled(true);
        paneLayout->addWidget(canvas, 0, 0);

        auto* informationOverlay = new QWidget(pane);
        informationOverlay->setObjectName(QStringLiteral("compareInformationOverlay%1").arg(slot));
        informationOverlay->setAttribute(Qt::WA_TranslucentBackground);
        informationOverlay->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* informationLayout = new QVBoxLayout(informationOverlay);
        informationLayout->setContentsMargins(0, 0, 0, 0);
        informationLayout->setSpacing(3);
        auto* fileLabel = new QLabel(informationOverlay);
        fileLabel->setObjectName(QStringLiteral("compareFileOverlay%1").arg(slot));
        applyOutlinedText(fileLabel);
        fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        informationLayout->addWidget(fileLabel);

        auto* exifLabel = new QLabel(informationOverlay);
        exifLabel->setObjectName(QStringLiteral("compareExifOverlay%1").arg(slot));
        applyOutlinedText(exifLabel);
        exifLabel->setWordWrap(true);
        exifLabel->setMaximumWidth(520);
        exifLabel->hide();
        informationLayout->addWidget(exifLabel);
        auto* histogram = new HistogramPanel(informationOverlay);
        histogram->setObjectName(QStringLiteral("compareHistogram%1").arg(slot));
        histogram->setCompactLumaOnly(true);
        histogram->setFixedSize(220, 110);
        histogram->hide();
        informationLayout->addWidget(histogram);
        informationLayout->addStretch(1);
        paneLayout->addWidget(informationOverlay, 0, 0, Qt::AlignLeft | Qt::AlignTop);

        auto* pixelLabel = new QLabel(QStringLiteral("Move the pointer over an image"), pane);
        pixelLabel->setObjectName(QStringLiteral("comparePixelOverlay%1").arg(slot));
        applyOutlinedText(pixelLabel);
        pixelLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pixelLabel->hide();
        paneLayout->addWidget(pixelLabel, 0, 0, Qt::AlignRight | Qt::AlignBottom);

        layout->addWidget(pane, slot / columns, slot % columns);
        panes_.append(pane);
        canvases_.append(canvas);
        fileLabels_.append(fileLabel);
        exifLabels_.append(exifLabel);
        histograms_.append(histogram);
        pixelLabels_.append(pixelLabel);
    }
    setCentralWidget(central);

    auto* toolbar = addToolBar(QStringLiteral("Compare"));
    toolbar->setObjectName(QStringLiteral("compareToolbar"));
    toolbar->setMovable(false);
    QAction* sideBySide = toolbar->addAction(QStringLiteral("Side by Side"));
    auto* holdButton = new QToolButton(toolbar);
    holdButton->setObjectName(QStringLiteral("holdComparisonButton"));
    holdButton->setText(QStringLiteral("Hold B over A"));
    holdButton->setToolTip(
        QStringLiteral("Press and hold to replace the left image with the right image"));
    holdButton->setEnabled(boundedPaths.size() == 2);
    toolbar->addWidget(holdButton);
    QAction* verticalSplit = toolbar->addAction(QStringLiteral("Vertical Split"));
    QAction* horizontalSplit = toolbar->addAction(QStringLiteral("Horizontal Split"));
    sideBySide->setObjectName(QStringLiteral("sideBySideAction"));
    verticalSplit->setObjectName(QStringLiteral("verticalSplitAction"));
    horizontalSplit->setObjectName(QStringLiteral("horizontalSplitAction"));
    auto* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    for (QAction* action : {sideBySide, verticalSplit, horizontalSplit}) {
        action->setCheckable(true);
        modeGroup->addAction(action);
    }
    sideBySide->setChecked(true);
    connect(sideBySide, &QAction::triggered, this,
            [this] { setPresentationMode(PresentationMode::SideBySide); });
    connect(verticalSplit, &QAction::triggered, this,
            [this] { setPresentationMode(PresentationMode::VerticalSplit); });
    connect(horizontalSplit, &QAction::triggered, this,
            [this] { setPresentationMode(PresentationMode::HorizontalSplit); });
    connect(holdButton, &QToolButton::pressed, this, [this] { setHoldComparison(true); });
    connect(holdButton, &QToolButton::released, this, [this] { setHoldComparison(false); });

    toolbar->addSeparator();
    QAction* syncAction = toolbar->addAction(QStringLiteral("Sync"));
    syncAction->setObjectName(QStringLiteral("compareSyncAction"));
    syncAction->setCheckable(true);
    syncAction->setChecked(true);
    connect(syncAction, &QAction::toggled, this, [this](bool enabled) { synchronized_ = enabled; });
    QAction* fitAction = toolbar->addAction(QStringLiteral("Fit"));
    connect(fitAction, &QAction::triggered, this, [this] {
        for (ImageCanvas* canvas : canvases_) {
            canvas->fitImage();
        }
    });
    QAction* actualAction = toolbar->addAction(QStringLiteral("100%"));
    connect(actualAction, &QAction::triggered, this, [this] {
        for (ImageCanvas* canvas : canvases_) {
            canvas->actualPixels();
        }
    });
    QAction* screenshotAction = toolbar->addAction(QStringLiteral("Screenshot"));
    screenshotAction->setObjectName(QStringLiteral("compareScreenshotAction"));
    screenshotAction->setToolTip(
        QStringLiteral("Save the currently displayed comparison area as PNG"));
    connect(screenshotAction, &QAction::triggered, this, &CompareWindow::saveScreenshot);

    toolbar->addSeparator();
    fileInfoAction_ = toolbar->addAction(QStringLiteral("File"));
    exifAction_ = toolbar->addAction(QStringLiteral("EXIF"));
    histogramAction_ = toolbar->addAction(QStringLiteral("Histogram"));
    pixelAction_ = toolbar->addAction(QStringLiteral("Pixel Values"));
    fileInfoAction_->setObjectName(QStringLiteral("compareFileInfoAction"));
    exifAction_->setObjectName(QStringLiteral("compareExifAction"));
    histogramAction_->setObjectName(QStringLiteral("compareHistogramAction"));
    pixelAction_->setObjectName(QStringLiteral("comparePixelAction"));
    for (QAction* action : {fileInfoAction_, exifAction_, histogramAction_, pixelAction_}) {
        action->setCheckable(true);
    }
    fileInfoAction_->setChecked(true);
    connect(fileInfoAction_, &QAction::toggled, this, [this](bool visible) {
        for (QLabel* label : fileLabels_) {
            label->setVisible(visible);
        }
    });
    connect(exifAction_, &QAction::toggled, this, [this](bool visible) {
        for (QLabel* label : exifLabels_) {
            label->setVisible(visible);
        }
    });
    connect(histogramAction_, &QAction::toggled, this, [this](bool visible) {
        for (HistogramPanel* histogram : histograms_) {
            histogram->setVisible(visible);
        }
    });
    connect(pixelAction_, &QAction::toggled, this, [this](bool visible) {
        for (QLabel* label : pixelLabels_) {
            label->setVisible(visible);
        }
    });
    exifAction_->setChecked(false);
    histogramAction_->setChecked(false);
    pixelAction_->setChecked(false);

    verticalSplit->setEnabled(boundedPaths.size() == 2);
    horizontalSplit->setEnabled(boundedPaths.size() == 2);
    for (int slot = 0; slot < boundedPaths.size(); ++slot) {
        ImageCanvas* canvas = canvases_.at(slot);
        connect(canvas, &ImageCanvas::viewStateChanged, this,
                [this, canvas](const ViewState& state) { propagate(canvas, state); });
        connect(canvas, &ImageCanvas::pixelHovered, this,
                [this, slot](const QPoint& pixel, const QColor&, bool valid) {
                    if (valid) {
                        updatePixelOverlays(slot, pixel);
                    }
                });
        load(slot, boundedPaths.at(slot));
    }
    statusBar()->hide();
}

CompareWindow::CompareWindow(ImageLoader* loader, const QString& leftPath, const QString& rightPath,
                             QWidget* parent)
    : CompareWindow(loader, QStringList{leftPath, rightPath}, parent) {}

void CompareWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        setHoldComparison(false);
    }
}

void CompareWindow::keyPressEvent(QKeyEvent* event) {
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_B && modifiers == Qt::NoModifier) {
        if (!event->isAutoRepeat()) {
            setHoldComparison(true);
        }
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void CompareWindow::keyReleaseEvent(QKeyEvent* event) {
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_B && modifiers == Qt::NoModifier) {
        if (!event->isAutoRepeat()) {
            setHoldComparison(false);
        }
        event->accept();
        return;
    }
    QMainWindow::keyReleaseEvent(event);
}

void CompareWindow::load(int slot, const QString& path) {
    const quint64 generation = ++generations_[slot];
    const QPointer<CompareWindow> self(this);
    loader_->request(
        generation, {path, DecodePurpose::Preview, QSize(2560, 1600)},
        [self, slot, path, generation](quint64 id, const DecodeResult& preview) {
            if (!self || id != generation || self->generations_.at(slot) != generation) {
                return;
            }
            if (!preview.frame) {
                self->fileLabels_.at(slot)->setText(preview.error);
                return;
            }
            self->frames_[slot] = preview.frame;
            self->canvases_.at(slot)->setFrame(preview.frame, true);
            self->histograms_.at(slot)->setFrame(preview.frame);
            self->updateInformationOverlay(slot);
            if (self->frames_.size() == 2 && self->frames_.at(0) && self->frames_.at(1)) {
                self->canvases_.at(0)->setComparisonFrame(self->frames_.at(1));
            }
            self->loader_->request(
                generation, {path, DecodePurpose::Full, {}},
                [self, slot, generation](quint64 id, const DecodeResult& full) {
                    if (!self || id != generation || self->generations_.at(slot) != generation ||
                        !full.frame) {
                        return;
                    }
                    self->frames_[slot] = full.frame;
                    if (!(self->holdActive_ && slot == 0)) {
                        self->canvases_.at(slot)->setFrame(full.frame, false);
                    }
                    self->histograms_.at(slot)->setFrame(full.frame);
                    self->updateInformationOverlay(slot);
                    if (self->frames_.size() == 2 && self->frames_.at(1)) {
                        self->canvases_.at(0)->setComparisonFrame(self->frames_.at(1));
                    }
                    if (self->holdActive_) {
                        self->setHoldComparison(true);
                    }
                },
                1);
        },
        3);
}

void CompareWindow::updateInformationOverlay(int slot) {
    const ImageFramePtr& frame = frames_.at(slot);
    if (!frame) {
        return;
    }
    fileLabels_.at(slot)->setText(frame->metadata.fileName);
    fileLabels_.at(slot)->setToolTip(frame->metadata.path);
    exifLabels_.at(slot)->setText(cameraText(frame->metadata));
}

void CompareWindow::updatePixelOverlays(int sourceSlot, const QPoint& pixel) {
    const QSize sourceSize = canvases_.at(sourceSlot)->logicalImageSize();
    const QPointF normalized = ComparisonPixelProbe::normalizedPixelCenter(pixel, sourceSize);
    for (int slot = 0; slot < frames_.size(); ++slot) {
        if (!frames_.at(slot)) {
            continue;
        }
        const ComparisonPixelSample sample =
            ComparisonPixelProbe::sample(*frames_.at(slot), normalized);
        pixelLabels_.at(slot)->setText(
            sample.valid ? QStringLiteral("(%1, %2)  %3  •  %4")
                               .arg(sample.displayPixel.x())
                               .arg(sample.displayPixel.y())
                               .arg(sample.sourceValueText(), sample.displayValueText())
                         : QStringLiteral("Outside image"));
    }
}

void CompareWindow::propagate(ImageCanvas* source, const ViewState& state) {
    if (!synchronized_ || applyingSync_) {
        return;
    }
    applyingSync_ = true;
    for (ImageCanvas* target : canvases_) {
        if (target != source && target->frame()) {
            target->setViewState(syncGroup_.synchronizedState(state, target->viewState()), false);
        }
    }
    applyingSync_ = false;
}

void CompareWindow::setPresentationMode(PresentationMode mode) {
    presentationMode_ = mode;
    setHoldComparison(false);
    if (mode == PresentationMode::SideBySide) {
        canvases_.at(0)->setCompareMode(ImageCompareMode::Single);
        for (QWidget* pane : panes_) {
            pane->show();
        }
        return;
    }
    if (canvases_.size() != 2) {
        return;
    }
    panes_.at(1)->hide();
    canvases_.at(0)->setComparisonFrame(frames_.value(1));
    canvases_.at(0)->setCompareAmount(0.5F);
    canvases_.at(0)->setCompareMode(mode == PresentationMode::VerticalSplit
                                        ? ImageCompareMode::VerticalSplit
                                        : ImageCompareMode::HorizontalSplit);
}

void CompareWindow::setHoldComparison(bool active) {
    if (frames_.size() != 2 || !frames_.at(0) || !frames_.at(1)) {
        return;
    }
    holdActive_ = active;
    if (presentationMode_ != PresentationMode::SideBySide) {
        return;
    }
    const ViewState state = canvases_.at(0)->viewState();
    canvases_.at(0)->setFrame(active ? frames_.at(1) : frames_.at(0), false);
    canvases_.at(0)->setViewState(state, false);
}

void CompareWindow::saveScreenshot() {
    QWidget* comparisonArea = centralWidget();
    if (!comparisonArea) {
        return;
    }

    const QPixmap screenshot = comparisonArea->grab();
    if (screenshot.isNull()) {
        QMessageBox::warning(this, QStringLiteral("Screenshot"),
                             QStringLiteral("Unable to capture the comparison area."));
        return;
    }

    const QString defaultFileName =
        QStringLiteral("screen_shot_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    QString initialDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (initialDirectory.isEmpty()) {
        initialDirectory = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }

    QFileDialog dialog(this, QStringLiteral("Save Comparison Screenshot"), initialDirectory,
                       QStringLiteral("PNG Images (*.png)"));
    dialog.setObjectName(QStringLiteral("compareScreenshotDialog"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(QStringLiteral("png"));
    dialog.selectFile(defaultFileName);
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        dialog.setOption(QFileDialog::DontUseNativeDialog);
    }
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return;
    }
    const QString filePath = dialog.selectedFiles().constFirst();

    if (!screenshot.save(filePath, "PNG")) {
        QMessageBox::warning(this, QStringLiteral("Screenshot"),
                             QStringLiteral("Unable to save the screenshot:\n%1").arg(filePath));
        return;
    }

    emit screenshotSaved(filePath);
    QToolTip::showText(QCursor::pos(), QStringLiteral("Screenshot saved:\n%1").arg(filePath), this);
}

} // namespace ispview
