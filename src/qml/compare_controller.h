#pragma once

#include "core/image_types.h"
#include <QObject>
#include <QStringList>
#include <QVector>
#include <QPointer>
#include <QVariantMap>

namespace ispview {

class ImageLoader;
class QmlImageCanvas;

// Presentation-independent comparison session. It owns exactly the same
// asynchronous preview/full-frame lifecycle used by CompareWindow.
class CompareController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList paths READ paths NOTIFY pathsChanged)
    Q_PROPERTY(int presentationMode READ presentationMode WRITE setPresentationMode NOTIFY presentationModeChanged)
    Q_PROPERTY(qreal splitAmount READ splitAmount WRITE setSplitAmount NOTIFY splitAmountChanged)
    Q_PROPERTY(bool synchronized READ synchronized WRITE setSynchronized NOTIFY synchronizedChanged)
    Q_PROPERTY(bool holdCandidate READ holdCandidate WRITE setHoldCandidate NOTIFY holdCandidateChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(int histogramRevision READ histogramRevision NOTIFY histogramRevisionChanged)

  public:
    explicit CompareController(ImageLoader* loader, QObject* parent = nullptr);
    QStringList paths() const { return paths_; }
    int presentationMode() const { return presentationMode_; }
    qreal splitAmount() const { return splitAmount_; }
    bool synchronized() const { return synchronized_; }
    bool holdCandidate() const { return holdCandidate_; }
    ImageFramePtr frame(int slot) const;
    int revision() const { return revision_; }
    int histogramRevision() const { return histogramRevision_; }
    Q_INVOKABLE QString fileText(int slot) const;
    Q_INVOKABLE QString cameraText(int slot) const;

    Q_INVOKABLE void setPaths(const QStringList& paths);
    Q_INVOKABLE void setPresentationMode(int mode);
    Q_INVOKABLE void setSplitAmount(qreal amount);
    Q_INVOKABLE void setSynchronized(bool enabled);
    Q_INVOKABLE void setHoldCandidate(bool active);
    Q_INVOKABLE void attachCanvas(QObject* canvas);
    Q_INVOKABLE void fitAll();
    Q_INVOKABLE void actualPixelsAll();
    Q_INVOKABLE QVariantList pixelTexts(int sourceSlot, int x, int y) const;
    Q_INVOKABLE QString chooseScreenshotPath();
    // source: 0 = display image, 1 = original RAW/YUV planes.
    Q_INVOKABLE void requestHistogram(int slot, int source);
    Q_INVOKABLE QVariantMap histogram(int slot, int source) const;

  signals:
    void pathsChanged();
    void presentationModeChanged();
    void splitAmountChanged();
    void synchronizedChanged();
    void holdCandidateChanged();
    void frameChanged(int slot, bool fullResolution);
    void loadFailed(int slot, const QString& error);
    void revisionChanged();
    void histogramRevisionChanged();
    void histogramChanged(int slot, int source);

  private:
    void requestFrame(int slot, const QString& path);
    void applyHoldFrame();
    void refreshCanvas(int changedSlot = -1, bool resetChangedView = false);
    void clearHistograms(int slot);
    void completeHistogram(int slot, int source, quint64 generation, QVariantMap value);
    ImageLoader* loader_;
    QStringList paths_;
    QVector<ImageFramePtr> frames_;
    QStringList errors_;
    QVector<quint64> generations_;
    QPointer<QmlImageCanvas> canvas_;
    int presentationMode_ = 0;
    qreal splitAmount_ = 0.5;
    bool synchronized_ = true;
    bool holdCandidate_ = false;
    int revision_ = 0;
    int histogramRevision_ = 0;
    QVector<quint64> histogramGenerations_;
    QVector<QVariantMap> displayHistograms_;
    QVector<QVariantMap> rawHistograms_;
};

} // namespace ispview
