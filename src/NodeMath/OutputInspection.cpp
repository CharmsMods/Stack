#include "NodeMath/OutputInspection.h"

#include "NodeMath/DescriptorSerialization.h"

#include <utility>

namespace Stack::NodeMath {
namespace {

Diagnostic MakeOutputDiagnostic(
    const char* ruleId,
    const ValueDescriptor& descriptor,
    std::string message,
    std::string repair = {}) {
    Diagnostic diagnostic;
    diagnostic.ruleId = ruleId;
    diagnostic.stage = DiagnosticStage::Semantic;
    diagnostic.severity = DiagnosticSeverity::HardError;
    diagnostic.authoredSourceIdentity = "output.direct.v2";
    diagnostic.affectedIdentity = "output.direct.v2";
    diagnostic.semanticFingerprint = Sha256ContentIdentity(
        std::string(ruleId) + "\n" + DescriptorContentIdentity(descriptor));
    diagnostic.message = std::move(message);
    diagnostic.suggestedRepair = std::move(repair);
    return diagnostic;
}

} // namespace

const char* OutputChannelViewModeToken(OutputChannelViewMode mode) {
    switch (mode) {
        case OutputChannelViewMode::Neutral: return "neutral";
        case OutputChannelViewMode::Red: return "red";
        case OutputChannelViewMode::Green: return "green";
        case OutputChannelViewMode::Blue: return "blue";
    }
    return "neutral";
}

const char* OutputChannelViewModeLabel(OutputChannelViewMode mode) {
    switch (mode) {
        case OutputChannelViewMode::Neutral: return "Neutral";
        case OutputChannelViewMode::Red: return "Red";
        case OutputChannelViewMode::Green: return "Green";
        case OutputChannelViewMode::Blue: return "Blue";
    }
    return "Neutral";
}

bool ParseOutputChannelViewMode(
    const std::string& token,
    OutputChannelViewMode& mode) {
    if (token == "neutral") {
        mode = OutputChannelViewMode::Neutral;
    } else if (token == "red") {
        mode = OutputChannelViewMode::Red;
    } else if (token == "green") {
        mode = OutputChannelViewMode::Green;
    } else if (token == "blue") {
        mode = OutputChannelViewMode::Blue;
    } else {
        return false;
    }
    return true;
}

std::array<float, 4> MapChannelForOutputInspection(
    float sample,
    OutputChannelViewMode mode) {
    switch (mode) {
        case OutputChannelViewMode::Neutral:
            return { sample, sample, sample, 1.0f };
        case OutputChannelViewMode::Red:
            return { sample, 0.0f, 0.0f, 1.0f };
        case OutputChannelViewMode::Green:
            return { 0.0f, sample, 0.0f, 1.0f };
        case OutputChannelViewMode::Blue:
            return { 0.0f, 0.0f, sample, 1.0f };
    }
    return { sample, sample, sample, 1.0f };
}

OutputInspectionPolicy EvaluateOutputInspectionPolicy(
    const ValueDescriptor& descriptor) {
    OutputInspectionPolicy result;
    result.descriptor = descriptor;
    if (descriptor.logicalType != LogicalValueType::ColorImage &&
        descriptor.logicalType != LogicalValueType::Channel) {
        result.executable = false;
        result.diagnostics.push_back(MakeOutputDiagnostic(
            "nmr.output.type-mismatch",
            descriptor,
            "Output accepts a Color Image or Channel.",
            "Connect an Image or Channel value to Result."));
        return result;
    }
    if (!ValidateDescriptor(descriptor).empty()) {
        result.executable = false;
        result.diagnostics.push_back(MakeOutputDiagnostic(
            "nmr.semantic.metadata-missing",
            descriptor,
            "The Output value descriptor is structurally invalid.",
            "Repair the upstream value descriptor before Output."));
    }
    return result;
}

} // namespace Stack::NodeMath
