#pragma once
#include "Raw/RawAssetCapture.h"

namespace Stack::Renderer {
// UI-context GPU resources for temporary range feedback. Never a graph result.
class RawAssetCaptureOverlay {
public:
    RawAssetCaptureOverlay() = default;
    RawAssetCaptureOverlay(const RawAssetCaptureOverlay&) = delete;
    RawAssetCaptureOverlay& operator=(const RawAssetCaptureOverlay&) = delete;
    bool Render(const std::shared_ptr<const RawAssetCapture::PreviewField>& input,
        const RawAssetCapture::Qualifier& qualifier,std::string& error);
    unsigned int Texture() const { return m_Output; }
    void ClearInput();
    void Shutdown();
private:
    std::shared_ptr<const RawAssetCapture::PreviewField> m_Input;
    unsigned int m_Source=0,m_Output=0,m_Fbo=0,m_Program=0,m_Vao=0;
};
} // namespace Stack::Renderer
