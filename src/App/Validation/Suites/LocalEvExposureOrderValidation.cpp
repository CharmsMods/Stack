#include "App/Validation/ValidationImageUtils.h"
#include "Editor/Internal/HdrExportRender.h"
#include "NodeMath/PngMetadataWriter.h"
#include "ThirdParty/stb_image.h"
#include "Utils/PngEncodingUtils.h"
#include "Hdr/Color.h"
#include "Hdr/GraphView.h"
#include "Hdr/ImageFile.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Raw/RawLoader.h"
#include "Renderer/GLLoader.h"
#include "Renderer/RenderPipeline.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
using RawRecipe::GraphOperationKind;
using RawRecipe::ReadGraphOperation;
using RawRecipe::WriteGraphOperation;
using Json = nlohmann::json;
using Region = std::array<float, 4>;

void Require(bool passed, const std::string& message) {
    if (!passed) throw std::runtime_error(message);
}

struct Image {
    int width = 0;
    int height = 0;
    std::vector<float> pixels;
};

Image Render(RenderPipeline& pipeline, const RenderGraphSnapshot& graph) {
    pipeline.ExecuteGraph(graph);
    const auto& stats = pipeline.GetLastGraphExecutionStats();
    Require(pipeline.GetOutputTexture() && !stats.allocationFailed && stats.lastSpecializedFailure.empty(),
        "Local EV render failed: " + stats.lastSpecializedFailure);
    Image image{pipeline.GetCanvasWidth(), pipeline.GetCanvasHeight()};
    image.pixels = ReadTextureRgbaFloat(pipeline.GetOutputTexture(), image.width, image.height);
    Require(!image.pixels.empty(), "Local EV readback was empty.");
    const auto stride = std::size_t(image.width) * 4;
    for (int y = 0; y < image.height / 2; ++y) {
        auto row = image.pixels.begin() + y * stride;
        std::swap_ranges(row, row + stride, image.pixels.begin() + (image.height - y - 1) * stride);
    }
    return image;
}

std::array<float, 3> LumaQuantiles(const Image& image, const Region& region, bool scene) {
    const auto edge = [](float coordinate, int size) {
        return std::clamp(int(std::round(coordinate * size)), 0, size);
    };
    const auto weights = scene ? std::array{.2627002f, .6779981f, .0593017f} : std::array{.2126f, .7152f, .0722f};
    std::vector<float> luma;
    for (int y = edge(region[1], image.height); y < edge(region[3], image.height); ++y) {
        for (int x = edge(region[0], image.width); x < edge(region[2], image.width); ++x) {
            const auto i = (std::size_t(y) * image.width + x) * 4;
            const float value = weights[0]*image.pixels[i] + weights[1]*image.pixels[i+1] + weights[2]*image.pixels[i+2];
            Require(std::isfinite(value), "Nonfinite luminance in comparison region.");
            luma.push_back(value);
        }
    }
    Require(!luma.empty(), "Comparison region is outside the rendered image.");
    std::sort(luma.begin(), luma.end());
    return {luma[luma.size()/10], luma[luma.size()/2], luma[(luma.size()-1)*9/10]};
}

void CheckGain(const Image& original, const Image& adjusted, float gain) {
    Require(original.width == adjusted.width && original.height == adjusted.height &&
        original.pixels.size() == adjusted.pixels.size(), "Exposure changed the image extent.");
    for (std::size_t i = 0; i < original.pixels.size(); ++i) {
        const float expected = original.pixels[i] * (i % 4 == 3 ? 1.f : gain);
        const float error = std::abs(expected - adjusted.pixels[i]);
        if (!(error <= 2e-6f + 1e-4f*std::abs(expected)))
            throw std::runtime_error("Later Exposure did not apply the requested uniform gain.");
    }
}

void SaveRendition(const std::filesystem::path& path, Image image, bool hdr) {
    if (hdr) {
        const auto encoded = Hdr::EncodePqPng({image.width, image.height, std::move(image.pixels)});
        Require(Hdr::IsPqPng(encoded), "Cannot encode HDR test rendition.");
        const auto reopened = Hdr::DecodePqPng(encoded);
        Require(reopened.width == image.width && reopened.height == image.height, "Cannot reopen HDR test rendition.");
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        Require(bool(file), "Cannot save HDR test rendition.");
        return;
    }
    std::vector<unsigned char> encoded(image.pixels.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const float value = i % 4 == 3 ? image.pixels[i] : Hdr::Srgb(image.pixels[i]);
        encoded[i] = static_cast<unsigned char>(std::round(255.f*std::clamp(value, 0.f, 1.f)));
    }
    Require(WriteValidationPng(path, encoded, image.width, image.height), "Cannot save SDR test rendition.");
}

float ProtectedGain(float luminance, float exposureEv) {
    constexpr float knee = 2.f;
    constexpr float width = 5.f;
    if (exposureEv <= 0 || luminance <= .18f*std::exp2(knee-exposureEv)) return std::exp2(exposureEv);
    const float inputEv = std::log2(luminance/.18f);
    const float excess = inputEv+exposureEv-knee;
    const float gap = std::max(0.f,knee+width-inputEv);
    const float shoulderEv = inputEv < knee ? exposureEv-excess*excess/(width+excess)
        : exposureEv*gap*gap/(width*width+exposureEv*gap);
    return std::exp2(.5f*(exposureEv+shoulderEv));
}

Json CheckCase(const std::string& name, const Json& settings, const std::filesystem::path& folder, bool fullResolutionProtection) {
    const auto path = settings.at("input").at("path").get<std::string>();
    const auto mid = settings.at("midtone_rect").get<Region>();
    const auto highlight = settings.at("highlight_rect").get<Region>();
    auto raw = std::make_shared<Raw::RawImageData>();
    Require(Raw::RawLoader::LoadFile(path,*raw), "Cannot load RAW: " + path);
    auto sourceRecipe = RawRecipe::MakeDefaultRecipe(path);
    sourceRecipe.technical.workingSpace = Raw::RawWorkingSpace::LinearRec2020D65;
    sourceRecipe.technical.mosaicDenoise.enabled = false;
    sourceRecipe.rgbDenoise.enabled = false;
    RenderGraphSnapshot source;
    RenderGraphNode input;
    input.nodeId = 1;
    input.kind = RenderGraphNodeKind::RawDevelopment;
    input.rawDevelopment.recipe = sourceRecipe;
    input.rawDevelopment.embeddedRawData = raw;
    source.nodes.push_back(std::move(input));
    source.outputNodeId = 1;
    source.outputSocketId = "imageOut";

    Project::RawLayerStackState layers;
    auto& authored = layers.background.graph;
    const auto lightId = Project::FindRawOperation(layers.background, GraphOperationKind::LocalEv)->id;
    const auto currentId = Project::FindRawRole(layers.background, GraphModel::NodeRole::CurrentImage)->id;
    const auto normalizationId = authored.AddRawOperationNode(GraphOperationKind::Exposure, {})->id;
    std::string error;
    Require(authored.TryConnectSockets(currentId,"imageOut",normalizationId,"imageIn",&error), error);
    Require(authored.TryConnectSockets(normalizationId,"imageOut",lightId,"imageIn",&error), error);

    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    pipeline.SetGraphCacheBudget((fullResolutionProtection ? 768ull : 256ull)*1024*1024);
    pipeline.SetPreviewMaxDimension(fullResolutionProtection ? 0 : 1152);
    pipeline.Resize(64,64);
    auto full = source;
    Require(Project::LowerRawLayerStack(full,layers,1,1,error),error);
    const auto& ids = full.rawLayerMaskNodeIds.at(Project::kRawBackgroundId);
    const int light = ids.at(lightId);
    const int normalization = ids.at(normalizationId);
    const auto operation = [&](int id) -> RawRecipe::GraphOperation& {
        return std::find_if(full.nodes.begin(),full.nodes.end(),[&](const auto& node) {return node.nodeId==id;})->rawOperation;
    };
    const auto sceneGraph = [&] {
        auto graph = full;
        graph.outputNodeId = light;
        graph.outputSocketId = "imageOut";
        graph.outputDescriptor = Project::RawLayerSceneDescriptor(sourceRecipe.technical.workingSpace);
        return graph;
    };
    int width = 0, height = 0;
    const float rawMid = [&] {
        const auto rawScene = Render(pipeline,sceneGraph());
        width = rawScene.width; height = rawScene.height;
        if (fullResolutionProtection) {
            Require(width == Raw::DisplayWidth(raw->metadata) && height == Raw::DisplayHeight(raw->metadata),
                "The native render did not match the RAW dimensions.");
        }
        return LumaQuantiles(rawScene,mid,true)[1];
    }();
    Require(rawMid > 0.f,"RAW midtone patch is too dark to match.");
    auto normalizationRecipe = ReadGraphOperation(operation(normalization));
    normalizationRecipe.preToneExposureEv = std::log2(.12f/rawMid);
    WriteGraphOperation(operation(normalization),normalizationRecipe);

    full.rawLayerScopeNodeId = normalization;
    full.rawLayerScopeSocketId = "imageOut";
    full.rawLayerScopeOperationId = light;
    pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::LocalRangeInput,192);
    Render(pipeline,sceneGraph());
    const auto fit = pipeline.GetRawDevelopmentGraphScopeReadback().lightRange;
    Require(fit.valid,"Cannot fit the Light range on the normalized RAW.");
    pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::None,0);

    Json result;
    for (const bool manual : {false,true}) {
        auto recipe = ReadGraphOperation(operation(light));
        recipe.preToneExposureEv = 0;
        recipe.exposureAfterLight = false;
        recipe.protectExposureHighlights = false;
        recipe.localRange = RawRecipe::DefaultLocalRangeRecipe();
        recipe.localRange.enabled = true;
        recipe.localRange.light.automatic = !manual;
        recipe.localRange.light.lowEv = fit.lowEv;
        recipe.localRange.light.highEv = fit.highEv;
        recipe.localRange.light.amounts = manual ? std::array{-13.f,31.f,14.f,-35.f,-22.f} : std::array{-9.f,19.f,8.f,-23.f,-14.f};
        if (manual) recipe.localRange.points = {{-8,.6f,{1},{1}},{6,-.6f,{1},{1}}};
        const auto apply = [&](float ev) {
            recipe.preToneExposureEv = ev;
            WriteGraphOperation(operation(light),recipe);
        };
        apply(0);
        const auto accepted=ReadGraphOperation(operation(light));
        Require(accepted.exposureAfterLight && accepted.protectExposureHighlights && accepted.localRange.light.automatic,
            "Local EV allowed a retired order, protection, or range switch.");
        const auto baseline=Render(pipeline,sceneGraph());
        apply(-1);
        CheckGain(baseline,Render(pipeline,sceneGraph()),.5f);
        apply(0);
        pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::LocalRangeInput,192);
        Render(pipeline,sceneGraph());
        const auto zeroScope=pipeline.GetRawDevelopmentGraphScopeReadback();
        apply(2);
        const auto raised=Render(pipeline,sceneGraph());
        const auto& raisedScope=pipeline.GetRawDevelopmentGraphScopeReadback();
        Require(zeroScope.valid && raisedScope.valid && !zeroScope.controlSignal.empty() &&
            zeroScope.controlSignal==raisedScope.controlSignal && raisedScope.inputExposureEv==0,
            "Exposure changed the Light input guide.");
        pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::None,0);
        for(std::size_t i=0;i<baseline.pixels.size();i+=4) {
            const float luma=.2627002f*baseline.pixels[i]+.6779981f*baseline.pixels[i+1]+.0593017f*baseline.pixels[i+2];
            const float gain=ProtectedGain(luma,2.f);
            for(int c=0;c<4;++c) {
                const float expected=baseline.pixels[i+c]*(c==3?1.f:gain);
                Require(std::abs(raised.pixels[i+c]-expected)<=3e-5f+2e-4f*std::abs(expected),
                    "Protected Exposure differs from its analytic gain or changed alpha.");
            }
        }
        const std::string profile=manual?"advanced-curve":"light-bands";
        result[profile]={{"midtoneLuminance",LumaQuantiles(raised,mid,true)[1]},
            {"highlightLuminance",LumaQuantiles(raised,highlight,true)[1]}};
        if(fullResolutionProtection)for(bool hdr:{false,true}) {
            auto graph=full;
            Require(Hdr::ConfigureView(graph,hdr?Hdr::MasterHeadroom:1.f,false),"Cannot configure View Transform.");
            SaveRendition(folder/(SanitizeValidationFileStem(name)+"-"+profile+(hdr?"-hdr.png":"-sdr.png")),Render(pipeline,graph),hdr);
        }
        std::cout<<name<<' '<<profile<<": protected gain and stable Light guide verified.\n";
    }

    const auto uuid = authored.FindNode(lightId)->instanceUuid;
    auto edited = Project::ReadRawLayerOperation(layers.background,uuid);
    Project::RawLayerStackDocument document;
    edited.preToneExposureEv = 1.f;
    Require(Project::WriteRawLayerOperation(layers.background,uuid,edited,error),error);
    Require(document.Load(Project::SerializeRawLayerStack(layers),error),error);
    const auto legacy = Project::ReadRawLayerOperation(document.State().background,uuid);
    Require(legacy.exposureAfterLight && legacy.protectExposureHighlights && legacy.localRange.light.automatic && legacy.preToneExposureEv == 1.f,
        "Saved Light did not use its permanent processing rules.");
    edited.exposureAfterLight = true;
    edited.protectExposureHighlights = fullResolutionProtection;
    edited.preToneExposureEv = 2.f;
    Require(Project::WriteRawLayerOperation(layers.background,uuid,edited,error),error);
    Require(document.Apply(layers,error),error);
    Require(document.Undo(&error) && Project::ReadRawLayerOperation(document.State().background,uuid).preToneExposureEv==1.f,
        "Undo did not restore Exposure.");
    Require(document.Redo(&error) && Project::ReadRawLayerOperation(document.State().background,uuid).exposureAfterLight,
        "Redo did not restore Exposure after Light.");
    Project::RawLayerStackState reopened;
    Require(Project::DeserializeRawLayerStack(Project::SerializeRawLayerStack(layers),reopened,error),error);
    const auto saved = Project::ReadRawLayerOperation(reopened.background,uuid);
    Require(saved.exposureAfterLight && saved.preToneExposureEv == 2.f &&
        saved.protectExposureHighlights && saved.localRange.light.automatic,"Exposure settings did not survive save and reopen.");
    result["studySize"] = {width,height};
    return result;
}
}

bool ValidateLocalEvExposureOrder(const std::filesystem::path& manifest, const std::filesystem::path& folder,
    bool fullResolutionProtection) {
    if (!glfwInit()) return false;
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto* window = glfwCreateWindow(64,64,"Local EV exposure order validation",nullptr,nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    bool passed = false;
    try {
        Require(LoadGLFunctions(),"Cannot initialize OpenGL.");
        std::ifstream file(manifest);
        const auto cases = Json::parse(file);
        Require(cases.is_object() && !cases.empty(),"The exposure comparison needs at least one RAW case.");
        std::filesystem::create_directories(folder);
        Json results;
        for (const auto& [name,settings] : cases.items()) {
            results[name] = CheckCase(name,settings,folder,fullResolutionProtection);
            std::ofstream report(folder/(fullResolutionProtection ? "gentle-highlights.json" : "exposure-order.json"));
            report<<results.dump(2)<<'\n';
            Require(bool(report),"Cannot write the exposure comparison report.");
        }
        passed = true;
    } catch (const std::exception& error) {
        std::cerr<<"Local EV exposure order validation failed: "<<error.what()<<'\n';
    }
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return passed;
}
}
