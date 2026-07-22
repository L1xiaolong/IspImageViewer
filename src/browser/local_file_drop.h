#pragma once

#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QMimeData;
QT_END_NAMESPACE

namespace ispview {

// Presentation-neutral conversion of the standard local-file representations used by Qt,
// Finder and Explorer into paths.
// The fallback representations matter when a platform drag reaches Qt before its native
// pasteboard data has been promoted to QMimeData::urls().
[[nodiscard]] QStringList localFileDropPaths(const QMimeData* mimeData);

// Returns a bounded, content-free description suitable for status diagnostics.  File names and
// text payloads are intentionally not included.
[[nodiscard]] QString localFileDropFormats(const QMimeData* mimeData);

} // namespace ispview
