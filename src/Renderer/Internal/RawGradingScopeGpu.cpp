#include "Renderer/Internal/RawGradingScopeGpu.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <string>

#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif

namespace {

using Plot = RawGradingScopeVisualization;
static_assert(Plot::kHistogramBins == 256, "The shared histogram uses one bin per workgroup lane.");
constexpr int kVectorBins = Plot::kVectorscopeResolution * Plot::kVectorscopeResolution;
constexpr int kParadeBins = 3 * Plot::kParadeColumns * Plot::kParadeRows;
constexpr std::size_t kCountBytes = (Plot::kHistogramBins + 8 * kVectorBins + kParadeBins + 4) * sizeof(std::uint32_t);

struct ComputeState {
    GLint program = 0, activeTexture = 0, texture = 0, buffer = 0, indexed[2] {};
    ComputeState() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &buffer);
        for (GLuint i = 0; i < 2; ++i) glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &indexed[i]);
    }
    ~ComputeState() {
        glUseProgram(program);
        glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(activeTexture);
        for (GLuint i = 0; i < 2; ++i) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, indexed[i]);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    }
};

std::string ComputeShader() {
    return std::string("#version 430 core\n") +
        "#define H " + std::to_string(Plot::kHistogramBins) + "\n" +
        "#define V " + std::to_string(Plot::kVectorscopeResolution) + "\n" +
        "#define PC " + std::to_string(Plot::kParadeColumns) + "\n" +
        "#define PR " + std::to_string(Plot::kParadeRows) + "\n" + R"GLSL(
layout(local_size_x = 256) in;
layout(std430, binding = 0) buffer Counts {
    uint histogram[H];
    uvec4 vectors[V * V];
    uvec4 vectorHigh[V * V];
    uint parade[3 * PC * PR];
    uvec4 peak;
};
layout(std430, binding = 1) writeonly buffer PlotData { uint plotValues[]; };
uniform sampler2D source;
uniform ivec2 sourceSize;
uniform int phase;
uniform bool sceneLinear;
uniform bool encodedSrgb;
uniform bool rec2020;
shared uvec3 localPeak[256];
shared uint localHistogram[H];

float finiteOrZero(float value) { return isnan(value) || isinf(value) ? 0.0 : value; }
float encode(float value) {
    value = finiteOrZero(value);
    float magnitude = abs(value);
    float encoded = magnitude <= 0.0031308 ? magnitude * 12.92
        : 1.055 * pow(magnitude, 1.0 / 2.4) - 0.055;
    return value < 0.0 ? -encoded : encoded;
}
vec3 analysisRgb(vec3 inputRgb) {
    precise vec3 rgb = vec3(finiteOrZero(inputRgb.r), finiteOrZero(inputRgb.g), finiteOrZero(inputRgb.b));
    if (sceneLinear && rec2020) {
        vec3 s = rgb;
        rgb.r = 1.6604910 * s.r - 0.5876411 * s.g - 0.0728499 * s.b;
        rgb.g = -0.1245505 * s.r + 1.1328999 * s.g - 0.0083494 * s.b;
        rgb.b = -0.0181508 * s.r - 0.1005789 * s.g + 1.1187297 * s.b;
    }
    if (sceneLinear && !encodedSrgb) rgb = vec3(encode(rgb.r), encode(rgb.g), encode(rgb.b));
    return clamp(rgb, 0.0, 1.0);
}
float density(uint count, uint maximum) {
    return maximum == 0u ? 0.0 : clamp(log(1.0 + float(count)) / log(1.0 + float(maximum)), 0.0, 1.0);
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint lane = gl_LocalInvocationID.x;
    if (phase == 0) {
        if (i < H) histogram[i] = 0u;
        if (i < V * V) { vectors[i] = uvec4(0); vectorHigh[i] = uvec4(0); }
        if (i < 3 * PC * PR) parade[i] = 0u;
        if (i == 0u) peak = uvec4(0);
    } else if (phase == 1) {
        localHistogram[lane] = 0u;
        barrier();
        if (i < uint(sourceSize.x * sourceSize.y)) {
            ivec2 pixel = ivec2(i % uint(sourceSize.x), i / uint(sourceSize.x));
            vec4 sampleValue = texelFetch(source, pixel, 0);
            if (sampleValue.a > 0.0) {
            precise vec3 rgb = analysisRgb(sampleValue.rgb);
            precise float luma = 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
            atomicAdd(localHistogram[uint(clamp(floor(luma * 255.0 + 0.5), 0.0, 255.0))], 1u);
            vec2 chroma = clamp(vec2(0.5 + (rgb.b - luma) / 1.8556 * 0.94,
                0.5 - (rgb.r - luma) / 1.5748 * 0.94), 0.0, 1.0);
            ivec2 bin = min(ivec2(chroma * V), ivec2(V - 1));
            uint v = uint(bin.y * V + bin.x);
            // Split sums preserve dark trace colors without requiring float/64-bit atomics.
            uvec3 color = min(uvec3(floor(rgb * 16777215.0 + 0.5)), uvec3(16777215));
            for (int c = 0; c < 3; ++c) {
                uint previous = atomicAdd(vectors[v][c], color[c]);
                if (previous > 0xffffffffu - color[c]) atomicAdd(vectorHigh[v][c], 1u);
            }
            atomicAdd(vectors[v].w, 1u);
            int column = pixel.x * PC / sourceSize.x;
            for (int channel = 0; channel < 3; ++channel) {
                int row = int(clamp(floor((1.0 - rgb[channel]) * (PR - 1) + 0.5), 0.0, float(PR - 1)));
                atomicAdd(parade[channel * PC * PR + row * PC + column], 1u);
            }
        }
        }
        barrier();
        if (localHistogram[lane] != 0u) atomicAdd(histogram[lane], localHistogram[lane]);
    } else if (phase == 2) {
        uvec3 maximum = uvec3(histogram[lane], 0, 0);
        for (uint j = lane; j < V * V; j += 256u) maximum.y = max(maximum.y, vectors[j].w);
        for (uint j = lane; j < 3 * PC * PR; j += 256u) maximum.z = max(maximum.z, parade[j]);
        localPeak[lane] = maximum;
        barrier();
        for (uint stride = 128u; stride > 0u; stride /= 2u) {
            if (lane < stride) localPeak[lane] = max(localPeak[lane], localPeak[lane + stride]);
            barrier();
        }
        if (lane == 0u) peak = uvec4(localPeak[0], 0);
    } else {
        if (i < H) {
            plotValues[i] = floatBitsToUint(density(histogram[i], peak.x));
        } else if (i < H + V * V) {
            uint v = i - H;
            uvec4 bin = vectors[v];
            vec2 xy = (vec2(v % V, v / V) + 0.5) / V * 2.0 - 1.0;
            vec3 rgb = (vec3(bin.xyz) + vec3(vectorHigh[v].xyz) * 4294967296.0) /
                (16777215.0 * max(1.0, float(bin.w)));
            float maximum = max(rgb.r, max(rgb.g, rgb.b));
            rgb = maximum > 0.001 ? rgb / maximum : vec3(0.92, 0.94, 0.96);
            plotValues[i] = bin.w == 0u || dot(xy, xy) > 1.0 ? 0u :
                packUnorm4x8(vec4(rgb, 0.06 + density(bin.w, peak.y) * 0.76));
        } else if (i < H + V * V + 3 * PC * PR) {
            uint p = i - H - V * V;
            uint count = parade[p];
            uint channel = p / (PC * PR);
            vec3 rgb = channel == 0u ? vec3(1.0, 0.20, 0.16) :
                channel == 1u ? vec3(0.20, 1.0, 0.34) : vec3(0.20, 0.42, 1.0);
            plotValues[i] = count == 0u ? 0u : packUnorm4x8(vec4(rgb, 0.06 + density(count, peak.z) * 0.68));
        }
    }
}
)GLSL";
}

} // namespace

bool RawGradingScopeGpu::Dispatch(unsigned int texture, int width, int height,
    Raw::RawWorkingSpace workingSpace, bool sceneLinear, bool encodedSrgb,
    unsigned int& outputBuffer) {
    if (texture == 0 || width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * height > 1024u * 1024u) return false;
    const ComputeState savedState;
    if (!m_InitializationAttempted) {
        m_InitializationAttempted = true;
        m_Program = GLHelpers::CreateComputeProgram(ComputeShader().c_str());
        if (m_Program != 0) {
            m_PhaseLocation = glGetUniformLocation(m_Program, "phase");
            m_SizeLocation = glGetUniformLocation(m_Program, "sourceSize");
            m_SceneLinearLocation = glGetUniformLocation(m_Program, "sceneLinear");
            m_EncodedLocation = glGetUniformLocation(m_Program, "encodedSrgb");
            m_Rec2020Location = glGetUniformLocation(m_Program, "rec2020");
        }
    }
    if (m_Program == 0) return false;
    while (glGetError() != GL_NO_ERROR) {}
    if (m_Counts == 0) {
        glGenBuffers(1, &m_Counts);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_Counts);
        glBufferData(GL_SHADER_STORAGE_BUFFER, kCountBytes, nullptr, GL_DYNAMIC_DRAW);
    }
    if (outputBuffer == 0) {
        glGenBuffers(1, &outputBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, Raw::kGradingScopePackedValueCount * sizeof(std::uint32_t), nullptr, GL_STREAM_READ);
    }
    if (m_Counts == 0 || outputBuffer == 0 || glGetError() != GL_NO_ERROR) {
        if (outputBuffer != 0) glDeleteBuffers(1, &outputBuffer);
        outputBuffer = 0;
        if (m_Counts != 0) glDeleteBuffers(1, &m_Counts);
        m_Counts = 0;
        return false;
    }
    glUseProgram(m_Program);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform2i(m_SizeLocation, width, height);
    glUniform1i(m_SceneLinearLocation, sceneLinear);
    glUniform1i(m_EncodedLocation, encodedSrgb);
    glUniform1i(m_Rec2020Location, workingSpace == Raw::RawWorkingSpace::LinearRec2020D65);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_Counts);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, outputBuffer);
    const unsigned int groups[] { (std::max({kVectorBins, kParadeBins, Plot::kHistogramBins}) + 255) / 256,
        static_cast<unsigned int>((width * height + 255) / 256), 1,
        static_cast<unsigned int>((Raw::kGradingScopePackedValueCount + 255) / 256) };
    for (int phase = 0; phase < 4; ++phase) {
        glUniform1i(m_PhaseLocation, phase);
        glDispatchCompute(groups[phase], 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    }
    return glGetError() == GL_NO_ERROR;
}

bool RawGradingScopeGpu::Read(unsigned int outputBuffer, RawDevelopmentGradingScopeReadback& readback) {
    if (outputBuffer == 0) return false;
    std::vector<std::uint32_t> packed(Raw::kGradingScopePackedValueCount);
    GLint previousBuffer = 0;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previousBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputBuffer);
    while (glGetError() != GL_NO_ERROR) {}
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, packed.size() * sizeof(std::uint32_t), packed.data());
    const bool ok = glGetError() == GL_NO_ERROR;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, previousBuffer);
    if (ok) readback.visualization = Raw::BuildGradingScopeVisualizationFromPacked(readback, packed);
    return ok && readback.visualization != nullptr;
}

void RawGradingScopeGpu::Shutdown() {
    if (m_Program != 0) glDeleteProgram(m_Program);
    if (m_Counts != 0) glDeleteBuffers(1, &m_Counts);
    m_Program = m_Counts = 0;
    m_InitializationAttempted = false;
}
