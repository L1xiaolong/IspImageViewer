#include "ui/raw_parameter_panel.h"

#include "io/raw_preset_store.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace ispview {
namespace {

class NoWheelSpinBox final : public QSpinBox {
  public:
    using QSpinBox::QSpinBox;

  protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

class NoWheelDoubleSpinBox final : public QDoubleSpinBox {
  public:
    using QDoubleSpinBox::QDoubleSpinBox;

  protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

QSpinBox* parameterSpin(QWidget* parent) {
    auto* spin = new NoWheelSpinBox(parent);
    spin->setRange(0, 1'000'000'000);
    return spin;
}

QDoubleSpinBox* parameterDouble(QWidget* parent, double minimum, double maximum, int decimals = 5) {
    auto* spin = new NoWheelDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setSingleStep(0.01);
    return spin;
}

template <typename Enum> void addEnum(QComboBox* combo, const QString& text, Enum value) {
    combo->addItem(text, static_cast<int>(value));
}

QString defaultConfigurationName(const RawImageParameters& parameters) {
    return QStringLiteral("%1_%2_%3")
        .arg(parameters.size.width())
        .arg(parameters.size.height())
        .arg(rawPixelFormatName(parameters.format).replace(QLatin1Char(' '), QLatin1Char('_')));
}

} // namespace

RawParameterPanel::RawParameterPanel(QWidget* parent)
    : QWidget(parent), preset_(new QComboBox(this)), format_(new QComboBox(this)),
      width_(parameterSpin(this)), height_(parameterSpin(this)), rowStride_(parameterSpin(this)),
      chromaStride_(parameterSpin(this)), offset_(parameterSpin(this)),
      validBits_(parameterSpin(this)), bayerPattern_(new QComboBox(this)),
      matrix_(new QComboBox(this)), range_(new QComboBox(this)), orientation_(new QComboBox(this)),
      littleEndian_(new QCheckBox(QStringLiteral("Little endian"), this)),
      msbAligned_(new QCheckBox(QStringLiteral("Valid bits are MSB aligned"), this)),
      blackLevel_(parameterSpin(this)), whiteLevel_(parameterSpin(this)),
      gamma_(parameterDouble(this, 0.1, 10.0, 4)), form_(new QFormLayout),
      whiteBalanceWidget_(new QWidget(this)),
      colorMatrixGroup_(new QGroupBox(QStringLiteral("Color correction matrix"), this)),
      editorActions_(new QWidget(this)),
      deleteConfiguration_(
          new QPushButton(QStringLiteral("Delete Current Configuration"), editorActions_)),
      changeTimer_(new QTimer(this)) {
    setObjectName(QStringLiteral("rawParameterPanel"));
    preset_->setObjectName(QStringLiteral("rawConfigurationPreset"));
    width_->setObjectName(QStringLiteral("rawPanelWidth"));
    height_->setObjectName(QStringLiteral("rawPanelHeight"));
    format_->setObjectName(QStringLiteral("rawPanelFormat"));
    rowStride_->setObjectName(QStringLiteral("rawPanelRowStride"));
    chromaStride_->setObjectName(QStringLiteral("rawPanelChromaStride"));
    offset_->setObjectName(QStringLiteral("rawPanelHeaderOffset"));
    validBits_->setObjectName(QStringLiteral("rawPanelValidBits"));
    bayerPattern_->setObjectName(QStringLiteral("rawPanelBayerPattern"));
    matrix_->setObjectName(QStringLiteral("rawPanelYuvMatrix"));
    range_->setObjectName(QStringLiteral("rawPanelYuvRange"));
    orientation_->setObjectName(QStringLiteral("rawPanelOrientation"));
    littleEndian_->setObjectName(QStringLiteral("rawPanelLittleEndian"));
    msbAligned_->setObjectName(QStringLiteral("rawPanelMsbAligned"));
    validBits_->setRange(0, 16);
    blackLevel_->setObjectName(QStringLiteral("rawPanelBlackLevel"));
    whiteLevel_->setObjectName(QStringLiteral("rawPanelWhiteLevel"));
    gamma_->setObjectName(QStringLiteral("rawPanelGamma"));

    addEnum(format_, QStringLiteral("NV12"), RawPixelFormat::NV12);
    addEnum(format_, QStringLiteral("NV21"), RawPixelFormat::NV21);
    addEnum(format_, QStringLiteral("I420"), RawPixelFormat::I420);
    addEnum(format_, QStringLiteral("P010"), RawPixelFormat::P010);
    addEnum(format_, QStringLiteral("MIPI RAW10"), RawPixelFormat::MipiRaw10);
    addEnum(format_, QStringLiteral("MIPI RAW12"), RawPixelFormat::MipiRaw12);
    addEnum(format_, QStringLiteral("RAW in 16-bit container"), RawPixelFormat::Raw16);
    addEnum(bayerPattern_, QStringLiteral("RGGB"), BayerPattern::RGGB);
    addEnum(bayerPattern_, QStringLiteral("GRBG"), BayerPattern::GRBG);
    addEnum(bayerPattern_, QStringLiteral("GBRG"), BayerPattern::GBRG);
    addEnum(bayerPattern_, QStringLiteral("BGGR"), BayerPattern::BGGR);
    addEnum(matrix_, QStringLiteral("BT.601"), YuvMatrix::BT601);
    addEnum(matrix_, QStringLiteral("BT.709"), YuvMatrix::BT709);
    addEnum(matrix_, QStringLiteral("BT.2020"), YuvMatrix::BT2020);
    addEnum(range_, QStringLiteral("Full"), QuantizationRange::Full);
    addEnum(range_, QStringLiteral("Limited"), QuantizationRange::Limited);
    addEnum(orientation_, QStringLiteral("Normal"), ImageOrientation::Normal);
    addEnum(orientation_, QStringLiteral("Rotate 90° clockwise"),
            ImageOrientation::Rotate90Clockwise);
    addEnum(orientation_, QStringLiteral("Rotate 180°"), ImageOrientation::Rotate180);
    addEnum(orientation_, QStringLiteral("Rotate 270° clockwise"),
            ImageOrientation::Rotate270Clockwise);

    auto* whiteBalanceLayout = new QHBoxLayout(whiteBalanceWidget_);
    whiteBalanceLayout->setContentsMargins(0, 0, 0, 0);
    const std::array<QString, 3> whiteBalanceNames{QStringLiteral("R"), QStringLiteral("G"),
                                                   QStringLiteral("B")};
    for (int index = 0; index < static_cast<int>(whiteBalance_.size()); ++index) {
        const auto position = static_cast<std::size_t>(index);
        whiteBalance_[position] = parameterDouble(this, 0.001, 64.0);
        whiteBalance_[position]->setObjectName(
            QStringLiteral("rawPanelWhiteBalance%1").arg(whiteBalanceNames.at(position)));
        whiteBalanceLayout->addWidget(
            new QLabel(whiteBalanceNames.at(position), whiteBalanceWidget_));
        whiteBalanceLayout->addWidget(whiteBalance_[position]);
    }

    auto* matrixLayout = new QGridLayout(colorMatrixGroup_);
    for (int index = 0; index < static_cast<int>(colorMatrix_.size()); ++index) {
        colorMatrix_[static_cast<std::size_t>(index)] = parameterDouble(this, -64.0, 64.0);
        colorMatrix_[static_cast<std::size_t>(index)]->setObjectName(
            QStringLiteral("rawPanelCcm%1%2").arg(index / 3).arg(index % 3));
        matrixLayout->addWidget(colorMatrix_[static_cast<std::size_t>(index)], index / 3,
                                index % 3);
    }

    auto* content = new QWidget(this);
    form_->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form_->setHorizontalSpacing(8);
    form_->setVerticalSpacing(8);
    form_->addRow(QStringLiteral("Format"), format_);
    form_->addRow(QStringLiteral("Width"), width_);
    form_->addRow(QStringLiteral("Height"), height_);
    form_->addRow(QStringLiteral("Row stride (0 = auto)"), rowStride_);
    form_->addRow(QStringLiteral("Chroma stride (0 = auto)"), chromaStride_);
    form_->addRow(QStringLiteral("Header offset"), offset_);
    form_->addRow(QStringLiteral("Valid bits (0 = default)"), validBits_);
    form_->addRow(QStringLiteral("Bayer pattern"), bayerPattern_);
    form_->addRow(QStringLiteral("YUV matrix"), matrix_);
    form_->addRow(QStringLiteral("YUV range"), range_);
    form_->addRow(QStringLiteral("Orientation"), orientation_);
    form_->addRow(littleEndian_);
    form_->addRow(msbAligned_);
    form_->addRow(QStringLiteral("Black level"), blackLevel_);
    form_->addRow(QStringLiteral("White level (0 = maximum)"), whiteLevel_);
    form_->addRow(QStringLiteral("White balance"), whiteBalanceWidget_);
    form_->addRow(QStringLiteral("Display gamma"), gamma_);

    auto* saveConfiguration =
        new QPushButton(QStringLiteral("Save Configuration…"), editorActions_);
    saveConfiguration->setObjectName(QStringLiteral("saveRawConfiguration"));
    auto* applyFolder = new QPushButton(QStringLiteral("Apply to This Folder"), editorActions_);
    applyFolder->setObjectName(QStringLiteral("applyRawConfigurationToFolder"));
    auto* saveAndApply =
        new QPushButton(QStringLiteral("Save && Apply to Folder…"), editorActions_);
    saveAndApply->setObjectName(QStringLiteral("saveAndApplyRawConfiguration"));
    deleteConfiguration_->setObjectName(QStringLiteral("deleteRawConfiguration"));
    deleteConfiguration_->setEnabled(false);
    auto* editorActionsLayout = new QVBoxLayout(editorActions_);
    editorActionsLayout->setContentsMargins(0, 0, 0, 0);
    editorActionsLayout->addWidget(saveConfiguration);
    editorActionsLayout->addWidget(applyFolder);
    editorActionsLayout->addWidget(saveAndApply);
    editorActionsLayout->addWidget(deleteConfiguration_);
    connect(saveConfiguration, &QPushButton::clicked, this,
            [this] { (void)saveNamedConfiguration(); });
    connect(applyFolder, &QPushButton::clicked, this, &RawParameterPanel::applyCurrentToFolder);
    connect(saveAndApply, &QPushButton::clicked, this, [this] {
        if (saveNamedConfiguration()) {
            applyCurrentToFolder();
        }
    });
    connect(deleteConfiguration_, &QPushButton::clicked, this,
            &RawParameterPanel::deleteCurrentConfiguration);

    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(12, 10, 12, 12);
    contentLayout->setSpacing(10);
    auto* presetTitle = new QLabel(QStringLiteral("Configuration"), content);
    QFont titleFont = presetTitle->font();
    titleFont.setBold(true);
    presetTitle->setFont(titleFont);
    contentLayout->addWidget(presetTitle);
    contentLayout->addWidget(preset_);
    contentLayout->addLayout(form_);
    contentLayout->addWidget(colorMatrixGroup_);
    contentLayout->addWidget(editorActions_);
    contentLayout->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("rawParameterScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);

    changeTimer_->setSingleShot(true);
    changeTimer_->setInterval(150);
    connect(changeTimer_, &QTimer::timeout, this, [this] {
        if (!updating_ && !path_.isEmpty()) {
            emit parametersChanged(path_, parameters());
        }
    });
    for (QSpinBox* spin : {width_, height_, rowStride_, chromaStride_, offset_, validBits_}) {
        connect(spin, &QSpinBox::valueChanged, this, &RawParameterPanel::scheduleChange);
    }
    for (QComboBox* combo : {format_, bayerPattern_, matrix_, range_}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &RawParameterPanel::scheduleChange);
    }
    connect(format_, &QComboBox::currentIndexChanged, this,
            &RawParameterPanel::updateFormatControls);
    connect(littleEndian_, &QCheckBox::toggled, this, &RawParameterPanel::scheduleChange);
    connect(msbAligned_, &QCheckBox::toggled, this, &RawParameterPanel::scheduleChange);
    connect(orientation_, &QComboBox::currentIndexChanged, this,
            &RawParameterPanel::scheduleChange);
    for (QSpinBox* spin : {blackLevel_, whiteLevel_}) {
        connect(spin, &QSpinBox::valueChanged, this, &RawParameterPanel::scheduleChange);
    }
    for (QDoubleSpinBox* spin : whiteBalance_) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, &RawParameterPanel::scheduleChange);
    }
    for (QDoubleSpinBox* spin : colorMatrix_) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, &RawParameterPanel::scheduleChange);
    }
    connect(gamma_, &QDoubleSpinBox::valueChanged, this, &RawParameterPanel::scheduleChange);
    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int index) {
        deleteConfiguration_->setEnabled(index > 0);
        if (updating_ || index <= 0 || path_.isEmpty()) {
            return;
        }
        const auto selected = RawPresetStore::loadNamedPreset(preset_->currentText());
        if (!selected) {
            return;
        }
        setSource(path_, *selected);
        emit parametersChanged(path_, parameters());
    });
    refreshPresets();
    updateFormatControls();
}

void RawParameterPanel::setSource(const QString& path, const RawImageParameters& value) {
    refreshPresets();
    updating_ = true;
    path_ = path;
    baseParameters_ = value;
    format_->setCurrentIndex(format_->findData(static_cast<int>(value.format)));
    width_->setValue(value.size.width());
    height_->setValue(value.size.height());
    rowStride_->setValue(static_cast<int>(value.rowStride));
    chromaStride_->setValue(static_cast<int>(value.chromaStride));
    offset_->setValue(static_cast<int>(value.headerOffset));
    validBits_->setValue(value.validBitsOverride);
    bayerPattern_->setCurrentIndex(bayerPattern_->findData(static_cast<int>(value.bayerPattern)));
    matrix_->setCurrentIndex(matrix_->findData(static_cast<int>(value.yuvMatrix)));
    range_->setCurrentIndex(range_->findData(static_cast<int>(value.range)));
    orientation_->setCurrentIndex(orientation_->findData(static_cast<int>(value.orientation)));
    littleEndian_->setChecked(value.littleEndian);
    msbAligned_->setChecked(value.msbAligned);
    blackLevel_->setValue(value.blackLevel);
    whiteLevel_->setValue(value.whiteLevel);
    for (int index = 0; index < static_cast<int>(whiteBalance_.size()); ++index) {
        whiteBalance_.at(static_cast<std::size_t>(index))
            ->setValue(value.whiteBalanceGains.at(static_cast<std::size_t>(index)));
    }
    for (int index = 0; index < static_cast<int>(colorMatrix_.size()); ++index) {
        colorMatrix_.at(static_cast<std::size_t>(index))
            ->setValue(value.colorCorrectionMatrix.at(static_cast<std::size_t>(index)));
    }
    gamma_->setValue(value.displayGamma);
    updating_ = false;
    updateFormatControls();
}

void RawParameterPanel::refreshPresets() {
    const QString selected = preset_->currentText();
    const QSignalBlocker blocker(preset_);
    preset_->clear();
    preset_->addItem(QStringLiteral("<None>"));
    preset_->addItems(RawPresetStore::namedPresetNames());
    const int restored = preset_->findText(selected);
    preset_->setCurrentIndex(std::max(0, restored));
    deleteConfiguration_->setEnabled(preset_->currentIndex() > 0);
}

RawImageParameters RawParameterPanel::parameters() const {
    RawImageParameters value = baseParameters_;
    value.size = {width_->value(), height_->value()};
    value.format = static_cast<RawPixelFormat>(format_->currentData().toInt());
    value.rowStride = rowStride_->value();
    value.chromaStride = chromaStride_->value();
    value.headerOffset = offset_->value();
    value.frameIndex = 0;
    value.validBitsOverride = value.format == RawPixelFormat::Raw16 ? validBits_->value() : 0;
    value.bayerPattern = static_cast<BayerPattern>(bayerPattern_->currentData().toInt());
    value.yuvMatrix = static_cast<YuvMatrix>(matrix_->currentData().toInt());
    value.range = static_cast<QuantizationRange>(range_->currentData().toInt());
    value.orientation = static_cast<ImageOrientation>(orientation_->currentData().toInt());
    value.littleEndian = littleEndian_->isChecked();
    value.msbAligned = msbAligned_->isChecked();
    value.blackLevel = blackLevel_->value();
    value.whiteLevel = whiteLevel_->value();
    for (int index = 0; index < static_cast<int>(whiteBalance_.size()); ++index) {
        value.whiteBalanceGains.at(static_cast<std::size_t>(index)) =
            whiteBalance_.at(static_cast<std::size_t>(index))->value();
    }
    for (int index = 0; index < static_cast<int>(colorMatrix_.size()); ++index) {
        value.colorCorrectionMatrix.at(static_cast<std::size_t>(index)) =
            colorMatrix_.at(static_cast<std::size_t>(index))->value();
    }
    value.displayGamma = gamma_->value();
    return value;
}

void RawParameterPanel::scheduleChange() {
    if (!updating_) {
        changeTimer_->start();
    }
}

void RawParameterPanel::updateFormatControls() {
    const RawPixelFormat format = static_cast<RawPixelFormat>(format_->currentData().toInt());
    const bool yuv = format == RawPixelFormat::NV12 || format == RawPixelFormat::NV21 ||
                     format == RawPixelFormat::I420 || format == RawPixelFormat::P010;
    form_->setRowVisible(chromaStride_, yuv);
    form_->setRowVisible(matrix_, yuv);
    form_->setRowVisible(range_, yuv);
    form_->setRowVisible(bayerPattern_, !yuv);
    form_->setRowVisible(blackLevel_, !yuv);
    form_->setRowVisible(whiteLevel_, !yuv);
    form_->setRowVisible(whiteBalanceWidget_, !yuv);
    form_->setRowVisible(gamma_, !yuv);
    colorMatrixGroup_->setVisible(!yuv);
    const std::array<QWidget*, 16> controls{format_,       width_,      height_,      rowStride_,
                                            chromaStride_, offset_,     validBits_,   bayerPattern_,
                                            matrix_,       range_,      orientation_, littleEndian_,
                                            msbAligned_,   blackLevel_, whiteLevel_,  gamma_};
    for (QWidget* control : controls) {
        control->setEnabled(true);
    }
    for (QDoubleSpinBox* spin : whiteBalance_) {
        spin->setEnabled(!yuv);
    }
    for (QDoubleSpinBox* spin : colorMatrix_) {
        spin->setEnabled(!yuv);
    }
    validBits_->setEnabled(format == RawPixelFormat::Raw16);
    littleEndian_->setEnabled(format == RawPixelFormat::P010 || format == RawPixelFormat::Raw16);
    msbAligned_->setEnabled(format == RawPixelFormat::P010 || format == RawPixelFormat::Raw16);
    form_->setRowVisible(validBits_, format == RawPixelFormat::Raw16);
    form_->setRowVisible(littleEndian_,
                         format == RawPixelFormat::P010 || format == RawPixelFormat::Raw16);
    form_->setRowVisible(msbAligned_,
                         format == RawPixelFormat::P010 || format == RawPixelFormat::Raw16);
}

bool RawParameterPanel::saveNamedConfiguration() {
    if (path_.isEmpty()) {
        return false;
    }
    bool accepted = false;
    const QString suggested = defaultConfigurationName(parameters());
    const QString name = QInputDialog::getText(this, QStringLiteral("Save RAW/YUV Configuration"),
                                               QStringLiteral("Configuration name:"),
                                               QLineEdit::Normal, suggested, &accepted)
                             .trimmed();
    if (!accepted || name.isEmpty()) {
        return false;
    }
    if (!RawPresetStore::saveNamedPreset(name, parameters())) {
        emit notificationRequested(QStringLiteral("The configuration is incomplete or invalid."),
                                   true);
        return false;
    }
    refreshPresets();
    preset_->setCurrentIndex(preset_->findText(name));
    emit notificationRequested(QStringLiteral("Configuration saved: %1").arg(name), false);
    return true;
}

void RawParameterPanel::deleteCurrentConfiguration() {
    if (preset_->currentIndex() <= 0) {
        return;
    }
    const QString name = preset_->currentText();
    if (!RawPresetStore::removeNamedPreset(name)) {
        emit notificationRequested(QStringLiteral("Unable to delete configuration: %1").arg(name),
                                   true);
        return;
    }
    refreshPresets();
    emit notificationRequested(QStringLiteral("Configuration deleted: %1").arg(name), false);
}

void RawParameterPanel::applyCurrentToFolder() {
    if (path_.isEmpty()) {
        return;
    }
    RawImageParameters value = parameters();
    value.frameIndex = 0;
    RawPresetStore::saveForFile(path_, value);
    emit folderParametersApplied(path_, value);
    emit notificationRequested(QStringLiteral("Configuration applied to %1 files in this folder")
                                   .arg(QFileInfo(path_).suffix().toUpper()),
                               false);
}

} // namespace ispview
