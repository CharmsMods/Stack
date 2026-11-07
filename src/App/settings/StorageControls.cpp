#include "StorageControls.h"
#include "App/AppPaths.h"
#include "Async/TaskSystem.h"
#include "BurstEngine/ScratchDirectory.h"
#include "Raw/GalleryTileCache.h"
#include "Storage/ScratchBudget.h"
#include <imgui.h>
#include <atomic>

namespace Stack::Settings {
namespace {
struct StorageState {
    std::atomic<bool> requested{false},busy{false},failed{false};
    std::atomic<std::uint64_t> reserved{0},limit{Storage::ScratchBudget::DefaultLimit};
};
StorageState& State(){static StorageState state;return state;}
void Request(int thumbnailMiB=-1,int scratchGiB=-1,bool clear=false) {
    auto& state=State();if(state.busy.exchange(true))return;
    Async::ActivityMetadata activity;activity.label="Updating storage usage";activity.maintenance=true;
    if(!Async::TaskSystem::Get().Submit(std::move(activity),[thumbnailMiB,scratchGiB,clear] {
        auto& state=State();state.failed=false;
        try {
            const auto root=AppPaths::GetCacheDirectory();
            GalleryTileCache::Initialize();
            if(thumbnailMiB>=0)GalleryTileCache::SetLimitMiB(unsigned(thumbnailMiB));
            if(scratchGiB>=0 && !Storage::ScratchBudget::SetLimit(root,unsigned(scratchGiB)))state.failed=true;
            if(clear)GalleryTileCache::Clear();
            BurstEngine::RecoverAbandonedScratch(root);
            state.reserved=Storage::ScratchBudget::Total(root);
            state.limit=Storage::ScratchBudget::Limit(root);
        }catch(...){state.failed=true;}
        state.busy=false;
    })) {state.failed=true;state.busy=false;}
}
}
void RenderStorageControls() {
    auto& state=State();if(!state.requested.exchange(true))Request();
    const auto cache=GalleryTileCache::Status();
    ImGui::TextUnformatted("Gallery thumbnails");
    ImGui::Text("%.1f MB used / %.0f MB limit",double(cache.bytes)/(1024*1024),double(cache.limitBytes)/(1024*1024));
    ImGui::TextWrapped("Small local copies make repeat browsing faster. Older thumbnails are removed automatically. Original images and saved projects are never removed.");
    ImGui::BeginDisabled(state.busy);
    const char* labels[]={"Off","256 MB","512 MB","1 GB","2 GB"};
    const unsigned limits[]={0,256,512,1024,2048};int selected=0;
    for(int i=0;i<5;++i)if(cache.limitBytes>=std::uint64_t(limits[i])*1024*1024)selected=i;
    ImGui::SetNextItemWidth(220);
    if(ImGui::Combo("Thumbnail limit",&selected,labels,5))Request(int(limits[selected]));
    if(ImGui::Button("Clear thumbnail cache"))Request(-1,-1,true);
    ImGui::EndDisabled();
    if(cache.ready&&!cache.writable)ImGui::TextWrapped("Local thumbnail storage is unavailable or in use by another Stack instance. Browsing still works.");
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    ImGui::TextUnformatted("Temporary bracket storage");
    ImGui::Text("%.2f GB reserved / %.0f GB limit",double(state.reserved)/Storage::ScratchBudget::GiB,double(state.limit)/Storage::ScratchBudget::GiB);
    ImGui::TextWrapped("The allowance includes temporary writes by active bracket sessions. It is released when those sessions close. Interrupted sessions are cleaned automatically. At least 2 GB of free space is kept when admitting writes.");
    ImGui::BeginDisabled(state.busy);
    const char* scratchLabels[]={"4 GB","8 GB","16 GB","32 GB","64 GB"};
    const unsigned scratchLimits[]={4,8,16,32,64};int scratch=0;
    for(int i=0;i<5;++i)if(state.limit>=std::uint64_t(scratchLimits[i])*Storage::ScratchBudget::GiB)scratch=i;
    ImGui::SetNextItemWidth(220);
    if(ImGui::Combo("Temporary limit",&scratch,scratchLabels,5))Request(-1,int(scratchLimits[scratch]));
    if(ImGui::Button("Refresh usage and clean abandoned sessions"))Request();
    ImGui::EndDisabled();
    ImGui::TextWrapped("Saved bracket results, project assets, and older unmarked cache folders are separate. This cleanup does not delete them. Lowering a limit never removes an active job's files.");
    if(state.busy)ImGui::TextDisabled("Updating storage...");
    if(state.failed)ImGui::TextWrapped("Storage could not be updated. Check free space and folder access, then refresh.");
}
}
