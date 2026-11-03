#include "Renderer/LocalToneGuide.h"
#include "Renderer/Internal/LocalToneGuideShaders.h"
#include "Renderer/FullscreenQuad.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/ScopedGLObjects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Stack::Renderer {
namespace {

// This is a nested processing pass. In particular, the caller may already have
// a destination framebuffer, shader, sampler or partial color mask installed.
struct PassState {
    GLint framebuffer = 0, viewport[4]{}, program = 0, vao = 0;
    GLint activeTexture = 0, texture = 0, sampler = 0, scissorBox[4]{}, polygonMode[2]{};
    GLboolean colorMask[4]{};
    static constexpr GLenum capabilities[] = {
        GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST,
        GL_CULL_FACE, GL_RASTERIZER_DISCARD, GL_FRAMEBUFFER_SRGB
    };
    GLboolean enabled[sizeof(capabilities) / sizeof(capabilities[0])]{};

    PassState() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
        glGetIntegerv(GL_POLYGON_MODE, polygonMode);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
        glBindSampler(0, 0);
        for (std::size_t i = 0; i < sizeof(capabilities) / sizeof(capabilities[0]); ++i) {
            enabled[i] = glIsEnabled(capabilities[i]);
            glDisable(capabilities[i]);
        }
        glEnable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    ~PassState() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vao));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        glBindSampler(0, static_cast<GLuint>(sampler));
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygonMode[0]));
        for (std::size_t i = 0; i < sizeof(capabilities) / sizeof(capabilities[0]); ++i) {
            if (enabled[i]) glEnable(capabilities[i]);
            else glDisable(capabilities[i]);
        }
    }
};

struct Framebuffer {
    GLuint name = 0;
    Framebuffer() { glGenFramebuffers(1, &name); }
    ~Framebuffer() { if (name) glDeleteFramebuffers(1, &name); }
};

bool Fail(std::string* error, const std::string& reason) {
    if (error) *error = reason;
    return false;
}

} // namespace

LocalToneGuide::~LocalToneGuide() {
    if (m_InitializeProgram) glDeleteProgram(m_InitializeProgram);
    if (m_BoxProgram) glDeleteProgram(m_BoxProgram);
}

bool LocalToneGuide::Ensure(std::string* error) {
    namespace S = LocalToneGuideShaders;
    std::string detail;
    if (!m_InitializeProgram)
        m_InitializeProgram = GLHelpers::CreateShaderProgram(S::Vertex, S::Initialize, &detail);
    if (!m_InitializeProgram) return Fail(error, "Local tone guide initialization shader failed: " + detail);
    if (!m_BoxProgram)
        m_BoxProgram = GLHelpers::CreateShaderProgram(S::Vertex, S::Box, &detail);
    if (!m_BoxProgram) return Fail(error, "Local tone guide box shader failed: " + detail);
    return true;
}

unsigned int LocalToneGuide::Render(unsigned int inputTexture, int width, int height,
    const std::array<float, 3>& lumaWeights, float radiusFraction, float rangeEv,
    FullscreenQuad& quad, const std::function<bool()>& shouldCancel, std::string* error) {
    if (error) error->clear();
    const auto canceled = [&] {
        if (!shouldCancel || !shouldCancel()) return false;
        Fail(error, "Local tone guide was canceled.");
        return true;
    };
    if (canceled()) return 0;
    if (!inputTexture || width <= 0 || height <= 0 ||
        !std::isfinite(radiusFraction) || !std::isfinite(rangeEv) ||
        !std::all_of(lumaWeights.begin(), lumaWeights.end(), [](float value) { return std::isfinite(value); })) {
        Fail(error, "Local tone guide received an invalid image or parameter.");
        return 0;
    }
    const double lumaSum = double(lumaWeights[0]) + lumaWeights[1] + lumaWeights[2];
    if (!(lumaSum > 0.0)) { Fail(error, "Local tone guide needs positive luminance weights."); return 0; }
    radiusFraction = std::clamp(radiusFraction, .002f, .25f);
    rangeEv = std::clamp(rangeEv, .05f, 2.f);
    PassState state;
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    GLint actualWidth = 0, actualHeight = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &actualWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &actualHeight);
    if (actualWidth != width || actualHeight != height) {
        Fail(error, "Local tone guide input extent does not match its texture.");
        return 0;
    }
    if (!Ensure(error)) return 0;
    const auto [analysisWidth, analysisHeight] = AnalysisExtent(width, height);
    const int radius = std::max(1, int(std::lround(radiusFraction * std::max(analysisWidth, analysisHeight))));
    // Three RGBA32F fields need at most 12 MiB, independent of source size.
    ScopedGLTexture first(GLHelpers::CreateStorageTexture(analysisWidth, analysisHeight, GL_RGBA32F));
    ScopedGLTexture second(GLHelpers::CreateStorageTexture(analysisWidth, analysisHeight, GL_RGBA32F));
    ScopedGLTexture third(GLHelpers::CreateStorageTexture(analysisWidth, analysisHeight, GL_RGBA32F));
    Framebuffer framebuffer;
    if (!first || !second || !third || !framebuffer.name) {
        Fail(error, "Local tone guide could not allocate its analysis fields.");
        return 0;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer.name);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, analysisWidth, analysisHeight);
    const auto render = [&](GLuint target, GLuint input, GLuint program, int rows) {
        if (canceled()) return false;
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return Fail(error, "Local tone guide framebuffer is incomplete.");
        glUseProgram(program);
        glBindTexture(GL_TEXTURE_2D, input);
        glUniform1i(glGetUniformLocation(program, "uInput"), 0);
        for (int y = 0; y < analysisHeight; y += rows) {
            if (canceled()) return false;
            // Scissoring bounds submitted work without restarting coordinates
            // or filter neighborhoods at an artificial processing boundary.
            glScissor(0, y, analysisWidth, std::min(rows, analysisHeight - y));
            quad.Draw();
        }
        const auto glError = glGetError();
        if (glError != GL_NO_ERROR)
            return Fail(error, "Local tone guide GPU pass failed (GL error " + std::to_string(glError) + ").");
        return true;
    };
    glUseProgram(m_InitializeProgram);
    glUniform2i(glGetUniformLocation(m_InitializeProgram, "uSourceSize"), width, height);
    glUniform2i(glGetUniformLocation(m_InitializeProgram, "uAnalysisSize"), analysisWidth, analysisHeight);
    glUniform3fv(glGetUniformLocation(m_InitializeProgram, "uLuma"), 1, lumaWeights.data());
    const std::uint64_t pixelsPerRow = std::uint64_t(width) * ((std::uint64_t(height) + analysisHeight - 1) / analysisHeight);
    const int initializationRows = int(std::clamp<std::uint64_t>((1024u * 1024u) / pixelsPerRow, 1, 32));
    if (!render(first.Get(), inputTexture, m_InitializeProgram, initializationRows)) return 0;
    const auto box = [&](GLuint target, GLuint input, bool vertical, bool coefficients) {
        glUseProgram(m_BoxProgram);
        glUniform2i(glGetUniformLocation(m_BoxProgram, "uDirection"), vertical ? 0 : 1, vertical ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_BoxProgram, "uRadius"), radius);
        glUniform1i(glGetUniformLocation(m_BoxProgram, "uMakeCoefficients"), coefficients ? 1 : 0);
        glUniform1f(glGetUniformLocation(m_BoxProgram, "uEpsilon"), rangeEv * rangeEv);
        return render(target, input, m_BoxProgram, 32);
    };
    if (!box(second.Get(), first.Get(), false, false) ||
        !box(third.Get(), second.Get(), true, true) ||
        !box(first.Get(), third.Get(), false, false) ||
        !box(second.Get(), first.Get(), true, false) || canceled()) return 0;
    return second.Release();
}

} // namespace Stack::Renderer
