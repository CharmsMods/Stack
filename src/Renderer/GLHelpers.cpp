#include "GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <fstream>
#include <sstream>
#include <iostream>

#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_MAX_TEXTURE_SIZE
#define GL_MAX_TEXTURE_SIZE 0x0D33
#endif
#ifndef GL_MAX_ARRAY_TEXTURE_LAYERS
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#endif
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif
#ifndef GL_TEXTURE_BINDING_2D_ARRAY
#define GL_TEXTURE_BINDING_2D_ARRAY 0x8C1D
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER_BINDING
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_UNPACK_SKIP_ROWS
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#endif
#ifndef GL_UNPACK_SKIP_PIXELS
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#endif

namespace {

unsigned int LinkProgram(const unsigned int* shaderIds, int shaderCount, std::string* error = nullptr) {
    unsigned int program = glCreateProgram();
    for (int i = 0; i < shaderCount; ++i) {
        glAttachShader(program, shaderIds[i]);
    }

    // Most fullscreen layer shaders rely on the shared quad feeding position at
    // attribute 0 and UVs at attribute 1. Bind the common names before link so
    // core-profile contexts don't assign them unpredictably.
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aTexCoord");
    glBindAttribLocation(program, 1, "aTex");
    glBindAttribLocation(program, 1, "aUV");
    glBindFragDataLocation(program, 0, "FragColor");

    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[2048];
        glGetProgramInfoLog(program, 2048, nullptr, infoLog);
        if (error != nullptr) *error = infoLog;
        std::cerr << "[GLHelpers] Program linking failed:\n" << infoLog << "\n";
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

void ClearGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

bool IsSupportedTextureExtent(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    return limit > 0 && width <= limit && height <= limit;
}

} // namespace

namespace GLHelpers {

std::string ReadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[GLHelpers] Failed to open file: " << path << "\n";
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

unsigned int CompileShader(unsigned int type, const char* source) {
    return CompileShader(type, source, nullptr);
}

unsigned int CompileShader(unsigned int type, const char* source, std::string* error) {
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);

    int success = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[2048];
        glGetShaderInfoLog(id, 2048, nullptr, infoLog);
        if (error != nullptr) *error = infoLog;
        std::cerr << "[GLHelpers] Shader compilation failed:\n" << infoLog << "\n";
        glDeleteShader(id);
        return 0;
    }
    return id;
}

unsigned int CreateShaderProgram(const char* vertexSrc, const char* fragmentSrc) {
    return CreateShaderProgram(vertexSrc, fragmentSrc, nullptr);
}

unsigned int CreateShaderProgram(const char* vertexSrc, const char* fragmentSrc, std::string* error) {
    if (error != nullptr) error->clear();
    unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertexSrc, error);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc, error);

    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    const unsigned int shaderIds[] = { vs, fs };
    unsigned int program = LinkProgram(shaderIds, 2, error);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

unsigned int CreateComputeProgram(const char* computeSrc) {
    unsigned int cs = CompileShader(GL_COMPUTE_SHADER, computeSrc);
    if (!cs) {
        return 0;
    }

    const unsigned int shaderIds[] = { cs };
    unsigned int program = LinkProgram(shaderIds, 1);
    glDeleteShader(cs);
    return program;
}

unsigned int CreateTextureFromPixels(
    const unsigned char* data,
    int width,
    int height,
    int channels,
    bool generateMipmaps) {
    if (!IsSupportedTextureExtent(width, height) ||
        channels < 1 || channels > 4) {
        return 0;
    }

    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpackState;
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, tex);

    GLenum format = GL_RGBA;
    if (channels == 3) format = GL_RGB;
    else if (channels == 2) format = GL_RG;
    else if (channels == 1) format = GL_RED;

    savedUnpackState.ConfigureTightCpuUpload();
    ClearGlErrors();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    const GLenum uploadError = glGetError();
    savedUnpackState.Restore();
    if (uploadError != GL_NO_ERROR) {
        std::cerr << "[GLHelpers] Texture upload failed (" << width << "x" << height
                  << ", channels " << channels << ", GL error " << uploadError << ").\n";
        savedTexture.Restore();
        glDeleteTextures(1, &tex);
        return 0;
    }
    if (generateMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    savedTexture.Restore();
    return tex;
}

unsigned int CreateTextureFromData(
    const void* data, int width, int height,
    unsigned int internalFormat, unsigned int format, unsigned int type,
    unsigned int filter) {
    if (!IsSupportedTextureExtent(width, height)) return 0;

    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpack;
    unsigned int texture = 0;
    ClearGlErrors();
    glGenTextures(1, &texture);
    if (texture == 0) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    savedUnpack.ConfigureTightCpuUpload();
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
        format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLenum uploadError = glGetError();
    savedUnpack.Restore();
    savedTexture.Restore();
    if (uploadError != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

unsigned int CreateEmptyTexture(int width, int height) {
    if (!IsSupportedTextureExtent(width, height)) {
        return 0;
    }

    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpackState;
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    savedUnpackState.ConfigureTightCpuUpload();
    ClearGlErrors();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    const GLenum allocationError = glGetError();
    savedUnpackState.Restore();
    if (allocationError != GL_NO_ERROR) {
        std::cerr << "[GLHelpers] Empty RGBA16F texture allocation failed (" << width << "x" << height
                  << ", GL error " << allocationError << ").\n";
        savedTexture.Restore();
        glDeleteTextures(1, &tex);
        return 0;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    savedTexture.Restore();
    return tex;
}

unsigned int CreateStorageTexture(int width, int height, unsigned int internalFormat) {
    if (!IsSupportedTextureExtent(width, height)) {
        return 0;
    }
    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    ClearGlErrors();
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, width, height);
    const GLenum allocationError = glGetError();
    if (allocationError != GL_NO_ERROR) {
        std::cerr << "[GLHelpers] Immutable texture allocation failed ("
                  << width << "x" << height << ", GL error "
                  << allocationError << ").\n";
        savedTexture.Restore();
        glDeleteTextures(1, &tex);
        return 0;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    savedTexture.Restore();
    return tex;
}

unsigned int CreateDepthTexture(int width, int height) {
    if (!IsSupportedTextureExtent(width, height)) {
        return 0;
    }
    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpackState;
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    savedUnpackState.ConfigureTightCpuUpload();
    ClearGlErrors();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    const GLenum allocationError = glGetError();
    savedUnpackState.Restore();
    if (allocationError != GL_NO_ERROR) {
        std::cerr << "[GLHelpers] Depth texture allocation failed ("
                  << width << "x" << height << ", GL error "
                  << allocationError << ").\n";
        savedTexture.Restore();
        glDeleteTextures(1, &tex);
        return 0;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    savedTexture.Restore();
    return tex;
}

unsigned int CreateTextureArray(int width, int height, int layers, unsigned int internalFormat) {
    GLint layerLimit = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &layerLimit);
    if (!IsSupportedTextureExtent(width, height) ||
        layers <= 0 || layerLimit <= 0 || layers > layerLimit) {
        return 0;
    }

    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BINDING_2D_ARRAY);
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    ClearGlErrors();
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, internalFormat, width, height, layers);
    const GLenum allocationError = glGetError();
    if (allocationError != GL_NO_ERROR) {
        std::cerr << "[GLHelpers] Texture-array allocation failed ("
                  << width << "x" << height << "x" << layers
                  << ", GL error " << allocationError << ").\n";
        savedTexture.Restore();
        glDeleteTextures(1, &tex);
        return 0;
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    savedTexture.Restore();
    return tex;
}

void UploadTextureArrayLayer(unsigned int texture, int layer, int width, int height, unsigned int format, unsigned int type, const void* pixels) {
    if (texture == 0 || layer < 0 || width <= 0 || height <= 0) {
        return;
    }

    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BINDING_2D_ARRAY);
    const Stack::Renderer::GLState::PixelUnpackState savedUnpackState;
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    savedUnpackState.ConfigureTightCpuUpload();
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1, format, type, pixels);
    savedUnpackState.Restore();
    savedTexture.Restore();
}

unsigned int CreateFBO(unsigned int colorTexture) {
    if (colorTexture == 0) {
        return 0;
    }

    const Stack::Renderer::GLState::FramebufferState savedState;
    unsigned int fbo = 0;
    glGenFramebuffers(1, &fbo);
    if (fbo == 0) {
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[GLHelpers] Framebuffer incomplete!\n";
        savedState.Restore();
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }
    savedState.Restore();
    return fbo;
}

} // namespace GLHelpers
