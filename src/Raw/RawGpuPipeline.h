#pragma once

#include "RawImageData.h"

#include <cstddef>
#include <string>

namespace Raw {

class RawGpuPipeline {
public:
    RawGpuPipeline() = default;
    ~RawGpuPipeline();

    unsigned int Render(const RawImageData& raw, const RawDevelopSettings& settings, int previewMaxDimension = 0);
    void Clear();
    const std::string& GetLastError() const { return m_LastError; }
    int GetOutputWidth() const { return m_OutputWidth; }
    int GetOutputHeight() const { return m_OutputHeight; }

private:
    unsigned int m_Program = 0;
    unsigned int m_LinearProgram = 0;
    unsigned int m_RawTexture = 0;
    unsigned int m_CorrectedRawTexture = 0;
    unsigned int m_LinearTexture = 0;
    unsigned int m_OutputTexture = 0;
    unsigned int m_OutputFbo = 0;
    unsigned int m_QuadVao = 0;
    unsigned int m_QuadVbo = 0;
    int m_RawWidth = 0;
    int m_RawHeight = 0;
    int m_OutputWidth = 0;
    int m_OutputHeight = 0;
    std::size_t m_RawFingerprint = 0;
    std::size_t m_CorrectedRawFingerprint = 0;
    std::size_t m_LinearFingerprint = 0;
    std::string m_LastError;

    bool EnsureProgram();
    bool EnsureLinearProgram();
    bool UploadRawTexture(const RawImageData& raw);
    bool UploadCorrectedRawTexture(const RawImageData& raw, const RawDevelopSettings& settings, bool& outHasCorrectedRaw);
    bool UploadLinearTexture(const RawImageData& raw, const RawDevelopSettings& settings);
    bool EnsureOutput(int width, int height);
    bool EnsureFullscreenQuad();
};

} // namespace Raw
