#pragma once

#include <QtTypes>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace ispview {

class TrashConfirmation final {
  public:
    // Returns true when deletion may continue. The preference is written only after an explicit
    // affirmative answer, so checking "Don't ask again" and then canceling never suppresses the
    // next warning.
    [[nodiscard]] static bool request(QWidget* parent, qsizetype fileCount);
};

} // namespace ispview
