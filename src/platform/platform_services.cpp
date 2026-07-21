#include "platform/platform_services.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

namespace ispview {

bool PlatformServices::revealInFileManager(const QString& path) {
#if defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#elif defined(Q_OS_WINDOWS)
    return QProcess::startDetached(
        QStringLiteral("explorer.exe"),
        {QStringLiteral("/select,%1").arg(QDir::toNativeSeparators(path))});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

bool PlatformServices::openDirectoryInFileManager(const QString& path) {
    const QFileInfo info(path);
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    return !directory.isEmpty() && QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

} // namespace ispview
