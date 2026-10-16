#include "PixelationLayer.h"
#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"

namespace {
const char* kPixelationVertex=R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() { vUV=aTexCoord; gl_Position=vec4(aPos,0.0,1.0); }
)";
const char* kPixelationFragment=R"(
#version 130
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform float uResScale;
void main() {
    float blockSize=max(2.0,(100.0-uResScale)/5.0+1.0);
    vec2 pixelPos=floor(vUV*uRes/blockSize)*blockSize/uRes;
    FragColor=texture(uInputTex,pixelPos+(blockSize*0.5)/uRes);
}
)";
}
PixelationLayer::~PixelationLayer() { if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram); }
void PixelationLayer::InitializeGL() {
    m_ShaderProgram=GLHelpers::CreateShaderProgram(kPixelationVertex,kPixelationFragment);
}
void PixelationLayer::Execute(unsigned int inputTexture,int width,int height,FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram,"uInputTex"),0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram,"uRes"),static_cast<float>(width),static_cast<float>(height));
    glUniform1f(glGetUniformLocation(m_ShaderProgram,"uResScale"),m_ResScale);
    quad.Draw(); glUseProgram(0);
}
void PixelationLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Quality Scale","##QualityScale",&m_ResScale,1.f,100.f,"%.0f");
}
json PixelationLayer::Serialize() const { return {{"type","Pixelation"},{"resScale",m_ResScale}}; }
void PixelationLayer::Deserialize(const json& value) { m_ResScale=value.value("resScale",50.f); }
