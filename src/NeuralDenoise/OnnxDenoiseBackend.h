#pragma once

#include "INeuralDenoiseBackend.h"

#include <filesystem>
#include <memory>

namespace NeuralDenoise {

class OnnxDenoiseBackend : public INeuralDenoiseBackend {
public:
    explicit OnnxDenoiseBackend(std::filesystem::path runtimeDirectory = {});
    ~OnnxDenoiseBackend() override;

    void SetRuntimeDirectory(std::filesystem::path runtimeDirectory);
    BackendStatus GetStatus() const override;
    bool SupportsModel(const NeuralDenoiseModelInfo& model) const override;
    bool LoadModel(
        const NeuralDenoiseModelInfo& model,
        RuntimePreference runtimePreference,
        bool allowCpuFallback,
        std::string& outError) override;
    bool IsModelLoaded(const std::string& modelId) const override;
    NeuralDenoiseInferenceResult RunRgbInference(const NeuralDenoiseInferenceRequest& request) override;

    bool HasCoreRuntime() const;
    bool HasCudaProvider() const;
    const std::string& CudaProviderStatus() const { Probe(); return m_CudaProviderStatus; }
    const std::string& LastStatusText() const { return m_LastStatusText; }
    bool LastSessionUsedCuda() const { return m_LastSessionUsedCuda; }
    bool LastSessionUsedCpu() const { return m_LastSessionUsedCpu; }

private:
    struct OrtState;

    std::filesystem::path m_RuntimeDirectory;
    mutable bool m_Probed = false;
    mutable bool m_CudaProviderAvailable = false;
    mutable BackendStatus m_Status;
    mutable std::string m_CudaProviderStatus;
    std::string m_LoadedModelId;
    std::string m_InputName;
    std::string m_OutputName;
    std::string m_LastStatusText;
    bool m_LastSessionUsedCuda = false;
    bool m_LastSessionUsedCpu = false;
    std::unique_ptr<OrtState> m_Ort;

    void Probe() const;
    void ResetSession();
};

} // namespace NeuralDenoise
