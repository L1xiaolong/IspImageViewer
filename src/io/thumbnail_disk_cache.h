#pragma once

#include <QImage>
#include <QString>

namespace ispview {

class ThumbnailDiskCache final {
  public:
    explicit ThumbnailDiskCache(QString rootDirectory = {});

    [[nodiscard]] QImage load(const QString& key) const;
    [[nodiscard]] bool store(const QString& key, const QImage& image) const;
    [[nodiscard]] QString rootDirectory() const { return rootDirectory_; }
    [[nodiscard]] QString pathForKey(const QString& key) const;

  private:
    QString rootDirectory_;
};

} // namespace ispview
