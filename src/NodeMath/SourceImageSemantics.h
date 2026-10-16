#pragma once

#include "NodeMath/ContractTypes.h"

namespace Stack::NodeMath {

// File decoders may deliberately expand gray/gray-alpha encodings to RGBA.
// Their source descriptor records that decoded color value. Packed numerical
// buffers have no such conversion: GL_RED and GL_RG do not create missing RGB.
inline ValueDescriptor DescribeStoredImageValue(ValueDescriptor descriptor,
    int storageChannels, int width, int height) {
    if (descriptor.logicalType == LogicalValueType::Invalid) {
        if (storageChannels == 1) {
            descriptor = MakeUnknownDescriptor(LogicalValueType::Channel);
            descriptor.channels = SemanticField<ChannelDescriptor>::Known({ChannelLayout::Gray, {"value"}});
            descriptor.units = SemanticField<UnitDescriptor>::Known({UnitKind::Unitless, {}});
        } else if (storageChannels >= 2 && storageChannels <= 4) {
            descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
            auto components = MakeImageComponentSet({ImageComponent::Red, ImageComponent::Green});
            if (storageChannels >= 3) AddImageComponent(components, ImageComponent::Blue);
            if (storageChannels == 4) AddImageComponent(components, ImageComponent::Alpha);
            descriptor.presentImageComponents = SemanticField<ImageComponentSet>::Known(components);
            descriptor.channels = SemanticField<ChannelDescriptor>::Known(MakeImageChannelDescriptor(components));
            descriptor.alpha = SemanticField<AlphaMode>::Known(storageChannels == 4 ? AlphaMode::Straight : AlphaMode::Absent);
        }
    }
    if (IsImageLike(descriptor.logicalType)) {
        auto spatial = descriptor.spatial.state == KnowledgeState::Known
            ? descriptor.spatial.value : SpatialDescriptor{};
        spatial.kind = SpatialExtentKind::Finite;
        spatial.fullWindow = spatial.dataWindow = {0, 0, width, height};
        if (spatial.nativeWidth <= 0) spatial.nativeWidth = width;
        if (spatial.nativeHeight <= 0) spatial.nativeHeight = height;
        spatial.rasterOrigin = RasterOrigin::BottomLeft;
        spatial.pixelAspect = 1;
        descriptor.spatial = SemanticField<SpatialDescriptor>::Known(spatial);
        descriptor.precision = SemanticField<LogicalPrecision>::Known(LogicalPrecision::UInt8);
    }
    return descriptor;
}

} // namespace Stack::NodeMath
