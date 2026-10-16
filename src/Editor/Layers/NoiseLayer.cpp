#include "NoiseLayer.h"
#include "NoiseLayerLegacyShaders.h"
#include "Renderer/FullscreenQuad.h"

NoiseLayer::NoiseLayer() {}
NoiseLayer::~NoiseLayer() { if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram); }

void NoiseLayer::InitializeGL() {
    if(!m_ShaderProgram)m_ShaderProgram = GLHelpers::CreateShaderProgram(s_NoiseVert, s_NoiseFrag);
}

void NoiseLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    if (m_Type==13) {
        m_FilmRenderer.Render(inputTexture,width,height,quad,m_Film,m_FilmInput);
        return;
    }
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uType"), m_Type);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uBlendMode"), m_BlendMode);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSeed"), m_Seed);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uScale"), m_Scale);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uOrigRes"), (float)width, (float)height);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uParamA"), m_ParamA);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uParamB"), m_ParamB);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uParamC"), m_ParamC);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uStrength"), m_Strength);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSatStrength"), m_SatStrength);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSatImpact"), m_SatImpact);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uOpacity"), m_Opacity);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlurriness"), m_Blurriness / 100.0f);

    quad.Draw();
    glUseProgram(0);
}
