#pragma once

#include "NodeMath/NodeDefinition.h"

#include <string>
#include <vector>

namespace Stack::NodeMath {

struct PropagationResult {
    ValueDescriptor descriptor;
    std::vector<Diagnostic> diagnostics;
    bool executable = true;
};

PropagationResult PropagateIdentity(const ValueDescriptor& input);
PropagationResult PropagateGenericArithmetic(
    const ValueDescriptor& input,
    const std::string& operationIdentity);
PropagationResult PropagateExposure(
    const ValueDescriptor& input,
    double exposureValue,
    const std::string& operationIdentity);
PropagationResult PropagateMask(
    const ValueDescriptor& input,
    const std::string& operationIdentity);
PropagationResult PropagateGeometry(
    const ValueDescriptor& input,
    const SpatialDescriptor& outputSpatial,
    const SamplingDescriptor& outputSampling,
    const std::string& operationIdentity);
PropagationResult PropagateColorTransform(
    const ValueDescriptor& input,
    const ColorIdentity& destinationColor,
    const TransferDescriptor& destinationTransfer,
    ReferenceState destinationReference,
    const std::string& operationIdentity);
PropagationResult PropagateComposite(
    const ValueDescriptor& source,
    const ValueDescriptor& backdrop,
    bool extentPolicyDeclared,
    const SpatialDescriptor& outputSpatial,
    AlphaMode requiredInputAlpha,
    AlphaMode outputAlpha,
    const std::string& operationIdentity);
PropagationResult PropagateReduction(
    const ValueDescriptor& input,
    LogicalValueType outputType,
    const UnitDescriptor& outputUnits,
    const std::string& operationIdentity);
PropagationResult PropagateDirectOutput(const ValueDescriptor& input);
PropagationResult PropagateUnknownExternal(
    const ValueDescriptor& input,
    const ValueDescriptor& declaredOutput,
    const std::vector<FieldDispositionRule>& fieldPolicy,
    const std::string& operationIdentity);

} // namespace Stack::NodeMath
