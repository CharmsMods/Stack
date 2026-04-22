#include "GLHelpers.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace {

unsigned int LinkProgram(const unsigned int* shaderIds, int shaderCount) {
    unsigned int program = glCreateProgram();
    for (int i = 0; i < shaderCount; ++i) {
        glAttachShader(program, shaderIds[i]);
    }

    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[2048];
        glGetProgramInfoLog(program, 2048, nullptr, infoLog);
        std::cerr << "[GLHelpers] Program linking failed:\n" << infoLog << "\n";
        glDeleteProgram(program);
        return 0;
    }

    return program;
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
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);

    int success = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[2048];
        glGetShaderInfoLog(id, 2048, nullptr, infoLog);
        std::cerr << "[GLHelpers] Shader compilation failed:\n" << infoLog << "\n";
        glDeleteShader(id);
        return 0;
    }
    return id;
}

unsigned int CreateShaderProgram(const char* vertexSrc, const char* fragmentSrc) {
    unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertexSrc);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);

    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    const unsigned int shaderIds[] = { vs, fs };
    unsigned int program = LinkProgram(shaderIds, 2);

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

unsigned int CreateTextureFromPixels(const unsigned char* data, int width, int height, int channels) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    GLenum format = GL_RGBA;
    if (channels == 3) format = GL_RGB;
    else if (channels == 1) format = GL_RED;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

unsigned int CreateEmptyTexture(int width, int height) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

unsigned int CreateStorageTexture(int width, int height, unsigned int internalFormat) {
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

unsigned int CreateDepthTexture(int width, int height) {
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

unsigned int CreateTextureArray(int width, int height, int layers, unsigned int internalFormat) {
    if (width <= 0 || height <= 0 || layers <= 0) {
        return 0;
    }

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, internalFormat, width, height, layers);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return tex;
}

void UploadTextureArrayLayer(unsigned int texture, int layer, int width, int height, unsigned int format, unsigned int type, const void* pixels) {
    if (texture == 0 || layer < 0 || width <= 0 || height <= 0) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1, format, type, pixels);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

unsigned int CreateFBO(unsigned int colorTexture) {
    unsigned int fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[GLHelpers] Framebuffer incomplete!\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

} // namespace GLHelpers
