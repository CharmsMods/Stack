#include "Renderer/Frequency/GpuFft.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "Renderer/GLStateGuards.h"

#include <algorithm>
#include <limits>
#include <iostream>

namespace Stack::Renderer::Frequency {

namespace {

// GL_RG32F / GL_RG and compute/image-access constants are not part of the
// legacy GLLoader defines; declare them locally (values from the desktop GL
// spec). RG32F gives the butterfly full 32-bit precision per real/imaginary
// component, which keeps log2(N) accumulated error manageable.
#ifndef GL_RG32F
#define GL_RG32F 0x8230
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif
#ifndef GL_TEXTURE_FETCH_BARRIER_BIT
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#endif
#ifndef GL_FRAMEBUFFER_BARRIER_BIT
#define GL_FRAMEBUFFER_BARRIER_BIT 0x00000400
#endif
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif

// 8x8 workgroup tiles keep dispatch sizes simple and match common
// warp/wavefront granularity. Each invocation transforms one texel.
constexpr int kWorkgroupX = 8;
constexpr int kWorkgroupY = 8;

struct ScopedComputeState {
    struct ImageBinding {
        GLint texture = 0;
        GLint level = 0;
        GLint layered = GL_FALSE;
        GLint layer = 0;
        GLint access = GL_READ_ONLY;
        GLint format = GL_RGBA16F;
    };

    GLint program = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint texture0 = 0;
    ImageBinding imageBindings[2];

    ScopedComputeState() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
        glActiveTexture(static_cast<GLenum>(activeTexture));
        for (GLuint unit = 0; unit < 2; ++unit) {
            ImageBinding& binding = imageBindings[unit];
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME, unit, &binding.texture);
            glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL, unit, &binding.level);
            glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED, unit, &binding.layered);
            glGetIntegeri_v(GL_IMAGE_BINDING_LAYER, unit, &binding.layer);
            glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, unit, &binding.access);
            glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, unit, &binding.format);
        }
    }

    ~ScopedComputeState() {
        for (GLuint unit = 0; unit < 2; ++unit) {
            const ImageBinding& binding = imageBindings[unit];
            const GLuint texture =
                binding.texture > 0 &&
                    glIsTexture(static_cast<GLuint>(binding.texture))
                ? static_cast<GLuint>(binding.texture)
                : 0;
            glBindImageTexture(
                unit,
                texture,
                binding.level,
                static_cast<GLboolean>(binding.layered),
                binding.layer,
                static_cast<GLenum>(binding.access),
                binding.format != 0
                    ? static_cast<GLenum>(binding.format)
                    : GL_RGBA16F);
        }
        glUseProgram(static_cast<GLuint>(program));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture0));
        glActiveTexture(static_cast<GLenum>(activeTexture));
    }
};

void ClearGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

// Allocate an immutable-storage RG32F texture usable as an image2D target.
unsigned int CreateComplexTexture(int w, int h) {
    const unsigned int tex =
        GLHelpers::CreateStorageTexture(w, h, GL_RG32F);
    if (tex == 0) return 0;
    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    savedTexture.Restore();
    return tex;
}

// Copy a texture by FBO blit (glCopyImageSubData is not loaded in Stack's
// loader; blit is, and it works between identically-sized RG32F textures).
unsigned int BlitClone(unsigned int src, int w, int h) {
    if (src == 0 || w <= 0 || h <= 0) return 0;
    unsigned int dst = CreateComplexTexture(w, h);
    if (dst == 0) return 0;
    const Stack::Renderer::GLState::FramebufferState savedState;
    const unsigned int readFbo = GLHelpers::CreateFBO(src);
    const unsigned int drawFbo = GLHelpers::CreateFBO(dst);
    if (readFbo == 0 || drawFbo == 0) {
        if (readFbo != 0) glDeleteFramebuffers(1, &readFbo);
        if (drawFbo != 0) glDeleteFramebuffers(1, &drawFbo);
        glDeleteTextures(1, &dst);
        return 0;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    ClearGlErrors();
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    const bool copied = glGetError() == GL_NO_ERROR;
    savedState.Restore();
    glDeleteFramebuffers(1, &readFbo);
    glDeleteFramebuffers(1, &drawFbo);
    if (!copied) {
        glDeleteTextures(1, &dst);
        return 0;
    }
    return dst;
}

void DispatchCover(int w, int h) {
    const unsigned int groupsX = static_cast<unsigned int>((w + kWorkgroupX - 1) / kWorkgroupX);
    const unsigned int groupsY = static_cast<unsigned int>((h + kWorkgroupY - 1) / kWorkgroupY);
    glDispatchCompute(groupsX, groupsY, 1);
}

int BitsForPowerOfTwo(int n) {
    int bits = 0;
    int value = std::max(1, n);
    while (value > 1) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

bool IsPowerOfTwo(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

} // namespace

GpuFft::~GpuFft() {
    Shutdown();
}

void GpuFft::Shutdown() {
    if (m_ComplexA) glDeleteTextures(1, &m_ComplexA);
    if (m_ComplexB) glDeleteTextures(1, &m_ComplexB);
    if (m_ButterflyProgram) glDeleteProgram(m_ButterflyProgram);
    if (m_PackProgram) glDeleteProgram(m_PackProgram);
    if (m_BitReverseProgram) glDeleteProgram(m_BitReverseProgram);
    if (m_UnpackProgram) glDeleteProgram(m_UnpackProgram);
    m_ComplexA = 0;
    m_ComplexB = 0;
    m_ButterflyProgram = 0;
    m_PackProgram = 0;
    m_BitReverseProgram = 0;
    m_UnpackProgram = 0;
    m_ScratchW = 0;
    m_ScratchH = 0;
}

int GpuFft::NextPowerOfTwo(int n) {
    if (n <= 1) return 1;
    constexpr int highestPositivePowerOfTwo =
        1 << (std::numeric_limits<int>::digits - 1);
    if (n > highestPositivePowerOfTwo) return 0;
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

const char* GpuFft::PackComputeSource() {
    return R"(
        #version 430 core
        layout (local_size_x = 8, local_size_y = 8) in;
        layout (binding = 0, rg32f) writeonly uniform image2D uOut;
        uniform sampler2D uSource;
        uniform int uSourceW;
        uniform int uSourceH;
        uniform int uPaddedW;
        uniform int uPaddedH;
        uniform ivec2 uPaddingOrigin;
        uniform int uEdgePolicy;
        uniform int uBitsX;
        uniform int uBitsY;

        uint reverseBits(uint v, int bits) {
            uint r = 0u;
            for (int i = 0; i < 24; ++i) {
                if (i >= bits) break;
                r = (r << 1u) | (v & 1u);
                v >>= 1u;
            }
            return r;
        }

        int wrapIndex(int value, int size) {
            if (size <= 1) return 0;
            // Avoid driver-dependent signed remainder behavior for negative
            // padding coordinates. floor-based modulo is exact for the image
            // extents Stack can allocate and maps -1 to size - 1.
            return value - int(floor(float(value) / float(size))) * size;
        }

        int mirrorIndex(int value, int size) {
            if (size <= 1) return 0;
            int period = 2 * (size - 1);
            int folded = wrapIndex(value, period);
            return folded < size ? folded : period - folded;
        }

        void main() {
            ivec2 p = ivec2(gl_GlobalInvocationID.xy);
            if (p.x >= uPaddedW || p.y >= uPaddedH) return;
            ivec2 outP = ivec2(
                int(reverseBits(uint(p.x), uBitsX)),
                int(reverseBits(uint(p.y), uBitsY)));
            ivec2 sourceP = p - uPaddingOrigin;
            bool inside = sourceP.x >= 0 && sourceP.x < uSourceW &&
                          sourceP.y >= 0 && sourceP.y < uSourceH;
            if (!inside && uEdgePolicy == 2) {
                imageStore(uOut, outP, vec4(0.0, 0.0, 0.0, 0.0));
                return;
            }
            if (!inside && uEdgePolicy == 0) {
                sourceP = ivec2(
                    mirrorIndex(sourceP.x, uSourceW),
                    mirrorIndex(sourceP.y, uSourceH));
            } else if (!inside) {
                sourceP = ivec2(
                    wrapIndex(sourceP.x, uSourceW),
                    wrapIndex(sourceP.y, uSourceH));
            }
            float v = texelFetch(uSource, sourceP, 0).r;
            imageStore(uOut, outP, vec4(v, 0.0, 0.0, 0.0));
        }
    )";
}

const char* GpuFft::ButterflyComputeSource() {
    // One decimation-in-time butterfly pass along a chosen axis. Inputs are
    // bit-reversed up front, so each pass can write natural-index outputs.
    //   m        = current sub-transform size (2, 4, 8, ...)
    //   m2       = m / 2
    //   direction= -1 for forward, +1 for inverse
    //   axis     = 0 -> transform along X (rows), 1 -> along Y (columns)
    //
    return R"(
        #version 430 core
        layout (local_size_x = 8, local_size_y = 8) in;
        layout (binding = 0, rg32f) readonly uniform image2D uSrc;
        layout (binding = 1, rg32f) writeonly uniform image2D uDst;
        uniform int uW;
        uniform int uH;
        uniform int uM;        // current butterfly size (power of two >= 2)
        uniform int uM2;       // m / 2
        uniform int uDirection; // -1 forward, +1 inverse
        uniform int uAxis;     // 0 = X axis, 1 = Y axis

        const float PI = 3.14159265358979323846;

        void main() {
            ivec2 p = ivec2(gl_GlobalInvocationID.xy);
            if (uAxis == 0) {
                if (p.x >= uW || p.y >= uH) return;
                int j = p.x;
                int blockStart = (j / uM) * uM;
                int k = j % uM2;
                bool upper = (j % uM) < uM2;
                vec2 a = imageLoad(uSrc, ivec2(blockStart + k, p.y)).rg;
                vec2 b = imageLoad(uSrc, ivec2(blockStart + k + uM2, p.y)).rg;
                float ang = float(uDirection) * 2.0 * PI * float(k) / float(uM);
                float cs = cos(ang);
                float sn = sin(ang);
                // Complex multiply: w * b  (w = cs + i*sn)
                vec2 wb = vec2(b.x * cs - b.y * sn, b.x * sn + b.y * cs);
                vec2 lo = a + wb;
                vec2 hi = a - wb;
                vec2 outValue = upper ? lo : hi;
                imageStore(uDst, p, vec4(outValue.x, outValue.y, 0.0, 0.0));
            } else {
                if (p.x >= uW || p.y >= uH) return;
                int j = p.y;
                int blockStart = (j / uM) * uM;
                int k = j % uM2;
                bool upper = (j % uM) < uM2;
                vec2 a = imageLoad(uSrc, ivec2(p.x, blockStart + k)).rg;
                vec2 b = imageLoad(uSrc, ivec2(p.x, blockStart + k + uM2)).rg;
                float ang = float(uDirection) * 2.0 * PI * float(k) / float(uM);
                float cs = cos(ang);
                float sn = sin(ang);
                vec2 wb = vec2(b.x * cs - b.y * sn, b.x * sn + b.y * cs);
                vec2 lo = a + wb;
                vec2 hi = a - wb;
                vec2 outValue = upper ? lo : hi;
                imageStore(uDst, p, vec4(outValue.x, outValue.y, 0.0, 0.0));
            }
        }
    )";
}

const char* GpuFft::BitReverseComputeSource() {
    return R"(
        #version 430 core
        layout (local_size_x = 8, local_size_y = 8) in;
        layout (binding = 0, rg32f) readonly uniform image2D uSrc;
        layout (binding = 1, rg32f) writeonly uniform image2D uDst;
        uniform int uW;
        uniform int uH;
        uniform int uBitsX;
        uniform int uBitsY;

        uint reverseBits(uint v, int bits) {
            uint r = 0u;
            for (int i = 0; i < 24; ++i) {
                if (i >= bits) break;
                r = (r << 1u) | (v & 1u);
                v >>= 1u;
            }
            return r;
        }

        void main() {
            ivec2 p = ivec2(gl_GlobalInvocationID.xy);
            if (p.x >= uW || p.y >= uH) return;
            ivec2 outP = ivec2(
                int(reverseBits(uint(p.x), uBitsX)),
                int(reverseBits(uint(p.y), uBitsY)));
            vec2 c = imageLoad(uSrc, p).rg;
            imageStore(uDst, outP, vec4(c.x, c.y, 0.0, 0.0));
        }
    )";
}

const char* GpuFft::UnpackComputeSource() {
    // Read complex result, write the real part to an RG32F spatial texture.
    // Inverse includes the 1/(W*H) normalization that makes a forward->inverse
    // round trip reproduce the original values.
    return R"(
        #version 430 core
        layout (local_size_x = 8, local_size_y = 8) in;
        layout (binding = 0, rg32f) readonly uniform image2D uSrc;
        layout (binding = 1, rg32f) writeonly uniform image2D uDst;
        uniform int uW;
        uniform int uH;
        uniform int uNormalize;

        void main() {
            ivec2 p = ivec2(gl_GlobalInvocationID.xy);
            if (p.x >= uW || p.y >= uH) return;
            vec2 c = imageLoad(uSrc, p).rg;
            float v = c.x;
            if (uNormalize != 0) {
                v /= float(uW * uH);
            }
            imageStore(uDst, p, vec4(v, 0.0, 0.0, 0.0));
        }
    )";
}

void GpuFft::EnsurePrograms() {
    if (m_ButterflyProgram && m_PackProgram && m_BitReverseProgram && m_UnpackProgram) return;

    if (!m_PackProgram) {
        m_PackProgram = GLHelpers::CreateComputeProgram(PackComputeSource());
        if (!m_PackProgram) {
            std::cerr << "[GpuFft] Failed to compile pack compute program.\n";
        }
    }
    if (!m_ButterflyProgram) {
        m_ButterflyProgram = GLHelpers::CreateComputeProgram(ButterflyComputeSource());
        if (!m_ButterflyProgram) {
            std::cerr << "[GpuFft] Failed to compile butterfly compute program.\n";
        }
    }
    if (!m_BitReverseProgram) {
        m_BitReverseProgram = GLHelpers::CreateComputeProgram(BitReverseComputeSource());
        if (!m_BitReverseProgram) {
            std::cerr << "[GpuFft] Failed to compile bit-reverse compute program.\n";
        }
    }
    if (!m_UnpackProgram) {
        m_UnpackProgram = GLHelpers::CreateComputeProgram(UnpackComputeSource());
        if (!m_UnpackProgram) {
            std::cerr << "[GpuFft] Failed to compile unpack compute program.\n";
        }
    }
}

bool GpuFft::EnsureScratch(int paddedW, int paddedH) {
    if (m_ScratchW == paddedW && m_ScratchH == paddedH && m_ComplexA && m_ComplexB) {
        return true;
    }
    if (m_ComplexA) glDeleteTextures(1, &m_ComplexA);
    if (m_ComplexB) glDeleteTextures(1, &m_ComplexB);
    m_ComplexA = 0;
    m_ComplexB = 0;
    m_ScratchW = 0;
    m_ScratchH = 0;
    m_ComplexA = CreateComplexTexture(paddedW, paddedH);
    if (m_ComplexA == 0) return false;
    m_ComplexB = CreateComplexTexture(paddedW, paddedH);
    if (m_ComplexB == 0) {
        glDeleteTextures(1, &m_ComplexA);
        m_ComplexA = 0;
        return false;
    }
    m_ScratchW = paddedW;
    m_ScratchH = paddedH;
    return true;
}

// Run the full log2(N) Stockham passes along one axis. Reads start from
// complexIn and the final result lands in the returned texture id (which is
// one of complexIn/complexOut depending on parity of pass count).
static unsigned int RunAxis(unsigned int butterflyProgram,
                            unsigned int complexIn,
                            unsigned int complexOut,
                            int paddedW,
                            int paddedH,
                            int axis,
                            int direction) {
    const int length = (axis == 0) ? paddedW : paddedH;
    if (length <= 1) return complexIn;

    glUseProgram(butterflyProgram);
    glUniform1i(glGetUniformLocation(butterflyProgram, "uW"), paddedW);
    glUniform1i(glGetUniformLocation(butterflyProgram, "uH"), paddedH);
    glUniform1i(glGetUniformLocation(butterflyProgram, "uDirection"), direction);
    glUniform1i(glGetUniformLocation(butterflyProgram, "uAxis"), axis);

    unsigned int readTex = complexIn;
    unsigned int writeTex = complexOut;

    // Each pass doubles the sub-transform size m: 2, 4, 8, ..., length.
    for (int m = 2; m <= length; m <<= 1) {
        const int m2 = m / 2;
        glUniform1i(glGetUniformLocation(butterflyProgram, "uM"), m);
        glUniform1i(glGetUniformLocation(butterflyProgram, "uM2"), m2);

        glBindImageTexture(0, readTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
        glBindImageTexture(1, writeTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);

        DispatchCover(paddedW, paddedH);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        std::swap(readTex, writeTex);
    }

    // After each pass we swap, so after the last write into writeTex we
    // swapped readTex<-writeTex. The final result therefore lives in readTex.
    return readTex;
}

unsigned int GpuFft::Forward(unsigned int sourceTexture,
                             int sourceW,
                             int sourceH,
                             int paddedW,
                             int paddedH,
                             int paddingOriginX,
                             int paddingOriginY,
                             FftEdgePolicy edgePolicy) {
    if (!sourceTexture ||
        sourceW <= 0 || sourceH <= 0 ||
        !IsPowerOfTwo(paddedW) || !IsPowerOfTwo(paddedH) ||
        paddingOriginX < 0 || paddingOriginY < 0 ||
        sourceW > paddedW - paddingOriginX ||
        sourceH > paddedH - paddingOriginY) {
        return 0;
    }
    const ScopedComputeState savedState;
    EnsurePrograms();
    if (!Ready()) return 0;
    if (!EnsureScratch(paddedW, paddedH)) return 0;
    ClearGlErrors();

    // Pack source (sampled) -> complexA (zero-padded).
    glUseProgram(m_PackProgram);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uSourceW"), sourceW);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uSourceH"), sourceH);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uPaddedW"), paddedW);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uPaddedH"), paddedH);
    glUniform2i(
        glGetUniformLocation(m_PackProgram, "uPaddingOrigin"),
        paddingOriginX,
        paddingOriginY);
    glUniform1i(
        glGetUniformLocation(m_PackProgram, "uEdgePolicy"),
        static_cast<int>(edgePolicy));
    glUniform1i(glGetUniformLocation(m_PackProgram, "uBitsX"), BitsForPowerOfTwo(paddedW));
    glUniform1i(glGetUniformLocation(m_PackProgram, "uBitsY"), BitsForPowerOfTwo(paddedH));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uSource"), 0);

    glBindImageTexture(0, m_ComplexA, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
    DispatchCover(paddedW, paddedH);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // 2D FFT = FFT each row (axis 0), then FFT each column (axis 1).
    // Direction -1 = forward.
    unsigned int rowResult = RunAxis(m_ButterflyProgram, m_ComplexA, m_ComplexB, paddedW, paddedH, 0, -1);
    unsigned int colSource = rowResult;
    unsigned int colOther = (rowResult == m_ComplexA) ? m_ComplexB : m_ComplexA;
    unsigned int finalResult = RunAxis(m_ButterflyProgram, colSource, colOther, paddedW, paddedH, 1, -1);

    // The scratch buffers are reused across calls, but graph caching assumes
    // returned textures are uniquely owned. Hand back a fresh clone.
    glMemoryBarrier(
        GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
        GL_TEXTURE_FETCH_BARRIER_BIT |
        GL_FRAMEBUFFER_BARRIER_BIT);
    if (glGetError() != GL_NO_ERROR) return 0;
    return BlitClone(finalResult, paddedW, paddedH);
}

unsigned int GpuFft::Inverse(unsigned int spectrumTexture,
                             int paddedW,
                             int paddedH) {
    if (!spectrumTexture ||
        !IsPowerOfTwo(paddedW) || !IsPowerOfTwo(paddedH)) {
        return 0;
    }
    const ScopedComputeState savedState;
    EnsurePrograms();
    if (!Ready()) return 0;
    if (!EnsureScratch(paddedW, paddedH)) return 0;
    ClearGlErrors();

    // Inverse DIT needs bit-reversed input just like forward. Copy the
    // externally-owned spectrum into complexA while reversing both axes.
    glUseProgram(m_BitReverseProgram);
    glUniform1i(glGetUniformLocation(m_BitReverseProgram, "uW"), paddedW);
    glUniform1i(glGetUniformLocation(m_BitReverseProgram, "uH"), paddedH);
    glUniform1i(glGetUniformLocation(m_BitReverseProgram, "uBitsX"), BitsForPowerOfTwo(paddedW));
    glUniform1i(glGetUniformLocation(m_BitReverseProgram, "uBitsY"), BitsForPowerOfTwo(paddedH));
    glBindImageTexture(0, spectrumTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
    glBindImageTexture(1, m_ComplexA, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
    DispatchCover(paddedW, paddedH);
    glMemoryBarrier(
        GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
        GL_TEXTURE_FETCH_BARRIER_BIT |
        GL_FRAMEBUFFER_BARRIER_BIT);

    unsigned int rowResult = RunAxis(m_ButterflyProgram, m_ComplexA, m_ComplexB, paddedW, paddedH, 0, +1);
    unsigned int colSource = rowResult;
    unsigned int colOther = (rowResult == m_ComplexA) ? m_ComplexB : m_ComplexA;
    unsigned int finalResult = RunAxis(m_ButterflyProgram, colSource, colOther, paddedW, paddedH, 1, +1);
    if (glGetError() != GL_NO_ERROR) return 0;

    // Unpack complex -> RG32F spatial (real part only, normalized).
    unsigned int outTex = CreateComplexTexture(paddedW, paddedH);
    if (outTex == 0) return 0;
    glUseProgram(m_UnpackProgram);
    glUniform1i(glGetUniformLocation(m_UnpackProgram, "uW"), paddedW);
    glUniform1i(glGetUniformLocation(m_UnpackProgram, "uH"), paddedH);
    glUniform1i(glGetUniformLocation(m_UnpackProgram, "uNormalize"), 1);

    glBindImageTexture(0, finalResult, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
    glBindImageTexture(1, outTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
    DispatchCover(paddedW, paddedH);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &outTex);
        return 0;
    }

    return outTex;
}

unsigned int GpuFft::RoundTrip(unsigned int sourceTexture,
                               int sourceW,
                               int sourceH,
                               int paddedW,
                               int paddedH,
                               int paddingOriginX,
                               int paddingOriginY,
                               FftEdgePolicy edgePolicy) {
    const unsigned int spectrum = Forward(
        sourceTexture,
        sourceW,
        sourceH,
        paddedW,
        paddedH,
        paddingOriginX,
        paddingOriginY,
        edgePolicy);
    if (!spectrum) return 0;
    unsigned int reconstructed = Inverse(spectrum, paddedW, paddedH);
    glDeleteTextures(1, &spectrum);
    return reconstructed;
}

} // namespace Stack::Renderer::Frequency
