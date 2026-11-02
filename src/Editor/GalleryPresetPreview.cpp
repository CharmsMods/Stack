#include "Editor/GalleryPresetPreview.h"
#include "Editor/RawRenderService.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Project/GraphSnapshotBuilder.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Persistence/ProjectStore.h"
#include "Persistence/ProjectPayloadAssets.h"
#include "Persistence/CreatedAssetStore.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Renderer/RawPreviewProxy.h"
#include "NodeMath/ContractTypes.h"
#include "Renderer/GLStateGuards.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include "Utils/AtomicExportFile.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <fstream>

namespace Stack::GalleryPresets {
namespace {
std::shared_ptr<Raw::RawImageData> RgbInput(int w, int h, const std::string& identity) {
    auto image = std::make_shared<Raw::RawImageData>();
    auto& m = image->metadata;
    m.rawWidth=m.visibleWidth=w; m.rawHeight=m.visibleHeight=h;
    m.pixelLayout=Raw::RawPixelLayout::LinearRgb; m.mosaiced=false; m.linearChannels=3;
    m.cameraWhiteBalance={1,1,1,1}; m.sourcePath=identity;
    image->contentIdentity=identity;
    image->linearFloatBuffer.resize(static_cast<std::size_t>(w)*h*3);
    return image;
}
std::shared_ptr<const Raw::RawImageData> TestPattern() {
    auto image = RgbInput(previewSize,previewSize,"stack://preset-pattern/v1");
    constexpr float patches[][3]={{.8f,.04f,.03f},{.05f,.6f,.06f},{.02f,.08f,.8f},
        {.6f,.55f,.04f},{.02f,.55f,.55f},{.6f,.04f,.6f},{.52f,.29f,.19f},{.18f,.18f,.18f}};
    for (int y=0;y<previewSize;++y) for (int x=0;x<previewSize;++x) {
        const float t=float(x)/(previewSize-1);
        for (int c=0;c<3;++c) {
            float value=t*t;
            if (y>=previewSize/3 && y<2*previewSize/3) value=patches[x*8/previewSize][c];
            if (y>=2*previewSize/3) {
                const int scale=1+x/(previewSize/4);
                value=((x/scale+y/scale)&1) ? .75f : .04f;
            }
            image->linearFloatBuffer[(std::size_t(y)*previewSize+x)*3+c]=value;
        }
    }
    return image;
}
}
std::shared_ptr<const Raw::RawImageData> Comparison(const std::filesystem::path& path) {
    if (path.empty()) { static const auto pattern=TestPattern(); return pattern; }
    const auto identity=RawEvidence::ComputeSourceIdentity(path);
    if (!identity.valid) throw std::runtime_error("The comparison image could not be read.");
    if (Raw::RawLoader::IsRawPath(path.u8string())) {
        auto raw=std::make_shared<Raw::RawImageData>();
        if (!Raw::RawLoader::LoadFile(path.u8string(),*raw)) throw std::runtime_error(raw->metadata.error);
        auto proxy=std::make_shared<Raw::RawImageData>();
        if (Renderer::RawPreviewProxy::BuildPreviewRawData(*raw,previewSize,*proxy)) raw=std::move(proxy);
        raw->contentIdentity=identity.sha256;
        return raw;
    }
    std::ifstream input(path,std::ios::binary);
    std::vector<unsigned char> encoded((std::istreambuf_iterator<char>(input)),{});
    if (encoded.size()>INT_MAX) throw std::runtime_error("The comparison image exceeds decoder limits.");
    int width=0,height=0,channels=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> bytes(stbi_load_from_memory(encoded.data(),
        static_cast<int>(encoded.size()),&width,&height,&channels,3),stbi_image_free);
    if (!bytes) throw std::runtime_error("Choose a readable RAW, JPEG or PNG comparison image.");
    const auto [w,h]=Renderer::RawPreviewProxy::FitPreviewExtent(width,height,previewSize);
    auto image=RgbInput(w,h,identity.sha256);
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) for (int c=0;c<3;++c) {
        const auto source=(std::size_t(y)*height/h*width+std::size_t(x)*width/w)*3+c;
        const float v=bytes.get()[source]/255.f;
        image->linearFloatBuffer[(std::size_t(y)*w+x)*3+c]=v<=.04045f ? v/12.92f : std::pow((v+.055f)/1.055f,2.4f);
    }
    return image;
}
std::filesystem::path Render(const std::filesystem::path& project, std::uint64_t revision,
    const std::filesystem::path& previews, const std::shared_ptr<const Raw::RawImageData>& comparison) {
    auto opened=Project::OpenProjectStore(project);
    if (!opened || revision!=opened.snapshot.persistedStorageRevision) throw std::runtime_error("The project changed during preview.");
    const auto image=comparison ? comparison : Comparison({});
    const auto key=NodeMath::Sha256ContentIdentity(opened.snapshot.projectId + "/" + std::to_string(revision) +
        "/v2/" + project.generic_u8string() + "/" + image->contentIdentity + "/" + std::to_string(previewSize));
    const auto path=previews/"Preset previews"/(key.substr(7)+".png");
    if (std::filesystem::is_regular_file(path)) return path;
    auto& data=opened.snapshot.pipelineData;
    const auto read=[&](const std::string& id,std::vector<unsigned char>& bytes) {
        const auto* asset=Project::FindEmbeddedAsset(opened.snapshot,id);
        if (!asset || asset->linkedSource) return false;
        auto stream=opened.store->OpenAssetStream(id);
        if (!stream) return false;
        bytes.assign(std::istreambuf_iterator<char>(*stream.stream),{});
        return RawEvidence::ComputeSourceIdentity(bytes).sha256==asset->sha256;
    };
    if (!Project::RehydratePayloadBytes(data,read) || !Project::RehydrateCreatedAssets(data,read))
        throw std::runtime_error("Missing authored mask or asset.");
    EditorNodeGraph::Graph composition;
    std::vector<std::shared_ptr<LayerBase>> layers;
    for (const auto& saved : EditorNodeGraph::ExtractLayerArray(data)) {
        auto layer=LayerRegistry::CreateLayerFromTypeId(saved.at("type").get<std::string>());
        if (!layer) throw std::runtime_error("A graph operation is unavailable.");
        layer->Deserialize(saved);
        layers.push_back(std::move(layer));
    }
    EditorNodeGraph::DeserializeGraphPayload(data,composition,static_cast<int>(layers.size()));
    Project::RawProjectEditRecipeBinding binding;
    if (!Project::ResolveRawProjectEditRecipe(opened.snapshot,opened.snapshot.activeSourceSetId,binding))
        binding.recipe=RawRecipe::MakeDefaultRecipe(image->metadata.sourcePath);
    binding.recipe.source.sourcePath=image->metadata.sourcePath;
    Timeline::TimelineAnimationState timeline;
    std::unordered_map<int,std::uint64_t> generations;
    std::vector<Timeline::AnimatableParameterTarget> targets;
    Project::GraphSnapshotInputs inputs{composition,layers,timeline,generations,binding.recipe,image,targets};
    inputs.authoredRevision=revision;
    auto graph=Project::BuildGraphSnapshot(inputs).snapshot;
    const auto sourceUuid=data.value("rawLayerSourceNodeUuid",std::string{});
    int background=0;
    for (auto& node : graph.nodes) {
        const auto* authored=composition.FindNode(node.nodeId);
        if (authored && authored->instanceUuid==sourceUuid) background=node.nodeId;
        if (node.kind==RenderGraphNodeKind::RawDevelopment || node.kind==RenderGraphNodeKind::RawProjectSourceSet ||
            node.kind==RenderGraphNodeKind::Image || node.kind==RenderGraphNodeKind::RawSource) {
            node.kind=RenderGraphNodeKind::RawDevelopment;
            node.rawDevelopment.recipe=binding.recipe;
            node.rawDevelopment.embeddedRawData=image;
            node.nativeWidth=image->metadata.visibleWidth;node.nativeHeight=image->metadata.visibleHeight;
        }
    }
    std::string error;
    if (data.contains("rawLayerStack")) {
        Project::RawLayerStackState stack;
        if (!Project::ReadRawLayersFromPipeline(data,stack,error,true) ||
            !Project::LowerRawLayerStack(graph,stack,background,revision,error,0,120,30,&composition)) throw std::runtime_error(error);
    }
    std::vector<unsigned char> pixels;
    int w=0,h=0;
    const bool ok=EditorRendering::RawRenderService::Get().ExecuteOpenGlTaskBlocking([&](std::string& failure) {
        RenderPipeline pipeline; pipeline.Initialize();
        pipeline.SetRawDevelopmentAnalysisEnabled(false); pipeline.SetRawRgbDenoiseAsyncEnabled(false);
        pipeline.Resize(image->metadata.visibleWidth,image->metadata.visibleHeight);
        pipeline.ExecuteGraph(graph);
        w=pipeline.GetCanvasWidth(); h=pipeline.GetCanvasHeight();
        if (!pipeline.GetOutputTexture() || w<=0 || h<=0 || pipeline.GetLastGraphExecutionStats().allocationFailed) {
            failure="Could not render the selected edits on the comparison image."; return false;
        }
        const Renderer::GLState::PixelPackState pack; pack.ConfigureTightCpuReadback();
        GLint previous=0; glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
        glBindTexture(GL_TEXTURE_2D,pipeline.GetOutputTexture());
        pixels.resize(std::size_t(w)*h*4); glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        glBindTexture(GL_TEXTURE_2D,previous); pack.Restore();
        return true;
    },error,Raw::OpenGlTaskPriority::Background);
    if (!ok) throw std::runtime_error(error);
    const auto stride=std::size_t(w)*4;
    for (int y=0;y<h/2;++y)
        std::swap_ranges(pixels.begin()+stride*y,pixels.begin()+stride*(y+1),pixels.begin()+stride*(h-1-y));
    std::vector<unsigned char> encoded;
    const auto write=[](void* ctx,void* data,int n) {
        auto& bytes=*static_cast<std::vector<unsigned char>*>(ctx); auto* p=static_cast<unsigned char*>(data);
        bytes.insert(bytes.end(),p,p+n);
    };
    if (!stbi_write_png_to_func(write,&encoded,w,h,4,pixels.data(),w*4)) throw std::runtime_error("Could not encode preset preview.");
    std::filesystem::create_directories(path.parent_path());
    FileSave::ExportFileTarget target;
    if (!FileSave::PrepareExportFileTarget(path,{}, {".png"},target,error) ||
        !FileSave::WriteExportFileAtomically(target,encoded,error)) throw std::runtime_error(error);
    return path;
}
}
