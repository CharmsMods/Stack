#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"
#include "App/Validation/Suites/RawViewportValidationFixture.h"
#include "Renderer/RenderPipeline.h"
#include <iostream>

namespace Stack::Validation {
bool ValidateRawViewportDenoiseReuse() {
    constexpr int width = 2048, height = 1536;
    auto raw = MakeViewportValidationRaw(width,height);
    auto recipe = RawRecipe::MakeDefaultRecipe(raw->metadata.sourcePath);
    recipe.rgbDenoise.enabled = true;
    recipe.rgbDenoise.lumaMap.baseMultiplier = 0.25f;
    recipe.finishTone.layerJson["localBaselineEnabled"] = false;
    recipe.finishTone.layerJson["foundationAdaptiveAssist"] = false;
    RenderGraphSnapshot graph;
    RenderGraphNode source;
    source.nodeId = 1;
    source.kind = RenderGraphNodeKind::RawDevelopment;
    source.rawDevelopment.embeddedRawData = raw;
    graph.nodes.push_back(source);
    RenderGraphNode output;
    output.nodeId = 2;
    output.kind = RenderGraphNodeKind::Output;
    graph.nodes.push_back(output);
    graph.links.push_back({1,"imageOut",2,"imageIn"});
    graph.outputNodeId = 2;
    graph.outputSocketId = "imageOut";
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.SetRawDevelopmentViewportValidationEnabled(false);
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    pipeline.SetGraphCacheBudget(128ull*1024*1024, 48ull*1024*1024);
    const auto render = [&](int edge, const Raw::ViewportRegion& region, int generation) {
        graph.nodes[0].rawDevelopment.recipe = recipe;
        pipeline.SetPreviewMaxDimension(edge);
        pipeline.SetRawViewportRequest({region,1.0,static_cast<std::uint64_t>(generation)});
        pipeline.Resize(width,height);
        pipeline.ExecuteGraph(graph);
        glFinish();
        return pipeline.GetOutputTexture() && !pipeline.GetLastGraphExecutionStats().allocationFailed;
    };
    // Opening may produce a smaller denoised preview. Native preparation
    // replaces that dependency exactly once before any downstream edits.
    if (!render(512,{},1) || !render(0,{},2) || pipeline.GetLastGraphExecutionStats().rawRgbDenoisePasses != 1) return false;
    const auto native = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,0).neutralPlacement;
    int generation = 3;
    for (int edge : {512,1024,0,768,0}) for (int view = 0; view < 3; ++view) {
        recipe.preToneExposureEv += 0.02f;
        if (view == 1) {
            recipe.colorWarp.enabled = true;
            RawRecipe::RawColorWarpPin pin;
            pin.id = "reuse"; pin.targetA = 0.02f + generation*0.0001f;
            recipe.colorWarp.pins = {pin};
        }
        if (view == 2) {
            recipe.viewTransform.layerJson["exposure"] = generation*0.01f;
            recipe.finishTone.layerJson["points"] = nlohmann::json::array({
                {{"x",0.0f},{"y",0.0f}}, {{"x",0.5f},{"y",0.5f+generation*0.001f}}, {{"x",1.0f},{"y",1.0f}}});
            recipe.finishTone.layerJson["preparedPoints"] = recipe.finishTone.layerJson["points"];
        }
        const Raw::ViewportRegion region = view == 0 ? Raw::ViewportRegion{} : view == 1
            ? Raw::ViewportRegion{width,height,700,500,400,300}
            : Raw::ViewportRegion{width,height,7,9,width-17,height-21};
        if (!render(edge,region,generation++)) return false;
        const auto& stats = pipeline.GetLastGraphExecutionStats();
        if (stats.rawRgbDenoisePasses != 0 || stats.rawGpuPreprocessDispatches != 0 ||
            stats.rawNativeDenoiseReuses != 1 || pipeline.RawViewportCachedStages(recipe,0)[1] != native) {
            std::cerr << "Native denoise reuse failed, edge " << edge << ", view " << view << ", passes "
                << stats.rawRgbDenoisePasses << ", reuse " << stats.rawNativeDenoiseReuses << "\n";
            return false;
        }
        if (region.Partial()) {
            const auto& actual = pipeline.GetRawViewportRegion();
            const auto expected = Raw::ScaleViewportRegion(region, edge ? edge : width, edge ? edge*height/width : height);
            if (actual != expected || pipeline.GetCanvasWidth() != actual.width || pipeline.GetCanvasHeight() != actual.height) return false;
        }
    }
    recipe.rgbDenoise.lumaMap.baseMultiplier = 0.5f;
    if (!render(0,{},generation++) || pipeline.GetLastGraphExecutionStats().rawRgbDenoisePasses != 1) {
        std::cerr << "A denoise change did not replace its native dependency.\n";
        return false;
    }
    recipe.preToneExposureEv += 0.1f;
    if (!render(0,{width,height,100,200,700,500},generation++) || pipeline.GetLastGraphExecutionStats().rawRgbDenoisePasses != 0) return false;
    pipeline.SetRawDevelopmentPreferredCacheInputStage(Raw::ViewportStage::NeutralPlacement);
    pipeline.SetGraphCacheBudget(0);
    if (pipeline.RawViewportCachedStages(recipe,0)[1] != 0) {
        std::cerr << "Native dependency ignored a reduced memory budget.\n";
        return false;
    }
    std::cout << "Native denoise reuse passed: 15 zoom/size/edit transitions, one replacement after a denoise edit.\n";
    RenderPipeline asyncValidation;
    asyncValidation.Initialize();
    return asyncValidation.ValidateRawNativeDenoiseHandoffForTesting() && ValidateRawViewportDetailDrawing();
}
}
