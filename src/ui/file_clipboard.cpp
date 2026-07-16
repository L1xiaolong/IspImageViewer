#include "ui/file_clipboard.h"

#include "ui/local_file_drop.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>

namespace ispview {

void FileClipboard::setPaths(const QStringList& paths, bool cut) {
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString& path : paths) {
        urls.append(QUrl::fromLocalFile(path));
    }
    auto* mimeData = new QMimeData;
    mimeData->setUrls(urls);
    if (cut) {
        mimeData->setData(QString::fromLatin1(CutMimeType), QByteArrayLiteral("1"));
    }
    QApplication::clipboard()->setMimeData(mimeData);
}

FileClipboardContents FileClipboard::contents() {
    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    return {localFileDropPaths(mimeData),
            mimeData && mimeData->hasFormat(QString::fromLatin1(CutMimeType))};
}

bool FileClipboard::hasFiles() { return !contents().paths.isEmpty(); }

void FileClipboard::clear() { QApplication::clipboard()->clear(); }

} // namespace ispview
