#pragma once

#include "NodeMath/TechnicalImageMath.h"

#include <string>

namespace Stack::EditorRendering {

// Composite raster blending expects straight-alpha images. Apply the same
// explicit-conversion policy as direct PNG export before consuming each input.
inline bool ValidateCompositePngInput(
    int outputNodeId,
    const NodeMath::ValueDescriptor* descriptor,
    bool pixelsCurrent,
    std::string& error) {
    error.clear();
    if (!pixelsCurrent) {
        error = "Wait for composite output " + std::to_string(outputNodeId) +
            " to finish rendering, then export again.";
        return false;
    }
    if (!descriptor) {
        error = "The composite output " + std::to_string(outputNodeId) +
            " could not be validated for PNG export.";
        return false;
    }
    const auto policy = NodeMath::EvaluateDirectPngOutputPolicy(*descriptor);
    if (policy.executable) return true;
    error = "Composite output " + std::to_string(outputNodeId) + ": " +
        (policy.diagnostics.empty() ? "The declared graph output cannot be written as PNG." :
            policy.diagnostics.front().message);
    if (!policy.diagnostics.empty() && !policy.diagnostics.front().suggestedRepair.empty()) {
        error += " " + policy.diagnostics.front().suggestedRepair;
    }
    return false;
}

} // namespace Stack::EditorRendering
