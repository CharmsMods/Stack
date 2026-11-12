#pragma once

#include "NodeMath/ContractTypes.h"
#include "ThirdParty/json.hpp"
#include <cmath>
#include <string>

namespace Stack::GraphModel {
// Legacy fixed-canvas transforms can introduce coverage without changing the
// raster size. Mirror their shared shader's crop/rotation behavior, not storage.
inline void DescribeLayerGeometryCoverage(NodeMath::ValueDescriptor& value,
    const std::string& type, const nlohmann::json& parameters) {
    using namespace NodeMath;
    if ((type != "Crop" && type != "Rotate" && type != "Flip") ||
        !parameters.is_object() || !parameters.value("enabled", true) ||
        !parameters.value("visible", true) || value.logicalType != LogicalValueType::ColorImage ||
        value.presentImageComponents.state != KnowledgeState::Known) return;
    const auto components = value.presentImageComponents.value;
    if (!HasImageComponent(components, ImageComponent::Red) ||
        !HasImageComponent(components, ImageComponent::Green) ||
        !HasImageComponent(components, ImageComponent::Blue)) return;
    const double rotation = parameters.value("rotation", 0.0);
    const bool rotatedCorners = std::isfinite(rotation) && std::abs(rotation) > 0.001 &&
        std::remainder(rotation, 90.0) != 0.0;
    const bool cropped = parameters.value("cropLeft", 0.0) > 0.0 ||
        parameters.value("cropRight", 0.0) > 0.0 || parameters.value("cropTop", 0.0) > 0.0 ||
        parameters.value("cropBottom", 0.0) > 0.0;
    if (!rotatedCorners && !cropped) return;
    AddImageComponent(value.presentImageComponents.value, ImageComponent::Alpha);
    value.channels = SemanticField<ChannelDescriptor>::Known(MakeImageChannelDescriptor(value.presentImageComponents.value));
    if (value.alpha.state == KnowledgeState::Known &&
        (value.alpha.value == AlphaMode::Opaque || value.alpha.value == AlphaMode::Absent))
        value.alpha = SemanticField<AlphaMode>::Known(AlphaMode::Straight);
}
}
