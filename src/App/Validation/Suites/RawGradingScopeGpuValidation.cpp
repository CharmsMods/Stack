#include "App/Validation/ValidationSuites.h"
#include "Renderer/Internal/RawGradingScopeGpu.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif

namespace Stack::Validation {
namespace {

bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "RAW GPU scope validation failed: " << message << '\n';
    return condition;
}

bool ComparePoints(const std::vector<RawGradingScopePoint>& reference,
    const std::vector<RawGradingScopePoint>& gpu) {
    if (!Check(reference.size() == gpu.size(), "occupied bin count differs")) return false;
    float worstColor = 0;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        if (!Check(reference[i].x == gpu[i].x && reference[i].y == gpu[i].y, "plot bin position differs")) return false;
        for (int c = 0; c < 4; ++c) worstColor = std::max(worstColor, std::abs(reference[i].color[c] - gpu[i].color[c]));
    }
    return Check(worstColor < 0.006f, "trace color or normalized density differs");
}

} // namespace

// The caller owns the hidden OpenGL context.
bool ValidateRawGradingScopeGpu() {
    bool ok = true;
    RawGradingScopeGpu reducer;
    unsigned int outputBuffers[2] {};
    const std::array<std::array<float, 3>, 10> colors {{
        {0,0,0}, {1,1,1}, {0.18f,0.18f,0.18f}, {0.7f,0.12f,0.07f},
        {0.11f,0.69f,0.23f}, {0.07f,0.21f,0.83f}, {0.3f,0.6f,0.8f},
        {-0.3f,0.04f,2.3f}, {std::numeric_limits<float>::quiet_NaN(),0,0},
        {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),0}
    }};
    RawDevelopmentGradingScopeReadback input;
    input.valid = true;
    input.source = RawDevelopmentGradingScopeSource::NeutralScene;
    input.sourceKey = "GPU scope fixture";
    input.generation = 9;
    input.width = 137;
    input.height = 81;
    input.pixels.resize(static_cast<std::size_t>(input.width) * input.height * 3);
    std::vector<float> rgba(static_cast<std::size_t>(input.width) * input.height * 4);
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            const auto& color = colors[(x / 7 + y / 5) % colors.size()];
            for (int c = 0; c < 3; ++c) {
                input.pixels[(static_cast<std::size_t>(y) * input.width + x) * 3 + c] = color[c];
                rgba[(static_cast<std::size_t>(input.height - 1 - y) * input.width + x) * 4 + c] = color[c];
            }
        }
    }
    const auto texture = GLHelpers::CreateStorageTexture(input.width, input.height, GL_RGBA32F);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, input.width, input.height, GL_RGBA, GL_FLOAT, rgba.data());
    unsigned int sentinelBuffer = 0;
    glGenBuffers(1, &sentinelBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sentinelBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 32, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sentinelBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sentinelBuffer);
    glActiveTexture(GL_TEXTURE1);
    for (const auto workingSpace : {Raw::RawWorkingSpace::LinearSrgbD65, Raw::RawWorkingSpace::LinearRec2020D65}) {
        input.workingSpace = workingSpace;
        for (const int mode : {0, 1, 2}) {
            input.sceneLinear = mode != 0;
            input.encodedSrgb = mode == 2;
            const auto reference = Raw::BuildGradingScopeVisualization(input);
            ok &= Check(reducer.Dispatch(texture, input.width, input.height,
                workingSpace, input.sceneLinear, input.encodedSrgb, outputBuffers[0]), "compute dispatch failed");
            GLint activeTexture = 0, genericBuffer = 0, indexedBuffer = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
            glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &genericBuffer);
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0, &indexedBuffer);
            ok &= Check(activeTexture == GL_TEXTURE1 && genericBuffer == static_cast<GLint>(sentinelBuffer) &&
                indexedBuffer == static_cast<GLint>(sentinelBuffer), "compute state leaked");
            // Scratch may be reused while earlier captures retain their own output.
            ok &= Check(reducer.Dispatch(texture, input.width, input.height,
                workingSpace, false, false, outputBuffers[1]), "second in-flight output failed");
            glFinish();
            auto actual = input;
            actual.pixels.clear();
            ok &= Check(RawGradingScopeGpu::Read(outputBuffers[0], actual), "plot readback failed");
            if (actual.visualization && reference) {
                for (std::size_t i = 0; i < reference->histogram.size(); ++i) {
                    ok &= Check(std::abs(reference->histogram[i] - actual.visualization->histogram[i]) < 0.0001f,
                        "histogram differs from CPU reference");
                }
                ok &= ComparePoints(reference->vectorscopePoints, actual.visualization->vectorscopePoints);
                ok &= ComparePoints(reference->paradePoints, actual.visualization->paradePoints);
                ok &= Check(actual.visualization->sourceKey == input.sourceKey && actual.visualization->generation == 9 &&
                    actual.pixels.empty(), "plot-only identity or pixel ownership is wrong");
            }
        }
    }
    // Low-amplitude colors expose loss of chroma from coarse integer sums.
    for (std::size_t i = 0; i < input.pixels.size() / 3; ++i) {
        for (int c = 0; c < 3; ++c) input.pixels[i * 3 + c] = rgba[i * 4 + c] = 0.00101f - c * 0.0001f;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, input.width, input.height, GL_RGBA, GL_FLOAT, rgba.data());
    input.sceneLinear = false;
    const auto darkReference = Raw::BuildGradingScopeVisualization(input);
    ok &= Check(reducer.Dispatch(texture, input.width, input.height, input.workingSpace, false, false,
        outputBuffers[0]), "dark color dispatch failed");
    glFinish();
    ok &= Check(RawGradingScopeGpu::Read(outputBuffers[0], input), "dark color readback failed");
    if (input.visualization) ok &= ComparePoints(darkReference->vectorscopePoints, input.visualization->vectorscopePoints);
    ok &= Check(!reducer.Dispatch(texture, 2048, 2048, input.workingSpace, true, false, outputBuffers[1]),
        "oversized fixed-point accumulation was accepted");
    reducer.Shutdown();
    reducer.Shutdown();
    glDeleteBuffers(2, outputBuffers);
    glDeleteBuffers(1, &sentinelBuffer);
    glDeleteTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    ok &= Check(glGetError() == GL_NO_ERROR, "OpenGL error after reducer cleanup");
    return ok;
}

} // namespace Stack::Validation
