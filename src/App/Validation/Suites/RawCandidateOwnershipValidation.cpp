#include "App/Validation/Suites/RawCandidateOwnershipValidation.h"
#include "App/Validation/Suites/RawViewportValidationFixture.h"
#include "Renderer/RawCandidateEvaluation.h"
#include "Renderer/RawCandidateRenderer.h"

#include <iostream>
#include <stdexcept>

namespace Stack::Validation {

void ValidateRawCandidateOwnership() {
    const auto raw = MakeViewportValidationRaw(64, 48);
    auto a = RawRecipe::MakeDefaultRecipe(raw->metadata.sourcePath);
    a.rgbDenoise.enabled = false;
    a.finishTone.layerJson["localBaselineEnabled"] = false;
    a.finishTone.layerJson["foundationAdaptiveAssist"] = false;
    auto b = a;
    b.preToneExposureEv = 1.0f;
    auto graph = Renderer::RawCandidateEvaluation::BuildGraph(a);
    graph.nodes.front().rawDevelopment.embeddedRawData = raw;
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    const auto render = [&](const auto& recipe) {
        RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest request;
        request.valid = request.hasRecipe = true;
        request.recipe = recipe;
        request.stage = RawAutoStartPoint::RawAutoStartPointStage::RawPlacement;
        request.featureReadbackMaxDimension = 16;
        const auto result = Renderer::RenderRawCandidates(
            pipeline, graph, raw->metadata.sourcePath, {request});
        if (result.size() != 1 || !result.front().success) {
            throw std::runtime_error(result.empty()
                ? "RAW candidate returned no result" : result.front().error);
        }
        int width = 0, height = 0;
        auto pixels = pipeline.GetOutputPixels(width, height);
        if (width != 64 || height != 48 || pixels.empty())
            throw std::runtime_error("RAW candidate changed its source dimensions");
        return pixels;
    };
    const auto firstA = render(a);
    const auto imageB = render(b);
    const auto resumedA = render(a);
    if (firstA == imageB || firstA != resumedA ||
        graph.nodes.front().rawDevelopment.embeddedRawData != raw ||
        graph.nodes.front().rawDevelopment.recipe.preToneExposureEv != a.preToneExposureEv) {
        throw std::runtime_error("Independent RAW recipes or pixels crossed candidate ownership");
    }
    std::cout << "RAW candidate pixel isolation passed without constructing an editor.\n";
}

} // namespace Stack::Validation
