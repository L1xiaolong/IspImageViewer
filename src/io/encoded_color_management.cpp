#include "io/encoded_color_management.h"

#include <QByteArray>
#include <QColorSpace>
#include <QCryptographicHash>

#include <algorithm>
#include <cstring>
#include <limits>

#if ISPVIEW_HAS_LCMS2
#include <lcms2.h>
#endif

namespace ispview {
namespace {

constexpr qsizetype kMaximumIccProfileBytes = 64LL * 1024 * 1024;
constexpr qsizetype kMaximumProfileTextLength = 512;

QString boundedProfileText(QString text) {
    text.replace(QChar::Null, QChar::ReplacementCharacter);
    for (QChar& character : text) {
        if (character.category() == QChar::Other_Control && character != QLatin1Char('\n') &&
            character != QLatin1Char('\t')) {
            character = QChar::ReplacementCharacter;
        }
    }
    return text.trimmed().left(kMaximumProfileTextLength);
}

QString profileFingerprint(const QByteArray& profile) {
    return QString::fromLatin1(
        QCryptographicHash::hash(profile, QCryptographicHash::Sha256).toHex().left(16));
}

#if ISPVIEW_HAS_LCMS2

constexpr qsizetype kMaximumColorWarningLength = 512;
constexpr int kTransformRowsPerChunk = 64;

QString boundedWarning(QString warning) {
    warning.replace(QChar::Null, QChar::ReplacementCharacter);
    return warning.trimmed().left(kMaximumColorWarningLength);
}

struct LcmsErrorState {
    QString message;
};

void lcmsErrorHandler(cmsContext context, cmsUInt32Number, const char* text) {
    if (auto* state = static_cast<LcmsErrorState*>(cmsGetContextUserData(context))) {
        state->message = boundedWarning(QString::fromUtf8(text));
    }
}

struct LcmsResources {
    cmsContext context = nullptr;
    cmsHPROFILE sourceProfile = nullptr;
    cmsHPROFILE destinationProfile = nullptr;
    cmsHTRANSFORM transform = nullptr;

    ~LcmsResources() {
        if (transform) {
            cmsDeleteTransform(transform);
        }
        if (destinationProfile) {
            cmsCloseProfile(destinationProfile);
        }
        if (sourceProfile) {
            cmsCloseProfile(sourceProfile);
        }
        if (context) {
            cmsDeleteContext(context);
        }
    }
};

QString profileDescription(cmsHPROFILE profile, const QString& fallback) {
    const cmsUInt32Number required =
        cmsGetProfileInfoUTF8(profile, cmsInfoDescription, "en", "US", nullptr, 0);
    if (required <= 1 || required > 4096) {
        return fallback;
    }
    QByteArray buffer(static_cast<qsizetype>(required), Qt::Uninitialized);
    if (cmsGetProfileInfoUTF8(profile, cmsInfoDescription, "en", "US", buffer.data(), required) ==
        0) {
        return fallback;
    }
    return boundedProfileText(QString::fromUtf8(buffer.constData()));
}

#endif

} // namespace

bool EncodedColorManagement::isAvailable() {
#if ISPVIEW_HAS_LCMS2
    return true;
#else
    return false;
#endif
}

QString EncodedColorManagement::version() {
#if ISPVIEW_HAS_LCMS2
    return QStringLiteral("%1.%2")
        .arg(LCMS_VERSION / 1000)
        .arg((LCMS_VERSION % 1000) / 10);
#else
    return {};
#endif
}

void EncodedColorManagement::normalizeToSrgb(QImage& image, ImageMetadata& metadata) {
    const QColorSpace sourceColorSpace = image.colorSpace();
    if (!sourceColorSpace.isValid()) {
        return;
    }
    const QByteArray embeddedProfile = sourceColorSpace.iccProfile();
    if (embeddedProfile.isEmpty()) {
        return;
    }

    ImageMetadata::ColorProfile colorProfile;
    colorProfile.sourceDescription = boundedProfileText(sourceColorSpace.description());
    if (colorProfile.sourceDescription.isEmpty()) {
        colorProfile.sourceDescription = QStringLiteral("Embedded ICC profile");
    }
    colorProfile.destinationColorSpace = QStringLiteral("Unchanged");
    if (embeddedProfile.size() > kMaximumIccProfileBytes) {
        metadata.colorWarning = QStringLiteral("Embedded ICC profile exceeds the 64 MiB limit");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    colorProfile.sourceFingerprint = profileFingerprint(embeddedProfile);

#if ISPVIEW_HAS_LCMS2
    LcmsErrorState errorState;
    LcmsResources resources;
    resources.context = cmsCreateContext(nullptr, &errorState);
    if (!resources.context) {
        metadata.colorWarning = QStringLiteral("LittleCMS could not create a conversion context");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    cmsSetLogErrorHandlerTHR(resources.context, lcmsErrorHandler);
    resources.sourceProfile = cmsOpenProfileFromMemTHR(
        resources.context, embeddedProfile.constData(),
        static_cast<cmsUInt32Number>(embeddedProfile.size()));
    if (!resources.sourceProfile) {
        metadata.colorWarning = errorState.message.isEmpty()
                                    ? QStringLiteral("LittleCMS rejected the embedded ICC profile")
                                    : errorState.message;
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    colorProfile.sourceDescription =
        profileDescription(resources.sourceProfile, colorProfile.sourceDescription);
    if (cmsGetColorSpace(resources.sourceProfile) != cmsSigRgbData) {
        metadata.colorWarning = QStringLiteral("Only embedded RGB ICC profiles are supported");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    resources.destinationProfile = cmsCreate_sRGBProfileTHR(resources.context);
    if (!resources.destinationProfile) {
        metadata.colorWarning = QStringLiteral("LittleCMS could not create the sRGB profile");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    resources.transform = cmsCreateTransformTHR(
        resources.context, resources.sourceProfile, TYPE_RGBA_8, resources.destinationProfile,
        TYPE_RGBA_8, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA);
    if (!resources.transform) {
        metadata.colorWarning = errorState.message.isEmpty()
                                    ? QStringLiteral("LittleCMS could not create the ICC transform")
                                    : errorState.message;
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    colorProfile.renderingIntent = QStringLiteral("Relative colorimetric");

    if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }
    image.detach();
    const qsizetype rowBytes = static_cast<qsizetype>(image.width()) * 4;
    const int maximumChunkRows = std::min(kTransformRowsPerChunk, image.height());
    QByteArray sourceChunk(rowBytes * maximumChunkRows, Qt::Uninitialized);
    if (image.bytesPerLine() != rowBytes ||
        sourceChunk.size() != rowBytes * maximumChunkRows) {
        metadata.colorWarning = QStringLiteral("Could not allocate an ICC chunk buffer");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    const quint64 maximumChunkPixels = quint64(image.width()) * quint64(maximumChunkRows);
    if (maximumChunkPixels > std::numeric_limits<cmsUInt32Number>::max()) {
        metadata.colorWarning = QStringLiteral("ICC transform chunk exceeds the pixel limit");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    for (int firstRow = 0; firstRow < image.height(); firstRow += kTransformRowsPerChunk) {
        const int chunkRows = std::min(kTransformRowsPerChunk, image.height() - firstRow);
        const qsizetype chunkBytes = rowBytes * chunkRows;
        const quint64 chunkPixels = quint64(image.width()) * quint64(chunkRows);
        std::memcpy(sourceChunk.data(), image.constScanLine(firstRow),
                    static_cast<std::size_t>(chunkBytes));
        cmsDoTransform(resources.transform, sourceChunk.constData(), image.scanLine(firstRow),
                       static_cast<cmsUInt32Number>(chunkPixels));
    }
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    colorProfile.destinationColorSpace = QStringLiteral("sRGB");
    colorProfile.transformEngine = QStringLiteral("LittleCMS %1").arg(version());
    colorProfile.converted = true;
#else
    colorProfile.transformEngine = QStringLiteral("Not applied — LittleCMS unavailable");
#endif
    metadata.colorProfile = std::move(colorProfile);
}

} // namespace ispview
