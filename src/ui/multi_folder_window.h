#pragma once

#include <QMainWindow>
#include <QVector>

namespace ispview {

class FolderBrowserPane;
class ImageLoader;

class MultiFolderWindow final : public QMainWindow {
    Q_OBJECT

  public:
    MultiFolderWindow(ImageLoader* loader, const QString& initialDirectory,
                      QWidget* parent = nullptr);

  private:
    void compareSelected();

    ImageLoader* loader_;
    QVector<FolderBrowserPane*> panes_;
};

} // namespace ispview
