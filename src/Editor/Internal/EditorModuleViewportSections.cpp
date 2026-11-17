#include "Editor/EditorModule.h"
#include "Async/TaskSystem.h"
#include "App/UiPerformanceProbe.h"
#include <imgui.h>
#include <algorithm>
#include <iostream>

namespace {
bool Intersects(const Stack::Renderer::ImageRegion& a,const Stack::Renderer::ImageRegion& b) {
    return a.x < b.x+b.width && b.x < a.x+a.width && a.y < b.y+b.height && b.y < a.y+a.height;
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
    state.sectionCheckedView=0;state.sectionRevision=state.sectionCheckedRevision=0;
    state.nextSection=-1;
}
int EditorModule::NextViewportNativeSection(Stack::EditorRendering::ViewportPreviewState& state) {
    if(!state.sections || !state.request.visible.Valid())return -1;
    if(state.sectionsComplete)state.completion.wholeImagePending=false;
    const bool whole=state.completion.wholeImagePending;
    // Keep recently seen sharp pixels through pan reversals and zoom-out. RAM
    // and disk remain authoritative; this bounded GPU working set avoids a
    // disposable low-detail flash merely because a section crossed the pane.
    const std::uint64_t budget=std::min<std::uint64_t>(1024ull*1024*1024,
        m_RawWorkspaceVramWorkingBudgetBytes ? m_RawWorkspaceVramWorkingBudgetBytes/4 : 128ull*1024*1024);
    if(state.sectionCheckedView && state.sectionCheckedView==state.request.generation &&
        state.sectionCheckedRevision==state.sectionRevision && state.sectionCheckedWhole==whole && state.sectionCheckedBudget==budget)
        return state.nextSection;
    const auto& visible=state.request.visible;
    state.sectionNativeDemand=std::max(double(state.physicalWidth)/visible.width,
        double(state.physicalHeight)/visible.height)>.249;
    int next=-1;
    const auto useSerial=++state.sectionUseSerial;
    std::uint64_t residentBytes=0;
    std::vector<int> evictionCandidates;
    const auto nearby=Stack::Renderer::PadViewportPreviewRegion(visible);
    int prefetch=-1;
    for(int i=0;i<state.sections->Count();++i) {
        auto& tile=state.sectionTextures[i];
        const bool shown=Intersects(state.sections->Bounds(i),visible);
        if(tile.native.texture) {
            residentBytes+=std::uint64_t(tile.native.width)*tile.native.height*(tile.native.hdr ? 32u : 16u);
            if(shown && state.sectionNativeDemand)tile.lastUse=useSerial;
            else evictionCandidates.push_back(i);
        }
        if(shown && next<0 && (!tile.preview.texture || (state.sectionNativeDemand && !tile.native.texture)))next=i;
        if(!shown && prefetch<0 && state.sectionNativeDemand && !tile.native.texture && tile.preview.texture &&
            Intersects(state.sections->Bounds(i),nearby))prefetch=i;
    }
    if(residentBytes>budget) {
        std::sort(evictionCandidates.begin(),evictionCandidates.end(),[&](int a,int b) {
            return state.sectionTextures[a].lastUse<state.sectionTextures[b].lastUse;
        });
        for(int i:evictionCandidates) {
            if(residentBytes<=budget)break;
            auto& texture=state.sectionTextures[i].native;
            residentBytes-=std::uint64_t(texture.width)*texture.height*(texture.hdr ? 32u : 16u);
            QueueViewportOutputTextureRelease(texture);
        }
    }
    state.sectionsCoverView=next<0;
    // Restore nearby finished sections only with spare space; otherwise
    // prefetch/eviction could chase one another forever on small GPUs.
    if(next<0 && prefetch>=0 && residentBytes+40ull*1024*1024<=budget)next=prefetch;
    if(next<0 && whole)next=state.sections->NextMissing(visible,true);
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
    if(!state.sections || state.acceptedContentIdentity!=state.contentIdentity || state.sectionTextures.empty())return;
    const ImVec2 size(maximum.x-minimum.x,maximum.y-minimum.y);
    if(size.x<=0 || size.y<=0)return;
    const auto clipMin=draw->GetClipRectMin(),clipMax=draw->GetClipRectMax();
    const auto full=state.sections->Bounds(0);
    const int columns=(full.fullWidth+Stack::Renderer::ViewportSectionStore::Edge-1)/Stack::Renderer::ViewportSectionStore::Edge;
    const int rows=(full.fullHeight+Stack::Renderer::ViewportSectionStore::Edge-1)/Stack::Renderer::ViewportSectionStore::Edge;
    const auto cell=[&](float position,float origin,float extent,int pixels,int limit) {
        return std::clamp(int((position-origin)/extent*pixels)/Stack::Renderer::ViewportSectionStore::Edge,0,limit-1);
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
        if(rounding>0 && corners)draw->AddImageRounded((ImTextureID)(intptr_t)texture.Display(hdr),low,high,uv0,uv1,tint,rounding,corners);
        else draw->AddImage((ImTextureID)(intptr_t)texture.Display(hdr),low,high,uv0,uv1,tint);
    }
}
