#include "NodeMath/ImageOperationInput.h"

namespace Stack::NodeMath {
namespace {
ImageComponentSet Components(const ValueDescriptor& value) {
    if (value.logicalType != LogicalValueType::ColorImage) return {};
    if (value.presentImageComponents.state == KnowledgeState::Known)
        return value.presentImageComponents.value;
    if (value.channels.state != KnowledgeState::Known) return {};
    const auto layout = value.channels.value.layout;
    if (layout != ChannelLayout::RGB && layout != ChannelLayout::RGBA) return {};
    auto result = MakeImageComponentSet({ImageComponent::Red, ImageComponent::Green, ImageComponent::Blue});
    if (layout == ChannelLayout::RGBA) AddImageComponent(result, ImageComponent::Alpha);
    return result;
}
bool IsFullRgb(ImageComponentSet components) {
    return HasImageComponent(components, ImageComponent::Red) && HasImageComponent(components, ImageComponent::Green) &&
        HasImageComponent(components, ImageComponent::Blue);
}
bool IsRec2020(const std::string& id) {
    return id == "rec2020-d65" || id == "linear-rec2020-d65";
}
std::string CheckRgb(const ValueDescriptor& value, ImageComponentSet components, bool sceneOnly, bool requireResolved) {
    if (value.logicalType == LogicalValueType::Invalid && !requireResolved) return {};
    if (value.logicalType != LogicalValueType::ColorImage ||
        (!IsFullRgb(components) && (requireResolved || value.presentImageComponents.state == KnowledgeState::Known ||
            value.channels.state == KnowledgeState::Known)))
        return "This operation needs red, green and blue components.";
    if (value.transfer.state == KnowledgeState::Known && value.transfer.value.kind != TransferKind::Linear)
        return "This operation needs linear RGB. Decode or convert the input first.";
    if (sceneOnly && value.reference.state == KnowledgeState::Known && value.reference.value != ReferenceState::Scene)
        return "This operation needs scene-referred RGB.";
    if (value.color.state == KnowledgeState::Known) {
        const auto& id = value.color.value.identity;
        if (!IsRec2020(id) && id != "srgb-d65" && id != "linear-srgb-d65")
            return "Convert the input to linear sRGB or linear Rec.2020.";
    }
    if (HasImageComponent(components, ImageComponent::Alpha) &&
        ((requireResolved && value.alpha.state != KnowledgeState::Known) ||
        (value.alpha.state == KnowledgeState::Known && value.alpha.value != AlphaMode::Straight &&
            value.alpha.value != AlphaMode::Opaque)))
        return "This operation needs straight or opaque RGBA.";
    if (requireResolved && (value.color.state != KnowledgeState::Known || value.transfer.state != KnowledgeState::Known))
        return "This operation needs a declared RGB color space and transfer.";
    return {};
}
}

std::string PhotoRgbInputError(const ValueDescriptor& descriptor, bool sceneOnly, bool requireResolved) {
    return CheckRgb(descriptor, Components(descriptor), sceneOnly, requireResolved);
}

ImageOperationInput DescribeImageOperationInput(const ValueDescriptor& descriptor) {
    ImageOperationInput input;
    input.scalar = descriptor.logicalType == LogicalValueType::Channel || descriptor.logicalType == LogicalValueType::ScalarField ||
        descriptor.logicalType == LogicalValueType::Mask;
    input.components = Components(descriptor);
    input.fullRgb = IsFullRgb(input.components);
    input.linearRgb = input.fullRgb && descriptor.transfer.state == KnowledgeState::Known &&
        descriptor.transfer.value.kind == TransferKind::Linear;
    if (descriptor.color.state == KnowledgeState::Known && IsRec2020(descriptor.color.value.identity))
        input.lumaWeights = {.2627f,.6780f,.0593f};
    input.colorUnavailableReason = CheckRgb(descriptor, input.components, false, true);
    input.colorRelationships = input.colorUnavailableReason.empty();
    return input;
}
}
