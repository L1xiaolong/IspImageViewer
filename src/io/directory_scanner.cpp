#include "io/directory_scanner.h"

#include "io/supported_image_formats.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

#include <algorithm>
#include <utility>

namespace ispview {
DirectoryScanner::DirectoryScanner(QObject* parent) : QObject(parent) {}

quint64 DirectoryScanner::scanAsync(const QString& directory) {
    const quint64 generation = ++generation_;
    const QPointer<DirectoryScanner> self(this);
    QThreadPool::globalInstance()->start(
        [self, directory, generation] {
            auto files = scan(directory);
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, directory, files = std::move(files), generation] {
                    if (self) {
                        emit self->scanFinished(directory, files, generation);
                    }
                },
                Qt::QueuedConnection);
        },
        0);
    return generation;
}

quint64 DirectoryScanner::scanImageFoldersAsync(const QString& directory) {
    const quint64 generation = ++generation_;
    const QPointer<DirectoryScanner> self(this);
    QThreadPool::globalInstance()->start(
        [self, directory, generation] {
            auto files = scanImageFoldersRecursively(directory);
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, directory, files = std::move(files), generation] {
                    if (self) {
                        emit self->scanFinished(directory, files, generation);
                    }
                },
                Qt::QueuedConnection);
        },
        0);
    return generation;
}

QVector<ImageFileRecord> DirectoryScanner::scan(const QString& directory) {
    QDir dir(directory);
    const QStringList filters = supportedImageNameFilters();
    const QFileInfoList entries =
        dir.entryInfoList(filters, QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::NoSort);

    const QFileInfoList directories = dir.entryInfoList(
        QDir::Dirs | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::NoSort);

    QVector<ImageFileRecord> result;
    result.reserve(entries.size() + directories.size());
    for (const QFileInfo& info : directories) {
        result.push_back({info.absoluteFilePath(), info.fileName(), 0, info.lastModified(), true});
    }
    for (const auto& info : entries) {
        result.push_back(
            {info.absoluteFilePath(), info.fileName(), info.size(), info.lastModified()});
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.begin(), result.end(), [&collator](const auto& left, const auto& right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory;
        }
        return collator.compare(left.fileName, right.fileName) < 0;
    });
    return result;
}

QVector<ImageFileRecord> DirectoryScanner::scanImageFoldersRecursively(const QString& directory) {
    const QDir root(directory);
    const QString rootPath = QDir::cleanPath(root.absolutePath());
    if (!QFileInfo(rootPath).isDir()) {
        return {};
    }

    QVector<ImageFileRecord> result;

    // Images directly in the selected folder remain selectable. Descendant images are represented
    // by their containing folders, so a large tree does not turn into one unstructured image list.
    const QFileInfoList directImages = root.entryInfoList(
        supportedImageNameFilters(), QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::NoSort);
    result.reserve(directImages.size());
    for (const QFileInfo& image : directImages) {
        result.push_back({image.absoluteFilePath(), image.fileName(), image.size(),
                          image.lastModified(), false});
    }

    QSet<QString> imageFolders;
    QDirIterator iterator(rootPath, supportedImageNameFilters(),
                          QDir::Files | QDir::Readable | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo image(iterator.next());
        QString folder = QDir::cleanPath(image.absolutePath());
        if (folder == rootPath) {
            continue;
        }

        // Mark both the direct image folder and every ancestor below the selected root. This makes
        // every branch that can lead to an image visible while excluding empty/document-only trees.
        while (folder != rootPath && folder.startsWith(rootPath + QDir::separator())) {
            imageFolders.insert(folder);
            const QString parent = QDir::cleanPath(QFileInfo(folder).absolutePath());
            if (parent == folder) {
                break;
            }
            folder = parent;
        }
    }

    for (const QString& folder : std::as_const(imageFolders)) {
        const QFileInfo info(folder);
        result.push_back({folder, root.relativeFilePath(folder), 0, info.lastModified(), true});
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.begin(), result.end(), [&collator](const auto& left, const auto& right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory;
        }
        return collator.compare(left.fileName, right.fileName) < 0;
    });
    return result;
}

bool DirectoryScanner::isSupportedImageFile(const QString& path) {
    const QFileInfo info(path);
    if (!info.isFile()) {
        return false;
    }
    return hasSupportedImageSuffix(info.fileName());
}

} // namespace ispview
