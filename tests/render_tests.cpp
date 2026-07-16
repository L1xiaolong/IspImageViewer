#include "io/raw_image_decoder.h"
#include "render/bayer_render_parameters.h"
#include "render/image_canvas.h"
#include "render/yuv_render_parameters.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace ispview {
namespace {

constexpr auto kRequireNativeRhiEnvironment = "ISPVIEW_REQUIRE_NATIVE_RHI_TESTS";

bool nativeRhiIsRequired() {
    bool parsed = false;
    const int value = qEnvironmentVariableIntValue(kRequireNativeRhiEnvironment, &parsed);
    return parsed && value != 0;
}

bool nativeSurfaceIsAvailable() {
    if (!QGuiApplication::primaryScreen()) {
        return false;
    }
    const QString platform = QGuiApplication::platformName().toLower();
    return platform != QStringLiteral("offscreen") && platform != QStringLiteral("minimal");
}

QString requestedBackendName() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("Metal");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Direct3D 11");
#else
    return QStringLiteral("platform default");
#endif
}

int testCfaChannel(BayerPattern pattern, int x, int y) {
    const bool evenX = (x & 1) == 0;
    const bool evenY = (y & 1) == 0;
    switch (pattern) {
    case BayerPattern::RGGB:
        return evenY ? (evenX ? 0 : 1) : (evenX ? 1 : 2);
    case BayerPattern::GRBG:
        return evenY ? (evenX ? 1 : 0) : (evenX ? 2 : 1);
    case BayerPattern::GBRG:
        return evenY ? (evenX ? 1 : 2) : (evenX ? 0 : 1);
    case BayerPattern::BGGR:
        return evenY ? (evenX ? 2 : 1) : (evenX ? 1 : 0);
    }
    return 1;
}

QByteArray packedBayerFixture(RawPixelFormat format, BayerPattern pattern, bool littleEndian,
                              int validBitsOverride = 0, bool msbAligned = false) {
    constexpr int width = 4;
    constexpr int height = 4;
    const int bits = format == RawPixelFormat::MipiRaw10
                         ? 10
                         : (format == RawPixelFormat::MipiRaw12
                                ? 12
                                : (validBitsOverride > 0 ? validBitsOverride : 16));
    const quint16 maximum = static_cast<quint16>((1U << bits) - 1U);
    const std::array<quint16, 3> levels{static_cast<quint16>(maximum / 4),
                                        static_cast<quint16>(maximum / 2),
                                        static_cast<quint16>(maximum * 3 / 4)};
    QByteArray bytes;
    for (int y = 0; y < height; ++y) {
        std::array<quint16, width> row{};
        for (int x = 0; x < width; ++x) {
            row[static_cast<std::size_t>(x)] =
                levels.at(static_cast<std::size_t>(testCfaChannel(pattern, x, y)));
        }
        if (format == RawPixelFormat::MipiRaw10) {
            for (quint16 value : row) {
                bytes.append(static_cast<char>(value >> 2));
            }
            quint8 low = 0;
            for (int lane = 0; lane < width; ++lane) {
                low |=
                    static_cast<quint8>((row[static_cast<std::size_t>(lane)] & 0x03) << (lane * 2));
            }
            bytes.append(static_cast<char>(low));
        } else if (format == RawPixelFormat::MipiRaw12) {
            for (int group = 0; group < width / 2; ++group) {
                const quint16 first = row[static_cast<std::size_t>(group * 2)];
                const quint16 second = row[static_cast<std::size_t>(group * 2 + 1)];
                bytes.append(static_cast<char>(first >> 4));
                bytes.append(static_cast<char>(second >> 4));
                bytes.append(static_cast<char>((first & 0x0F) | ((second & 0x0F) << 4)));
            }
        } else {
            for (quint16 value : row) {
                if (msbAligned && bits < 16) {
                    value = static_cast<quint16>(value << (16 - bits));
                }
                const char low = static_cast<char>(value & 0xFF);
                const char high = static_cast<char>(value >> 8);
                bytes.append(littleEndian ? low : high);
                bytes.append(littleEndian ? high : low);
            }
        }
    }
    return bytes;
}

} // namespace

class RenderTests final : public QObject {
    Q_OBJECT

  private slots:
    void yuvUniformsDescribeMatrixRangeAndLayout();
    void bayerUniformsDescribePackingAndDisplayTransform();
    void boundedFallbackUsesLogicalFrameGeometry();
    void imageCanvasStoresComparisonStateWithoutCopyingFrames();
    void encodedComparisonModesRenderThroughPlatformRhi();
    void highPrecisionEncodedImagesRenderThroughPlatformRhi();
    void rawPlaneComparisonsRenderThroughPlatformRhi();
    void yuvPlanesRenderThroughPlatformRhi();
    void bayerPlanesRenderThroughPlatformRhi();
};

void RenderTests::yuvUniformsDescribeMatrixRangeAndLayout() {
    RawImageParameters parameters;
    parameters.format = RawPixelFormat::NV21;
    parameters.yuvMatrix = YuvMatrix::BT709;
    parameters.range = QuantizationRange::Limited;
    auto values = makeYuvRenderUniformData(parameters);
    QCOMPARE(values[0], 1.5748F);
    QCOMPARE(values[4], 16.0F / 255.0F);
    QCOMPARE(values[5], 219.0F / 255.0F);
    QCOMPARE(values[6], 128.0F / 255.0F);
    QCOMPARE(values[7], 224.0F / 255.0F);
    QCOMPARE(values[8], 0.0F);
    QCOMPARE(values[9], 1.0F);

    parameters.format = RawPixelFormat::P010;
    parameters.msbAligned = true;
    parameters.orientation = ImageOrientation::Rotate90Clockwise;
    values = makeYuvRenderUniformData(parameters);
    QCOMPARE(values[4], 16.0F * 4.0F * 64.0F / 65535.0F);
    QCOMPARE(values[6], 512.0F * 64.0F / 65535.0F);
    QCOMPARE(values[9], 0.0F);
    QCOMPARE(values[10], 1.0F);
}

void RenderTests::bayerUniformsDescribePackingAndDisplayTransform() {
    RawImageParameters parameters;
    parameters.size = {8, 6};
    parameters.format = RawPixelFormat::MipiRaw12;
    parameters.rowStride = 16;
    parameters.bayerPattern = BayerPattern::GBRG;
    parameters.blackLevel = 64;
    parameters.whiteLevel = 4095;
    parameters.whiteBalanceGains = {2.0, 1.0, 0.5};
    parameters.colorCorrectionMatrix = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
    parameters.displayGamma = 2.0;
    parameters.orientation = ImageOrientation::Rotate270Clockwise;
    const auto values = makeBayerRenderUniformData(parameters);
    QCOMPARE(values[0], 8.0F);
    QCOMPARE(values[1], 6.0F);
    QCOMPARE(values[2], 16.0F);
    QCOMPARE(values[3], 1.0F);
    QCOMPARE(values[4], 2.0F);
    QCOMPARE(values[7], 12.0F);
    QCOMPARE(values[8], 64.0F);
    QCOMPARE(values[9], 4095.0F);
    QCOMPARE(values[10], 2.0F);
    QCOMPARE(values[11], 3.0F);
    QCOMPARE(values[12], 2.0F);
    QCOMPARE(values[14], 0.5F);
    QCOMPARE(values[16], 1.0F);
    QCOMPARE(values[17], 4.0F);
    QCOMPARE(values[18], 7.0F);
    QCOMPARE(values[20], 2.0F);
    QCOMPARE(values[24], 3.0F);
    QCOMPARE(values[26], 9.0F);
}

void RenderTests::boundedFallbackUsesLogicalFrameGeometry() {
    QImage fallback(1600, 1200, QImage::Format_RGBA8888);
    fallback.fill(Qt::black);
    auto frame = std::make_shared<ImageFrame>();
    frame->descriptor.size = {4000, 3000};
    frame->storage = std::move(fallback);

    ImageCanvas canvas;
    canvas.resize(1000, 1000);
    canvas.setFrame(std::move(frame));
    QCOMPARE(canvas.viewState().pixelsPerImagePixel, 0.25);
    canvas.actualPixels();
    QCOMPARE(canvas.viewState().pixelsPerImagePixel, 1.0);

    QImage rawPreview(320, 240, QImage::Format_RGBA8888);
    rawPreview.fill(Qt::black);
    auto rawFrame = std::make_shared<ImageFrame>();
    rawFrame->descriptor.size = rawPreview.size();
    RawImageParameters rawParameters;
    rawParameters.size = {4000, 3000};
    rawParameters.format = RawPixelFormat::Raw16;
    rawParameters.orientation = ImageOrientation::Rotate90Clockwise;
    rawFrame->rawParameters = rawParameters;
    rawFrame->storage = std::move(rawPreview);
    canvas.setFrame(std::move(rawFrame));
    QCOMPARE(canvas.viewState().pixelsPerImagePixel, 0.25);
}

void RenderTests::imageCanvasStoresComparisonStateWithoutCopyingFrames() {
    auto primary = std::make_shared<ImageFrame>();
    QImage primaryImage(4, 4, QImage::Format_RGBA8888);
    primaryImage.fill(Qt::red);
    primary->descriptor.size = primaryImage.size();
    primary->storage = std::move(primaryImage);
    auto secondary = std::make_shared<ImageFrame>();
    QImage secondaryImage(8, 6, QImage::Format_RGBA8888);
    secondaryImage.fill(Qt::blue);
    secondary->descriptor.size = secondaryImage.size();
    secondary->storage = std::move(secondaryImage);

    ImageCanvas canvas;
    canvas.setFrame(primary);
    canvas.setComparisonFrame(secondary);
    canvas.setCompareMode(ImageCompareMode::VerticalSplit);
    canvas.setCompareAmount(0.25F);
    QCOMPARE(canvas.frame().get(), primary.get());
    QCOMPARE(canvas.comparisonFrame().get(), secondary.get());
    QCOMPARE(canvas.compareMode(), ImageCompareMode::VerticalSplit);
    QCOMPARE(canvas.compareAmount(), 0.25F);
    canvas.setCompareAmount(2.0F);
    QCOMPARE(canvas.compareAmount(), 1.0F);
}

void RenderTests::encodedComparisonModesRenderThroughPlatformRhi() {
    if (!nativeSurfaceIsAvailable()) {
        if (nativeRhiIsRequired()) {
            QFAIL("Native comparison shader acceptance requires a screen surface");
        }
        QSKIP("No screen is available for a native QRhiWidget surface");
    }

    const auto makeFrame = [](const QColor& color) {
        auto frame = std::make_shared<ImageFrame>();
        QImage image(4, 4, QImage::Format_RGBA8888);
        image.fill(color);
        frame->descriptor.size = image.size();
        frame->storage = std::move(image);
        return frame;
    };
    const auto primary = makeFrame(Qt::red);
    const auto secondary = makeFrame(Qt::blue);
    ImageCanvas canvas;
    canvas.resize(160, 120);
    canvas.setFrame(primary);
    canvas.setComparisonFrame(secondary);
    QSignalSpy submitted(&canvas, &QRhiWidget::frameSubmitted);
    QSignalSpy failed(&canvas, &QRhiWidget::renderFailed);
    canvas.show();

    const auto renderMode = [&](ImageCompareMode mode, float amount) {
        submitted.clear();
        failed.clear();
        canvas.setCompareMode(mode);
        canvas.setCompareAmount(amount);
        for (int elapsed = 0; submitted.isEmpty() && failed.isEmpty() && elapsed < 5000;
             elapsed += 20) {
            QTest::qWait(20);
        }
        if (submitted.isEmpty() || !failed.isEmpty()) {
            return QImage{};
        }
        return canvas.grabFramebuffer();
    };
    const auto close = [](const QColor& actual, const QColor& expected, int tolerance = 2) {
        return std::abs(actual.red() - expected.red()) <= tolerance &&
               std::abs(actual.green() - expected.green()) <= tolerance &&
               std::abs(actual.blue() - expected.blue()) <= tolerance;
    };

    QImage framebuffer = renderMode(ImageCompareMode::VerticalSplit, 0.5F);
    QVERIFY(!framebuffer.isNull());
    QVERIFY(close(framebuffer.pixelColor(framebuffer.width() * 3 / 8, framebuffer.height() / 2),
                  Qt::red));
    QVERIFY(close(framebuffer.pixelColor(framebuffer.width() * 5 / 8, framebuffer.height() / 2),
                  Qt::blue));
    QVERIFY(close(framebuffer.pixelColor(framebuffer.width() / 2, framebuffer.height() / 2),
                  Qt::white));
    framebuffer = renderMode(ImageCompareMode::HorizontalSplit, 0.5F);
    QVERIFY(!framebuffer.isNull());
    QVERIFY(close(framebuffer.pixelColor(framebuffer.width() / 2, framebuffer.height() * 3 / 8),
                  Qt::red));
    QVERIFY(close(framebuffer.pixelColor(framebuffer.width() / 2, framebuffer.height() * 5 / 8),
                  Qt::blue));
    QVERIFY(close(framebuffer.pixelColor(framebuffer.width() / 2, framebuffer.height() / 2),
                  Qt::white));
}

void RenderTests::highPrecisionEncodedImagesRenderThroughPlatformRhi() {
    if (!nativeSurfaceIsAvailable()) {
        if (nativeRhiIsRequired()) {
            QFAIL("Native high-precision texture acceptance requires a screen surface");
        }
        QSKIP("No screen is available for a native QRhiWidget surface");
    }

    const auto render = [](QImage image) {
        auto frame = std::make_shared<ImageFrame>();
        frame->descriptor.size = image.size();
        frame->descriptor.storageBits = 16;
        frame->storage = std::move(image);
        ImageCanvas canvas;
        canvas.resize(160, 120);
        canvas.setFrame(std::move(frame));
        QSignalSpy submitted(&canvas, &QRhiWidget::frameSubmitted);
        QSignalSpy failed(&canvas, &QRhiWidget::renderFailed);
        canvas.show();
        for (int elapsed = 0; submitted.isEmpty() && failed.isEmpty() && elapsed < 5000;
             elapsed += 20) {
            QTest::qWait(20);
        }
        return failed.isEmpty() && !submitted.isEmpty() ? canvas.grabFramebuffer() : QImage{};
    };

    QImage halfFloat(2, 2, QImage::Format_RGBA16FPx4);
    auto* halfPixels = reinterpret_cast<QRgbaFloat16*>(halfFloat.bits());
    std::fill_n(halfPixels, 4,
                QRgbaFloat16{qfloat16(0.25F), qfloat16(0.5F), qfloat16(0.75F), qfloat16(1.0F)});
    QImage framebuffer = render(std::move(halfFloat));
    QVERIFY(!framebuffer.isNull());
    QColor actual = framebuffer.pixelColor(framebuffer.width() / 2, framebuffer.height() / 2);
    QVERIFY(std::abs(actual.red() - 64) <= 2);
    QVERIFY(std::abs(actual.green() - 128) <= 2);
    QVERIFY(std::abs(actual.blue() - 191) <= 2);

    QImage rgba64(2, 2, QImage::Format_RGBA64);
    rgba64.fill(QColor::fromRgba64(16384, 32768, 49151, 65535));
    framebuffer = render(std::move(rgba64));
    QVERIFY(!framebuffer.isNull());
    actual = framebuffer.pixelColor(framebuffer.width() / 2, framebuffer.height() / 2);
    QVERIFY(std::abs(actual.red() - 64) <= 2);
    QVERIFY(std::abs(actual.green() - 128) <= 2);
    QVERIFY(std::abs(actual.blue() - 191) <= 2);
}

void RenderTests::rawPlaneComparisonsRenderThroughPlatformRhi() {
    if (!nativeSurfaceIsAvailable()) {
        if (nativeRhiIsRequired()) {
            QFAIL("Native Full Plane comparison acceptance requires a screen surface");
        }
        QSKIP("No screen is available for a native QRhiWidget surface");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    RawImageDecoder decoder;

    RawImageParameters yuvParameters;
    yuvParameters.size = {2, 2};
    yuvParameters.format = RawPixelFormat::NV12;
    yuvParameters.range = QuantizationRange::Full;
    yuvParameters.yuvMatrix = YuvMatrix::BT601;
    const auto writeNv12 = [&](const QString& name, int luma) {
        QByteArray bytes(4, static_cast<char>(luma));
        bytes.append(static_cast<char>(128));
        bytes.append(static_cast<char>(128));
        const QString path = directory.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            return DecodeResult{{}, file.errorString()};
        }
        file.close();
        return decoder.decode({path, DecodePurpose::Full, {}, yuvParameters});
    };
    const DecodeResult yuvPrimary = writeNv12(QStringLiteral("primary_nv12.yuv"), 64);
    const DecodeResult yuvCandidate = writeNv12(QStringLiteral("candidate_nv12.yuv"), 96);
    QVERIFY2(yuvPrimary.succeeded(), qPrintable(yuvPrimary.error));
    QVERIFY2(yuvCandidate.succeeded(), qPrintable(yuvCandidate.error));

    ImageCanvas yuvCanvas;
    yuvCanvas.resize(160, 120);
    yuvCanvas.setFrame(yuvPrimary.frame);
    yuvCanvas.setComparisonFrame(yuvCandidate.frame);
    QSignalSpy yuvSubmitted(&yuvCanvas, &QRhiWidget::frameSubmitted);
    QSignalSpy yuvFailed(&yuvCanvas, &QRhiWidget::renderFailed);
    yuvCanvas.show();
    QTRY_VERIFY_WITH_TIMEOUT(!yuvSubmitted.isEmpty() || !yuvFailed.isEmpty(), 5000);
    QCOMPARE(yuvFailed.size(), 0);
    QVERIFY(yuvCanvas.usingGpuYuvPlanes());
    QVERIFY(!yuvCanvas.usingGpuYuvComparison());

    yuvSubmitted.clear();
    yuvCanvas.setCompareMode(ImageCompareMode::VerticalSplit);
    QTRY_VERIFY_WITH_TIMEOUT(!yuvSubmitted.isEmpty() || !yuvFailed.isEmpty(), 5000);
    QCOMPARE(yuvFailed.size(), 0);
    QVERIFY(yuvCanvas.usingGpuYuvComparison());
    QImage framebuffer = yuvCanvas.grabFramebuffer();
    QVERIFY(!framebuffer.isNull());
    const QColor yuvLeft =
        framebuffer.pixelColor(framebuffer.width() * 3 / 8, framebuffer.height() / 2);
    const QColor yuvRight =
        framebuffer.pixelColor(framebuffer.width() * 5 / 8, framebuffer.height() / 2);
    QVERIFY(yuvLeft != yuvRight);

    yuvSubmitted.clear();
    yuvCanvas.setCompareMode(ImageCompareMode::Single);
    QTRY_VERIFY_WITH_TIMEOUT(!yuvSubmitted.isEmpty() || !yuvFailed.isEmpty(), 5000);
    QCOMPARE(yuvFailed.size(), 0);
    QVERIFY(!yuvCanvas.usingGpuYuvComparison());

    RawImageParameters bayerParameters;
    bayerParameters.size = {4, 4};
    bayerParameters.format = RawPixelFormat::Raw16;
    bayerParameters.validBitsOverride = 12;
    bayerParameters.littleEndian = true;
    bayerParameters.blackLevel = 0;
    bayerParameters.whiteLevel = 4095;
    bayerParameters.displayGamma = 1.0;
    const auto writeRaw16 = [&](const QString& name, int value) {
        QByteArray bytes;
        bytes.reserve(4 * 4 * 2);
        for (int index = 0; index < 16; ++index) {
            bytes.append(static_cast<char>(value & 0xFF));
            bytes.append(static_cast<char>((value >> 8) & 0xFF));
        }
        const QString path = directory.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            return DecodeResult{{}, file.errorString()};
        }
        file.close();
        return decoder.decode({path, DecodePurpose::Full, {}, bayerParameters});
    };
    const DecodeResult bayerPrimary = writeRaw16(QStringLiteral("primary_raw16.raw"), 1000);
    const DecodeResult bayerCandidate = writeRaw16(QStringLiteral("candidate_raw16.raw"), 1200);
    QVERIFY2(bayerPrimary.succeeded(), qPrintable(bayerPrimary.error));
    QVERIFY2(bayerCandidate.succeeded(), qPrintable(bayerCandidate.error));

    ImageCanvas bayerCanvas;
    bayerCanvas.resize(160, 120);
    bayerCanvas.setFrame(bayerPrimary.frame);
    bayerCanvas.setComparisonFrame(bayerCandidate.frame);
    bayerCanvas.setCompareMode(ImageCompareMode::VerticalSplit);
    QSignalSpy bayerSubmitted(&bayerCanvas, &QRhiWidget::frameSubmitted);
    QSignalSpy bayerFailed(&bayerCanvas, &QRhiWidget::renderFailed);
    bayerCanvas.show();
    QTRY_VERIFY_WITH_TIMEOUT(!bayerSubmitted.isEmpty() || !bayerFailed.isEmpty(), 5000);
    QCOMPARE(bayerFailed.size(), 0);
    QVERIFY(bayerCanvas.usingGpuBayerComparison());
    framebuffer = bayerCanvas.grabFramebuffer();
    QVERIFY(!framebuffer.isNull());
    const QColor bayerLeft =
        framebuffer.pixelColor(framebuffer.width() * 3 / 8, framebuffer.height() / 2);
    const QColor bayerRight =
        framebuffer.pixelColor(framebuffer.width() * 5 / 8, framebuffer.height() / 2);
    QVERIFY(bayerLeft != bayerRight);
}

void RenderTests::yuvPlanesRenderThroughPlatformRhi() {
    if (!nativeSurfaceIsAvailable()) {
        if (nativeRhiIsRequired()) {
            QFAIL(qPrintable(
                QStringLiteral("%1=1, but Qt platform '%2' has no native screen surface for the "
                               "QRhiWidget %3 acceptance test")
                    .arg(QString::fromLatin1(kRequireNativeRhiEnvironment),
                         QGuiApplication::platformName(), requestedBackendName())));
        }
        QSKIP("No screen is available for a native QRhiWidget surface");
    }

    qInfo().noquote() << "Native RHI acceptance is running with requested backend"
                      << requestedBackendName() << "on Qt platform"
                      << QGuiApplication::platformName();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    struct TestCase {
        QString name;
        RawPixelFormat format;
        QByteArray bytes;
        bool littleEndian = true;
        ImageOrientation orientation = ImageOrientation::Normal;
        bool verifyGrid = false;
        qsizetype rowStride = 0;
        qsizetype chromaStride = 0;
    };
    const QByteArray yPlane(4, static_cast<char>(81));
    const QByteArray orientedNv12 = QByteArray::fromRawData("\x10\x40\x80\xEB\x80\x80", 6);
    QByteArray p010LittleEndian;
    QByteArray p010BigEndianPadded;
    const auto appendP010 = [](QByteArray& bytes, quint16 tenBitValue, bool littleEndian) {
        const quint16 stored = static_cast<quint16>(tenBitValue << 6);
        const char low = static_cast<char>(stored & 0xFF);
        const char high = static_cast<char>(stored >> 8);
        bytes.append(littleEndian ? low : high);
        bytes.append(littleEndian ? high : low);
    };
    for (int i = 0; i < 4; ++i) {
        appendP010(p010LittleEndian, 81 * 4, true);
    }
    appendP010(p010LittleEndian, 90 * 4, true);
    appendP010(p010LittleEndian, 240 * 4, true);
    for (int y = 0; y < 2; ++y) {
        appendP010(p010BigEndianPadded, 81 * 4, false);
        appendP010(p010BigEndianPadded, 81 * 4, false);
        appendP010(p010BigEndianPadded, 0, false);
    }
    appendP010(p010BigEndianPadded, 90 * 4, false);
    appendP010(p010BigEndianPadded, 240 * 4, false);
    appendP010(p010BigEndianPadded, 0, false);

    const std::array<TestCase, 6> cases{{
        {QStringLiteral("nv12"), RawPixelFormat::NV12,
         yPlane + QByteArray::fromRawData("\x5A\xF0", 2)},
        {QStringLiteral("nv12-rotate90"), RawPixelFormat::NV12, orientedNv12, true,
         ImageOrientation::Rotate90Clockwise, true},
        {QStringLiteral("nv21"), RawPixelFormat::NV21,
         yPlane + QByteArray::fromRawData("\xF0\x5A", 2)},
        {QStringLiteral("i420"), RawPixelFormat::I420,
         yPlane + QByteArray::fromRawData("\x5A\xF0", 2)},
        {QStringLiteral("p010-le"), RawPixelFormat::P010, p010LittleEndian},
        {QStringLiteral("p010-be-padded"), RawPixelFormat::P010, p010BigEndianPadded, false,
         ImageOrientation::Normal, false, 6, 6},
    }};

    const ImageCanvas backendProbe;
#if defined(Q_OS_MACOS)
    QCOMPARE(backendProbe.api(), QRhiWidget::Api::Metal);
#elif defined(Q_OS_WIN)
    QCOMPARE(backendProbe.api(), QRhiWidget::Api::Direct3D11);
#endif

    RawImageDecoder decoder;
    for (const TestCase& testCase : cases) {
        const QString path = directory.filePath(testCase.name + QStringLiteral(".yuv"));
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        QCOMPARE(file.write(testCase.bytes), testCase.bytes.size());
        file.close();

        RawImageParameters parameters;
        parameters.size = {2, 2};
        parameters.format = testCase.format;
        parameters.yuvMatrix = YuvMatrix::BT601;
        parameters.range = QuantizationRange::Limited;
        parameters.msbAligned = testCase.format == RawPixelFormat::P010;
        parameters.littleEndian = testCase.littleEndian;
        parameters.orientation = testCase.orientation;
        parameters.rowStride = testCase.rowStride;
        parameters.chromaStride = testCase.chromaStride;
        const DecodeResult result = decoder.decode({path, DecodePurpose::Full, {}, parameters});
        QVERIFY2(result.succeeded(),
                 qPrintable(testCase.name + QStringLiteral(": ") + result.error));
        const QImage* reference = result.frame->qImage();
        QVERIFY(reference != nullptr);
        const QColor expected = reference->pixelColor(0, 0);

        ImageCanvas canvas;
        canvas.resize(160, 120);
        canvas.setFrame(result.frame);
        QSignalSpy submitted(&canvas, &QRhiWidget::frameSubmitted);
        QSignalSpy failed(&canvas, &QRhiWidget::renderFailed);
        canvas.show();
        QTRY_VERIFY_WITH_TIMEOUT(!submitted.isEmpty() || !failed.isEmpty(), 5000);
        QCOMPARE(failed.size(), 0);
        QVERIFY(!submitted.isEmpty());
        QVERIFY2(canvas.usingGpuYuvPlanes(),
                 qPrintable(testCase.name + QStringLiteral(" used CPU fallback")));
        const QImage framebuffer = canvas.grabFramebuffer();
        QVERIFY(!framebuffer.isNull());
        constexpr int tolerance = 2;
        const auto colorsAreClose = [](const QColor& actual, const QColor& reference) {
            return std::abs(actual.red() - reference.red()) <= tolerance &&
                   std::abs(actual.green() - reference.green()) <= tolerance &&
                   std::abs(actual.blue() - reference.blue()) <= tolerance;
        };
        if (testCase.verifyGrid) {
            const double scale = std::min(framebuffer.width() / 2.0, framebuffer.height() / 2.0);
            const double left = (framebuffer.width() - 2.0 * scale) * 0.5;
            const double top = (framebuffer.height() - 2.0 * scale) * 0.5;
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 2; ++x) {
                    const QColor gridExpected = reference->pixelColor(x, y);
                    const QColor gridActual = framebuffer.pixelColor(
                        static_cast<int>(std::lround(left + (x + 0.5) * scale)),
                        static_cast<int>(std::lround(top + (y + 0.5) * scale)));
                    QVERIFY2(colorsAreClose(gridActual, gridExpected),
                             qPrintable(QStringLiteral("%1 GPU grid (%2,%3) %4,%5,%6; CPU %7,%8,%9")
                                            .arg(testCase.name)
                                            .arg(x)
                                            .arg(y)
                                            .arg(gridActual.red())
                                            .arg(gridActual.green())
                                            .arg(gridActual.blue())
                                            .arg(gridExpected.red())
                                            .arg(gridExpected.green())
                                            .arg(gridExpected.blue())));
                }
            }
        } else {
            const QColor actual =
                framebuffer.pixelColor(framebuffer.width() / 2, framebuffer.height() / 2);
            QVERIFY2(colorsAreClose(actual, expected),
                     qPrintable(QStringLiteral("%1 GPU pixel %2,%3,%4; CPU reference %5,%6,%7")
                                    .arg(testCase.name)
                                    .arg(actual.red())
                                    .arg(actual.green())
                                    .arg(actual.blue())
                                    .arg(expected.red())
                                    .arg(expected.green())
                                    .arg(expected.blue())));
        }
    }
}

void RenderTests::bayerPlanesRenderThroughPlatformRhi() {
    if (!nativeSurfaceIsAvailable()) {
        if (nativeRhiIsRequired()) {
            QFAIL(qPrintable(
                QStringLiteral("%1=1, but Qt platform '%2' has no native screen surface for the "
                               "QRhiWidget %3 Bayer acceptance test")
                    .arg(QString::fromLatin1(kRequireNativeRhiEnvironment),
                         QGuiApplication::platformName(), requestedBackendName())));
        }
        QSKIP("No screen is available for a native QRhiWidget surface");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    struct TestCase {
        QString name;
        RawPixelFormat format;
        BayerPattern pattern;
        bool littleEndian;
        int validBitsOverride;
        bool msbAligned;
    };
    const std::array<TestCase, 6> cases{{
        {QStringLiteral("raw10-rggb"), RawPixelFormat::MipiRaw10, BayerPattern::RGGB, true, 0,
         false},
        {QStringLiteral("raw12-grbg"), RawPixelFormat::MipiRaw12, BayerPattern::GRBG, true, 0,
         false},
        {QStringLiteral("raw16-gbrg-le"), RawPixelFormat::Raw16, BayerPattern::GBRG, true, 0,
         false},
        {QStringLiteral("raw16-bggr-be"), RawPixelFormat::Raw16, BayerPattern::BGGR, false, 0,
         false},
        {QStringLiteral("raw14-rggb-right"), RawPixelFormat::Raw16, BayerPattern::RGGB, true, 14,
         false},
        {QStringLiteral("raw14-bggr-msb"), RawPixelFormat::Raw16, BayerPattern::BGGR, true, 14,
         true},
    }};

    RawImageDecoder decoder;
    for (const TestCase& testCase : cases) {
        const QString path = directory.filePath(testCase.name + QStringLiteral(".raw"));
        const QByteArray bytes =
            packedBayerFixture(testCase.format, testCase.pattern, testCase.littleEndian,
                               testCase.validBitsOverride, testCase.msbAligned);
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();

        RawImageParameters parameters;
        parameters.size = {4, 4};
        parameters.format = testCase.format;
        parameters.bayerPattern = testCase.pattern;
        parameters.littleEndian = testCase.littleEndian;
        parameters.validBitsOverride = testCase.validBitsOverride;
        parameters.msbAligned = testCase.msbAligned;
        parameters.blackLevel = testCase.format == RawPixelFormat::MipiRaw10 ? 16 : 64;
        parameters.whiteBalanceGains = {1.1, 0.9, 0.8};
        parameters.colorCorrectionMatrix = {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0};
        parameters.displayGamma = 2.0;
        const DecodeResult result = decoder.decode({path, DecodePurpose::Full, {}, parameters});
        QVERIFY2(result.succeeded(),
                 qPrintable(testCase.name + QStringLiteral(": ") + result.error));
        const QImage* reference = result.frame->qImage();
        QVERIFY(reference != nullptr);
        const QColor expected = reference->pixelColor(1, 1);

        ImageCanvas canvas;
        canvas.resize(160, 120);
        canvas.setFrame(result.frame);
        QSignalSpy submitted(&canvas, &QRhiWidget::frameSubmitted);
        QSignalSpy failed(&canvas, &QRhiWidget::renderFailed);
        canvas.show();
        QTRY_VERIFY_WITH_TIMEOUT(!submitted.isEmpty() || !failed.isEmpty(), 5000);
        QCOMPARE(failed.size(), 0);
        QVERIFY(!submitted.isEmpty());
        QVERIFY2(canvas.usingGpuBayerPlane(),
                 qPrintable(testCase.name + QStringLiteral(" used CPU fallback")));
        const QImage framebuffer = canvas.grabFramebuffer();
        QVERIFY(!framebuffer.isNull());
        const QColor actual =
            framebuffer.pixelColor(framebuffer.width() / 2, framebuffer.height() / 2);
        constexpr int tolerance = 2;
        const bool close = std::abs(actual.red() - expected.red()) <= tolerance &&
                           std::abs(actual.green() - expected.green()) <= tolerance &&
                           std::abs(actual.blue() - expected.blue()) <= tolerance;
        QVERIFY2(close, qPrintable(QStringLiteral("%1 GPU pixel %2,%3,%4; CPU reference %5,%6,%7")
                                       .arg(testCase.name)
                                       .arg(actual.red())
                                       .arg(actual.green())
                                       .arg(actual.blue())
                                       .arg(expected.red())
                                       .arg(expected.green())
                                       .arg(expected.blue())));

        if (testCase.format == RawPixelFormat::MipiRaw10) {
            auto encoded = std::make_shared<ImageFrame>();
            encoded->descriptor.size = {2, 2};
            QImage encodedImage(2, 2, QImage::Format_RGBA8888);
            encodedImage.fill(Qt::green);
            encoded->storage = std::move(encodedImage);
            submitted.clear();
            failed.clear();
            canvas.setFrame(std::move(encoded));
            QTRY_VERIFY_WITH_TIMEOUT(!submitted.isEmpty() || !failed.isEmpty(), 5000);
            QCOMPARE(failed.size(), 0);
            QVERIFY(!canvas.usingGpuYuvPlanes());
            QVERIFY(!canvas.usingGpuBayerPlane());

            submitted.clear();
            failed.clear();
            canvas.setFrame(result.frame);
            QTRY_VERIFY_WITH_TIMEOUT(!submitted.isEmpty() || !failed.isEmpty(), 5000);
            QCOMPARE(failed.size(), 0);
            QVERIFY(canvas.usingGpuBayerPlane());
        }
    }
}

} // namespace ispview

QTEST_MAIN(ispview::RenderTests)
#include "render_tests.moc"
