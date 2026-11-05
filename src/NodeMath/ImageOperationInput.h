#pragma once
#include "NodeMath/ContractTypes.h"
#include <array>

namespace Stack::NodeMath {
struct ImageOperationInput {
    bool scalar = false;
    ImageComponentSet components;
    bool fullRgb = false;
    bool linearRgb = false;
    bool colorRelationships = false;
    std::array<float,3> lumaWeights{.2126f,.7152f,.0722f};
    std::string colorUnavailableReason;
};
// Draft wiring can wait for a source to publish its metadata. Execution needs
// the component layout, working RGB basis, transfer and applicable alpha.
std::string PhotoRgbInputError(const ValueDescriptor& descriptor, bool sceneOnly, bool requireResolved);
ImageOperationInput DescribeImageOperationInput(const ValueDescriptor& descriptor);
}
