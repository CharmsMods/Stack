#pragma once

#include "NodeMath/ContractTypes.h"
#include "ThirdParty/json.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Stack::NodeMath {

struct DescriptorParseResult {
    std::optional<ValueDescriptor> descriptor;
    std::vector<ContractIssue> issues;
};

nlohmann::json SerializeValueDescriptor(const ValueDescriptor& descriptor);
DescriptorParseResult ParseValueDescriptor(const nlohmann::json& value);

// Stable semantic identity used by live edges, cache keys, diagnostics, and
// saved source metadata. Physical texture/resource state is intentionally not
// included.
std::string CanonicalDescriptorContent(const ValueDescriptor& descriptor);
std::string DescriptorContentIdentity(const ValueDescriptor& descriptor);

} // namespace Stack::NodeMath
