#include "PhotoPreview.h"
#include "GraphView.h"
#include "Color.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/ScopedGLObjects.h"
namespace Stack::Hdr {
Raw::RawGpuImageLease RenderCompanion(RenderPipeline& pipeline,const RenderGraphSnapshot& graph) try {
    if(graph.outputDescriptor.logicalType!=NodeMath::LogicalValueType::ColorImage) return {};
    auto hdr=graph;
    if(!ConfigureView(hdr,MasterHeadroom,true)) return {};
    pipeline.ExecuteGraph(hdr);
    if(pipeline.GetLastGraphExecutionStats().allocationFailed || !pipeline.GetLastGraphExecutionStats().lastSpecializedFailure.empty()) return {};
    int w=0,h=0;
    Renderer::ScopedGLTexture texture(pipeline.PublishSharedOutputTexture(w,h,false));
    if(!texture) return {};
    auto lease=Raw::RawGpuImageLease::AdoptOwned(texture.Get(),w,h,Raw::RawGpuImageFamily::Presentation);
    if(lease) texture.Release();
    return lease;
} catch(const std::bad_alloc&) {
    // The ordinary rendition remains available on memory-constrained adapters.
    return {};
}
}
