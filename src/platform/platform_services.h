#pragma once

#include <QString>

namespace ispview {

// OS integration boundary. Callers receive portable success/failure results and do not include
// platform process APIs or conditional compilation.
class PlatformServices final {
  public:
    [[nodiscard]] static bool revealInFileManager(const QString& path);
    [[nodiscard]] static bool openDirectoryInFileManager(const QString& path);
};

} // namespace ispview
