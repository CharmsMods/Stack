#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"
#include "Renderer/RenderPipeline.h"
#include <iostream>
#include <chrono>
#include "App/Validation/Suites/RawViewportValidationFixture.h"

namespace Stack::Validation {
bool ValidateRawViewportRegions() {
    auto raw = MakeViewportValidationRaw();
    const auto& metadata = raw->metadata;
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.SetRawDevelopmentViewportValidationEnabled(false);
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    pipeline.SetGraphCacheBudget(128ull * 1024 * 1024);
    RenderGraphSnapshot graph;
    RenderGraphNode development;
    development.nodeId = 1;
    development.kind = RenderGraphNodeKind::RawDevelopment;
    development.rawDevelopment.embeddedRawData = raw;
    graph.nodes.push_back(development);
    RenderGraphNode output;
    output.nodeId = 2;
    output.kind = RenderGraphNodeKind::Output;
    graph.nodes.push_back(output);
    graph.links.push_back({1, "imageOut", 2, "imageIn"});
    graph.outputNodeId = 2;
    graph.outputSocketId = "imageOut";
    auto base = RawRecipe::MakeDefaultRecipe(metadata.sourcePath, "Viewport test");
    base.technical.processingVersion = Raw::RawProcessingVersion::TruthfulV2;
    base.rgbDenoise.enabled = false;
    base.finishTone.layerJson["localBaselineEnabled"] = false;
    base.finishTone.layerJson["foundationAdaptiveAssist"] = false;
    std::uint64_t generation = 1;
    int comparisons = 0;
    double firstUpdateMs = 0, completedMs = 0;
    int completedFrames = 0, resolutionChanges = 0, previousEdge = 0;
    for (int variant = 0; variant < 12; ++variant) {
        auto recipe = base;
        if (variant == 1) recipe.preToneExposureEv = 0.75f;
        if (variant == 2) {
            recipe.localRange.enabled = true;
            recipe.localRange.points = {{-8, 0}, {0, 0.3f}, {6, 0}};
        }
        if (variant == 3) {
            recipe.rgbDenoise.enabled = true;
            recipe.rgbDenoise.lumaMap.baseMultiplier = 0.25f;
        }
        if (variant == 4) {
            recipe.cropRotation.rotationDegrees = 90;
            recipe.cropRotation.flipHorizontally = true;
            recipe.cropRotation.cropEnabled = true;
            recipe.cropRotation.cropX = 0.125f;
            recipe.cropRotation.cropY = 0.125f;
            recipe.cropRotation.cropWidth = recipe.cropRotation.cropHeight = 0.75f;
        }
        if (variant == 5) {
            recipe.technical.mosaicDenoise.enabled = true;
            recipe.technical.mosaicDenoise.lumaStrength = 0.25f;
            recipe.colorWarp.enabled = true;
            RawRecipe::RawColorWarpPin pin;
            pin.id = "test"; pin.targetA = 0.04f;
            recipe.colorWarp.pins = {pin};
        }
        if (variant == 6) {
            recipe.cropRotation.rotationDegrees = 270;
            recipe.cropRotation.flipVertically = true;
            recipe.cropRotation.cropEnabled = true;
            recipe.cropRotation.cropX = 0.137f; recipe.cropRotation.cropY = 0.093f;
            recipe.cropRotation.cropWidth = 0.731f; recipe.cropRotation.cropHeight = 0.817f;
        }
        if (variant == 7 || variant == 8) {
            recipe.localRange.enabled = true;
            recipe.localRange.points = {{-8,0},{0,0.3f},{6,0}};
            if (variant == 7) {
                recipe.localRange.regionMaskEnabled = true;
                recipe.localRange.regionMaskMode = "radial";
                recipe.localRange.regionMaskCenterX = 0.3f;
                recipe.localRange.regionMaskFeather = 0.4f;
            } else {
                RawRecipe::RawLocalRangeTargetZone zone;
                zone.id = "connected-test"; zone.centerEv = 1; zone.deltaEv = 0.4f;
                zone.seeds = {{0.5f,0.5f}};
                recipe.localRange.targetZones = {zone};
            }
        }
        if (variant == 9) {
            recipe.finishTone.layerJson["localBaselineEnabled"] = true;
            recipe.finishTone.layerJson["localBaselineRadius"] = 8.0f;
        }
        if (variant == 10) {
            recipe.technical.demosaicMethod = Raw::DemosaicMethod::Bilinear;
            recipe.technical.mosaicDenoise.enabled = true;
            recipe.technical.mosaicDenoise.lumaStrength = 0.4f;
            recipe.technical.mosaicDenoise.radius = 4;
            recipe.technical.mosaicDenoise.iterations = 3;
        }
        if (variant == 11) {
            recipe.cropRotation.rotationDegrees = 180;
            recipe.cropRotation.flipVertically = recipe.cropRotation.flipHorizontally = true;
            RawRecipe::RawColorWarpRegion region;
            region.id = "spatial-test"; region.featherPixels = 4;
            region.circles = {{"circle",0.5f,0.5f,0.1f,0.1f}};
            RawRecipe::RawColorWarpPin pin;
            pin.id = "pin"; pin.targetA = 0.03f; pin.regionId = region.id;
            recipe.colorWarp.pins = {pin}; recipe.colorWarp.regions = {region};
        }
        graph.nodes[0].rawDevelopment.recipe = recipe;
        for (int edge : {256, 512}) {
            pipeline.SetPreviewMaxDimension(edge);
            pipeline.SetRawViewportRequest({});
            pipeline.Resize(512, 384);
            if (previousEdge && previousEdge != edge) ++resolutionChanges;
            previousEdge = edge;
            pipeline.BeginRawViewportTiming(true);
            const auto started = std::chrono::steady_clock::now();
            pipeline.ExecuteGraph(graph);
            pipeline.m_RawViewportGpuTiming.Finish();
            pipeline.CollectRawViewportTiming({});
            const double completed = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            if (!firstUpdateMs) firstUpdateMs = completed;
            else { completedMs += completed; ++completedFrames; }
            const int width = pipeline.GetCanvasWidth(), height = pipeline.GetCanvasHeight();
            if (!pipeline.GetOutputTexture() || width <= 0 || height <= 0) {
                std::cerr << "RAW region reference unavailable, variant " << variant
                    << ", edge " << edge << ", extent " << width << "x" << height << "\n";
                return false;
            }
            std::vector<float> full(static_cast<std::size_t>(width) * height * 4);
            glBindTexture(GL_TEXTURE_2D, pipeline.GetOutputTexture());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, full.data());
            float lo = full[0], hi = full[0];
            for (std::size_t i = 0; i < full.size(); i += 4) { lo = std::min(lo,full[i]); hi = std::max(hi,full[i]); }
            if (!std::isfinite(lo) || !std::isfinite(hi) || hi-lo < 0.01f) {
                std::cerr << "RAW region reference lost image detail.\n";
                return false;
            }
            for (int corner : {0, 1, 2}) {
                Raw::ViewportRegion visible {width, height, corner == 2 ? width - width / 2 : corner ? width / 3 : 0,
                    corner == 2 ? height - height / 2 : corner ? height / 3 : 0, width / 2, height / 2};
                pipeline.SetRawViewportRequest({visible, 1.0, generation++});
                pipeline.Resize(512, 384);
                pipeline.ExecuteGraph(graph);
                const auto region = pipeline.GetRawViewportRegion();
                if (!region.Valid() || pipeline.GetCanvasWidth() != region.width ||
                    pipeline.GetCanvasHeight() != region.height) {
                    std::cerr << "RAW region extent failed, variant " << variant << ", edge " << edge << "\n";
                    return false;
                }
                std::vector<float> cropped(static_cast<std::size_t>(region.width) * region.height * 4);
                glBindTexture(GL_TEXTURE_2D, pipeline.GetOutputTexture());
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, cropped.data());
                double difference = 0;
                for (int y = 0; y < region.height; ++y) for (int x = 0; x < region.width; ++x)
                    for (int c = 0; c < 3; ++c) {
                        const float a = full[((height - region.y - region.height + y) * width + region.x + x) * 4 + c];
                        const float b = cropped[(y * region.width + x) * 4 + c];
                        if (!std::isfinite(a) || !std::isfinite(b)) {
                            std::cerr << "RAW region nonfinite pixels, variant " << variant
                                << ", edge " << edge << ", corner " << corner << "\n";
                            return false;
                        }
                        difference = std::max(difference, std::abs(double(a) - b));
                    }
                if (difference > 0.004) {
                    std::cerr << "RAW region pixel parity failed, variant " << variant << ", edge " << edge
                        << ", maximum error " << difference << "\n";
                    return false;
                }
                ++comparisons;
                // An auxiliary command can configure a full-image request and
                // be canceled before execution. Returning to the same view
                // must restore coverage even when its graph output is cached.
                const auto request = pipeline.m_RawViewportRequest;
                pipeline.SetRawViewportRequest({});
                pipeline.SetRawViewportRequest(request);
                pipeline.ExecuteGraph(graph);
                if (pipeline.GetRawViewportRegion() != region ||
                    pipeline.GetCanvasWidth() != region.width ||
                    pipeline.GetCanvasHeight() != region.height) {
                    std::cerr << "RAW cached viewport lost coverage after interrupted auxiliary work.\n";
                    return false;
                }
            }
        }
    }
    // Exercise the completed-input copy without requiring an external model.
    // The private overview must neither repeat denoise nor evict its owner.
    auto dependencyRecipe = base;
    dependencyRecipe.rgbDenoise.enabled = true;
    dependencyRecipe.rgbDenoise.lumaMap.baseMultiplier = 0.25f;
    graph.nodes[0].rawDevelopment.recipe = dependencyRecipe;
    pipeline.SetRawViewportRequest({});
    pipeline.SetPreviewMaxDimension(512);
    pipeline.Resize(512,384);
    pipeline.ExecuteGraph(graph);
    const auto originalCache = pipeline.RawViewportCachedStages(dependencyRecipe,512);
    RenderPipeline overview;
    overview.Initialize();
    overview.SetGraphCacheBudget(64ull*1024*1024);
    overview.SetRawDevelopmentAnalysisEnabled(false);
    overview.SetRawDevelopmentViewportValidationEnabled(false);
    overview.SetPreviewMaxDimension(256);
    if (!overview.SeedViewportDependency(pipeline,dependencyRecipe,256,512)) {
        std::cerr << "RAW overview dependency copy failed.\n";
        return false;
    }
    overview.Resize(512,384);
    overview.ExecuteGraph(graph);
    if (!overview.GetOutputTexture() || overview.GetCanvasWidth() != 256 ||
        overview.GetLastGraphExecutionStats().rawStageCacheHits == 0 ||
        pipeline.RawViewportCachedStages(dependencyRecipe,512) != originalCache) {
        std::cerr << "RAW overview dependency reuse failed: output " << overview.GetOutputTexture()
            << ", width " << overview.GetCanvasWidth()
            << ", cache hits " << overview.GetLastGraphExecutionStats().rawStageCacheHits
            << ", source cache unchanged "
            << (pipeline.RawViewportCachedStages(dependencyRecipe,512) == originalCache) << "\n";
        return false;
    }
    std::cout << "RAW region GPU parity passed: " << comparisons << " crop comparisons.\n";
    std::cout << "RAW validation timing: first update " << firstUpdateMs << " ms, subsequent completed-render throughput "
        << (completedMs > 0 ? 1000.0 * completedFrames / completedMs : 0) << " FPS, " << resolutionChanges
        << " requested resolution changes (synthetic 512x384 RAW).\n";
    // A wide native image exposes UV interpolation drift that a 512-pixel
    // fixture hides. Bright points beside dark pixels must not bleed into
    // different neighbors when the same RAW is rendered as a native crop.
    auto wide=MakeViewportValidationRaw(4608,96);
    wide->metadata.sourcePath="wide-native-pointwise-validation";
    for(int y=0;y<96;++y) for(int x=0;x<4608;++x)
        wide->rawBuffer[y*4608+x]=((x*17+y*31)%43==0)?15000:530;
    graph.nodes[0].rawDevelopment.embeddedRawData=wide;
    graph.nodes[0].rawDevelopment.recipe=base;
    graph.nodes[0].rawDevelopment.recipe.source.sourcePath=wide->metadata.sourcePath;
    graph.nodes[0].rawDevelopment.recipe.source.fingerprint="wide-native-pointwise-validation";
    pipeline.SetPreviewMaxDimension(0);pipeline.SetRawViewportRequest({});
    pipeline.ExecuteGraph(graph);
    if(!pipeline.GetOutputTexture()||pipeline.GetCanvasWidth()!=4608||pipeline.GetCanvasHeight()!=96) return false;
    std::vector<float> widePixels(4608*96*4);
    glBindTexture(GL_TEXTURE_2D,pipeline.GetOutputTexture());
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,widePixels.data());
    pipeline.SetRawViewportRequest({{4608,96,1536,16,384,64},1.0,generation++});
    pipeline.ExecuteGraph(graph);
    const auto wideRegion=pipeline.GetRawViewportRegion();
    if(!wideRegion.Valid()||wideRegion.width!=384||wideRegion.height!=64||
        pipeline.GetCanvasWidth()!=384||pipeline.GetCanvasHeight()!=64||!pipeline.GetOutputTexture()) return false;
    std::vector<float> wideCrop(384*64*4);
    glBindTexture(GL_TEXTURE_2D,pipeline.GetOutputTexture());
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,wideCrop.data());
    double wideError=0;
    for(int y=0;y<64;++y) for(int x=0;x<384;++x) for(int c=0;c<3;++c) {
        const float a=widePixels[((96-16-64+y)*4608+1536+x)*4+c];
        const float b=wideCrop[(y*384+x)*4+c];
        if(!std::isfinite(a)||!std::isfinite(b)) return false;
        wideError=std::max(wideError,std::abs(double(a)-b));
    }
    std::cout<<"RAW native bright-point crop error: "<<wideError<<"\n";
    return wideError<=.004&&ValidateRawViewportDenoiseReuse();
}
}
