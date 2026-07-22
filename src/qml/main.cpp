#include "io/default_image_decoder.h"
#include "qml/browse_controller.h"
#include "qml/browse_workspace_controller.h"
#include "qml/compare_controller.h"
#include "qml/image_properties_controller.h"
#include "qml/full_screen_controller.h"
#include "qml/raw_parameters_controller.h"
#include "qml/thumbnail_image_provider.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ISP Image Viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("ISPView"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.3"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QString initialDirectory;
    QString screenshotPath;
    QString selectedPath;
    QStringList initialComparePaths;
    QString displayMode;
    int initialFileManagerCount = 1;
    bool nativeScreenshot = false;
    int screenshotDelay = 1800;
    const QStringList arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("--screenshot") && i + 1 < arguments.size()) {
            screenshotPath = arguments.at(++i);
        } else if (arguments.at(i) == QStringLiteral("--screenshot-native")) {
            nativeScreenshot = true;
        } else if (arguments.at(i) == QStringLiteral("--select") && i + 1 < arguments.size()) {
            selectedPath = QFileInfo(arguments.at(++i)).absoluteFilePath();
        } else if (arguments.at(i) == QStringLiteral("--display-mode") &&
                   i + 1 < arguments.size()) {
            displayMode = arguments.at(++i).toLower();
        } else if (arguments.at(i) == QStringLiteral("--screenshot-delay") &&
                   i + 1 < arguments.size()) {
            screenshotDelay = qMax(250, arguments.at(++i).toInt());
        } else if (arguments.at(i) == QStringLiteral("--file-managers") &&
                   i + 1 < arguments.size()) {
            initialFileManagerCount = qBound(1, arguments.at(++i).toInt(), 4);
        } else if (arguments.at(i) == QStringLiteral("--qml-compare")) {
            while (i + 1 < arguments.size() && !arguments.at(i + 1).startsWith(QLatin1Char('-'))) {
                initialComparePaths.append(QFileInfo(arguments.at(++i)).absoluteFilePath());
            }
        } else if (!arguments.at(i).startsWith(QLatin1Char('-'))) {
            initialDirectory = arguments.at(i);
        }
    }
    // The browse page can use the software scene graph for deterministic CI
    // captures. ComparePage contains a QQuickRhiItem and therefore must keep a
    // hardware RHI backend even when --screenshot-native was not specified.
    if (!screenshotPath.isEmpty() && !nativeScreenshot && initialComparePaths.isEmpty()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    const auto decoder = ispview::createDefaultImageDecoder();
    ispview::BrowseWorkspaceController browseController(decoder, initialDirectory);
    while (browseController.paneCount() < initialFileManagerCount)
        browseController.addFileManagerPane();
    ispview::CompareController compareController(browseController.loader());
    ispview::ImagePropertiesController imagePropertiesController(browseController.loader());
    ispview::FullScreenController fullScreenController(browseController.loader());
    ispview::RawParametersController rawParametersController(browseController.loader());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("browseController"), &browseController);
    engine.rootContext()->setContextProperty(QStringLiteral("compareController"), &compareController);
    engine.rootContext()->setContextProperty(QStringLiteral("imagePropertiesController"),
                                             &imagePropertiesController);
    engine.rootContext()->setContextProperty(QStringLiteral("fullScreenController"),
                                             &fullScreenController);
    engine.rootContext()->setContextProperty(QStringLiteral("rawParametersController"),
                                             &rawParametersController);
    engine.rootContext()->setContextProperty(QStringLiteral("initialComparePaths"), initialComparePaths);
    engine.addImageProvider(QStringLiteral("thumbnail"),
                            new ispview::ThumbnailImageProvider(decoder,
                                                                browseController.loader()));
    engine.load(QUrl(QStringLiteral("qrc:/ISPViewQml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    if (!displayMode.isEmpty()) {
        const int mode = displayMode == QStringLiteral("list")
                             ? 1
                             : displayMode == QStringLiteral("gallery") ? 2 : 0;
        browseController.setActiveDisplayMode(mode);
    }

    if (!selectedPath.isEmpty()) {
        QTimer::singleShot(700, &browseController,
                           [&browseController, selectedPath] {
                               if (auto* pane = browseController.activeBrowsePane())
                                   pane->selectPath(selectedPath);
                           });
    }

    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(screenshotDelay, &app, [&app, &engine, screenshotPath] {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
            if (!window || !window->grabWindow().save(screenshotPath)) {
                app.exit(2);
                return;
            }
            window->setProperty("forceApplicationClose", true);
            app.quit();
        });
    }
    return app.exec();
}
