#pragma once

#include "io/image_decoder.h"

namespace ispview {

class QtImageDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] QString cacheIdentity() const override;
    [[nodiscard]] bool canDecode(const QString& path) const override;
    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override;
};

} // namespace ispview
