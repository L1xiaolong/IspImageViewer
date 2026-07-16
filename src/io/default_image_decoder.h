#pragma once

#include <memory>

namespace ispview {

class IImageDecoder;

// Composition boundary for the production decoder chain. UI code consumes only IImageDecoder and
// does not know which built-in or optional third-party adapters are present in the current build.
[[nodiscard]] std::shared_ptr<const IImageDecoder> createDefaultImageDecoder();

} // namespace ispview
