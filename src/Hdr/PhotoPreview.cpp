#include "PhotoPreview.h"
#include "GraphView.h"
#include "Color.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/ScopedGLObjects.h"
namespace Stack::Hdr {
Raw::RawGpuImageLease RenderCompanion(RenderPipeline& pipeline,const RenderGraphSnapshot& graph,
    std::string& error) try {
    error.clear();
    if(graph.outputDescriptor.logicalType!=NodeMath::LogicalValueType::ColorImage) return {};
    auto hdr=graph;
    if(!ConfigureView(hdr,MasterHeadroom,true)) return {};
    pipeline.ExecuteGraph(hdr);
    const auto& stats=pipeline.GetLastGraphExecutionStats();
    if(stats.allocationFailed || !stats.lastSpecializedFailure.empty()) {
        error=stats.lastSpecializedFailure.empty() ? "Not enough GPU memory for the HDR preview." :
            "HDR preview failed: "+stats.lastSpecializedFailure;
        return {};
    }
    int w=0,h=0;
    Renderer::ScopedGLTexture texture(pipeline.PublishSharedOutputTexture(w,h,false,&error));
    if(!texture) {
        error=error.empty() ? "The HDR renderer produced no preview image." : "HDR preview failed: "+error;
        return {};
    }
    auto lease=Raw::RawGpuImageLease::AdoptOwned(texture.Get(),w,h,Raw::RawGpuImageFamily::Presentation);
    if(lease) texture.Release();
    else error="Could not retain the HDR preview image.";
    return lease;
} catch(const std::bad_alloc&) {
    error="Not enough memory for the HDR preview.";
    return {};
} catch(const std::exception& exception) {
    error="HDR preview failed: "+std::string(exception.what());
    return {};
}
}
