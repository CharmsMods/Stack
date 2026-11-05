#pragma once

#include "NodeMath/Arithmetic.h"
#include "NodeMath/ContractTypes.h"

namespace Stack::NodeMath {

inline bool ArithmeticChannel(LogicalValueType type) {
    return type == LogicalValueType::Channel || type == LogicalValueType::Mask ||
           type == LogicalValueType::ScalarField;
}

enum class ArithmeticShape {
    Number,
    Channel,
    RGB,
    RGBA,
    Pending,
    Unsupported
};

inline ArithmeticShape ShapeOf(const ValueDescriptor *value) {
    if (!value || value->logicalType == LogicalValueType::Scalar)
        return ArithmeticShape::Number;
    if (ArithmeticChannel(value->logicalType))
        return ArithmeticShape::Channel;
    if (value->logicalType == LogicalValueType::Invalid)
        return ArithmeticShape::Pending;
    if (value->logicalType != LogicalValueType::ColorImage)
        return ArithmeticShape::Unsupported;
    if (value->presentImageComponents.state == KnowledgeState::Known) {
        const auto rgb =
            MakeImageComponentSet({ImageComponent::Red, ImageComponent::Green, ImageComponent::Blue});
        auto rgba = rgb;
        AddImageComponent(rgba, ImageComponent::Alpha);
        if (value->presentImageComponents.value == rgb)
            return ArithmeticShape::RGB;
        if (value->presentImageComponents.value == rgba)
            return ArithmeticShape::RGBA;
        return ArithmeticShape::Unsupported;
    }
    if (value->channels.state == KnowledgeState::Known) {
        if (value->channels.value.layout == ChannelLayout::RGB)
            return ArithmeticShape::RGB;
        if (value->channels.value.layout == ChannelLayout::RGBA)
            return ArithmeticShape::RGBA;
        return ArithmeticShape::Unsupported;
    }
    return ArithmeticShape::Pending;
}

inline const char *ShapeName(ArithmeticShape shape) {
    switch (shape) {
    case ArithmeticShape::Number:
        return "Number";
    case ArithmeticShape::Channel:
        return "Channel";
    case ArithmeticShape::RGB:
        return "RGB";
    case ArithmeticShape::RGBA:
        return "RGBA";
    case ArithmeticShape::Unsupported:
        return "unsupported layout";
    default:
        return "unresolved layout";
    }
}

struct ArithmeticResolution {
    LogicalValueType result;
    std::string error;
    ArithmeticShape a;
    ArithmeticShape b;

    ArithmeticShape outputShape() const {
        if (!error.empty()) return ArithmeticShape::Unsupported;
        if (result == LogicalValueType::Scalar) return ArithmeticShape::Number;
        if (result == LogicalValueType::Channel) return ArithmeticShape::Channel;
        if (a == ArithmeticShape::RGBA || b == ArithmeticShape::RGBA) return ArithmeticShape::RGBA;
        if (a == ArithmeticShape::RGB || b == ArithmeticShape::RGB) return ArithmeticShape::RGB;
        return ArithmeticShape::Pending;
    }
};

// Missing inputs are authored Numbers. The two operand shapes select uniform,
// per-pixel broadcast, or corresponding named-component reads for consumers.
inline ArithmeticResolution ResolveArithmetic(const ValueDescriptor *left, const ValueDescriptor *right) {
    using Type = LogicalValueType;
    const auto a = ShapeOf(left);
    const auto b = ShapeOf(right);
    const auto fail = [&](const char *reason) { return ArithmeticResolution{Type::Invalid, reason, a, b}; };
    const auto differs = [](const auto &x, const auto &y) {
        return x.state == KnowledgeState::Known && y.state == KnowledgeState::Known && !(x == y);
    };
    if (a == ArithmeticShape::Unsupported || b == ArithmeticShape::Unsupported)
        return fail("Arithmetic accepts a Number, Channel, RGB or RGBA. Select components explicitly.");
    const bool leftImage = left && left->logicalType == Type::ColorImage;
    const bool rightImage = right && right->logicalType == Type::ColorImage;
    if (leftImage && rightImage) {
        if (a != b && a != ArithmeticShape::Pending && b != ArithmeticShape::Pending)
            return fail("RGB and RGBA have different components. Choose matching components explicitly.");
        if (differs(left->color, right->color) || differs(left->transfer, right->transfer) ||
            differs(left->reference, right->reference) ||
            (differs(left->alpha, right->alpha) &&
             (left->alpha.value == AlphaMode::Premultiplied || right->alpha.value == AlphaMode::Premultiplied)))
            return fail("The images use different color representations. Convert explicitly.");
    }
    if (left && right && left->spatial.state == KnowledgeState::Known &&
        right->spatial.state == KnowledgeState::Known) {
        const auto &x = left->spatial.value;
        const auto &y = right->spatial.value;
        if (x.kind != y.kind || !(x.fullWindow == y.fullWindow) || !(x.dataWindow == y.dataWindow) ||
            x.rasterOrigin != y.rasterOrigin || x.pixelAspect != y.pixelAspect ||
            (left->sampling.state == KnowledgeState::Known && right->sampling.state == KnowledgeState::Known &&
             left->sampling.value.coordinates != right->sampling.value.coordinates))
            return fail("The operands use different pixel grids. Align or reformat them explicitly.");
    }
    const Type result = leftImage || rightImage                                          ? Type::ColorImage
                        : a == ArithmeticShape::Pending || b == ArithmeticShape::Pending ? Type::Invalid
                        : a == ArithmeticShape::Channel || b == ArithmeticShape::Channel ? Type::Channel
                                                                                         : Type::Scalar;
    return {result, {}, a, b};
}

using ArithmeticInputs = std::array<const ValueDescriptor*, kMixSlotCount>;

// A mismatch belongs to the endpoint being checked. Ignore unrelated existing
// failures so a user can repair one operand at a time.
inline std::string ArithmeticInputError(const ArithmeticInputs& inputs, int slot, int count) {
    const auto* candidate = inputs[slot];
    auto error = ResolveArithmetic(candidate, nullptr).error;
    for (int i = 0; error.empty() && i < count; ++i)
        if (i != slot && inputs[i]) error = ResolveArithmetic(candidate, inputs[i]).error;
    return error;
}

inline ArithmeticResolution ResolveArithmeticInputs(ArithmeticMode mode, const ArithmeticInputs& inputs) {
    if (Primitive(mode)) return ResolveArithmetic(inputs[0], inputs[1]);
    const int count = ArithmeticInputCount(mode);
    auto result = ResolveArithmetic(nullptr, nullptr);
    bool connected = false;
    for (int i = 0; i < count; ++i) {
        if (!inputs[i]) continue;
        connected = true;
        const auto operand = ResolveArithmetic(inputs[i], nullptr);
        auto error = ArithmeticInputError(inputs, i, count);
        if (!error.empty()) return {LogicalValueType::Invalid, std::move(error), operand.a, operand.b};
        if (operand.result == LogicalValueType::ColorImage || result.result == LogicalValueType::Scalar ||
            (operand.result == LogicalValueType::Invalid && result.result != LogicalValueType::ColorImage))
            result = operand;
    }
    if (!connected) result.result = LogicalValueType::Invalid;
    return result;
}

} // namespace Stack::NodeMath
