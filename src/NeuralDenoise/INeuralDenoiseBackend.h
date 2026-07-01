#pragma once

#include "NeuralDenoiseTypes.h"

#include <string>
#include <vector>

namespace NeuralDenoise {

struct BackendStatus {
    bool available = false;
    std::string name;
    std::string status;
    std::vector<std::string> warnings;
};

class INeuralDenoiseBackend {
public:
    virtual ~INeuralDenoiseBackend() = default;

    virtual BackendStatus GetStatus() const = 0;
    virtual bool SupportsModel(const NeuralDenoiseModelInfo& model) const = 0;
    virtual bool LoadModel(
        const NeuralDenoiseModelInfo& model,
        RuntimePreference runtimePreference,
        bool allowCpuFallback,
        std::string& outError) = 0;
    virtual bool IsModelLoaded(const std::string& modelId) const = 0;
    virtual NeuralDenoiseInferenceResult RunRgbInference(const NeuralDenoiseInferenceRequest& request) = 0;
};

} // namespace NeuralDenoise
