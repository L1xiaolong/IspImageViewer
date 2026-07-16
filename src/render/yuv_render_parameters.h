#pragma once

#include "core/raw_image_parameters.h"

#include <array>

namespace ispview {

// Matches the std140 YuvParameters block in yuv.frag. Keeping the conversion in
// a testable function prevents the CPU reference path and the GPU path from
// silently drifting when a new matrix, range, or bit alignment is added.
[[nodiscard]] std::array<float, 12>
makeYuvRenderUniformData(const RawImageParameters& parameters);

} // namespace ispview
