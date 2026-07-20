#pragma once

#include "Raw/RawPreciseIntegration.h"

#include <functional>
#include <string>

namespace Stack::PreciseIntegration {

inline constexpr const char* kNativeRuntimeVersion = "raw-precise-native-runtime-v1";

struct NativeSolveRequest {
    SolveIdentity identity;
    RawRecipe::RawDevelopmentRecipe inputRecipe;
    int proxyMaxDimension = 256;
    int featureMaxDimension = 256;
    int warmMaxDimension = 2048;
};

struct NativeSolveCallbacks {
    std::function<bool()> shouldCancel;
    std::function<void(const std::string& label, int completed, int total)> reportProgress;
};

struct NativeSolveResult {
    bool attempted = false;
    bool canceled = false;
    bool failed = false;
    std::string runtimeVersion = kNativeRuntimeVersion;
    VerifiedCandidate candidate;
    std::string reason;
};

NativeSolveResult RunNativePreciseSolve(
    const NativeSolveRequest& request,
    const NativeSolveCallbacks& callbacks = {});

} // namespace Stack::PreciseIntegration
