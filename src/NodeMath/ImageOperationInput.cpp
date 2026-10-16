#include "NodeMath/ImageOperationInput.h"

namespace Stack::NodeMath {
ImageOperationInput DescribeImageOperationInput(const ValueDescriptor& descriptor) {
    ImageOperationInput input;
    input.scalar=descriptor.logicalType==LogicalValueType::Channel ||
        descriptor.logicalType==LogicalValueType::ScalarField || descriptor.logicalType==LogicalValueType::Mask;
    if (descriptor.logicalType==LogicalValueType::ColorImage &&
        descriptor.presentImageComponents.state==KnowledgeState::Known)
        input.components=descriptor.presentImageComponents.value;
    input.fullRgb=HasImageComponent(input.components,ImageComponent::Red) &&
        HasImageComponent(input.components,ImageComponent::Green) &&
        HasImageComponent(input.components,ImageComponent::Blue);
    if (!input.fullRgb) {
        input.colorUnavailableReason="This input has fewer than three real RGB components."; return input;
    }
    // Luminance coefficients and relative curve math depend on color basis,
    // transfer and alpha, independently of scene/display reference labeling.
    // Preserve an unspecified reference rather than inventing one here.
    if (descriptor.color.state!=KnowledgeState::Known ||
        descriptor.transfer.state!=KnowledgeState::Known) {
        input.colorUnavailableReason="This input needs declared RGB color and transfer semantics."; return input;
    }
    input.linearRgb=descriptor.transfer.value.kind==TransferKind::Linear;
    if (!input.linearRgb) {
        input.colorUnavailableReason="Luminance processing needs linear RGB. Decode or convert the input first."; return input;
    }
    const auto& color=descriptor.color.value.identity;
    if ((color=="linear-rec2020-d65" || color=="rec2020-d65")) input.lumaWeights={.2627f,.6780f,.0593f};
    else if (color!="linear-srgb-d65" && color!="srgb-d65") {
        input.colorUnavailableReason="Convert this RGB color space to a supported linear working space for luminance."; return input;
    }
    if (HasImageComponent(input.components,ImageComponent::Alpha) &&
        (descriptor.alpha.state!=KnowledgeState::Known || descriptor.alpha.value==AlphaMode::Premultiplied)) {
        input.colorUnavailableReason="Luminance processing needs straight or opaque RGB alpha."; return input;
    }
    input.colorRelationships=true;
    return input;
}
}
