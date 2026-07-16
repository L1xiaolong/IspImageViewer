#include "io/image_loader.h"
#include "io/raw_image_decoder.h"
#include "io/raw_preset_store.h"
#include "raw_candidate_options.h"
#include "render/image_canvas.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QScreen>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

namespace ispview {
namespace {

constexpr QSize kDefaultFrameSize{3840, 2160};
constexpr QSize kLargeFrameSize{8000, 6000};
constexpr QSize kPreviewSize{960, 720};
constexpr int kMeasuredRuns = 3;
constexpr int kFrameTimeoutMilliseconds = 15'000;

struct Measurement {
    double submitMedianMilliseconds = 0.0;
    double readbackMedianMilliseconds = 0.0;
    double frameMiB = 0.0;
};

struct PipelineMeasurement {
    double coldReadyMedianMilliseconds = 0.0;
    double coldSubmitMedianMilliseconds = 0.0;
    double prefetchWaitMilliseconds = 0.0;
    double hitReadyMedianMilliseconds = 0.0;
    double hitSubmitMedianMilliseconds = 0.0;
};

struct RequestMeasurement {
    qint64 readyNanoseconds = 0;
    qint64 submittedNanoseconds = 0;
};

bool writeFixture(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool writeRepeatedFixture(const QString& path, const QByteArray& frame, int frameCount) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    for (int index = 0; index < frameCount; ++index) {
        if (file.write(frame) != frame.size()) {
            return false;
        }
    }
    return true;
}

QByteArray nv12Fixture(const QSize& size) {
    RawImageParameters parameters;
    parameters.size = size;
    parameters.format = RawPixelFormat::NV12;
    return QByteArray(frameByteSize(parameters), static_cast<char>(0x80));
}

QByteArray p010Fixture(const QSize& size) {
    RawImageParameters parameters;
    parameters.size = size;
    parameters.format = RawPixelFormat::P010;
    QByteArray bytes(frameByteSize(parameters), Qt::Uninitialized);
    const quint16 neutral = static_cast<quint16>(512U << 6U);
    for (qsizetype offset = 0; offset < bytes.size(); offset += 2) {
        qToLittleEndian(neutral, reinterpret_cast<uchar*>(bytes.data() + offset));
    }
    return bytes;
}

QByteArray raw10Fixture(const QSize& size) {
    RawImageParameters parameters;
    parameters.size = size;
    parameters.format = RawPixelFormat::MipiRaw10;
    const qsizetype stride = minimumRowStride(parameters);
    QByteArray bytes(frameByteSize(parameters), static_cast<char>(0));
    constexpr std::array<quint8, 5> group{64, 96, 128, 160, 0};
    constexpr qsizetype groupSize = static_cast<qsizetype>(group.size());
    for (int y = 0; y < size.height(); ++y) {
        char* row = bytes.data() + static_cast<qsizetype>(y) * stride;
        for (qsizetype offset = 0; offset + groupSize <= stride; offset += groupSize) {
            for (std::size_t index = 0; index < group.size(); ++index) {
                row[offset + static_cast<qsizetype>(index)] = static_cast<char>(group[index]);
            }
        }
    }
    return bytes;
}

bool waitForSubmission(ImageCanvas& canvas, const ImageFramePtr& frame,
                       qint64& elapsedNanoseconds) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool submitted = false;
    const QMetaObject::Connection submittedConnection =
        QObject::connect(&canvas, &QRhiWidget::frameSubmitted, &loop, [&] {
            submitted = true;
            loop.quit();
        });
    QObject::connect(&canvas, &QRhiWidget::renderFailed, &loop, [&] { loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QElapsedTimer timer;
    timer.start();
    canvas.setFrame(frame);
    timeout.start(kFrameTimeoutMilliseconds);
    loop.exec();
    elapsedNanoseconds = timer.nsecsElapsed();
    QObject::disconnect(submittedConnection);
    return submitted;
}

std::optional<Measurement> measure(const QString& path, RawImageParameters parameters) {
    RawImageDecoder decoder;
    DecodeResult decoded = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    if (!decoded.succeeded()) {
        return std::nullopt;
    }

    ImageCanvas canvas;
    canvas.resize(1280, 720);
    canvas.show();

    QImage warmImage(1, 1, QImage::Format_RGBA8888);
    warmImage.fill(Qt::black);
    auto warmFrame = std::make_shared<ImageFrame>();
    warmFrame->descriptor.size = warmImage.size();
    warmFrame->storage = std::move(warmImage);
    qint64 warmElapsed = 0;
    if (!waitForSubmission(canvas, warmFrame, warmElapsed)) {
        return std::nullopt;
    }

    std::array<qint64, kMeasuredRuns> submitNanoseconds{};
    std::array<qint64, kMeasuredRuns> readbackNanoseconds{};
    for (std::size_t run = 0; run < submitNanoseconds.size(); ++run) {
        if (!waitForSubmission(canvas, decoded.frame, submitNanoseconds.at(run))) {
            return std::nullopt;
        }
        const bool gpuRawReady =
            parameters.isYuv() ? canvas.usingGpuYuvPlanes() : canvas.usingGpuBayerPlane();
        if (!gpuRawReady) {
            return std::nullopt;
        }
        QElapsedTimer readbackTimer;
        readbackTimer.start();
        const QImage framebuffer = canvas.grabFramebuffer();
        readbackNanoseconds.at(run) = readbackTimer.nsecsElapsed();
        if (framebuffer.isNull()) {
            return std::nullopt;
        }
    }
    std::sort(submitNanoseconds.begin(), submitNanoseconds.end());
    std::sort(readbackNanoseconds.begin(), readbackNanoseconds.end());
    return Measurement{submitNanoseconds.at(kMeasuredRuns / 2) / 1'000'000.0,
                       readbackNanoseconds.at(kMeasuredRuns / 2) / 1'000'000.0,
                       decoded.frame->byteSize() / (1024.0 * 1024.0)};
}

std::optional<RequestMeasurement> measureRequest(ImageLoader& loader, ImageCanvas& canvas,
                                                 const DecodeRequest& request) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool frameApplied = false;
    bool succeeded = false;
    RequestMeasurement measurement;
    QElapsedTimer timer;
    const QMetaObject::Connection submittedConnection =
        QObject::connect(&canvas, &QRhiWidget::frameSubmitted, &loop, [&] {
            if (!frameApplied) {
                return;
            }
            measurement.submittedNanoseconds = timer.nsecsElapsed();
            succeeded = true;
            loop.quit();
        });
    QObject::connect(&canvas, &QRhiWidget::renderFailed, &loop, [&] { loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start();
    timeout.start(kFrameTimeoutMilliseconds);
    loader.request(1, request, [&](quint64, const DecodeResult& result) {
        measurement.readyNanoseconds = timer.nsecsElapsed();
        if (!result.succeeded()) {
            loop.quit();
            return;
        }
        frameApplied = true;
        canvas.setFrame(result.frame);
    });
    loop.exec();
    QObject::disconnect(submittedConnection);
    if (!succeeded) {
        return std::nullopt;
    }
    const bool expectsPlanes = request.purpose == DecodePurpose::Full;
    const bool usingExpectedPlanes = request.rawParameters && request.rawParameters->isYuv()
                                         ? canvas.usingGpuYuvPlanes()
                                         : canvas.usingGpuBayerPlane();
    if (usingExpectedPlanes != expectsPlanes) {
        return std::nullopt;
    }
    return measurement;
}

bool waitForCached(ImageLoader& loader, const QVector<DecodeRequest>& requests,
                   qint64& elapsedNanoseconds) {
    auto allCached = [&] {
        return std::all_of(
            requests.cbegin(), requests.cend(),
            [&loader](const DecodeRequest& request) { return loader.isCached(request); });
    };
    if (allCached()) {
        elapsedNanoseconds = 0;
        return true;
    }
    QEventLoop loop;
    QTimer poll;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (allCached()) {
            loop.quit();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QElapsedTimer timer;
    timer.start();
    poll.start(1);
    timeout.start(kFrameTimeoutMilliseconds);
    loop.exec();
    elapsedNanoseconds = timer.nsecsElapsed();
    return allCached();
}

std::optional<PipelineMeasurement> measurePipeline(const QString& path,
                                                   RawImageParameters parameters,
                                                   DecodePurpose purpose,
                                                   const QSize& maximumSize) {
    auto decoder = std::make_shared<RawImageDecoder>();
    ImageLoader loader(decoder);
    ImageCanvas canvas;
    canvas.resize(1280, 720);
    canvas.show();

    QImage warmImage(1, 1, QImage::Format_RGBA8888);
    warmImage.fill(Qt::black);
    auto warmFrame = std::make_shared<ImageFrame>();
    warmFrame->descriptor.size = warmImage.size();
    warmFrame->storage = std::move(warmImage);
    qint64 warmElapsed = 0;
    if (!waitForSubmission(canvas, warmFrame, warmElapsed)) {
        return std::nullopt;
    }

    RawImageParameters target = parameters;
    target.frameIndex = 2;
    const DecodeRequest targetRequest(path, purpose, maximumSize, target);
    std::array<qint64, kMeasuredRuns> coldReady{};
    std::array<qint64, kMeasuredRuns> coldSubmitted{};
    for (std::size_t run = 0; run < coldReady.size(); ++run) {
        loader.clearCache();
        const auto measured = measureRequest(loader, canvas, targetRequest);
        if (!measured) {
            return std::nullopt;
        }
        coldReady.at(run) = measured->readyNanoseconds;
        coldSubmitted.at(run) = measured->submittedNanoseconds;
    }

    loader.clearCache();
    RawImageParameters current = parameters;
    current.frameIndex = 1;
    loader.prefetchAdjacentRawFrames(path, current, kPreviewSize);
    QVector<DecodeRequest> expected;
    for (const int frameIndex : {0, 2}) {
        RawImageParameters adjacent = parameters;
        adjacent.frameIndex = frameIndex;
        expected.push_back({path, DecodePurpose::Preview, kPreviewSize, adjacent});
        if (purpose == DecodePurpose::Full) {
            expected.push_back({path, DecodePurpose::Full, {}, adjacent});
        }
    }
    qint64 prefetchWait = 0;
    if (!waitForCached(loader, expected, prefetchWait)) {
        return std::nullopt;
    }

    std::array<qint64, kMeasuredRuns> hitReady{};
    std::array<qint64, kMeasuredRuns> hitSubmitted{};
    for (std::size_t run = 0; run < hitReady.size(); ++run) {
        const auto measured = measureRequest(loader, canvas, targetRequest);
        if (!measured) {
            return std::nullopt;
        }
        hitReady.at(run) = measured->readyNanoseconds;
        hitSubmitted.at(run) = measured->submittedNanoseconds;
    }
    std::sort(coldReady.begin(), coldReady.end());
    std::sort(coldSubmitted.begin(), coldSubmitted.end());
    std::sort(hitReady.begin(), hitReady.end());
    std::sort(hitSubmitted.begin(), hitSubmitted.end());
    return PipelineMeasurement{
        coldReady.at(kMeasuredRuns / 2) / 1'000'000.0,
        coldSubmitted.at(kMeasuredRuns / 2) / 1'000'000.0,
        prefetchWait / 1'000'000.0,
        hitReady.at(kMeasuredRuns / 2) / 1'000'000.0,
        hitSubmitted.at(kMeasuredRuns / 2) / 1'000'000.0,
    };
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

bool printMeasurement(QTextStream& output, const QString& label, const QString& path,
                      const RawImageParameters& parameters) {
    const auto measured = measure(path, parameters);
    if (!measured) {
        return false;
    }
    output << label << '\t' << rawPixelFormatName(parameters.format) << '\t' << backendName()
           << '\t' << QString::number(measured->submitMedianMilliseconds, 'f', 2) << '\t'
           << QString::number(measured->readbackMedianMilliseconds, 'f', 2) << '\t'
           << QString::number(measured->frameMiB, 'f', 2) << '\n';
    output.flush();
    return true;
}

bool runFrameSize(QTextStream& output, const QString& directory, const QString& label,
                  const QSize& size) {
    const QString nv12Path = directory + QStringLiteral("/%1_nv12.yuv").arg(label);
    const QString p010Path = directory + QStringLiteral("/%1_p010.yuv").arg(label);
    if (!writeFixture(nv12Path, nv12Fixture(size)) || !writeFixture(p010Path, p010Fixture(size))) {
        return false;
    }
    RawImageParameters nv12;
    nv12.size = size;
    nv12.format = RawPixelFormat::NV12;
    RawImageParameters p010 = nv12;
    p010.format = RawPixelFormat::P010;
    p010.msbAligned = true;
    return printMeasurement(output, label, nv12Path, nv12) &&
           printMeasurement(output, label, p010Path, p010);
}

bool runBayerFrameSize(QTextStream& output, const QString& directory, const QString& label,
                       const QSize& size) {
    const QString raw10Path = directory + QStringLiteral("/%1_raw10.raw").arg(label);
    if (!writeFixture(raw10Path, raw10Fixture(size))) {
        return false;
    }
    RawImageParameters raw10;
    raw10.size = size;
    raw10.format = RawPixelFormat::MipiRaw10;
    raw10.bayerPattern = BayerPattern::RGGB;
    return printMeasurement(output, label, raw10Path, raw10);
}

bool runSampleDirectory(QTextStream& output, const QString& directory,
                        const std::optional<RawImageParameters>& raw16Candidate) {
    QStringList paths;
    QDirIterator iterator(directory,
                          {QStringLiteral("*.yuv"), QStringLiteral("*.YUV"),
                           QStringLiteral("*.raw"), QStringLiteral("*.RAW")},
                          QDir::Files | QDir::Readable | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        paths.push_back(iterator.next());
    }
    paths.sort(Qt::CaseInsensitive);
    if (paths.isEmpty()) {
        output << "No YUV samples found in " << directory << '\n';
        return false;
    }
    for (const QString& path : paths) {
        auto parameters = RawPresetStore::loadForFile(path);
        if (!parameters) {
            const RawImageParameters inferred = RawPresetStore::inferFromFileName(path);
            if (!inferred.size.isEmpty()) {
                parameters = inferred;
            }
        }
        if (!parameters &&
            QFileInfo(path).suffix().compare(QStringLiteral("raw"), Qt::CaseInsensitive) == 0) {
            parameters = raw16Candidate;
        }
        if (!parameters) {
            output << "Missing parameters for " << QFileInfo(path).fileName() << '\n';
            return false;
        }
        if (!printMeasurement(output, QFileInfo(path).fileName(), path, *parameters)) {
            output << "GPU sample failed: " << QFileInfo(path).fileName() << '\n';
            return false;
        }
    }
    return true;
}

bool printPipelineMeasurement(QTextStream& output, const QString& label, const QString& path,
                              const RawImageParameters& parameters, DecodePurpose purpose,
                              const QSize& maximumSize) {
    const auto measured = measurePipeline(path, parameters, purpose, maximumSize);
    if (!measured) {
        return false;
    }
    output << label << '\t' << rawPixelFormatName(parameters.format) << '\t'
           << (purpose == DecodePurpose::Full ? QStringLiteral("Full") : QStringLiteral("Preview"))
           << '\t' << QString::number(measured->coldReadyMedianMilliseconds, 'f', 2) << '\t'
           << QString::number(measured->coldSubmitMedianMilliseconds, 'f', 2) << '\t'
           << QString::number(measured->prefetchWaitMilliseconds, 'f', 2) << '\t'
           << QString::number(measured->hitReadyMedianMilliseconds, 'f', 3) << '\t'
           << QString::number(measured->hitSubmitMedianMilliseconds, 'f', 2) << '\n';
    output.flush();
    return true;
}

bool runPipelineFrameSize(QTextStream& output, const QString& directory, const QString& label,
                          const QSize& size, DecodePurpose purpose) {
    const QString nv12Path = directory + QStringLiteral("/%1_nv12_sequence.yuv").arg(label);
    const QString p010Path = directory + QStringLiteral("/%1_p010_sequence.yuv").arg(label);
    if (!writeRepeatedFixture(nv12Path, nv12Fixture(size), 3) ||
        !writeRepeatedFixture(p010Path, p010Fixture(size), 3)) {
        return false;
    }
    RawImageParameters nv12;
    nv12.size = size;
    nv12.format = RawPixelFormat::NV12;
    RawImageParameters p010 = nv12;
    p010.format = RawPixelFormat::P010;
    p010.msbAligned = true;
    const QSize maximumSize = purpose == DecodePurpose::Preview ? kPreviewSize : QSize{};
    return printPipelineMeasurement(output, label, nv12Path, nv12, purpose, maximumSize) &&
           printPipelineMeasurement(output, label, p010Path, p010, purpose, maximumSize);
}

} // namespace
} // namespace ispview

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QTextStream output(stdout);
    if (!application.primaryScreen()) {
        output << "A native screen is required for the GPU benchmark\n";
        return 1;
    }
    output << "Source\tFormat\tBackend\tUploadSubmitMedianMs\tReadbackMedianMs\tFrameMiB\n";
    const qsizetype sampleOption =
        application.arguments().indexOf(QStringLiteral("--sample-directory"));
    if (sampleOption >= 0) {
        if (sampleOption + 1 >= application.arguments().size()) {
            output << "--sample-directory requires a path\n";
            return 2;
        }
        std::optional<ispview::RawImageParameters> raw16Candidate;
        const qsizetype candidateOption =
            application.arguments().indexOf(QStringLiteral("--candidate-raw16"));
        if (candidateOption >= 0) {
            if (candidateOption + 1 >= application.arguments().size()) {
                output << "--candidate-raw16 requires WIDTHxHEIGHT:VALID_BITS:CFA\n";
                return 2;
            }
            raw16Candidate = ispview::tools::parseRaw16Candidate(
                application.arguments().at(candidateOption + 1),
                application.arguments().contains(QStringLiteral("--msb-aligned")),
                application.arguments().contains(QStringLiteral("--big-endian")));
            if (!raw16Candidate) {
                output << "Invalid RAW16 candidate; expected "
                          "WIDTHxHEIGHT:1-16:RGGB|GRBG|GBRG|BGGR\n";
                return 2;
            }
        }
        QString optionError;
        if (!ispview::tools::applyCandidateOrientationOption(
                application.arguments(), raw16Candidate, optionError)) {
            output << optionError << '\n';
            return 2;
        }
        return ispview::runSampleDirectory(output, application.arguments().at(sampleOption + 1),
                                           raw16Candidate)
                   ? 0
                   : 3;
    }

    QTemporaryDir directory;
    if (!directory.isValid()) {
        output << "Could not create benchmark directory\n";
        return 2;
    }
    if (!ispview::runFrameSize(output, directory.path(), QStringLiteral("4K"),
                               ispview::kDefaultFrameSize)) {
        output << "4K GPU benchmark failed\n";
        return 3;
    }
    if (application.arguments().contains(QStringLiteral("--48mp")) &&
        !ispview::runFrameSize(output, directory.path(), QStringLiteral("48MP"),
                               ispview::kLargeFrameSize)) {
        output << "48MP GPU benchmark failed\n";
        return 4;
    }
    if (application.arguments().contains(QStringLiteral("--bayer"))) {
        if (!ispview::runBayerFrameSize(output, directory.path(), QStringLiteral("4K"),
                                        ispview::kDefaultFrameSize)) {
            output << "4K Bayer GPU benchmark failed\n";
            return 5;
        }
        if (application.arguments().contains(QStringLiteral("--48mp")) &&
            !ispview::runBayerFrameSize(output, directory.path(), QStringLiteral("48MP"),
                                        ispview::kLargeFrameSize)) {
            output << "48MP Bayer GPU benchmark failed\n";
            return 6;
        }
    }
    if (application.arguments().contains(QStringLiteral("--pipeline"))) {
        output << "\nSource\tFormat\tPurpose\tColdReadyMs\tColdSubmitMs\tPrefetchWaitMs"
                  "\tHitReadyMs\tHitSubmitMs\n";
        if (!ispview::runPipelineFrameSize(output, directory.path(), QStringLiteral("4K"),
                                           ispview::kDefaultFrameSize,
                                           ispview::DecodePurpose::Full)) {
            output << "4K pipeline benchmark failed\n";
            return 7;
        }
        if (application.arguments().contains(QStringLiteral("--48mp")) &&
            !ispview::runPipelineFrameSize(output, directory.path(), QStringLiteral("48MP"),
                                           ispview::kLargeFrameSize,
                                           ispview::DecodePurpose::Preview)) {
            output << "48MP pipeline benchmark failed\n";
            return 8;
        }
    }
    return 0;
}
