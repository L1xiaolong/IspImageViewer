#include "io/single_file_rename.h"

#include "io/raw_preset_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace ispview {
namespace {

QString portableFileNameError(const QString& fileName) {
    if (fileName.isEmpty() || fileName == QStringLiteral(".") ||
        fileName == QStringLiteral("..")) {
        return QStringLiteral("The filename is empty or reserved");
    }
    if (fileName.size() > 240 || fileName.endsWith(QLatin1Char('.')) ||
        fileName.endsWith(QLatin1Char(' '))) {
        return QStringLiteral("The filename is not portable between Windows and macOS");
    }
    static const QString invalidCharacters = QStringLiteral("<>:\"/\\|?*");
    for (const QChar character : fileName) {
        if (character.unicode() < 0x20 || invalidCharacters.contains(character)) {
            return QStringLiteral("The filename contains an invalid character");
        }
    }
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    return reserved.match(fileName).hasMatch()
               ? QStringLiteral("The filename is reserved on Windows")
               : QString{};
}

void setError(QString* destination, const QString& message) {
    if (destination) {
        *destination = message;
    }
}

} // namespace

bool SingleFileRename::execute(const QString& sourcePath, const QString& destinationPath,
                               QString* error) {
    const QFileInfo source(sourcePath);
    const QFileInfo destination(destinationPath);
    if (!source.exists() || (!source.isFile() && !source.isDir())) {
        setError(error, QStringLiteral("The source item no longer exists"));
        return false;
    }
    if (source.absolutePath() != destination.absolutePath()) {
        setError(error, QStringLiteral("Rename cannot move a file to another folder"));
        return false;
    }
    if (const QString nameError = portableFileNameError(destination.fileName());
        !nameError.isEmpty()) {
        setError(error, nameError);
        return false;
    }
    if (QFileInfo::exists(destinationPath)) {
        setError(error, QStringLiteral("A file with that name already exists"));
        return false;
    }

    const QString sourceSidecar =
        source.isFile() ? RawPresetStore::sidecarPath(sourcePath) : QString{};
    const QString destinationSidecar =
        source.isFile() ? RawPresetStore::sidecarPath(destinationPath) : QString{};
    if (QFileInfo::exists(sourceSidecar) && QFileInfo::exists(destinationSidecar)) {
        setError(error, QStringLiteral("The destination RAW/YUV sidecar already exists"));
        return false;
    }
    const bool renamed = source.isDir() ? QDir().rename(sourcePath, destinationPath)
                                        : QFile::rename(sourcePath, destinationPath);
    if (!renamed) {
        setError(error, QStringLiteral("The operating system could not rename the item"));
        return false;
    }
    if (QFileInfo::exists(sourceSidecar) && !QFile::rename(sourceSidecar, destinationSidecar)) {
        if (!QFile::rename(destinationPath, sourcePath)) {
            setError(error, QStringLiteral(
                                "The sidecar rename failed and the image filename could not be "
                                "restored"));
        } else {
            setError(error,
                     QStringLiteral("The sidecar rename failed; the image filename was restored"));
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace ispview
