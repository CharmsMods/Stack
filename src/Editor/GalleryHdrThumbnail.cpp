#include "Editor/GalleryHdrThumbnail.h"
#include "Editor/GalleryPresetPreview.h"
#include "Editor/GalleryPreviewGraph.h"
#include "Editor/RawRenderService.h"
#include "Hdr/Color.h"
#include "Hdr/GraphView.h"
#include "Hdr/RasterSource.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/ProjectOpenCoordinator.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Raw/RawGalleryInputPolicy.h"
#include "Raw/RawLoader.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/RawPreviewProxy.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace Stack::GalleryHdr {
namespace {
bool DisplayRaster(const Request& request, Job& job) {
    std::ifstream input(request.source,std::ios::binary | std::ios::ate);
    const auto bytes=input.tellg();
    if(bytes<=0 || bytes>std::numeric_limits<int>::max()) return false;
    input.seekg(0);
    std::vector<unsigned char> file(static_cast<std::size_t>(bytes));
    if(!input.read(reinterpret_cast<char*>(file.data()),bytes) || job.canceled.load()) return false;
    Hdr::Image image;
    if(!Hdr::DecodeDisplaySource(file,image) || job.canceled.load()) return false;
    const auto [w,h]=Renderer::RawPreviewProxy::FitPreviewExtent(image.width,image.height,request.edge);
    if(w<=0 || h<=0 || image.rgba.size()!=size_t(image.width)*image.height*4) return false;
    job.width=w;
    job.height=h;
    job.pixels.resize(size_t(w)*h*4);
    // Average light and coverage before transfer encoding. A transparent edge
    // must not color a smaller thumbnail through its hidden RGB.
    for(int y=0;y<h;++y) {
        if(job.canceled.load()) return false;
        const int top=int(int64_t(y)*image.height/h);
        const int bottom=int(int64_t(y+1)*image.height/h);
        for(int x=0;x<w;++x) {
            const int left=int(int64_t(x)*image.width/w);
            const int right=int(int64_t(x+1)*image.width/w);
            double sum[4]{};
            for(int sy=top;sy<bottom;++sy) {
                const auto* p=&image.rgba[(size_t(sy)*image.width+left)*4];
                for(int sx=left;sx<right;++sx,p+=4) {
                    for(int c=0;c<3;++c) sum[c]+=double(p[c])*p[3];
                    sum[3]+=p[3];
                }
            }
            auto* out=&job.pixels[(size_t(h-1-y)*w+x)*4];
            for(int c=0;c<3;++c) out[c]=sum[3]>0 ? Hdr::Srgb(float(sum[c]/sum[3])) : 0.f;
            out[3]=float(sum[3]/(double(right-left)*(bottom-top)));
        }
    }
    return true;
}

RenderGraphSnapshot Original(const std::shared_ptr<const Raw::RawImageData>& image) {
    RenderGraphNode source;
    source.nodeId=1;
    source.kind=RenderGraphNodeKind::RawDevelopment;
    source.rawDevelopment.embeddedRawData=image;
    source.rawDevelopment.recipe=RawRecipe::MakeDefaultRecipe(image->metadata.sourcePath);
    RenderGraphSnapshot graph;
    graph.outputNodeId=source.nodeId;
    graph.outputSocketId="imageOut";
    graph.nodes.push_back(std::move(source));
    Project::RawLayerStackState stack;
    std::string error;
    if(!Project::LowerRawLayerStack(graph,stack,1,1,error)) throw std::runtime_error(error);
    return graph;
}
}

void Render(const Request& request, Job& job) {
    const auto canceled=[&]{return job.canceled.load(std::memory_order_relaxed);};
    if(canceled() || request.edge<=0) return;
    if(request.project.empty() && RawWorkspace::IsGalleryRasterPath(request.source)) {
        if(!DisplayRaster(request,job)) job.pixels.clear();
        return;
    }
    Project::ProjectStoreOpenResult opened;
    if(!request.project.empty() && (request.savedBracket || request.preset)) {
        opened=Project::OpenProjectStore(request.project);
        if(!opened || opened.snapshot.persistedStorageRevision!=request.projectRevision) return;
    }
    std::shared_ptr<const Raw::RawImageData> image;
    int nativeWidth=0,nativeHeight=0;
    std::shared_ptr<Project::LoadedProjectData> loaded;
    if(request.preset) image=request.comparison ? request.comparison : GalleryPresets::Comparison({});
    else if(request.savedBracket) {
        Raw::Bracketing::BracketingResult result;
        std::string error;
        if(!Project::RestoreBracketingResult(opened.store,opened.snapshot,opened.snapshot.activeSourceSetId,result,error,canceled)) return;
        image=std::move(result.raw);
    } else if(!request.project.empty()) {
        // The normal open coordinator verifies the actual linked or embedded
        // original and restores graph image assets, including portable files.
        auto project=Project::ProjectOpenCoordinator::Load(request.project);
        if(!project || canceled()) return;
        loaded=std::move(project.candidate);
        if(!loaded->rawProjectSnapshot || loaded->rawProjectSnapshot->persistedStorageRevision!=request.projectRevision)
            return;
        image=loaded->decodedRawSource;
        nativeWidth=loaded->width;
        nativeHeight=loaded->height;
        opened.store=loaded->projectStore;
        opened.snapshot=*loaded->rawProjectSnapshot;
        opened.snapshot.pipelineData=std::move(loaded->pipelineData);
    } else {
        auto original=std::make_shared<Raw::RawImageData>();
        if(!Raw::RawLoader::LoadFile(request.source.u8string(),*original,canceled)) return;
        image=std::move(original);
    }
    if(canceled() || (!image && !loaded)) return;
    if(image) {
        nativeWidth=Raw::DisplayWidth(image->metadata);
        nativeHeight=Raw::DisplayHeight(image->metadata);
    }
    // Preserve sensor geometry through demosaic, masks, crop, and spatial
    // operations. The renderer owns stage-specific proxy sizing.
    auto graph=opened ? GalleryPresets::PrepareGraph(opened,image,request.preset,request.savedBracket) : Original(image);
    if(!Hdr::ConfigureView(graph,Hdr::MasterHeadroom,true) || canceled()) return;
    const auto [w,h]=Renderer::RawPreviewProxy::FitPreviewExtent(
        nativeWidth,nativeHeight,request.edge);
    if(w<=0 || h<=0) return;
    std::string error;
    const bool ok=EditorRendering::RawRenderService::Get().ExecuteOpenGlTaskBlocking([&](std::string&) {
        if(canceled()) return false;
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.SetPreviewMaxDimension(request.edge);
        pipeline.SetRenderCancellationContext(1,canceled);
        pipeline.SetRawDevelopmentAnalysisEnabled(false);
        pipeline.SetRawRgbDenoiseAsyncEnabled(false);
        pipeline.Resize(w,h);
        pipeline.ExecuteGraph(graph);
        const auto& stats=pipeline.GetLastGraphExecutionStats();
        if(canceled() || !pipeline.GetOutputTexture() || stats.allocationFailed ||
            !stats.lastSpecializedFailure.empty() || pipeline.IsOutputDenoisePending()) return false;
        Renderer::GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
        glBindTexture(GL_TEXTURE_2D,pipeline.GetOutputTexture());
        GLint width=0,height=0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
        if(width<=0 || height<=0 || std::max(width,height)>request.edge) {
            binding.Restore();
            return false;
        }
        job.width=width; job.height=height;
        job.pixels.resize(size_t(width)*height*4);
        const Renderer::GLState::PixelPackState pack;
        pack.ConfigureTightCpuReadback();
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,job.pixels.data());
        const auto status=glGetError();
        pack.Restore();
        binding.Restore();
        return status==GL_NO_ERROR;
    },error,Raw::OpenGlTaskPriority::Background);
    if(!ok || canceled()) job.pixels.clear();
}
}
