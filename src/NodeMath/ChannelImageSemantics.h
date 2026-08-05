#pragma once

#include "NodeMath/ContractTypes.h"

#include <array>
#include <string>
#include <vector>

namespace Stack::NodeMath {

struct ChannelImageDescription {
    ValueDescriptor descriptor;
    std::vector<Diagnostic> diagnostics;
    bool executable = true;
};

ValueDescriptor MakePartialColorImageDescriptor(
    const ImageComponentSet& components,
    std::string operationIdentity);

ChannelImageDescription DescribeImageComponentExtraction(
    const ValueDescriptor& image,
    ImageComponent component,
    const std::string& operationIdentity);

// Inputs use canonical R, G, B, A order. A null entry is semantically absent.
ChannelImageDescription DescribeImageCombine(
    const std::array<const ValueDescriptor*, 4>& inputs,
    const std::string& operationIdentity);

} // namespace Stack::NodeMath
