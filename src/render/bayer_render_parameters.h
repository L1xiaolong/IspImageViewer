#pragma once

#include "core/raw_image_parameters.h"

#include <array>

namespace ispview {

// Matches the std140 BayerParameters block in bayer.frag. RAW bytes remain in their
// source packing; the shader uses these values to unpack and reproduce the CPU reference
// display transform without changing the original values used by pixel inspection.
[[nodiscard]] std::array<float, 28>
makeBayerRenderUniformData(const RawImageParameters& parameters);

} // namespace ispview
