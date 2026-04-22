#pragma once

// Use our own OpenGL loader instead of ImGui's partial one
#include "Renderer/GLLoader.h"

#include <string>

namespace GLHelpers {
    // Load a file's entire contents into a string (for shader source loading)
    std::string ReadFile(const std::string& path);

    // Compile a single shader stage from source string
    unsigned int CompileShader(unsigned int type, const char* source);

    // Link a vertex + fragment shader into a program, returns program ID
    unsigned int CreateShaderProgram(const char* vertexSrc, const char* fragmentSrc);

    // Link a compute shader into a program, returns program ID
    unsigned int CreateComputeProgram(const char* computeSrc);

    // Create a simple RGBA texture from CPU pixel data
    unsigned int CreateTextureFromPixels(const unsigned char* data, int width, int height, int channels);

    // Create an empty RGBA texture for FBO attachment
    unsigned int CreateEmptyTexture(int width, int height);

    // Create an immutable texture storage target for image load/store workloads
    unsigned int CreateStorageTexture(int width, int height, unsigned int internalFormat);

    // Create a simple depth texture for framebuffer depth attachment
    unsigned int CreateDepthTexture(int width, int height);

    // Create an immutable 2D texture array
    unsigned int CreateTextureArray(int width, int height, int layers, unsigned int internalFormat);

    // Upload one layer into a 2D texture array
    void UploadTextureArrayLayer(unsigned int texture, int layer, int width, int height, unsigned int format, unsigned int type, const void* pixels);

    // Create a Framebuffer Object with a color texture attachment, returns FBO ID
    unsigned int CreateFBO(unsigned int colorTexture);
}
