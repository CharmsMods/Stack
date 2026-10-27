#pragma once
#include "Raw/Bracketing/Processor.h"

namespace Stack::Editor {
// Full-quality inspection retains dimensions in CapturePreview, while its
// already display-mapped pixels live in the view's separate RGBA buffer.
inline bool HasBracketingOriginalPixels(const Raw::Bracketing::CapturePreview* image,
    bool fullQualityRgba,std::size_t rgbaBytes) {
    if(!image||!image->width||!image->height)return false;
    const auto pixels=std::size_t(image->width)*image->height;
    return fullQualityRgba?rgbaBytes==pixels*4:image->rgb.size()==pixels*3;
}
inline bool HasSavedBracketingDiagnostics(const Raw::Bracketing::BracketingResult& result) {
    if(!result.raw||result.raw->metadata.visibleWidth<=0||result.raw->metadata.visibleHeight<=0)return false;
    const auto pixels=std::size_t(result.raw->metadata.visibleWidth)*result.raw->metadata.visibleHeight;
    const auto& maps=result.raw->multiFrameMeasurementSidecars;
    return maps&&maps->validity&&maps->clipping&&maps->localRejection&&maps->fallbackReason&&
        maps->validity->size()==pixels&&maps->clipping->size()==pixels&&
        maps->localRejection->size()==pixels&&maps->fallbackReason->size()==pixels;
}
}
