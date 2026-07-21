#include "io/default_image_decoder.h"
#include "qml/browse_controller.h"
#include "qml/thumbnail_image_provider.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ISP Image Viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("ISPView"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.2-qml-browse"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QString initialDirectory;
    QString screenshotPath;
    QString selectedPath;
    QString displayMode;
    int screenshotDelay = 1800;
    const QStringList arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("--screenshot") && i + 1 < arguments.size()) {
            screenshotPath = arguments.at(++i);
        } else if (arguments.at(i) == QStringLiteral("--select") && i + 1 < arguments.size()) {
            selectedPath = QFileInfo(arguments.at(++i)).absoluteFilePath();
        } else if (arguments.at(i) == QStringLiteral("--display-mode") &&
                   i + 1 < arguments.size()) {
            displayMode = arguments.at(++i).toLower();
        } else if (arguments.at(i) == QStringLiteral("--screenshot-delay") &&
                   i + 1 < arguments.size()) {
            screenshotDelay = qMax(250, arguments.at(++i).toInt());
        } else if (!arguments.at(i).startsWith(QLatin1Char('-'))) {
            initialDirectory = arguments.at(i);
        }
    }
    if (!screenshotPath.isEmpty()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    const auto decoder = ispview::createDefaultImageDecoder();
    ispview::BrowseController browseController(decoder, initialDirectory);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("browseController"), &browseController);
    engine.addImageProvider(QStringLiteral("thumbnail"),
                            new ispview::ThumbnailImageProvider(decoder));
    engine.load(QUrl(QStringLiteral("qrc:/ISPViewQml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    if (!displayMode.isEmpty()) {
        const int mode = displayMode == QStringLiteral("list")
                             ? 1
                             : displayMode == QStringLiteral("gallery") ? 2 : 0;
        if (QObject* page = engine.rootObjects().constFirst()->findChild<QObject*>(
                QStringLiteral("browsePage"))) {
            page->setProperty("displayMode", mode);
        }
    }

    if (!selectedPath.isEmpty()) {
        QTimer::singleShot(700, &browseController,
                           [&browseController, selectedPath] {
                               browseController.selectPath(selectedPath);
                           });
    }

    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(screenshotDelay, &app, [&app, &engine, screenshotPath] {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
            if (!window || !window->grabWindow().save(screenshotPath)) {
                app.exit(2);
                return;
            }
            app.quit();
        });
    }
    return app.exec();
}
