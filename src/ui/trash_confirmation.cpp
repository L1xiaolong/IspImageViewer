#include "ui/trash_confirmation.h"

#include <QCheckBox>
#include <QMessageBox>
#include <QSettings>

namespace ispview {

bool TrashConfirmation::request(QWidget* parent, qsizetype fileCount) {
    QSettings settings;
    if (!settings.value(QStringLiteral("browser/confirmTrash"), true).toBool()) {
        return true;
    }

    QMessageBox confirmation(
        QMessageBox::Question, QStringLiteral("Move to Trash"),
        QStringLiteral("Move %1 selected image(s) to the system Trash?").arg(fileCount),
        QMessageBox::Yes | QMessageBox::No, parent);
    confirmation.setDefaultButton(QMessageBox::No);
    auto* dontAskAgain = new QCheckBox(QStringLiteral("Don't ask again"), &confirmation);
    dontAskAgain->setObjectName(QStringLiteral("trashDontAskAgain"));
    confirmation.setCheckBox(dontAskAgain);
    if (confirmation.exec() != QMessageBox::Yes) {
        return false;
    }
    if (dontAskAgain->isChecked()) {
        settings.setValue(QStringLiteral("browser/confirmTrash"), false);
    }
    return true;
}

} // namespace ispview
