#pragma once

#include "NodeMath/ContractTypes.h"

#include <array>
#include <string>
#include <vector>

namespace Stack::NodeMath {

enum class OutputChannelViewMode {
    Neutral,
    Red,
    Green,
    Blue
};

struct OutputInspectionPolicy {
    ValueDescriptor descriptor;
    std::vector<Diagnostic> diagnostics;
    bool executable = true;
};

const char* OutputChannelViewModeToken(OutputChannelViewMode mode);
const char* OutputChannelViewModeLabel(OutputChannelViewMode mode);
bool ParseOutputChannelViewMode(
    const std::string& token,
    OutputChannelViewMode& mode);

std::array<float, 4> MapChannelForOutputInspection(
    float sample,
    OutputChannelViewMode mode);

OutputInspectionPolicy EvaluateOutputInspectionPolicy(
    const ValueDescriptor& descriptor);

} // namespace Stack::NodeMath
