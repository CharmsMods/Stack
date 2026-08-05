#pragma once

#include "Raw/RawDevelopmentRecipe.h"
#include "Restormer/RestormerPackage.h"
#include "Restormer/RestormerProtocol.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Stack::Restormer {

struct DenoiseResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string provider;
    double inferenceMilliseconds = 0.0;
    int completedTiles = 0;
    int totalTiles = 0;
    std::string packageVersion;
    std::string modelSha256;
    std::vector<float> outputSrgbProxy;
};

class Client {
public:
    struct Impl;

    static Client& Instance();

    ValidationResult Validate(
        const RawRecipe::RawRgbDenoiseRecipe& settings,
        bool allowUnpinnedDevelopmentSelection = false);
    DenoiseResult Denoise(
        const RawRecipe::RawRgbDenoiseRecipe& settings,
        const std::vector<float>& inputSrgbProxy,
        int width,
        int height,
        Quality quality,
        std::uint64_t generation,
        const std::function<bool()>& shouldCancel);
    bool IsInferenceActive() const {
        return m_InferenceActive.load(std::memory_order_relaxed);
    }
    void Shutdown();

private:
    Client() = default;
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    Impl* m_Impl = nullptr;
    std::atomic<bool> m_InferenceActive { false };
};

} // namespace Stack::Restormer
