#include "core/sync_group.h"

namespace ispview {

ViewState SyncGroup::synchronizedState(const ViewState& source, const ViewState& target) const {
    ViewState result = target;
    if (syncZoom_) {
        result.pixelsPerImagePixel = source.pixelsPerImagePixel;
        result.fitMode = source.fitMode;
    }
    if (syncPan_) {
        result.normalizedCenter = source.normalizedCenter;
    }
    if (syncRoi_) {
        result.normalizedRoi = source.normalizedRoi;
    }
    return result;
}

} // namespace ispview
