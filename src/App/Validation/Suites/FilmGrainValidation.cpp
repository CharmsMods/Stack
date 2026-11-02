#include "BracketingValidationGl.h"
#include "Editor/Layers/NoiseLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <array>
#include <cmath>
#include <iostream>

namespace Stack::Validation {
bool ValidateFilmGrain() {
    BracketingValidationGl context;
    glfwMakeContextCurrent(context.window);
    constexpr int side = 32;
    std::array<float, side * side * 4> pixels;
    pixels.fill(.5f);
    const auto input = GLHelpers::CreateTextureFromData(pixels.data(), side, side, GL_RGBA32F, GL_RGBA, GL_FLOAT);
    const auto output = GLHelpers::CreateTextureFromData(nullptr, side, side, GL_RGBA32F, GL_RGBA, GL_FLOAT);
    const auto fbo = GLHelpers::CreateFBO(output);
    if (!input || !output || !fbo) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, side, side);
    FullscreenQuad quad;
    quad.Initialize();
    NoiseLayer layer;
    auto settings = layer.Serialize();
    settings["noiseType"] = 13;
    Renderer::FilmGrain::Input region;
    region.nativeWidth = 4000;
    region.nativeHeight = 3000;
    region.sampleSpan = {side, side};
    layer.SetFilmGrainInput(region);
    const auto render = [&](float mix) {
        settings["grainMix"] = mix;
        layer.Deserialize(settings);
        layer.Execute(input, side, side, quad);
        glReadPixels(0, 0, side, side, GL_RGBA, GL_FLOAT, pixels.data());
        double squared = 0.;
        for (std::size_t p = 0; p < pixels.size(); p += 4)
            squared += std::pow(double(pixels[p]) - .5, 2);
        return std::sqrt(squared / (side * side));
    };
    const double bypass = render(0.f);
    const double particles = render(1.f);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &input);
    glDeleteTextures(1, &output);
    std::cout << "Film Grain RMS pixel change, mix zero " << bypass << ", mix one " << particles << '\n';
    return bypass < .00001 && std::isfinite(particles) && particles > .001;
}
}
