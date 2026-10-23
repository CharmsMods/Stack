#pragma once
#include "NodeMath/CompoundDefinition.h"
#include <vector>

namespace EditorNodeGraphDefinitions {
// Built once with the shipped catalog. The graphs use ordinary editor nodes;
// there is deliberately no Halation renderer or private arithmetic executor.
void AppendModularCompoundRecipes(std::vector<Stack::NodeMath::CompoundDefinition>& catalog);
}
