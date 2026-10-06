#include "App/Validation/ValidationImageUtils.h"
#include "Editor/EditorModule.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Raw/RawLoader.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Image { std::vector<float> pixels; int width=0,height=0; };
Image Render(RenderPipeline& pipeline, const RenderGraphSnapshot& graph, int edge) {
    pipeline.SetPreviewMaxDimension(edge); pipeline.Resize(64,64); pipeline.ExecuteGraph(graph);
    Require(pipeline.GetOutputTexture()!=0 && !pipeline.GetLastGraphExecutionStats().allocationFailed,
        "Tone/detail image render failed");
    Image result; result.width=pipeline.GetCanvasWidth();result.height=pipeline.GetCanvasHeight();
    result.pixels=ReadTextureRgbaFloat(pipeline.GetOutputTexture(),result.width,result.height);
    Require(!result.pixels.empty(),"Empty tone/detail image");
    for(float value:result.pixels)Require(std::isfinite(value),"Tone/detail produced a nonfinite value");
    return result;
}
float Luma(const Image& image,int x,int y) {
    const auto i=(std::size_t(y)*image.width+x)*4;
    return std::max(1e-8f,.2627002f*image.pixels[i]+.6779981f*image.pixels[i+1]+.0593017f*image.pixels[i+2]);
}
void CheckReal(const std::filesystem::path& path,const std::filesystem::path& folder) {
    using namespace RawRecipe;
    auto raw=std::make_shared<Raw::RawImageData>();
    Require(Raw::RawLoader::LoadFile(path.string(),*raw),"Cannot load requested RAW");
    RenderPipeline pipeline;pipeline.Initialize();pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);pipeline.SetGraphCacheBudget(256ull*1024*1024);
    auto recipe=MakeDefaultRecipe(path.string());recipe.rgbDenoise.enabled=false;
    recipe.technical.mosaicDenoise.enabled=false;recipe.viewTransform.layerJson["enabled"]=false;
    RenderGraphNode source;source.nodeId=1;source.kind=RenderGraphNodeKind::RawDevelopment;
    source.rawDevelopment.recipe=recipe;source.rawDevelopment.embeddedRawData=raw;
    RenderGraphSnapshot graph;graph.nodes.push_back(source);graph.outputNodeId=1;graph.outputSocketId="imageOut";
    const auto previewBase=Render(pipeline,graph,768);
    // Coarse bands remain resolvable in both the small preview and native image.
    graph.nodes[0].rawDevelopment.recipe.detailContrast.scaleGains[6]=1.35f;
    graph.nodes[0].rawDevelopment.recipe.detailContrast.scaleGains[7]=1.25f;
    const auto previewDetail=Render(pipeline,graph,768);
    const auto nativeDetail=Render(pipeline,graph,0);
    graph.nodes[0].rawDevelopment.recipe.detailContrast={};
    const auto nativeBase=Render(pipeline,graph,0);
    double squared=0, brightSquared=0, maxDifference=0;std::size_t count=0, brightCount=0;
    std::array<float,4> worstLumas{};
    for(int y=1;y<previewBase.height-1;y+=3)for(int x=1;x<previewBase.width-1;x+=3) {
        const int nx=std::min(nativeBase.width-1,int((x+.5)*nativeBase.width/previewBase.width));
        const int ny=std::min(nativeBase.height-1,int((y+.5)*nativeBase.height/previewBase.height));
        const double a=std::log2(Luma(previewDetail,x,y)/Luma(previewBase,x,y));
        const double b=std::log2(Luma(nativeDetail,nx,ny)/Luma(nativeBase,nx,ny));
        squared+=(a-b)*(a-b);++count;
        if (std::abs(a-b)>maxDifference) {
            maxDifference=std::abs(a-b);
            worstLumas={Luma(previewBase,x,y),Luma(previewDetail,x,y),Luma(nativeBase,nx,ny),Luma(nativeDetail,nx,ny)};
        }
        if (Luma(previewBase,x,y) > .00018f && Luma(nativeBase,nx,ny) > .00018f) {
            brightSquared+=(a-b)*(a-b);++brightCount;
        }
    }
    const double rmse=std::sqrt(squared/std::max<std::size_t>(1,count));
    std::cout<<"Source-scale preview/native gain RMSE: "<<rmse<<" EV\n";
    std::cout<<"Detail comparison dimensions "<<previewBase.width<<'x'<<previewBase.height<<" / "<<nativeBase.width<<'x'<<nativeBase.height
        <<", largest difference "<<maxDifference<<" EV, samples above -10 EV "<<brightCount<<'/'<<count
        <<", above-floor RMSE "<<std::sqrt(brightSquared/std::max<std::size_t>(1,brightCount))<<" EV\n";
    std::cout<<"Largest-difference luminances: preview "<<worstLumas[0]<<" -> "<<worstLumas[1]
        <<", native "<<worstLumas[2]<<" -> "<<worstLumas[3]<<'\n';
    Require(rmse<.15,"Coarse detail changes materially with preview resolution");
    Project::RawLayerStackState sourceLayers;
    std::string sourceError;
    const auto renderOriginal = [&](const RawDevelopmentRecipe& sourceRecipe) {
        auto sourceGraph = graph;
        sourceGraph.nodes[0].rawDevelopment.recipe = sourceRecipe;
        Require(Project::LowerRawLayerStack(sourceGraph,sourceLayers,1,1,sourceError),sourceError.c_str());
        const auto* original = Project::FindRawRole(sourceLayers.background,GraphModel::NodeRole::OriginalImage);
        sourceGraph.outputNodeId = sourceGraph.rawLayerMaskNodeIds.at(Project::kRawBackgroundId).at(original->id);
        sourceGraph.outputSocketId = "imageOut";
        return Render(pipeline,sourceGraph,768);
    };
    const auto originalBefore = renderOriginal(recipe);
    auto changedSource = recipe;
    changedSource.whiteBalance.mode = WhiteBalanceMode::CustomMultipliers;
    changedSource.whiteBalance.hasMultipliers = true;
    changedSource.whiteBalance.multipliers = {2.f,1.f,.4f};
    changedSource.rgbDenoise.enabled = true;
    changedSource.technical.mosaicDenoise.enabled = true;
    const auto originalAfter = renderOriginal(changedSource);
    Require(originalBefore.width==originalAfter.width && originalBefore.height==originalAfter.height &&
        originalBefore.pixels==originalAfter.pixels,"Original changed with authored white balance or denoise");
    std::cout<<"Original source is unchanged by authored white balance and denoise\n";
    Project::RawLayerStackState layers;const auto id=Project::AddRawAdjustmentLayer(layers,"Tone and detail");
    auto& layer=layers.layers[0];SceneTone tone;tone.contrast=1.25f;tone.outerProtection=.65f;
    FindRawOperation(layer, GraphOperationKind::LuminanceTone)->rawOperation.parameters = SerializeSceneTone(tone);
    auto detail = ReadGraphOperation(FindRawOperation(layer, GraphOperationKind::DetailContrast)->rawOperation);
    detail.detailContrast.scaleGains[4]=1.2f; detail.detailContrast.scaleGains[6]=1.25f;
    WriteGraphOperation(FindRawOperation(layer, GraphOperationKind::DetailContrast)->rawOperation, detail);
    auto local = ReadGraphOperation(FindRawOperation(layer, GraphOperationKind::LocalEv)->rawOperation);
    local.localRange.enabled=true; local.localRange.points[2].deltaEv=.4f;
    WriteGraphOperation(FindRawOperation(layer, GraphOperationKind::LocalEv)->rawOperation, local);
    const auto mask=Project::AddRawGeneratedMask(layers,id,EditorNodeGraph::MaskGeneratorKind::LinearGradient,"Fade");
    layer.layerMask=mask;
    recipe.viewTransform.layerJson=DefaultViewTransformJson();recipe.viewTransform.layerJson["encodeSrgbOutput"]=true;
    recipe.cropRotation.cropEnabled=true;recipe.cropRotation.cropX=.1f;recipe.cropRotation.cropY=.1f;
    recipe.cropRotation.cropWidth=.8f;recipe.cropRotation.cropHeight=.8f;
    graph.nodes[0].rawDevelopment.recipe=recipe;
    auto lowered=graph;std::string error;
    Require(Project::LowerRawLayerStack(lowered,layers,1,1,error),error.c_str());
    Render(pipeline,lowered,768);
    int width=0,height=0;auto preview=pipeline.GetOutputPixels(width,height);
    std::filesystem::create_directories(folder);
    Require(WriteValidationPng(folder/"tone-detail-layer.png",preview,width,height),"Cannot save inspection image");
    Render(pipeline,graph,768);preview=pipeline.GetOutputPixels(width,height);
    Require(WriteValidationPng(folder/"baseline.png",preview,width,height),"Cannot save baseline image");
    Render(pipeline,lowered,0);const auto expected=pipeline.GetOutputPixels(width,height);
    EditorModule editor;editor.GetPipeline().Initialize();
    auto& project=editor.GetProjectSession();project.rawRecipe=recipe;project.singleRawSource=raw;
    project.snapshot=std::make_shared<Project::RawProjectSnapshot>();
    project.snapshot->projectKindHint=StackBinaryFormat::kRawProjectKind;
    Require(project.rawLayers.Apply(layers,error),error.c_str());
    auto& authored=editor.GetNodeGraph();authored.Clear();
    EditorNodeGraph::RawDevelopmentPayload payload;payload.recipe=recipe;
    const int input=authored.AddRawDevelopmentNode(std::move(payload),{0,0})->id;
    const int output=authored.AddOutputNode({480,0},true)->id;
    Require(authored.TryConnectSockets(input,"imageOut",output,"imageIn"),"Cannot connect export graph");
    std::vector<unsigned char> exported;int exportWidth=0,exportHeight=0;
    Require(editor.BuildSingleOutputExportRaster(exported,exportWidth,exportHeight),"Masked tone/detail export failed");
    Require(width==exportWidth&&height==exportHeight&&expected.size()==exported.size(),"Layer export geometry differs");
    int difference=0;
    for(std::size_t i=0;i<exported.size();++i)difference=std::max(difference,std::abs(int(expected[i])-int(exported[i])));
    std::cout<<"Masked native preview/export maximum byte difference: "<<difference<<'\n';
    Require(difference<=1,"Layered native preview and export disagree");
}
}
bool ValidateToneDetail(const std::filesystem::path& path,const std::filesystem::path& folder) {
    if(!glfwInit())return false;
    glfwDefaultWindowHints();glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto* window=glfwCreateWindow(64,64,"Tone/detail validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}glfwMakeContextCurrent(window);
    bool passed=false;
    try {Require(LoadGLFunctions(),"Cannot initialize OpenGL");CheckReal(path,folder);passed=true;}
    catch(const std::exception& error){std::cerr<<"Tone/detail validation failed: "<<error.what()<<'\n';}
    glfwMakeContextCurrent(nullptr);glfwDestroyWindow(window);glfwTerminate();return passed;
}
}
