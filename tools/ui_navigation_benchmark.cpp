#include "core/image_types.h"
#include "core/raw_image_parameters.h"
#include "io/default_image_decoder.h"
#include "io/directory_scanner.h"
#include "io/raw_image_decoder.h"
#include "io/raw_preset_store.h"
#include "raw_candidate_options.h"
#include "render/image_canvas.h"
#include "ui/histogram_panel.h"
#include "ui/main_window.h"
#include "ui/thumbnail_model.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QListView>
#include <QMouseEvent>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

namespace ispview {
namespace {

constexpr QSize kDefaultFrameSize{3840, 2160};
constexpr QSize kLargeFrameSize{8000, 6000};
constexpr int kFrameCount = 3;
constexpr int kMeasuredRuns = 3;
constexpr int kTimeoutMilliseconds = 30'000;

struct NavigationMeasurement {
    qint64 firstSubmitNanoseconds = 0;
    qint64 fullSubmitNanoseconds = 0;
    qsizetype fullFrameBytes = 0;
};

struct MedianMeasurement {
    double firstSubmitMilliseconds = 0.0;
    double fullSubmitMilliseconds = 0.0;
    double fullFrameMiB = 0.0;
};

struct EncodedNavigationMeasurement {
    QString fileName;
    QSize previewSize;
    QSize fullSize;
    qint64 firstSubmitNanoseconds = 0;
    qint64 fullSubmitNanoseconds = 0;
};

struct RawNavigationMeasurement {
    QString fileName;
    QSize previewSize;
    QSize sourceSize;
    QSize fallbackSize;
    qint64 firstSubmitNanoseconds = 0;
    qint64 fullSubmitNanoseconds = 0;
    qsizetype fullFrameBytes = 0;
};

bool nativeSurfaceIsAvailable() {
    if (!QGuiApplication::primaryScreen()) {
        return false;
    }
    const QString platform = QGuiApplication::platformName().toLower();
    return platform != QStringLiteral("offscreen") && platform != QStringLiteral("minimal");
}

QString backendName() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("Metal");
#elif defined(Q_OS_WIN)
    return QStringLiteral("D3D11");
#else
    return QStringLiteral("PlatformDefault");
#endif
}

bool createSparseSequence(const QString& path, const RawImageParameters& parameters) {
    const qsizetype bytesPerFrame = frameByteSize(parameters);
    if (bytesPerFrame <= 0 || bytesPerFrame > std::numeric_limits<qint64>::max() / kFrameCount) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.resize(bytesPerFrame * kFrameCount)) {
        return false;
    }
    QString error;
    return RawPresetStore::saveSidecar(path, parameters, &error);
}

bool waitForInitialSubmission(ImageCanvas& canvas, QString& error) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool submitted = false;
    QObject::connect(&canvas, &QRhiWidget::frameSubmitted, &loop, [&] {
        const ImageFramePtr frame = canvas.frame();
        if (frame && frame->rawParameters && frame->rawParameters->frameIndex == 0) {
            submitted = true;
            loop.quit();
        }
    });
    QObject::connect(&canvas, &QRhiWidget::renderFailed, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(kTimeoutMilliseconds);
    loop.exec();
    if (!submitted) {
        const ImageFramePtr frame = canvas.frame();
        error = QStringLiteral("initial frame was not submitted (frame=%1, platform=%2)")
                    .arg(frame && frame->rawParameters
                             ? QString::number(frame->rawParameters->frameIndex)
                             : QStringLiteral("none"),
                         QGuiApplication::platformName());
    }
    return submitted;
}

bool validateRoiWorkflow(MainWindow& window, ImageCanvas& canvas, QString& error) {
    auto* roiAction = window.findChild<QAction*>(QStringLiteral("roiSelectionAction"));
    auto* panel = window.findChild<HistogramPanel*>(QStringLiteral("histogramPanel"));
    auto* overlay = canvas.findChild<QWidget*>(QStringLiteral("roiOverlay"));
    const ImageFramePtr frame = canvas.frame();
    const QSize imageSize = canvas.logicalImageSize();
    if (!roiAction || !panel || !overlay || !frame || imageSize.isEmpty()) {
        error = QStringLiteral("MainWindow ROI controls were not found");
        return false;
    }

    roiAction->setChecked(true);
    const ViewState state = canvas.viewState();
    const QPoint start =
        ViewTransform::imageToWidget({imageSize.width() * 0.25, imageSize.height() * 0.25},
                                     canvas.size(), imageSize, state)
            .toPoint();
    const QPoint end =
        ViewTransform::imageToWidget({imageSize.width() * 0.75, imageSize.height() * 0.75},
                                     canvas.size(), imageSize, state)
            .toPoint();
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(&canvas, end);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end);

    QElapsedTimer timeout;
    timeout.start();
    while (timeout.elapsed() < kTimeoutMilliseconds &&
           (!panel->histogram() || !panel->histogram()->isRegionLimited())) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(10);
    }
    const auto roi = canvas.normalizedRoi();
    const bool correctRoi =
        roi && std::abs(roi->x() - 0.25) < 0.02 && std::abs(roi->y() - 0.25) < 0.02 &&
        std::abs(roi->width() - 0.5) < 0.02 && std::abs(roi->height() - 0.5) < 0.02;
    const bool overlayReady = overlay->isVisibleTo(&canvas) && !canvas.roiWidgetRect().isEmpty();
    if (!correctRoi || !overlayReady || !panel->histogram() ||
        !panel->histogram()->isRegionLimited()) {
        error = QStringLiteral("native ROI did not converge (roi=%1,%2 %3x%4, overlayVisible=%5, "
                               "overlayRect=%6x%7, histogram=%8, regionLimited=%9)")
                    .arg(roi ? roi->x() : -1.0, 0, 'f', 3)
                    .arg(roi ? roi->y() : -1.0, 0, 'f', 3)
                    .arg(roi ? roi->width() : -1.0, 0, 'f', 3)
                    .arg(roi ? roi->height() : -1.0, 0, 'f', 3)
                    .arg(overlay->isVisibleTo(&canvas))
                    .arg(canvas.roiWidgetRect().width(), 0, 'f', 1)
                    .arg(canvas.roiWidgetRect().height(), 0, 'f', 1)
                    .arg(panel->histogram().has_value())
                    .arg(panel->histogram() && panel->histogram()->isRegionLimited());
        return false;
    }
    panel->setSource(HistogramSource::SourcePlanes);
    timeout.restart();
    while (timeout.elapsed() < kTimeoutMilliseconds &&
           (!panel->rawHistogram() || !panel->rawHistogram()->isRegionLimited())) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(10);
    }
    if (!panel->rawHistogram() || !panel->rawHistogram()->isRegionLimited() ||
        panel->rawHistogram()->domain != RawHistogramDomain::Yuv ||
        panel->rawHistogram()->logicalSize != imageSize) {
        error = QStringLiteral("native source-plane ROI histogram did not converge");
        return false;
    }
    return true;
}

bool waitForEncodedSubmission(ImageCanvas& canvas, const QString& path, bool requireFull,
                              QString& error) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool submitted = false;
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const QSize sourceSize = QImageReader(path).size();
    QObject::connect(&canvas, &QRhiWidget::frameSubmitted, &loop, [&] {
        const ImageFramePtr frame = canvas.frame();
        if (!frame || frame->metadata.path != absolutePath) {
            return;
        }
        const qint64 framePixels =
            qint64(frame->descriptor.size.width()) * frame->descriptor.size.height();
        const qint64 sourcePixels = qint64(sourceSize.width()) * sourceSize.height();
        if (!requireFull || (sourcePixels > 0 && framePixels == sourcePixels)) {
            submitted = true;
            loop.quit();
        }
    });
    QObject::connect(&canvas, &QRhiWidget::renderFailed, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(kTimeoutMilliseconds);
    loop.exec();
    if (!submitted) {
        const ImageFramePtr frame = canvas.frame();
        error = QStringLiteral("encoded frame was not submitted (expected=%1, frame=%2, full=%3)")
                    .arg(absolutePath, frame ? frame->metadata.path : QStringLiteral("none"),
                         requireFull ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    return submitted;
}

void sendNextFrameKey(MainWindow& window) { QTest::keyClick(&window, Qt::Key_BracketRight); }

std::optional<NavigationMeasurement> measureNavigation(const QString& directory, QString& error) {
    MainWindow window(createDefaultImageDecoder(), directory);
    window.resize(1280, 800);
    window.show();
    window.raise();
    window.activateWindow();

    auto* canvas = window.findChild<ImageCanvas*>();
    auto* frameSpin = window.findChild<QSpinBox*>(QStringLiteral("rawFrameSpin"));
    if (!canvas || !frameSpin) {
        error = QStringLiteral("MainWindow child controls were not found");
        return std::nullopt;
    }
    if (!waitForInitialSubmission(*canvas, error)) {
        return std::nullopt;
    }
    NavigationMeasurement measurement;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool failed = false;
    QElapsedTimer timer;
    QObject::connect(canvas, &QRhiWidget::frameSubmitted, &loop, [&] {
        const ImageFramePtr frame = canvas->frame();
        if (!frame || !frame->rawParameters || frame->rawParameters->frameIndex != 1) {
            return;
        }
        if (measurement.firstSubmitNanoseconds == 0) {
            measurement.firstSubmitNanoseconds = timer.nsecsElapsed();
        }
        if (measurement.fullSubmitNanoseconds == 0 &&
            std::holds_alternative<std::shared_ptr<const PlaneBufferSet>>(frame->storage)) {
            measurement.fullSubmitNanoseconds = timer.nsecsElapsed();
            measurement.fullFrameBytes = frame->byteSize();
            if (!canvas->usingGpuYuvPlanes()) {
                failed = true;
            }
            loop.quit();
        }
    });
    QObject::connect(canvas, &QRhiWidget::renderFailed, &loop, [&] {
        failed = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start();
    timeout.start(kTimeoutMilliseconds);
    sendNextFrameKey(window);
    loop.exec();

    const bool reachedTarget = frameSpin->value() == 1 && measurement.firstSubmitNanoseconds > 0 &&
                               measurement.fullSubmitNanoseconds > 0;
    if (failed) {
        error = QStringLiteral("render failed or Full used the CPU fallback");
    } else if (!reachedTarget) {
        const ImageFramePtr frame = canvas->frame();
        error = QStringLiteral("target incomplete (spin=%1, frame=%2, first=%3 ns, full=%4 ns)")
                    .arg(frameSpin->value())
                    .arg(frame && frame->rawParameters
                             ? QString::number(frame->rawParameters->frameIndex)
                             : QStringLiteral("none"))
                    .arg(measurement.firstSubmitNanoseconds)
                    .arg(measurement.fullSubmitNanoseconds);
    }
    if (!failed && reachedTarget && !validateRoiWorkflow(window, *canvas, error)) {
        return std::nullopt;
    }
    window.close();
    return !failed && reachedTarget ? std::optional<NavigationMeasurement>(measurement)
                                    : std::nullopt;
}

std::optional<MedianMeasurement> measureFrameSize(const QString& root, const QString& label,
                                                  const QSize& size) {
    const QString directory = root + QLatin1Char('/') + label;
    if (!QDir().mkpath(directory)) {
        return std::nullopt;
    }
    RawImageParameters parameters;
    parameters.size = size;
    parameters.format = RawPixelFormat::NV12;
    const QString path =
        directory + QStringLiteral("/sequence_%1x%2_nv12.yuv").arg(size.width()).arg(size.height());
    if (!createSparseSequence(path, parameters)) {
        return std::nullopt;
    }

    std::array<qint64, kMeasuredRuns> firstSubmit{};
    std::array<qint64, kMeasuredRuns> fullSubmit{};
    qsizetype fullFrameBytes = 0;
    for (std::size_t run = 0; run < firstSubmit.size(); ++run) {
        QString error;
        const auto measured = measureNavigation(directory, error);
        if (!measured) {
            QTextStream(stderr) << label << " run " << run + 1 << " failed: " << error << '\n';
            return std::nullopt;
        }
        firstSubmit.at(run) = measured->firstSubmitNanoseconds;
        fullSubmit.at(run) = measured->fullSubmitNanoseconds;
        fullFrameBytes = measured->fullFrameBytes;
    }
    std::sort(firstSubmit.begin(), firstSubmit.end());
    std::sort(fullSubmit.begin(), fullSubmit.end());
    return MedianMeasurement{
        firstSubmit.at(kMeasuredRuns / 2) / 1'000'000.0,
        fullSubmit.at(kMeasuredRuns / 2) / 1'000'000.0,
        fullFrameBytes / (1024.0 * 1024.0),
    };
}

bool printMeasurement(QTextStream& output, const QString& label,
                      const std::optional<MedianMeasurement>& measurement) {
    if (!measurement) {
        return false;
    }
    output << label << '\t' << backendName() << '\t'
           << QString::number(measurement->firstSubmitMilliseconds, 'f', 2) << '\t'
           << QString::number(measurement->fullSubmitMilliseconds, 'f', 2) << '\t'
           << QString::number(measurement->fullFrameMiB, 'f', 2) << '\n';
    output.flush();
    return true;
}

QStringList prepareEncodedDirectory(const QString& sourceDirectory, const QString& destination,
                                    QString& error) {
    QDir source(sourceDirectory);
    if (!source.exists() || !QDir().mkpath(destination)) {
        error = QStringLiteral("encoded sample directory is unavailable: %1").arg(sourceDirectory);
        return {};
    }
    const QFileInfoList entries = source.entryInfoList(
        {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
         QStringLiteral("*.JPG"), QStringLiteral("*.JPEG"), QStringLiteral("*.PNG")},
        QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
    QStringList copied;
    copied.reserve(entries.size());
    for (const QFileInfo& entry : entries) {
        const QString target = QDir(destination).filePath(entry.fileName());
        if (!QFile::copy(entry.absoluteFilePath(), target)) {
            error = QStringLiteral("could not copy encoded benchmark sample: %1")
                        .arg(entry.absoluteFilePath());
            return {};
        }
        copied.append(target);
    }
    if (copied.size() < 2) {
        error = QStringLiteral("encoded benchmark needs at least two JPEG/PNG files");
        return {};
    }
    return copied;
}

std::optional<EncodedNavigationMeasurement>
measureEncodedStep(QListView& thumbnails, ImageCanvas& canvas, QString& error) {
    const QModelIndex current = thumbnails.currentIndex();
    if (!current.isValid() || current.row() + 1 >= thumbnails.model()->rowCount()) {
        error = QStringLiteral("no next encoded item to navigate to");
        return std::nullopt;
    }
    const QModelIndex target = thumbnails.model()->index(current.row() + 1, 0);
    const QString targetPath = target.data(ThumbnailModel::PathRole).toString();
    const QSize sourceSize = QImageReader(targetPath).size();
    if (targetPath.isEmpty() || !sourceSize.isValid()) {
        error = QStringLiteral("could not inspect next encoded item");
        return std::nullopt;
    }

    EncodedNavigationMeasurement measurement;
    measurement.fileName = QFileInfo(targetPath).fileName();
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QElapsedTimer timer;
    const QString absoluteTarget = QFileInfo(targetPath).absoluteFilePath();
    QObject::connect(&canvas, &QRhiWidget::frameSubmitted, &loop, [&] {
        const ImageFramePtr frame = canvas.frame();
        if (!frame || frame->metadata.path != absoluteTarget) {
            return;
        }
        if (measurement.firstSubmitNanoseconds == 0) {
            measurement.firstSubmitNanoseconds = timer.nsecsElapsed();
            measurement.previewSize = frame->descriptor.size;
        }
        const qint64 framePixels =
            qint64(frame->descriptor.size.width()) * frame->descriptor.size.height();
        const qint64 sourcePixels = qint64(sourceSize.width()) * sourceSize.height();
        if (framePixels == sourcePixels) {
            measurement.fullSubmitNanoseconds = timer.nsecsElapsed();
            measurement.fullSize = frame->descriptor.size;
            loop.quit();
        }
    });
    QObject::connect(&canvas, &QRhiWidget::renderFailed, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    thumbnails.setFocus();
    timer.start();
    timeout.start(kTimeoutMilliseconds);
    QTest::keyClick(&thumbnails, Qt::Key_Right);
    if (thumbnails.currentIndex().row() != target.row()) {
        error = QStringLiteral("Right key did not advance from row %1 to row %2 (actual=%3)")
                    .arg(current.row())
                    .arg(target.row())
                    .arg(thumbnails.currentIndex().row());
        return std::nullopt;
    }
    loop.exec();
    if (measurement.firstSubmitNanoseconds == 0 || measurement.fullSubmitNanoseconds == 0) {
        error = QStringLiteral("navigation to %1 did not submit Preview and Full")
                    .arg(measurement.fileName);
        return std::nullopt;
    }
    return measurement;
}

int runEncodedNavigationBenchmark(const QString& sourceDirectory, QTemporaryDir& root,
                                  QTextStream& output) {
    QString error;
    const QString directory = root.filePath(QStringLiteral("encoded"));
    const QStringList copied = prepareEncodedDirectory(sourceDirectory, directory, error);
    if (copied.isEmpty()) {
        output << error << '\n';
        return 1;
    }

    MainWindow window(createDefaultImageDecoder(), directory);
    window.resize(1280, 800);
    window.show();
    window.raise();
    window.activateWindow();
    auto* canvas = window.findChild<ImageCanvas*>();
    auto* thumbnails = window.findChild<QListView*>(QStringLiteral("thumbnailView"));
    if (!canvas || !thumbnails) {
        output << "MainWindow encoded benchmark controls were not found\n";
        return 1;
    }
    const QVector<ImageFileRecord> orderedFiles = DirectoryScanner::scan(directory);
    if (orderedFiles.isEmpty() ||
        !waitForEncodedSubmission(*canvas, orderedFiles.constFirst().path, true, error)) {
        output << error << '\n';
        return 1;
    }

    std::vector<EncodedNavigationMeasurement> measurements;
    measurements.reserve(static_cast<std::size_t>(thumbnails->model()->rowCount() - 1));
    while (thumbnails->currentIndex().row() + 1 < thumbnails->model()->rowCount()) {
        const auto measured = measureEncodedStep(*thumbnails, *canvas, error);
        if (!measured) {
            output << error << '\n';
            return 1;
        }
        measurements.push_back(*measured);
    }

    output << "File\tBackend\tPreviewSize\tInputToFirstSubmitMs\tFullSize\tInputToFullSubmitMs\n";
    std::vector<double> firstMilliseconds;
    std::vector<double> fullMilliseconds;
    firstMilliseconds.reserve(measurements.size());
    fullMilliseconds.reserve(measurements.size());
    for (const auto& measurement : measurements) {
        const double first = measurement.firstSubmitNanoseconds / 1'000'000.0;
        const double full = measurement.fullSubmitNanoseconds / 1'000'000.0;
        firstMilliseconds.push_back(first);
        fullMilliseconds.push_back(full);
        output << measurement.fileName << '\t' << backendName() << '\t'
               << measurement.previewSize.width() << 'x' << measurement.previewSize.height() << '\t'
               << QString::number(first, 'f', 2) << '\t' << measurement.fullSize.width() << 'x'
               << measurement.fullSize.height() << '\t' << QString::number(full, 'f', 2) << '\n';
    }
    std::sort(firstMilliseconds.begin(), firstMilliseconds.end());
    std::sort(fullMilliseconds.begin(), fullMilliseconds.end());
    const auto percentile = [](const std::vector<double>& values, double fraction) {
        const std::size_t index = std::min(
            values.size() - 1, static_cast<std::size_t>((values.size() - 1) * fraction + 0.5));
        return values.at(index);
    };
    output << "SUMMARY\t" << backendName() << "\ttransitions=" << measurements.size()
           << "\tfirst_median=" << QString::number(percentile(firstMilliseconds, 0.5), 'f', 2)
           << "\tfirst_p95=" << QString::number(percentile(firstMilliseconds, 0.95), 'f', 2)
           << "\tfirst_max=" << QString::number(firstMilliseconds.back(), 'f', 2)
           << "\tfull_median=" << QString::number(percentile(fullMilliseconds, 0.5), 'f', 2)
           << "\tfull_p95=" << QString::number(percentile(fullMilliseconds, 0.95), 'f', 2)
           << "\tfull_max=" << QString::number(fullMilliseconds.back(), 'f', 2) << '\n';
    output.flush();
    window.close();
    return 0;
}

QStringList prepareRawDirectory(const QString& sourceDirectory, const QString& destination,
                                const RawImageParameters& candidate, QString& error) {
    QDir source(sourceDirectory);
    if (!source.exists() || !QDir().mkpath(destination)) {
        error = QStringLiteral("RAW sample directory is unavailable: %1").arg(sourceDirectory);
        return {};
    }
    const QFileInfoList entries = source.entryInfoList(
        {QStringLiteral("*.raw"), QStringLiteral("*.RAW")},
        QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
    QStringList copied;
    copied.reserve(entries.size());
    for (const QFileInfo& entry : entries) {
        const QString target = QDir(destination).filePath(entry.fileName());
        QString sidecarError;
        if (!QFile::copy(entry.absoluteFilePath(), target) ||
            !RawPresetStore::saveSidecar(target, candidate, &sidecarError)) {
            error = QStringLiteral("could not prepare RAW benchmark sample %1: %2")
                        .arg(entry.absoluteFilePath(), sidecarError);
            return {};
        }
        copied.append(target);
    }
    if (copied.size() < 2) {
        error = QStringLiteral("RAW navigation benchmark needs at least two .raw files");
        return {};
    }
    return copied;
}

bool waitForRawFullSubmission(ImageCanvas& canvas, const QString& path,
                              const RawImageParameters& candidate, QString& error) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool submitted = false;
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    QObject::connect(&canvas, &QRhiWidget::frameSubmitted, &loop, [&] {
        const ImageFramePtr frame = canvas.frame();
        if (!frame || frame->metadata.path != absolutePath || !frame->rawParameters ||
            frame->descriptor.size != candidate.size ||
            !std::holds_alternative<std::shared_ptr<const PlaneBufferSet>>(frame->storage)) {
            return;
        }
        submitted = canvas.usingGpuBayerPlane();
        loop.quit();
    });
    QObject::connect(&canvas, &QRhiWidget::renderFailed, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(kTimeoutMilliseconds);
    loop.exec();
    if (!submitted) {
        error = QStringLiteral("initial RAW Full was not submitted through the Bayer GPU path");
    }
    return submitted;
}

std::optional<RawNavigationMeasurement> measureRawStep(MainWindow& window, QListView& thumbnails,
                                                       ImageCanvas& canvas,
                                                       const RawImageParameters& candidate,
                                                       QString& error) {
    const QModelIndex current = thumbnails.currentIndex();
    if (!current.isValid() || current.row() + 1 >= thumbnails.model()->rowCount()) {
        error = QStringLiteral("no next RAW item to navigate to");
        return std::nullopt;
    }
    const QModelIndex target = thumbnails.model()->index(current.row() + 1, 0);
    const QString targetPath =
        QFileInfo(target.data(ThumbnailModel::PathRole).toString()).absoluteFilePath();
    RawNavigationMeasurement measurement;
    measurement.fileName = QFileInfo(targetPath).fileName();

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool failed = false;
    QElapsedTimer timer;
    QObject::connect(&canvas, &QRhiWidget::frameSubmitted, &loop, [&] {
        const ImageFramePtr frame = canvas.frame();
        if (!frame || frame->metadata.path != targetPath || !frame->rawParameters) {
            return;
        }
        if (measurement.firstSubmitNanoseconds == 0) {
            measurement.firstSubmitNanoseconds = timer.nsecsElapsed();
            measurement.previewSize = frame->descriptor.size;
        }
        if (!std::holds_alternative<std::shared_ptr<const PlaneBufferSet>>(frame->storage)) {
            return;
        }
        measurement.fullSubmitNanoseconds = timer.nsecsElapsed();
        measurement.sourceSize = frame->descriptor.size;
        measurement.fullFrameBytes = frame->byteSize();
        if (const QImage* fallback = frame->qImage()) {
            measurement.fallbackSize = fallback->size();
        }
        failed = !canvas.usingGpuBayerPlane() || frame->descriptor.size != candidate.size ||
                 frame->rawParameters->validBits() != candidate.validBits() ||
                 !RawImageDecoder::bayerValueAt(*frame, candidate.size.width() / 2,
                                                candidate.size.height() / 2)
                      .has_value();
        loop.quit();
    });
    QObject::connect(&canvas, &QRhiWidget::renderFailed, &loop, [&] {
        failed = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    thumbnails.setFocus();
    timer.start();
    timeout.start(kTimeoutMilliseconds);
    QTest::keyClick(&thumbnails, Qt::Key_Right);
    loop.exec();
    if (failed || measurement.firstSubmitNanoseconds == 0 ||
        measurement.fullSubmitNanoseconds == 0) {
        error = QStringLiteral("RAW navigation did not submit valid Preview and GPU Full frames");
        return std::nullopt;
    }

    const QPoint center = canvas.rect().center();
    QMouseEvent probeEvent(QEvent::MouseMove, QPointF(center), QPointF(canvas.mapToGlobal(center)),
                           Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &probeEvent);
    QCoreApplication::processEvents();
    if (!window.statusBar()->currentMessage().contains(QStringLiteral("RAW("))) {
        error = QStringLiteral("MainWindow RAW pixel probe did not reach the status bar");
        return std::nullopt;
    }
    return measurement;
}

int runRawNavigationBenchmark(const QString& sourceDirectory, const RawImageParameters& candidate,
                              QTemporaryDir& root, QTextStream& output) {
    QString error;
    const QString directory = root.filePath(QStringLiteral("raw"));
    const QStringList copied = prepareRawDirectory(sourceDirectory, directory, candidate, error);
    if (copied.isEmpty()) {
        output << error << '\n';
        return 1;
    }

    MainWindow window(createDefaultImageDecoder(), directory);
    window.resize(1280, 800);
    window.show();
    window.raise();
    window.activateWindow();
    auto* canvas = window.findChild<ImageCanvas*>();
    auto* thumbnails = window.findChild<QListView*>(QStringLiteral("thumbnailView"));
    if (!canvas || !thumbnails ||
        !waitForRawFullSubmission(*canvas, copied.constFirst(), candidate, error)) {
        output << error << '\n';
        return 1;
    }

    const auto measured = measureRawStep(window, *thumbnails, *canvas, candidate, error);
    if (!measured) {
        output << error << '\n';
        return 1;
    }
    output << "File\tBackend\tPreviewSize\tInputToFirstSubmitMs\tSourceSize\tFallbackSize"
              "\tInputToFullSubmitMs\tFullFrameMiB\tPixelProbe\n";
    output << measured->fileName << '\t' << backendName() << '\t' << measured->previewSize.width()
           << 'x' << measured->previewSize.height() << '\t'
           << QString::number(measured->firstSubmitNanoseconds / 1'000'000.0, 'f', 2) << '\t'
           << measured->sourceSize.width() << 'x' << measured->sourceSize.height() << '\t'
           << measured->fallbackSize.width() << 'x' << measured->fallbackSize.height() << '\t'
           << QString::number(measured->fullSubmitNanoseconds / 1'000'000.0, 'f', 2) << '\t'
           << QString::number(measured->fullFrameBytes / (1024.0 * 1024.0), 'f', 2) << "\tPASS\n";
    output.flush();
    window.close();
    return 0;
}

} // namespace
} // namespace ispview

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ISP Image Viewer UI Benchmark"));
    QCoreApplication::setOrganizationName(QStringLiteral("ISPViewBenchmark"));
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QTextStream output(stdout);
    if (!ispview::nativeSurfaceIsAvailable()) {
        output << "A native screen is required for the MainWindow UI benchmark\n";
        return 1;
    }
    QTemporaryDir directory;
    if (!directory.isValid()) {
        output << "Could not create UI benchmark directory\n";
        return 1;
    }
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       directory.filePath(QStringLiteral("settings")));

    const QStringList arguments = application.arguments();
    const qsizetype encodedDirectoryIndex =
        arguments.indexOf(QStringLiteral("--encoded-directory"));
    if (encodedDirectoryIndex >= 0) {
        if (encodedDirectoryIndex + 1 >= arguments.size()) {
            output << "--encoded-directory requires a path\n";
            return 1;
        }
        return ispview::runEncodedNavigationBenchmark(arguments.at(encodedDirectoryIndex + 1),
                                                      directory, output);
    }
    const qsizetype rawDirectoryIndex = arguments.indexOf(QStringLiteral("--raw-directory"));
    if (rawDirectoryIndex >= 0) {
        const qsizetype candidateIndex = arguments.indexOf(QStringLiteral("--candidate-raw16"));
        if (rawDirectoryIndex + 1 >= arguments.size() || candidateIndex < 0 ||
            candidateIndex + 1 >= arguments.size()) {
            output << "--raw-directory requires a path and --candidate-raw16 "
                      "WIDTHxHEIGHT:VALID_BITS:CFA\n";
            return 1;
        }
        auto candidate = ispview::tools::parseRaw16Candidate(
            arguments.at(candidateIndex + 1), arguments.contains(QStringLiteral("--msb-aligned")),
            arguments.contains(QStringLiteral("--big-endian")));
        if (!candidate) {
            output << "Invalid RAW16 candidate; expected "
                      "WIDTHxHEIGHT:1-16:RGGB|GRBG|GBRG|BGGR\n";
            return 1;
        }
        QString optionError;
        if (!ispview::tools::applyCandidateOrientationOption(arguments, candidate, optionError)) {
            output << optionError << '\n';
            return 1;
        }
        return ispview::runRawNavigationBenchmark(arguments.at(rawDirectoryIndex + 1), *candidate,
                                                  directory, output);
    }

    output << "Size\tBackend\tInputToFirstSubmitMs\tInputToFullSubmitMs\tFullFrameMiB\n";
    if (!ispview::printMeasurement(output, QStringLiteral("4K"),
                                   ispview::measureFrameSize(directory.path(), QStringLiteral("4k"),
                                                             ispview::kDefaultFrameSize))) {
        output << "4K MainWindow navigation benchmark failed\n";
        return 1;
    }
    if (application.arguments().contains(QStringLiteral("--48mp")) &&
        !ispview::printMeasurement(output, QStringLiteral("48MP"),
                                   ispview::measureFrameSize(directory.path(),
                                                             QStringLiteral("48mp"),
                                                             ispview::kLargeFrameSize))) {
        output << "48MP MainWindow navigation benchmark failed\n";
        return 1;
    }
    return 0;
}
