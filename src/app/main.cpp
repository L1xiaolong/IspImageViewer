#include "io/default_image_decoder.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ISP Image Viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("ISPView"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    const QString initialDirectory = app.arguments().size() > 1 ? app.arguments().at(1) : QString{};
    ispview::MainWindow window(ispview::createDefaultImageDecoder(), initialDirectory);
    window.show();
    return app.exec();
}
