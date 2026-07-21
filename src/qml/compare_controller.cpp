#include "qml/compare_controller.h"
#include "qml/qml_image_canvas.h"

#include "core/comparison_pixel_probe.h"
#include "core/display_histogram.h"
#include "core/raw_plane_access.h"
#include "core/raw_plane_histogram.h"
#include "io/image_loader.h"
#include "io/raw_preset_store.h"

#include <QFileInfo>
#include <QPointer>
#include <QDateTime>
#include <QFileDialog>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QThreadPool>
#include <QLocale>

#include <algorithm>

namespace ispview {
namespace {

template <typename Bins>
double medianFor(const Bins& bins, quint64 samples) {
    if (samples == 0) return 0.0;
    const quint64 firstTarget = (samples - 1) / 2;
    const quint64 secondTarget = samples / 2;
    quint64 cumulative = 0;
    int first = -1;
    int second = -1;
    for (qsizetype index = 0; index < static_cast<qsizetype>(bins.size()); ++index) {
        cumulative += bins.at(static_cast<typename Bins::size_type>(index));
        if (first < 0 && cumulative > firstTarget) first = static_cast<int>(index);
        if (cumulative > secondTarget) { second = static_cast<int>(index); break; }
    }
    return (std::max(0, first) + std::max(0, second)) / 2.0;
}

QVariantList binsFor(const auto& bins) {
    QVariantList values;
    values.reserve(static_cast<qsizetype>(bins.size()));
    for (const auto value : bins) values.append(QVariant::fromValue<qulonglong>(value));
    return values;
}

QVariantMap histogramChannel(const QString& name, const QString& color, const auto& bins,
                            quint64 samples, double mean, double deviation, int minimum, int maximum) {
    return {{QStringLiteral("name"), name}, {QStringLiteral("color"), color},
            {QStringLiteral("bins"), binsFor(bins)}, {QStringLiteral("mean"), mean},
            {QStringLiteral("variance"), deviation * deviation}, {QStringLiteral("min"), minimum},
            {QStringLiteral("max"), maximum}, {QStringLiteral("median"), medianFor(bins, samples)}};
}

QVariantMap displayHistogramMap(const DisplayHistogram& histogram) {
    if (!histogram.isValid()) return {{QStringLiteral("valid"), false}, {QStringLiteral("summary"), QStringLiteral("Display histogram unavailable")}};
    QVariantList channels;
    const quint64 samples = static_cast<quint64>(std::max<qint64>(0, histogram.sampledPixelCount));
    channels << histogramChannel(QStringLiteral("Luma"), QStringLiteral("#25303A"), histogram.luma.bins, samples, histogram.luma.mean, histogram.luma.standardDeviation, histogram.luma.minimum, histogram.luma.maximum)
             << histogramChannel(QStringLiteral("Red"), QStringLiteral("#CE5B5B"), histogram.red.bins, samples, histogram.red.mean, histogram.red.standardDeviation, histogram.red.minimum, histogram.red.maximum)
             << histogramChannel(QStringLiteral("Green"), QStringLiteral("#43A86A"), histogram.green.bins, samples, histogram.green.mean, histogram.green.standardDeviation, histogram.green.minimum, histogram.green.maximum)
             << histogramChannel(QStringLiteral("Blue"), QStringLiteral("#4776C9"), histogram.blue.bins, samples, histogram.blue.mean, histogram.blue.standardDeviation, histogram.blue.minimum, histogram.blue.maximum);
    QString summary = QStringLiteral("Full display · %1×%2 · %3 samples")
        .arg(histogram.analyzedSize.width()).arg(histogram.analyzedSize.height())
        .arg(QLocale().toString(histogram.sampledPixelCount));
    if (histogram.usesDisplayProxy()) summary += QStringLiteral(" · proxy for %1×%2").arg(histogram.logicalSize.width()).arg(histogram.logicalSize.height());
    if (histogram.isSubsampled()) summary += QStringLiteral(" of %1").arg(QLocale().toString(histogram.availablePixelCount));
    return {{QStringLiteral("valid"), true}, {QStringLiteral("summary"), summary}, {QStringLiteral("maximumValue"), 255}, {QStringLiteral("channels"), channels}};
}

QString rawColor(RawHistogramChannelId id) {
    switch (id) {
    case RawHistogramChannelId::Y: return QStringLiteral("#25303A");
    case RawHistogramChannelId::U: return QStringLiteral("#45B7C4");
    case RawHistogramChannelId::V: return QStringLiteral("#C65AAE");
    case RawHistogramChannelId::Red: return QStringLiteral("#CE5B5B");
    case RawHistogramChannelId::GreenRedRow: return QStringLiteral("#43A86A");
    case RawHistogramChannelId::GreenBlueRow: return QStringLiteral("#2E8B57");
    case RawHistogramChannelId::Blue: return QStringLiteral("#4776C9");
    }
    return QStringLiteral("#69747D");
}

QVariantMap rawHistogramMap(const RawPlaneHistogram& histogram) {
    if (!histogram.isValid()) return {{QStringLiteral("valid"), false}, {QStringLiteral("summary"), QStringLiteral("Source-plane histogram unavailable")}};
    QVariantList channels;
    for (const RawHistogramChannel& channel : histogram.channels) {
        channels << histogramChannel(rawHistogramChannelName(channel.id), rawColor(channel.id), channel.bins,
                                     static_cast<quint64>(std::max<qint64>(0, channel.sampledSampleCount)), channel.mean, channel.standardDeviation,
                                     channel.minimum, channel.maximum);
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("summary"), QStringLiteral("Source %1 · %2-bit · %3 channels")
                 .arg(histogram.domain == RawHistogramDomain::Yuv ? QStringLiteral("YUV") : QStringLiteral("Bayer"))
                 .arg(histogram.validBits).arg(channels.size())},
            {QStringLiteral("maximumValue"), histogram.maximumValue}, {QStringLiteral("channels"), channels}};
}

QSize logicalFrameSize(const ImageFramePtr& frame) {
    if (!frame) return {};
    const RawPlaneAccessor accessor(*frame);
    return accessor.isValid() ? accessor.displaySize() : frame->descriptor.size;
}

} // namespace

CompareController::CompareController(ImageLoader* loader, QObject* parent)
    : QObject(parent), loader_(loader) {}

ImageFramePtr CompareController::frame(int slot) const {
    return slot >= 0 && slot < frames_.size() ? frames_.at(slot) : ImageFramePtr{};
}

QString CompareController::fileText(int slot) const {
    const auto value = frame(slot);
    return value ? value->metadata.fileName : errors_.value(slot);
}

QString CompareController::cameraText(int slot) const {
    const auto value = frame(slot);
    if (!value || !value->metadata.camera) return QStringLiteral("No EXIF camera data");
    const auto& camera = *value->metadata.camera;
    QStringList parts;
    const QString model = QStringLiteral("%1 %2").arg(camera.make, camera.model).trimmed();
    if (!model.isEmpty()) parts.append(model);
    if (camera.exposureSeconds > 0.0) parts.append(camera.exposureSeconds < 1.0
        ? QStringLiteral("1/%1 s").arg(qRound(1.0 / camera.exposureSeconds))
        : QStringLiteral("%1 s").arg(camera.exposureSeconds, 0, 'g', 4));
    if (camera.aperture > 0.0) parts.append(QStringLiteral("f/%1").arg(camera.aperture, 0, 'g', 3));
    if (camera.iso > 0) parts.append(QStringLiteral("ISO %1").arg(camera.iso));
    if (camera.focalLengthMm > 0.0) parts.append(QStringLiteral("%1 mm").arg(camera.focalLengthMm, 0, 'g', 4));
    return parts.isEmpty() ? QStringLiteral("No EXIF camera data") : parts.join(QStringLiteral("  •  "));
}

void CompareController::setPaths(const QStringList& requested) {
    const QStringList bounded = requested.mid(0, 4);
    if (bounded == paths_) return;
    paths_ = bounded;
    frames_.fill({}, paths_.size());
    errors_.fill({}, paths_.size());
    generations_.fill(0, paths_.size());
    histogramGenerations_.fill(0, paths_.size() * 2);
    displayHistograms_.fill({}, paths_.size());
    rawHistograms_.fill({}, paths_.size());
    holdCandidate_ = false;
    emit pathsChanged();
    emit holdCandidateChanged();
    refreshCanvas(-1, true);
    for (int slot = 0; slot < paths_.size(); ++slot) requestFrame(slot, paths_.at(slot));
}

void CompareController::requestFrame(int slot, const QString& path) {
    if (!loader_) return;
    std::optional<RawImageParameters> rawParameters = loader_->rawParameters(path);
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (!rawParameters && (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv"))) {
        rawParameters = RawPresetStore::loadForFile(path);
        if (!rawParameters) {
            const RawImageParameters inferred = RawPresetStore::inferFromFileName(path);
            if (inferred.size.isValid()) rawParameters = inferred;
        }
        if (rawParameters) loader_->setRawParameters(path, *rawParameters);
    }
    const quint64 generation = ++generations_[slot];
    const QPointer<CompareController> self(this);
    loader_->request(generation, {path, DecodePurpose::Preview, QSize(2560, 1600), rawParameters},
        [self, slot, path, generation, rawParameters](quint64 id, const DecodeResult& result) {
            if (!self || id != generation || self->generations_.at(slot) != generation) return;
            if (!result.frame) {
                self->errors_[slot] = result.error;
                emit self->loadFailed(slot, result.error);
                ++self->revision_;
                emit self->revisionChanged();
                return;
            }
            self->errors_[slot].clear();
            self->frames_[slot] = result.frame;
            self->refreshCanvas(slot, true);
            self->clearHistograms(slot);
            emit self->frameChanged(slot, false);
            ++self->revision_;
            emit self->revisionChanged();
            self->loader_->request(generation, {path, DecodePurpose::Full, {}, rawParameters},
                [self, slot, generation](quint64 fullId, const DecodeResult& full) {
                    if (!self || fullId != generation || self->generations_.at(slot) != generation || !full.frame) return;
                    self->frames_[slot] = full.frame;
                    self->refreshCanvas(slot, false);
                    self->clearHistograms(slot);
                    emit self->frameChanged(slot, true);
                    ++self->revision_;
                    emit self->revisionChanged();
                }, 1);
        }, 3);
}

void CompareController::setPresentationMode(int mode) {
    mode = std::clamp(mode, 0, 2); if (presentationMode_ == mode) return;
    setHoldCandidate(false);
    presentationMode_ = mode;
    if (canvas_) canvas_->setPresentationMode(mode);
    emit presentationModeChanged();
}
void CompareController::setSplitAmount(qreal amount) {
    amount = std::clamp(amount, qreal(0), qreal(1)); if (qFuzzyCompare(splitAmount_, amount)) return;
    splitAmount_ = amount;
    if (canvas_) canvas_->setCompareAmount(amount);
    emit splitAmountChanged();
}
void CompareController::setSynchronized(bool enabled) {
    if (synchronized_ == enabled) return;
    synchronized_ = enabled;
    if (canvas_) canvas_->setSynchronized(enabled);
    emit synchronizedChanged();
}
void CompareController::setHoldCandidate(bool active) {
    if (frames_.size() != 2 || !frames_.value(0) || !frames_.value(1) || presentationMode_ != 0)
        active = false;
    if (holdCandidate_ == active) return;
    holdCandidate_ = active;
    applyHoldFrame();
    emit holdCandidateChanged();
}

void CompareController::applyHoldFrame() {
    refreshCanvas();
}

void CompareController::fitAll() {
    if (canvas_) canvas_->fitAll();
}

void CompareController::actualPixelsAll() {
    if (canvas_) canvas_->actualPixelsAll();
}

void CompareController::attachCanvas(QObject* object) {
    auto* canvas = qobject_cast<QmlImageCanvas*>(object);
    if (!canvas) return;
    canvas_ = canvas;
    refreshCanvas(-1, true);
}

void CompareController::refreshCanvas(int changedSlot, bool resetChangedView) {
    if (!canvas_) return;
    QVector<ImageFramePtr> displayedFrames = frames_;
    if (holdCandidate_ && displayedFrames.size() == 2 && displayedFrames.value(1))
        displayedFrames[0] = displayedFrames.at(1);
    canvas_->setFrames(displayedFrames, changedSlot, resetChangedView);
    canvas_->setPresentationMode(presentationMode_);
    canvas_->setCompareAmount(splitAmount_);
    canvas_->setSynchronized(synchronized_);
}

QVariantList CompareController::pixelTexts(int sourceSlot, int x, int y) const {
    const auto source = frame(sourceSlot);
    const QSize sourceSize = logicalFrameSize(source);
    if (!source || sourceSize.isEmpty()) return {};
    const QPointF normalized = ComparisonPixelProbe::normalizedPixelCenter({x, y}, sourceSize);
    QVariantList values;
    values.reserve(frames_.size());
    for (const auto& candidate : frames_) {
        if (!candidate) {
            values.append(QString{});
            continue;
        }
        const auto sample = ComparisonPixelProbe::sample(*candidate, normalized);
        values.append(sample.valid ? QStringLiteral("(%1, %2)  %3  •  %4").arg(sample.displayPixel.x()).arg(sample.displayPixel.y()).arg(sample.sourceValueText(), sample.displayValueText()) : QStringLiteral("Outside image"));
    }
    return values;
}

QString CompareController::chooseScreenshotPath() {
    QString directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (directory.isEmpty()) directory = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QFileDialog dialog(nullptr, QStringLiteral("Save Comparison Screenshot"), directory,
                       QStringLiteral("PNG Images (*.png)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(QStringLiteral("png"));
    dialog.selectFile(QStringLiteral("screen_shot_%1.png").arg(QDateTime::currentMSecsSinceEpoch()));
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) dialog.setOption(QFileDialog::DontUseNativeDialog);
    return dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty() ? dialog.selectedFiles().constFirst() : QString{};
}

void CompareController::requestHistogram(int slot, int source) {
    if (slot < 0 || slot >= frames_.size() || source < 0 || source > 1 || !frames_.value(slot)) return;
    const int generationIndex = slot * 2 + source;
    const quint64 generation = ++histogramGenerations_[generationIndex];
    const ImageFramePtr frame = frames_.at(slot);
    const QPointer<CompareController> self(this);
    QThreadPool::globalInstance()->start([self, frame, slot, source, generation] {
        const QVariantMap result = source == 0 ? displayHistogramMap(DisplayHistogramAnalyzer::analyze(*frame))
                                               : rawHistogramMap(RawPlaneHistogramAnalyzer::analyze(*frame));
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, slot, source, generation, result] {
            if (self) self->completeHistogram(slot, source, generation, result);
        }, Qt::QueuedConnection);
    });
}

QVariantMap CompareController::histogram(int slot, int source) const {
    if (slot < 0 || slot >= frames_.size() || source < 0 || source > 1) return {};
    return source == 0 ? displayHistograms_.value(slot) : rawHistograms_.value(slot);
}

void CompareController::completeHistogram(int slot, int source, quint64 generation, QVariantMap value) {
    const int generationIndex = slot * 2 + source;
    if (slot < 0 || generationIndex >= histogramGenerations_.size() || histogramGenerations_.at(generationIndex) != generation) return;
    if (source == 0) displayHistograms_[slot] = std::move(value); else rawHistograms_[slot] = std::move(value);
    ++histogramRevision_;
    emit histogramChanged(slot, source);
    emit histogramRevisionChanged();
}

void CompareController::clearHistograms(int slot) {
    if (slot < 0 || slot >= frames_.size()) return;
    ++histogramGenerations_[slot * 2];
    ++histogramGenerations_[slot * 2 + 1];
    displayHistograms_[slot] = {};
    rawHistograms_[slot] = {};
    ++histogramRevision_;
    emit histogramChanged(slot, 0);
    emit histogramChanged(slot, 1);
    emit histogramRevisionChanged();
}

} // namespace ispview
