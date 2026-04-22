#pragma once

#include "Renderer/GLHelpers.h"
#include "Renderer/FullscreenQuad.h"
#include "Editor/Layers/LayerBase.h"
#include <vector>
#include <memory>

// The sequential rendering pipeline. 
// Enforces the core architectural rule: Layer N+1 only sees Layer N's output.
// Uses ping-pong FBOs to chain processing stages.
class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();

    void Initialize();
    void Resize(int width, int height);

    // Load a source image from disk into the pipeline
    bool LoadSourceImage(const std::string& filepath);
    void LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch);

    // Execute the full layer stack sequentially (ping-pong rendering)
    void Execute(const std::vector<std::shared_ptr<LayerBase>>& layers);

    // Returns the final output texture ID for display in the ImGui viewport
    unsigned int GetOutputTexture() const { return m_OutputTexture; }
    unsigned int GetSourceTexture() const { return m_SourceTexture; }
    int GetCanvasWidth() const { return m_Width; }
    int GetCanvasHeight() const { return m_Height; }
    bool HasSourceImage() const { return m_SourceTexture != 0; }
    
    // Read final output pixels (usually for thumbnails)
    std::vector<unsigned char> GetOutputPixels(int& outW, int& outH);
    // Read original source pixels
    std::vector<unsigned char> GetSourcePixels(int& outW, int& outH);
    // Read downsampled pixels for scopes (fast)
    std::vector<unsigned char> GetScopesPixels(int& outW, int& outH);

    FullscreenQuad& GetQuad() { return m_Quad; }

private:
    FullscreenQuad m_Quad;

    int m_Width;
    int m_Height;
    int m_SourceChannels;

    unsigned int m_SourceTexture;   // The original loaded image
    unsigned int m_PingTexture;     // Ping FBO color attachment
    unsigned int m_PongTexture;     // Pong FBO color attachment
    unsigned int m_PingFBO;
    unsigned int m_PongFBO;
    unsigned int m_OutputTexture;   // Points to whichever is the final result
    std::vector<unsigned char> m_SourcePixels;

    void CleanupFBOs();
};
