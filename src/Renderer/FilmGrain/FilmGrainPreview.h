#pragma once
#include "FilmGrainRenderer.h"
#include "Renderer/FullscreenQuad.h"

namespace Stack::Renderer::FilmGrain {
// A fixed-size reference chip, evaluated by the production grain renderer.
// The UI owner calls Render only when its recipe or sampled tone changes.
// All objects belong to that owner's current GL context.
class ReferencePreview {
public:
    ReferencePreview()=default;
    ReferencePreview(const ReferencePreview&)=delete;
    ReferencePreview& operator=(const ReferencePreview&)=delete;
    ~ReferencePreview();
    void Render(const Parameters& parameters,Input input,float linearTone);
    unsigned int Texture() const {return m_DisplayTexture;}
private:
    Renderer m_Renderer;
    FullscreenQuad m_Quad;
    bool m_Initialized=false;
    unsigned int m_Source=0,m_Output=0,m_OutputFbo=0,m_DisplayTexture=0,m_DisplayFbo=0,m_DisplayProgram=0;
};
}
