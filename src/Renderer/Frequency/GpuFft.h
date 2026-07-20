#pragma once

#include <cstddef>

namespace Stack::Renderer::Frequency {

// GpuFft is Stack's first compute-shader subsystem: a radix-2 Cooley-Tukey
// FFT implemented as GL 4.3 compute passes over RG32F textures (R = real,
// G = imaginary). It is intentionally self-contained so it can serve as the
// template every future compute feature in Stack follows.
//
// Pipeline:
//   1. The caller provides a source texture (sampled) and a logical size.
//   2. Forward()/Inverse() lift the data into a power-of-two RG32F complex
//      buffer (zero-padded for forward, treated as already-complex for
//      inverse), run log2(N) row butterfly passes, then log2(N) column
//      passes via ping-pong image2D load/store, and return a complex
//      texture handle.
//   3. The caller samples the result, converts back to spatial RGBA, or
//      passes it on to another frequency node.
//
// All GPU resources are owned by the instance and reused across calls so
// repeated evaluation of an FFT node does not reallocate.
class GpuFft {
public:
    GpuFft() = default;
    ~GpuFft();

    GpuFft(const GpuFft&) = delete;
    GpuFft& operator=(const GpuFft&) = delete;

    // Transform a sampled source texture (RGBA16F) into a complex RG32F
    // spectrum of size paddedW x paddedH (both powers of two). When the
    // source is smaller than the padded buffer it is zero-padded, which is
    // the standard way to FFT arbitrarily sized images with a radix-2
    // kernel. When luminanceOnly is true only the luma is transformed and
    // the result still occupies a single RG32F texture; otherwise the red
    // channel is transformed (color is handled by per-channel fan-out at
    // the node level). Returns 0 on failure. The returned texture is owned
    // by the instance and remains valid until the next call or destruction.
    unsigned int Forward(unsigned int sourceTexture,
                         int sourceW,
                         int sourceH,
                         int paddedW,
                         int paddedH,
                         bool luminanceOnly);

    // Transform a complex RG32F spectrum back into a spatial RG32F texture
    // (R = real part; imaginary output is discarded by the caller). The
    // returned texture has dimensions paddedW x paddedH; the caller crops
    // to the original source size when projecting back to RGBA16F.
    unsigned int Inverse(unsigned int spectrumTexture,
                         int paddedW,
                         int paddedH);

    // Round-trip helper used by validation: forwards then inverts the
    // source and returns the reconstructed RG32F texture. Used during
    // bring-up to confirm the butterfly math is correct.
    unsigned int RoundTrip(unsigned int sourceTexture,
                           int sourceW,
                           int sourceH,
                           int paddedW,
                           int paddedH,
                           bool luminanceOnly);

    // Smallest power of two not less than n, used to pick the zero-padded FFT size.
    static int NextPowerOfTwo(int n);

    bool Ready() const {
        return m_ButterflyProgram != 0 &&
            m_PackProgram != 0 &&
            m_BitReverseProgram != 0 &&
            m_UnpackProgram != 0;
    }

private:
    void EnsurePrograms();
    void EnsureScratch(int paddedW, int paddedH);

    // Compute shader sources -------------------------------------------------
    // Pack: read a source RGBA16F texel, convert to complex (real = luma or
    // red, imag = 0), write into the RG32F padded buffer at the matching
    // coordinate (zero elsewhere).
    static const char* PackComputeSource();
    // Butterfly: one Stockham pass along one axis. Stockham auto-sort
    // produces natural-order output without an explicit bit-reversal pass,
    // which keeps the kernel simple. directionSign = -1 for forward, +1 for
    // inverse.
    static const char* ButterflyComputeSource();
    // Bit-reverse copy for inverse input: the forward pack shader can do this
    // while it lifts spatial input into complex form, but inverse starts from
    // an already-complex spectrum texture.
    static const char* BitReverseComputeSource();
    // Unpack: read the complex result, write the real part into an RG32F
    // spatial texture (the caller then samples this and rebuilds RGBA16F).
    // For inverse, applies the 1/(W*H) normalization.
    static const char* UnpackComputeSource();

    unsigned int m_ButterflyProgram = 0;
    unsigned int m_PackProgram = 0;
    unsigned int m_BitReverseProgram = 0;
    unsigned int m_UnpackProgram = 0;

    // Ping-pong complex buffers (RG32F). Forward/Inverse flip between them
    // each butterfly pass.
    unsigned int m_ComplexA = 0;
    unsigned int m_ComplexB = 0;
    int m_ScratchW = 0;
    int m_ScratchH = 0;
};

} // namespace Stack::Renderer::Frequency
