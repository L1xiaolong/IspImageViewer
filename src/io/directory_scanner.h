#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

namespace ispview {

struct ImageFileRecord {
    QString path;
    QString fileName;
    qint64 fileSize = 0;
    QDateTime modifiedAt;
    bool isDirectory = false;
};

class DirectoryScanner final : public QObject {
    Q_OBJECT

  public:
    explicit DirectoryScanner(QObject* parent = nullptr);

    quint64 scanAsync(const QString& directory);
    quint64 scanImageFoldersAsync(const QString& directory);
    [[nodiscard]] static QVector<ImageFileRecord> scan(const QString& directory);
    [[nodiscard]] static QVector<ImageFileRecord>
    scanImageFoldersRecursively(const QString& directory);
    [[nodiscard]] static bool isSupportedImageFile(const QString& path);

  signals:
    void scanFinished(const QString& directory, const QVector<ispview::ImageFileRecord>& files,
                      quint64 generation);

  private:
    quint64 generation_ = 0;
};

} // namespace ispview
