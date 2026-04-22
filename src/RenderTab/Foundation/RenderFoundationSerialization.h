#pragma once

#include "RenderFoundationState.h"
#include "Persistence/StackBinaryFormat.h"

#include <string>

namespace RenderFoundation::Serialization {

StackBinaryFormat::json BuildPayload(const State& state, const std::string& latestFinalAssetFileName);

bool ApplyPayload(
    const StackBinaryFormat::json& payload,
    State& state,
    const std::string& projectName,
    const std::string& projectFileName,
    std::string& latestFinalAssetFileName,
    std::string& errorMessage);

} // namespace RenderFoundation::Serialization
