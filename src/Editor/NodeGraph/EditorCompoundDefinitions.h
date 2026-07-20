#pragma once

#include "NodeMath/CompoundDefinition.h"

#include <cstddef>
#include <vector>

namespace EditorNodeGraphDefinitions {

const std::vector<Stack::NodeMath::CompoundDefinition>& GetShippedCompoundTemplates();
const Stack::NodeMath::CompoundDefinition* FindShippedCompoundTemplate(std::size_t index);

} // namespace EditorNodeGraphDefinitions
