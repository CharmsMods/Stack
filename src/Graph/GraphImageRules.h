#pragma once

#include "NodeMath/ContractTypes.h"
#include <cmath>
#include <string>

namespace Stack::GraphModel {
// Combining color images needs one interpretation of their channels and
// coordinates. Numeric channels retain their existing broadcast behavior.
inline bool ValidateImageCombination(const NodeMath::ValueDescriptor& a,
    const NodeMath::ValueDescriptor& b, std::string& error) {
    using namespace NodeMath;
    if (a.logicalType != LogicalValueType::ColorImage || b.logicalType != LogicalValueType::ColorImage) return true;
    const auto compatible = [](const auto& x, const auto& y) {
        return x.state != KnowledgeState::Known || y.state != KnowledgeState::Known || x.value == y.value;
    };
    if (!compatible(a.color,b.color) || !compatible(a.transfer,b.transfer) || !compatible(a.reference,b.reference)) {
        error = "The image branches use different color representations. Add an explicit conversion before combining them.";
        return false;
    }
    if (a.spatial.state == KnowledgeState::Known && b.spatial.state == KnowledgeState::Known) {
        for (int i=0;i<9;++i) if (std::abs(a.spatial.value.sourceTransform[i]-b.spatial.value.sourceTransform[i])>1e-6) {
            error = "The image branches use different source coordinates. Add an explicit transform before combining them.";
            return false;
        }
    }
    return true;
}
}
