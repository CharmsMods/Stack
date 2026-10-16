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
ImageOperationInput DescribeImageOperationInput(const ValueDescriptor& descriptor);
}
