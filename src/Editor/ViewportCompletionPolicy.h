#pragma once

#include "Renderer/ViewportPreviewTypes.h"

namespace Stack::EditorRendering {

// Completion intent belongs to the edit. Camera changes can expand that
// intent and prioritize a region without clearing the whole-image request.
struct ViewportCompletionPolicy {
    static constexpr double QuietSeconds = 0.2;
    static constexpr double WholeImageVisibleFraction = 0.8;

    double lastAdjustmentAt = -1;
    bool wholeImagePending = false;

    void ImageChanged(double now) {
        lastAdjustmentAt = now;
        wholeImagePending = false;
    }

    bool Ready(double now, bool adjusting, const Renderer::ImageRegion& visible) {
        if (adjusting) {
            lastAdjustmentAt = now;
            return false;
        }
        if (lastAdjustmentAt < 0 || now - lastAdjustmentAt < QuietSeconds)
            return false;
        if (visible.Valid() &&
            double(visible.width) * visible.height >=
                WholeImageVisibleFraction * double(visible.fullWidth) * visible.fullHeight)
            wholeImagePending = true;
        return true;
    }
};

} // namespace Stack::EditorRendering
