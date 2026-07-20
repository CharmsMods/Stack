#pragma once

#include "NodeMath/ContractTypes.h"

#include <array>
#include <string>
#include <vector>

namespace Stack::NodeMath {

using Rgba32f = std::array<float, 4>;

enum class TechnicalImageOperation {
    AssignSrgb,
    AssignLinearSrgb,
    AssignLinearDisplayP3,
    SrgbDecode,
    SrgbEncode,
    LinearSrgbToDisplayP3,
    LinearDisplayP3ToSrgb,
    Exposure,
    Premultiply,
    Unpremultiply
};

enum class CompositeAlphaFormula {
    StraightSourceOver,
    PremultipliedSourceOver
};

float DecodeSrgbExtended(float value);
float EncodeSrgbExtended(float value);
Rgba32f ApplyTechnicalImageOperation(
    TechnicalImageOperation operation,
    const Rgba32f& input,
    float exposureValue = 0.0f);
Rgba32f CompositeSourceOver(
    CompositeAlphaFormula formula,
    const Rgba32f& source,
    const Rgba32f& backdrop,
    float sourceOpacity = 1.0f);

ValueDescriptor DescribeTechnicalImageOutput(
    TechnicalImageOperation operation,
    const ValueDescriptor& input,
    float exposureValue,
    std::vector<Diagnostic>& diagnostics);

struct DirectOutputPolicy {
    bool executable = true;
    bool writeSrgbChunk = false;
    bool writeDisplayP3Cicp = false;
    bool writeRetainedIccProfile = false;
    std::string retainedProfileDependency;
    std::vector<Diagnostic> diagnostics;
};

DirectOutputPolicy EvaluateDirectPngOutputPolicy(const ValueDescriptor& descriptor);
std::string CompactDescriptorLabel(const ValueDescriptor& descriptor);
std::string TechnicalOperationIdentity(TechnicalImageOperation operation);

} // namespace Stack::NodeMath
