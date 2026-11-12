#pragma once
#include "NodeMath/ImageOperationInput.h"
#include "NodeMath/ToneCurveParameters.h"
#include "Raw/RawGraphOperation.h"

namespace Stack::GraphModel {
enum class PhotoInput { Components, LinearRgb, SceneLinearRgb };

inline PhotoInput PhotoInputFor(const RawRecipe::GraphOperation& operation) {
    using Kind = RawRecipe::GraphOperationKind;
    // Disabled operations return the incoming value without interpreting color.
    if (!operation.enabled) return PhotoInput::Components;
    if (operation.kind == Kind::Exposure) return PhotoInput::Components;
    if (operation.kind == Kind::ToneCurve)
        return NodeMath::ToneCurveMode(operation.parameters) == "components"
            ? PhotoInput::Components : PhotoInput::LinearRgb;
    return PhotoInput::SceneLinearRgb;
}

inline std::string PhotoInputError(PhotoInput requirement, const NodeMath::ValueDescriptor& value, bool requireResolved) {
    if (requirement != PhotoInput::Components)
        return NodeMath::PhotoRgbInputError(value, requirement == PhotoInput::SceneLinearRgb, requireResolved);
    using Type = NodeMath::LogicalValueType;
    if (value.logicalType == Type::Invalid && !requireResolved) return {};
    return value.logicalType == Type::ColorImage || value.logicalType == Type::Channel ||
        value.logicalType == Type::ScalarField || value.logicalType == Type::Mask ? "" : "Connect an image or a spatial Channel.";
}
}
