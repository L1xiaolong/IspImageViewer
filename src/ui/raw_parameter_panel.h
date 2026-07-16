#pragma once

#include "core/raw_image_parameters.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;
QT_END_NAMESPACE

namespace ispview {

class RawParameterPanel final : public QWidget {
    Q_OBJECT

  public:
    explicit RawParameterPanel(QWidget* parent = nullptr);

    void setSource(const QString& path, const RawImageParameters& parameters);
    [[nodiscard]] QString sourcePath() const { return path_; }
    [[nodiscard]] RawImageParameters parameters() const;
    void refreshPresets();

  signals:
    void parametersChanged(const QString& path, const RawImageParameters& parameters);
    void folderParametersApplied(const QString& path, const RawImageParameters& parameters);
    void notificationRequested(const QString& message, bool error);

  private:
    void scheduleChange();
    void updateFormatControls();
    bool saveNamedConfiguration();
    void deleteCurrentConfiguration();
    void applyCurrentToFolder();

    QString path_;
    RawImageParameters baseParameters_;
    QComboBox* preset_;
    QComboBox* format_;
    QSpinBox* width_;
    QSpinBox* height_;
    QSpinBox* rowStride_;
    QSpinBox* chromaStride_;
    QSpinBox* offset_;
    QSpinBox* validBits_;
    QComboBox* bayerPattern_;
    QComboBox* matrix_;
    QComboBox* range_;
    QComboBox* orientation_;
    QCheckBox* littleEndian_;
    QCheckBox* msbAligned_;
    QSpinBox* blackLevel_;
    QSpinBox* whiteLevel_;
    std::array<QDoubleSpinBox*, 3> whiteBalance_{};
    std::array<QDoubleSpinBox*, 9> colorMatrix_{};
    QDoubleSpinBox* gamma_;
    QFormLayout* form_;
    QWidget* whiteBalanceWidget_;
    QGroupBox* colorMatrixGroup_;
    QWidget* editorActions_;
    QPushButton* deleteConfiguration_;
    QTimer* changeTimer_;
    bool updating_ = false;
};

} // namespace ispview
