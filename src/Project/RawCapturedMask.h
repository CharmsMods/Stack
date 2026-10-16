#pragma once
#include "Assets/CreatedAsset.h"
#include "Editor/NodeGraph/NodeGraphPayloads.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Stack::Project {
// Captured coverage retains native pixels. Render previews resample it at their own extent.
inline EditorNodeGraph::CustomMaskPayload CapturedMaskMetadata(const Assets::FloatData& data) {
    if(data.channels!=1 || data.width<=0 || data.height<=0 ||
       data.values.size()!=size_t(data.width)*data.height || !data.Valid(true))
        throw std::runtime_error("The captured asset is not a coverage mask.");
    EditorNodeGraph::CustomMaskPayload p;
    if (data.width>EditorNodeGraph::kMaximumMaskRasterDimension || data.height>EditorNodeGraph::kMaximumMaskRasterDimension)
        throw std::runtime_error("The captured mask exceeds the supported raster extent.");
    p.width=data.width;p.height=data.height;
    return p;
}
inline EditorNodeGraph::CustomMaskPayload CapturedMaskPayload(const Assets::FloatData& data) {
    auto payload=CapturedMaskMetadata(data);payload.rasterLayer=data.values;return payload;
}
inline EditorNodeGraph::CustomMaskPayload CapturedMaskPayload(const std::shared_ptr<const Assets::FloatData>& data) {
    if(!data) throw std::runtime_error("The captured asset has no pixels.");
    auto payload=CapturedMaskMetadata(*data);
    // The immutable asset owns the pixels. Publishing it on the UI thread
    // neither copies the native image nor hashes it again.
    payload.rasterLayer=Assets::MaskRaster::Share(
        std::shared_ptr<const std::vector<float>>(data,&data->values),data->Fingerprint());
    return payload;
}
}
