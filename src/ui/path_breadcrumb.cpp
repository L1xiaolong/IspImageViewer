#include "ui/path_breadcrumb.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStorageInfo>
#include <QTimer>
#include <QToolButton>

namespace ispview {

PathBreadcrumb::PathBreadcrumb(QWidget* parent)
    : QWidget(parent), scrollArea_(new QScrollArea(this)),
      segmentContainer_(new QWidget(scrollArea_)),
      segmentLayout_(new QHBoxLayout(segmentContainer_)) {
    setObjectName(QStringLiteral("pathBreadcrumb"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(34);

    segmentLayout_->setContentsMargins(3, 1, 3, 1);
    segmentLayout_->setSpacing(1);
    segmentLayout_->addStretch(1);

    scrollArea_->setObjectName(QStringLiteral("pathBreadcrumbScrollArea"));
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidget(segmentContainer_);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollArea_);
}

void PathBreadcrumb::setPath(const QString& path) {
    const QFileInfo info(path);
    if (!info.isDir()) {
        return;
    }
    path_ = QDir::cleanPath(info.absoluteFilePath());
    clearSegments();

    QString rootPath = QDir(path_).rootPath();
    if (rootPath.isEmpty()) {
        rootPath = QDir::rootPath();
    }
    rootPath = QDir::cleanPath(rootPath);
    QStorageInfo storage(path_);
    QString rootLabel = storage.displayName().trimmed();
    if (rootLabel.isEmpty()) {
        rootLabel = QDir::toNativeSeparators(rootPath);
    }
    addSegment(rootLabel, rootPath, true);

    const QString normalizedPath = QDir::fromNativeSeparators(path_);
    QString normalizedRoot = QDir::fromNativeSeparators(rootPath);
    if (!normalizedRoot.endsWith(QLatin1Char('/'))) {
        normalizedRoot += QLatin1Char('/');
    }
    const QString relative = normalizedPath.mid(normalizedRoot.size());
    // Build every segment from the normalized absolute prefix. On Windows,
    // QDir::cleanPath() can normalize a drive root to a drive-relative form
    // (for example, "D:"), which would make subsequent filePath() calls no
    // longer match the absolute directory shown by the model.
    QString accumulated = normalizedRoot;
    for (const QString& component : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (!accumulated.endsWith(QLatin1Char('/'))) {
            accumulated += QLatin1Char('/');
        }
        accumulated += component;
        addSegment(component, QDir::cleanPath(accumulated), false);
    }
    segmentLayout_->addStretch(1);

    QTimer::singleShot(0, this, [this] {
        scrollArea_->horizontalScrollBar()->setValue(
            scrollArea_->horizontalScrollBar()->maximum());
    });
}

void PathBreadcrumb::clearSegments() {
    while (QLayoutItem* item = segmentLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void PathBreadcrumb::addSegment(const QString& label, const QString& path, bool drive) {
    if (segmentLayout_->count() > 0) {
        auto* separator = new QLabel(QStringLiteral("›"), segmentContainer_);
        separator->setObjectName(QStringLiteral("pathBreadcrumbSeparator"));
        separator->setStyleSheet(QStringLiteral("color: palette(mid); padding: 0 1px;"));
        segmentLayout_->addWidget(separator);
    }

    auto* button = new QToolButton(segmentContainer_);
    button->setObjectName(QStringLiteral("pathBreadcrumbSegment"));
    button->setProperty("path", path);
    button->setText(label);
    button->setToolTip(QDir::toNativeSeparators(path));
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QFileIconProvider iconProvider;
    button->setIcon(iconProvider.icon(drive ? QFileIconProvider::Drive : QFileIconProvider::Folder));
    button->setIconSize({16, 16});
    button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(button, &QToolButton::clicked, this, [this, path] { emit pathActivated(path); });
    segmentLayout_->addWidget(button);
}

} // namespace ispview
