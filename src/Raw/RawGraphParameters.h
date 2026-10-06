#pragma once
#include "Raw/RawGraphOperation.h"
#include <vector>
namespace Stack::RawRecipe {
struct GraphParameter {
    std::string id, label, path, units;
    float initial, minimum, maximum;
};
std::vector<GraphParameter> GraphParameters(GraphOperationKind kind);
}
