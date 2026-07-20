#include "Renderer/Frequency/GpuFft.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"

#include <algorithm>
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

// Allocate an immutable-storage RG32F texture usable as an image2D target.
unsigned int CreateComplexTexture(int w, int h) {
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG32F, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Copy a texture by FBO blit (glCopyImageSubData is not loaded in Stack's
// loader; blit is, and it works between identically-sized RG32F textures).
unsigned int BlitClone(unsigned int src, int w, int h) {
    unsigned int dst = CreateComplexTexture(w, h);
    unsigned int readFbo = 0;
    unsigned int drawFbo = 0;
    glGenFramebuffers(1, &readFbo);
    glGenFramebuffers(1, &drawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &readFbo);
    glDeleteFramebuffers(1, &drawFbo);
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

} // namespace

GpuFft::~GpuFft() {
    if (m_ComplexA) glDeleteTextures(1, &m_ComplexA);
    if (m_ComplexB) glDeleteTextures(1, &m_ComplexB);
    if (m_ButterflyProgram) glDeleteProgram(m_ButterflyProgram);
    if (m_PackProgram) glDeleteProgram(m_PackProgram);
    if (m_BitReverseProgram) glDeleteProgram(m_BitReverseProgram);
    if (m_UnpackProgram) glDeleteProgram(m_UnpackProgram);
}

int GpuFft::NextPowerOfTwo(int n) {
    if (n <= 1) return 1;
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
        uniform int uLuminanceOnly;
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
            if (p.x >= uPaddedW || p.y >= uPaddedH) return;
            ivec2 outP = ivec2(
                int(reverseBits(uint(p.x), uBitsX)),
                int(reverseBits(uint(p.y), uBitsY)));
            if (p.x < uSourceW && p.y < uSourceH) {
                vec4 c = texelFetch(uSource, p, 0);
                float v = uLuminanceOnly != 0
                    ? dot(c.rgb, vec3(0.2126, 0.7152, 0.0722))
                    : c.r;
                imageStore(uOut, outP, vec4(v, 0.0, 0.0, 0.0));
            } else {
                // Zero-padding: out-of-source texels are complex zero.
                imageStore(uOut, outP, vec4(0.0, 0.0, 0.0, 0.0));
            }
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

void GpuFft::EnsureScratch(int paddedW, int paddedH) {
    if (m_ScratchW == paddedW && m_ScratchH == paddedH && m_ComplexA && m_ComplexB) {
        return;
    }
    if (m_ComplexA) glDeleteTextures(1, &m_ComplexA);
    if (m_ComplexB) glDeleteTextures(1, &m_ComplexB);
    m_ComplexA = CreateComplexTexture(paddedW, paddedH);
    m_ComplexB = CreateComplexTexture(paddedW, paddedH);
    m_ScratchW = paddedW;
    m_ScratchH = paddedH;
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
                             bool luminanceOnly) {
    if (!sourceTexture || paddedW <= 0 || paddedH <= 0) return 0;
    EnsurePrograms();
    if (!Ready()) return 0;
    EnsureScratch(paddedW, paddedH);

    // Pack source (sampled) -> complexA (zero-padded).
    glUseProgram(m_PackProgram);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uSourceW"), sourceW);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uSourceH"), sourceH);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uPaddedW"), paddedW);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uPaddedH"), paddedH);
    glUniform1i(glGetUniformLocation(m_PackProgram, "uLuminanceOnly"), luminanceOnly ? 1 : 0);
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
    return BlitClone(finalResult, paddedW, paddedH);
}

unsigned int GpuFft::Inverse(unsigned int spectrumTexture,
                             int paddedW,
                             int paddedH) {
    if (!spectrumTexture || paddedW <= 0 || paddedH <= 0) return 0;
    EnsurePrograms();
    if (!Ready()) return 0;
    EnsureScratch(paddedW, paddedH);

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
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    unsigned int rowResult = RunAxis(m_ButterflyProgram, m_ComplexA, m_ComplexB, paddedW, paddedH, 0, +1);
    unsigned int colSource = rowResult;
    unsigned int colOther = (rowResult == m_ComplexA) ? m_ComplexB : m_ComplexA;
    unsigned int finalResult = RunAxis(m_ButterflyProgram, colSource, colOther, paddedW, paddedH, 1, +1);

    // Unpack complex -> RG32F spatial (real part only, normalized).
    unsigned int outTex = CreateComplexTexture(paddedW, paddedH);
    glUseProgram(m_UnpackProgram);
    glUniform1i(glGetUniformLocation(m_UnpackProgram, "uW"), paddedW);
    glUniform1i(glGetUniformLocation(m_UnpackProgram, "uH"), paddedH);
    glUniform1i(glGetUniformLocation(m_UnpackProgram, "uNormalize"), 1);

    glBindImageTexture(0, finalResult, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
    glBindImageTexture(1, outTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
    DispatchCover(paddedW, paddedH);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    return outTex;
}

unsigned int GpuFft::RoundTrip(unsigned int sourceTexture,
                               int sourceW,
                               int sourceH,
                               int paddedW,
                               int paddedH,
                               bool luminanceOnly) {
    const unsigned int spectrum = Forward(sourceTexture, sourceW, sourceH, paddedW, paddedH, luminanceOnly);
    if (!spectrum) return 0;
    unsigned int reconstructed = Inverse(spectrum, paddedW, paddedH);
    glDeleteTextures(1, &spectrum);
    return reconstructed;
}

} // namespace Stack::Renderer::Frequency
