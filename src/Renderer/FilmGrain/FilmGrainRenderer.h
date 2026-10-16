#pragma once
#include "FilmGrainParameters.h"
#include <array>
#include <functional>

class FullscreenQuad;
namespace Stack::Renderer::FilmGrain {
struct Input {
    int nativeWidth=0, nativeHeight=0;
    // Optional native-pixel window for a magnified reference preview. Normal
    // processing leaves span zero and samples the complete native canvas.
    std::array<float,2> sampleOrigin{}, sampleSpan{};
    bool scalar=false, premultiplied=false;
    int encodingHint=-1; // 0: linear, 1: sRGB, -1: unspecified, -2: unsupported declared transfer.
    std::array<float,3> components{1.f,1.f,1.f}, luma{.2126f,.7152f,.0722f};
    std::function<bool()> shouldCancel;
};
// Owned by one processing instance in one GL context. Amplitude/color/tone
// changes reuse the immutable field; input pixels never participate in its key.
class Renderer {
public:
    Renderer()=default;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    ~Renderer();
    void Render(unsigned int inputTexture,int width,int height,FullscreenQuad& quad,
        const Parameters& parameters,const Input& input);
private:
    void ReleaseField();
    void PrepareResponseCurve(const Parameters& parameters);
    unsigned int m_FieldProgram=0,m_CompositeProgram=0,m_FieldTexture=0,m_FieldFbo=0;
    int m_Width=0,m_Height=0;
    std::array<float,17> m_FieldKey{};
    unsigned int m_ResponseTexture=0;
    int m_ResponseSegments=0;
    std::vector<RawRecipe::RawBezierCurvePoint> m_ResponseCurve;
    int m_Seed=0,m_Frame=0;
    bool m_Valid=false;
};
}
