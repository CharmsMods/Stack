#include "Editor/EditorModule.h"
#include "Async/TaskSystem.h"
#include "App/UiPerformanceProbe.h"
#include "Editor/ViewportSectionDrawing.h"
#include "Renderer/ViewportSectionResidency.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
bool Intersects(const Stack::Renderer::ImageRegion& a,const Stack::Renderer::ImageRegion& b) {
    return a.x < b.x+b.width && b.x < a.x+a.width && a.y < b.y+b.height && b.y < a.y+a.height;
}
std::uint64_t SectionTextureBytes(const EditorRenderWorker::SharedTextureResult& texture) {
    return texture.texture && texture.width>0 && texture.height>0 ?
        std::uint64_t(texture.width)*texture.height*(texture.hdr ? 32u : 16u) : 0;
}
}
void EditorModule::ClearViewportNativeSections(Stack::EditorRendering::ViewportPreviewState& state) {
    for(auto& tile:state.sectionTextures) {
        QueueViewportOutputTextureRelease(tile.native);QueueViewportOutputTextureRelease(tile.preview);
    }
    state.sectionTextures.clear();
    // File closure/delete-on-close and potentially large RAM releases belong
    // off the UI thread. In-flight snapshots also retain their original store.
    state.sections.reset();
    state.sectionsComplete=state.sectionsCoverView=false;
    state.sectionBudgetLimited=false;
    state.sectionCheckedView=0;state.sectionRevision=state.sectionCheckedRevision=0;
    state.nextSection=-1;
}
int EditorModule::NextViewportNativeSection(Stack::EditorRendering::ViewportPreviewState& state) {
    if(!state.sections || !state.request.visible.Valid() ||
        state.sectionTextures.size()!=std::size_t(state.sections->Count()))return -1;
    RefreshRawWorkspaceGpuMemoryBudget();
    state.sectionsComplete=state.sections->Complete();
    if(state.sectionsComplete)state.completion.wholeImagePending=false;
    const bool whole=state.completion.wholeImagePending;
    // This is an absolute cache allowance sampled with GPU headroom, not a
    // newly shrinking fraction of free memory after every adopted section.
    const std::uint64_t budget=state.sectionGpuBudgetBytes;
    if(state.sectionCheckedView && state.sectionCheckedView==state.request.generation &&
        state.sectionCheckedRevision==state.sectionRevision && state.sectionCheckedWhole==whole && state.sectionCheckedBudget==budget)
        return state.nextSection;
    const auto& visible=state.request.visible;
    state.sectionNativeDemand=std::max(double(state.physicalWidth)/visible.width,
        double(state.physicalHeight)/visible.height)>.249;
    const auto useSerial=++state.sectionUseSerial;
    const auto nearby=Stack::Renderer::PadViewportPreviewRegion(visible);
    std::vector<Stack::Renderer::ViewportSectionResidencyInput> inputs;
    inputs.reserve(state.sectionTextures.size());
    for(int i=0;i<state.sections->Count();++i) {
        auto& tile=state.sectionTextures[i];
        const bool shown=Intersects(state.sections->Bounds(i),visible);
        if(shown && (tile.native.texture || tile.preview.texture))tile.lastUse=useSerial;
        const auto raster=state.sections->Raster(i);
        // An unseen section may carry a paired HDR rendition. Use that upper
        // bound until an actual retained texture establishes its format.
        const bool hdr=tile.native.texture ? bool(tile.native.hdr) :
            (tile.preview.texture ? bool(tile.preview.hdr) : true);
        const std::uint64_t bytesPerPixel=hdr ? 32u : 16u;
        inputs.push_back({SectionTextureBytes(tile.native),SectionTextureBytes(tile.preview),
            std::uint64_t(raster.width)*raster.height*bytesPerPixel,
            std::uint64_t((raster.width+3)/4)*((raster.height+3)/4)*bytesPerPixel,
            tile.lastUse,shown,Intersects(state.sections->Bounds(i),nearby),state.sectionNativeDemand});
    }
    const auto plan=Stack::Renderer::PlanViewportSectionResidency(inputs,budget);
    for(std::size_t i=0;i<state.sectionTextures.size();++i) {
        auto& tile=state.sectionTextures[i];
        if(!plan.sections[i].wantNative)QueueViewportOutputTextureRelease(tile.native);
        if(!plan.sections[i].wantPreview)QueueViewportOutputTextureRelease(tile.preview);
    }
    state.sectionsCoverView=plan.covered;
    state.sectionBudgetLimited=plan.budgetLimited;
    int next=plan.nextVisibleRestore;
    // Native CPU/disk completion is independent of disposable GPU residency.
    // Never restore a finished section excluded by the desired display set.
    if(next<0)next=state.sections->NextMissing(visible,whole);
    state.sectionCheckedBudget=budget;
    state.sectionCheckedView=state.request.generation;
    state.sectionCheckedRevision=state.sectionRevision;state.sectionCheckedWhole=whole;
    state.nextSection=next;
    return next;
}
bool EditorModule::HasNativeViewportCoverage() const {
    if(!UsesGraphViewportPreview()) {
        Stack::Renderer::ViewportPreviewGeometry geometry;
        return TryGetViewportPreviewGeometry(geometry) && geometry.NativeSamples();
    }
    const auto& state=ActiveViewportPreviewState();
    if(state.acceptedContentIdentity!=state.contentIdentity || state.request.empty)return false;
    if(!state.acceptedInteractive && state.geometry.NativeSamples() &&
        Stack::Renderer::ContainsImageRegion(state.geometry.resident,state.request.visible))return true;
    // This query promises display coverage backed by settled native pixels,
    // not a single native texture suitable for export or pixel readback.
    return state.sections && state.sectionsCoverView && state.sectionCheckedView==state.request.generation &&
        state.sectionCheckedRevision==state.sectionRevision;
}
void EditorModule::AdoptViewportNativeSection(EditorRenderWorker::Result& result) {
    if(result.viewportViewId<1 || result.viewportViewId>2)return;
    auto& state=m_ViewportPreviewStates[result.viewportViewId-1];
    if(state.sections!=result.viewportSections || result.viewportSection<0 ||
        result.viewportSection>=int(state.sectionTextures.size()) || result.denoisePending ||
        !result.viewportSectionPreview.texture || !state.sections->Has(result.viewportSection))return;
    auto& tile=state.sectionTextures[result.viewportSection];
    QueueViewportOutputTextureRelease(tile.native);QueueViewportOutputTextureRelease(tile.preview);
    tile.native=std::move(result.outputTexture);tile.preview=std::move(result.viewportSectionPreview);
    tile.lastUse=++state.sectionUseSerial;
    ++state.sectionRevision;state.sectionsComplete=state.sections->Complete();
    if(Stack::UiPerformance::Probe::Active())
        std::clog << "Native section index=" << result.viewportSection << " restored=" << result.viewportSectionRestored
            << " camera=" << state.request.generation << " complete=" << state.sectionsComplete
            << " misses=" << result.mainGraphStats.imageCacheMisses
            << " workerMs=" << result.telemetry.workerTotalMs
            << " cache=" << result.mainGraphStats.persistentCacheBytes
            << "/" << result.mainGraphStats.persistentCacheBudgetBytes
            << " diskBytes=" << state.sections->DiskBytes() << '\n';
    (void)NextViewportNativeSection(state);
    if(state.sectionsCoverView) {
        state.acceptedInteractive=false;
        Stack::UiPerformance::Probe::AcceptedImage(m_ViewportOutputRenderGeneration,result.graphRequest.revision,this,true);
    }
}
void EditorModule::DrawViewportNativeSections(ImDrawList* draw,ImVec2 minimum,ImVec2 maximum,bool hdr,ImU32 tint,float rounding) const {
    if(!UsesGraphViewportPreview() || !draw)return;
    const auto& state=ActiveViewportPreviewState();
    if(!state.sections || state.acceptedContentIdentity!=state.contentIdentity ||
        !state.acceptedGeneration || state.acceptedGeneration!=m_ViewportOutputRenderGeneration || !state.geometry.Valid() ||
        state.sectionTextures.size()!=std::size_t(state.sections->Count()))return;
    const ImVec2 size(maximum.x-minimum.x,maximum.y-minimum.y);
    const auto clipMin=draw->GetClipRectMin(),clipMax=draw->GetClipRectMax();
    if(!std::isfinite(minimum.x) || !std::isfinite(minimum.y) ||
        !std::isfinite(maximum.x) || !std::isfinite(maximum.y) ||
        !std::isfinite(size.x) || !std::isfinite(size.y) || size.x<=0 || size.y<=0 ||
        !std::isfinite(clipMin.x) || !std::isfinite(clipMin.y) ||
        !std::isfinite(clipMax.x) || !std::isfinite(clipMax.y) ||
        clipMax.x<=minimum.x || clipMax.y<=minimum.y || clipMin.x>=maximum.x || clipMin.y>=maximum.y)return;
    const auto full=state.sections->Bounds(0);
    if(state.geometry.logicalWidth!=full.fullWidth || state.geometry.logicalHeight!=full.fullHeight)return;
    const bool currentOverview=state.overviewContentIdentity==state.contentIdentity &&
        state.overview.texture && state.overview.lease.Texture()==state.overview.texture && !state.overview.readyFence;
    const int overviewWidth=currentOverview ? state.overview.width : 0;
    const int overviewHeight=currentOverview ? state.overview.height : 0;
    const int columns=(full.fullWidth+Stack::Renderer::ViewportSectionStore::Edge-1)/Stack::Renderer::ViewportSectionStore::Edge;
    const int rows=(full.fullHeight+Stack::Renderer::ViewportSectionStore::Edge-1)/Stack::Renderer::ViewportSectionStore::Edge;
    const auto cell=[&](float position,float origin,float extent,int pixels,int limit) {
        const double fraction=std::clamp((double(position)-origin)/extent,0.0,1.0);
        return std::min(int(fraction*pixels)/Stack::Renderer::ViewportSectionStore::Edge,limit-1);
    };
    const int x0=cell(clipMin.x,minimum.x,size.x,full.fullWidth,columns),x1=cell(clipMax.x,minimum.x,size.x,full.fullWidth,columns);
    const int y0=cell(clipMin.y,minimum.y,size.y,full.fullHeight,rows),y1=cell(clipMax.y,minimum.y,size.y,full.fullHeight,rows);
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
        const int index=y*columns+x;const auto& tile=state.sectionTextures[index];
        const auto& texture=tile.native.texture ? tile.native : tile.preview;
        if(!texture.texture)continue;
        const auto core=state.sections->Bounds(index),raster=state.sections->Raster(index);
        const ImVec2 low(minimum.x+size.x*core.x/core.fullWidth,minimum.y+size.y*core.y/core.fullHeight);
        const ImVec2 high(minimum.x+size.x*(core.x+core.width)/core.fullWidth,minimum.y+size.y*(core.y+core.height)/core.fullHeight);
        const ImVec2 uv0(float(core.x-raster.x)/raster.width,1.f-float(core.y-raster.y)/raster.height);
        const ImVec2 uv1(float(core.x+core.width-raster.x)/raster.width,1.f-float(core.y+core.height-raster.y)/raster.height);
        ImDrawFlags corners=0;
        if(x==0 && y==0)corners|=ImDrawFlags_RoundCornersTopLeft;
        if(x==columns-1 && y==0)corners|=ImDrawFlags_RoundCornersTopRight;
        if(x==0 && y==rows-1)corners|=ImDrawFlags_RoundCornersBottomLeft;
        if(x==columns-1 && y==rows-1)corners|=ImDrawFlags_RoundCornersBottomRight;
        const auto drawSection=[&] {
            if(rounding>0 && corners)draw->AddImageRounded((ImTextureID)(intptr_t)texture.Display(hdr),low,high,uv0,uv1,tint,rounding,corners);
            else draw->AddImage((ImTextureID)(intptr_t)texture.Display(hdr),low,high,uv0,uv1,tint);
        };
        if(tile.native.texture) {
            drawSection();
            continue;
        }
        // Evicting native residency may leave only a quarter-sized display
        // copy. Preserve finer current base/overview pixels beneath it.
        const auto plan=Stack::EditorRendering::PlanViewportSectionPreviewDraw(core,raster,
            texture.width,texture.height,state.geometry,overviewWidth,overviewHeight);
        for(std::size_t i=0;i<plan.count;++i) {
            const auto& clip=plan.clips[i];
            draw->PushClipRect(
                ImVec2(float(minimum.x+size.x*clip.left),float(minimum.y+size.y*clip.top)),
                ImVec2(float(minimum.x+size.x*clip.right),float(minimum.y+size.y*clip.bottom)),true);
            drawSection();
            draw->PopClipRect();
        }
    }
}
