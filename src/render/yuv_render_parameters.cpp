#include "render/yuv_render_parameters.h"

namespace ispview {

std::array<float, 12> makeYuvRenderUniformData(const RawImageParameters& parameters) {
    std::array<float, 12> values{};
    switch (parameters.yuvMatrix) {
    case YuvMatrix::BT601:
        values[0] = 1.402F;
        values[1] = 0.344136F;
        values[2] = 0.714136F;
        values[3] = 1.772F;
        break;
    case YuvMatrix::BT2020:
        values[0] = 1.4746F;
        values[1] = 0.164553F;
        values[2] = 0.571353F;
        values[3] = 1.8814F;
        break;
    case YuvMatrix::BT709:
        values[0] = 1.5748F;
        values[1] = 0.187324F;
        values[2] = 0.468124F;
        values[3] = 1.8556F;
        break;
    }

    const bool highBitDepth = parameters.format == RawPixelFormat::P010;
    const float denominator = highBitDepth ? 65535.0F : 255.0F;
    const float alignment = highBitDepth && parameters.msbAligned ? 64.0F : 1.0F;
    const float levelScale = highBitDepth ? 4.0F : 1.0F;
    values[4] = parameters.range == QuantizationRange::Limited
                    ? 16.0F * levelScale * alignment / denominator
                    : 0.0F;
    values[5] = parameters.range == QuantizationRange::Limited
                    ? 219.0F * levelScale * alignment / denominator
                    : ((highBitDepth ? 1023.0F : 255.0F) * alignment / denominator);
    values[6] = (highBitDepth ? 512.0F : 128.0F) * alignment / denominator;
    values[7] = parameters.range == QuantizationRange::Limited
                    ? 224.0F * levelScale * alignment / denominator
                    : ((highBitDepth ? 1023.0F : 255.0F) * alignment / denominator);
    values[8] = parameters.format == RawPixelFormat::I420 ? 1.0F : 0.0F;
    values[9] = parameters.format == RawPixelFormat::NV21 ? 1.0F : 0.0F;
    values[10] = static_cast<float>(parameters.orientation);
    return values;
}

} // namespace ispview
