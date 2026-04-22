#include "RenderPipeline.h"
#include "ThirdParty/stb_image.h"
#include <iostream>

RenderPipeline::RenderPipeline()
    : m_Width(0), m_Height(0),
      m_SourceChannels(4),
      m_SourceTexture(0), m_PingTexture(0), m_PongTexture(0),
      m_PingFBO(0), m_PongFBO(0), m_OutputTexture(0)
{}

RenderPipeline::~RenderPipeline() {
    CleanupFBOs();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
}

void RenderPipeline::Initialize() {
    m_Quad.Initialize();
}

void RenderPipeline::CleanupFBOs() {
    if (m_PingFBO)     { glDeleteFramebuffers(1, &m_PingFBO);  m_PingFBO = 0; }
    if (m_PongFBO)     { glDeleteFramebuffers(1, &m_PongFBO);  m_PongFBO = 0; }
    if (m_PingTexture) { glDeleteTextures(1, &m_PingTexture); m_PingTexture = 0; }
    if (m_PongTexture) { glDeleteTextures(1, &m_PongTexture); m_PongTexture = 0; }
}

void RenderPipeline::Resize(int width, int height) {
    if (width == m_Width && height == m_Height) return;
    m_Width = width;
    m_Height = height;

    CleanupFBOs();

    m_PingTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    m_PongTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    m_PingFBO = GLHelpers::CreateFBO(m_PingTexture);
    m_PongFBO = GLHelpers::CreateFBO(m_PongTexture);
}

bool RenderPipeline::LoadSourceImage(const std::string& filepath) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(filepath.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::cerr << "[RenderPipeline] Failed to load image: " << filepath
                  << " (" << stbi_failure_reason() << ")\n";
        return false;
    }

    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    m_SourceTexture = GLHelpers::CreateTextureFromPixels(data, w, h, 4);
    m_SourceChannels = 4;
    m_SourcePixels.assign(data, data + (w * h * 4));
    stbi_image_free(data);

    Resize(w, h);

    std::cout << "[RenderPipeline] Loaded image " << w << "x" << h << " from: " << filepath << "\n";
    return true;
}

void RenderPipeline::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch) {
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    m_SourceTexture = GLHelpers::CreateTextureFromPixels(data, w, h, ch);
    m_SourceChannels = ch;
    const int clampedChannels = std::max(1, ch);
    m_SourcePixels.assign(data, data + (w * h * clampedChannels));
    Resize(w, h);
}

void RenderPipeline::Execute(const std::vector<std::shared_ptr<LayerBase>>& layers) {
    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture; // Nothing to process
        return;
    }

    // Save previous GL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    
    // Save and disable states that might interfere with full-screen quad rendering
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glViewport(0, 0, m_Width, m_Height);

    int activeCount = 0;
    for (auto& layer : layers) {
        if (layer->IsVisible()) activeCount++;
    }

    if (activeCount == 0) {
        // No layers active, output is just the source
        m_OutputTexture = m_SourceTexture;
        
        // Restore
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        if (prevScissor) glEnable(GL_SCISSOR_TEST);
        if (prevDepth) glEnable(GL_DEPTH_TEST);
        if (prevStencil) glEnable(GL_STENCIL_TEST);
        if (prevBlend) glEnable(GL_BLEND);
        return;
    }

    // Sequential ping-pong execution:
    // Layer 1 reads Source → writes Ping
    // Layer 2 reads Ping → writes Pong
    // Layer 3 reads Pong → writes Ping ... etc.
    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;

    for (auto& layer : layers) {
        if (!layer->IsVisible()) continue;

        unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
        unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;

        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glClear(GL_COLOR_BUFFER_BIT);

        layer->ExecuteWithSource(currentInput, m_SourceTexture, m_Width, m_Height, m_Quad);

        currentInput = targetTex;
        usePing = !usePing;
    }

    m_OutputTexture = currentInput;

    // Restore previous GL state
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

std::vector<unsigned char> RenderPipeline::GetOutputPixels(int& outW, int& outH) {
    outW = m_Width;
    outH = m_Height;
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};

    std::vector<unsigned char> pixels(m_Width * m_Height * 4);
    
    // Restore previous FBO binding
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    
    // Create a temporary FBO for reading
    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);

    glReadPixels(0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically
    int rowSize = m_Width * 4;
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; y++) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glDeleteFramebuffers(1, &tempFBO);

    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetScopesPixels(int& outW, int& outH) {
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};

    // Target a small size for analysis efficiency
    outW = 256;
    outH = 256;
    if (m_Width < outW) outW = m_Width;
    if (m_Height < outH) outH = m_Height;

    std::vector<unsigned char> pixels(outW * outH * 4);
    
    // Create a temporary FBO/Texture for downsampling
    unsigned int tempTex = GLHelpers::CreateEmptyTexture(outW, outH);
    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tempTex, 0);

    // Blit from current output to small target (GPU downsample)
    GLint prevReadFBO, prevDrawFBO;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    unsigned int srcFBO;
    glGenFramebuffers(1, &srcFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tempFBO);
    glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, outW, outH, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Read small pixels
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Cleanup
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    glDeleteFramebuffers(1, &srcFBO);
    glDeleteFramebuffers(1, &tempFBO);
    glDeleteTextures(1, &tempTex);

    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetSourcePixels(int& outW, int& outH) {
    if (m_SourceTexture == 0 || m_Width == 0 || m_Height == 0 || m_SourcePixels.empty()) {
        outW = outH = 0;
        return {};
    }

    outW = m_Width;
    outH = m_Height;

    std::vector<unsigned char> pixels = m_SourcePixels;
    const int rowSize = m_Width * std::max(1, m_SourceChannels);
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; ++y) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    return pixels;
}
