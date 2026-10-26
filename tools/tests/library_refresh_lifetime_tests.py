#!/usr/bin/env python3
"""Bounded CPU checks of production Library refresh/thumbnail method bodies.

The catalog entries are real ProjectData types; GL, image decode, and task queues
are small stand-ins. This checks ownership, exact-cover transfer, late callback
rejection, bounded retirement and close-time drain, not hardware frame times.
"""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
refresh = (root / 'src/Library/LibraryManagerRefresh.cpp').read_text()
thumbnails = (root / 'src/Library/LibraryManagerThumbnails.cpp').read_text()
manager = (root / 'src/Library/LibraryManager.cpp').read_text()
previews = (root / 'src/Library/LibraryModulePreviews.cpp').read_text()

def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, pos = 1, brace + 1
    while depth:
        if source[pos] == '{': depth += 1
        elif source[pos] == '}': depth -= 1
        pos += 1
    return source[start:pos] + '\n'

pre = r'''
#include "Library/ProjectData.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
std::vector<unsigned> deletedTextures;
std::function<void()> onDelete;
int currentContext=1, uploads=0;
void glDeleteTextures(int count,const unsigned* names) {
    if(onDelete) onDelete();
    deletedTextures.insert(deletedTextures.end(),names,names+count);
}
bool MutexAvailable(std::mutex& mutex) {
    bool acquired=false;
    std::thread contender([&] { acquired=mutex.try_lock();if(acquired) mutex.unlock(); });
    contender.join();return acquired;
}
namespace GLHelpers {
unsigned CreateTextureFromPixels(const unsigned char*,int,int,int) { return 10000u + ++uploads; }
}
namespace Async {
struct ActivityMetadata {};
class TaskSystem {
public:
    using Task=std::function<void()>;
    static TaskSystem& Get() { static TaskSystem instance;return instance; }
    std::deque<Task> workers, main;
    bool Submit(ActivityMetadata,Task task) { workers.push_back(std::move(task));return true; }
    bool PostToMain(Task task) { main.push_back(std::move(task));return true; }
    void RunWorkers() { while(!workers.empty()) { auto task=std::move(workers.front());workers.pop_front();task(); } }
    void PumpMain() { while(!main.empty()) { auto task=std::move(main.front());main.pop_front();task(); } }
};
}
namespace LibraryImage {
bool LoadRgbaImageFromFile(const std::filesystem::path&,std::vector<unsigned char>& pixels,int& width,int& height) { width=height=1;pixels={7,8,9,255};return true; }
std::vector<unsigned char> ResizePixelsNearest(const std::vector<unsigned char>& pixels,int width,int height,int& outW,int& outH) { outW=width;outH=height;return pixels; }
}
struct LibraryTextureUploadStats {
    int projectDecodeQueued=0,assetDecodeQueued=0,projectUploads=0,assetUploads=0;
    int pendingProjectDecodes=0,pendingAssetDecodes=0,pendingReadyUploads=0,projectCount=0,assetCount=0;
    double elapsedMs=0;bool budgetHit=false;
};
template<class T> struct FailureVector : std::vector<T> {
    bool failNextPush=false;
    void push_back(T value) { if(std::exchange(failNextPush,false)) throw std::bad_alloc();std::vector<T>::push_back(value); }
};
class LibraryManager {
public:
    static LibraryManager* selected;
    static LibraryManager& Get() { return *selected; }
    const auto& GetProjects() const { return m_Projects; }
    const auto& GetAssets() const { return m_Assets; }
    int projectPreviewRequests=0,assetPreviewRequests=0;
    std::shared_ptr<ProjectEntry> lastProjectPreview;
    std::shared_ptr<AssetEntry> lastAssetPreview;
    void RequestProjectPreview(const std::shared_ptr<ProjectEntry>& entry) { ++projectPreviewRequests;lastProjectPreview=entry; }
    void RequestAssetPreview(const std::shared_ptr<AssetEntry>& entry) { ++assetPreviewRequests;lastAssetPreview=entry; }
    std::vector<std::shared_ptr<ProjectEntry>> m_Projects;
    std::vector<std::shared_ptr<AssetEntry>> m_Assets;
    mutable std::mutex m_ProjectsMutex,m_RefreshMutex,m_SignatureMutex;
    std::uintmax_t m_LastLibrarySignature=0;
    std::vector<std::string> m_ProjectThumbnailPriority,m_AssetThumbnailPriority;
    std::size_t m_ProjectThumbnailWarmupCursor=0,m_AssetThumbnailWarmupCursor=0;
    FailureVector<unsigned> m_DeferredTextureDeletions;
    std::filesystem::path m_AssetsPath="/nonexistent-stack-library-refresh-test";
    std::uint64_t m_LibraryRefreshGeneration=0,m_LibrarySignatureGeneration=0;
    std::uint64_t m_ProjectPreviewGeneration=0,m_AssetPreviewGeneration=0;
    Async::TaskState m_LibrarySignatureTaskState=Async::TaskState::Idle;
    struct RefreshSnapshot { Async::TaskState state=Async::TaskState::Idle;std::string currentItem,statusText; } m_LibraryRefreshSnapshot;
    Async::ActivityMetadata MakeActivityMetadata(std::string,bool) { return {}; }
    static bool DecodeImageBytes(const std::vector<unsigned char>& bytes,std::vector<unsigned char>& pixels,int& width,int& height,int& channels) { pixels={static_cast<unsigned char>(bytes.empty()?0:bytes.front()),2,3,255};width=height=1;channels=4;return true; }
    void ReleaseLibraryTexture(unsigned&,bool);
    void ReleaseProjectTextures(std::shared_ptr<ProjectEntry>,bool=false);
    void ReleaseAssetTextures(std::shared_ptr<AssetEntry>,bool=false);
    void ProcessDeferredDeletions(bool=false);
    void ApplyLibraryScanEntries(std::vector<std::shared_ptr<ProjectEntry>>&,std::vector<std::shared_ptr<AssetEntry>>&,std::uintmax_t);
    void CancelLibraryRefreshRequests();
    LibraryTextureUploadStats UploadLibraryTextures(double=2.0);
    void InitializeThumbnail(std::shared_ptr<ProjectEntry>);
    void InitializeAssetThumbnail(std::shared_ptr<AssetEntry>);
    void QueueProjectThumbnailDecode(const std::shared_ptr<ProjectEntry>&);
    void QueueAssetThumbnailDecode(const std::shared_ptr<AssetEntry>&);
    bool ProjectPreviewAcceptance(const std::shared_ptr<ProjectEntry>&,std::uint64_t,std::uint64_t);
    bool AssetPreviewAcceptance(const std::shared_ptr<AssetEntry>&,std::uint64_t,std::uint64_t);
};
LibraryManager* LibraryManager::selected=nullptr;
class LibraryModule {
public:
    std::shared_ptr<ProjectEntry> m_PreviewProject;
    std::shared_ptr<AssetEntry> m_PreviewAsset;
    bool m_ProjectPreviewClosing=false,m_AssetPreviewClosing=false;
    std::string m_RenameTargetFileName;
    void ReconcileRefreshedPreviews();
};
constexpr std::uintmax_t kMaxSynchronousAssetThumbnailBytes=8ull*1024*1024;
'''
methods = method(previews, 'void LibraryModule::ReconcileRefreshedPreviews()')
for source, signatures in [
    (manager, ['void LibraryManager::ProcessDeferredDeletions(']),
    (refresh, ['void LibraryManager::ApplyLibraryScanEntries(', 'void LibraryManager::CancelLibraryRefreshRequests()']),
    (thumbnails, ['void LibraryManager::ReleaseLibraryTexture(', 'void LibraryManager::ReleaseProjectTextures(',
        'void LibraryManager::ReleaseAssetTextures(', 'LibraryTextureUploadStats LibraryManager::UploadLibraryTextures(',
        'void LibraryManager::InitializeThumbnail(', 'void LibraryManager::InitializeAssetThumbnail(',
        'void LibraryManager::QueueProjectThumbnailDecode(', 'void LibraryManager::QueueAssetThumbnailDecode('])]:
    for signature in signatures: methods += method(source, signature)

# Execute the production acceptance prefix without pretending to exercise the
# downstream full-preview graph execution/upload in this CPU fixture.
for kind, entry in [('Project', 'project'), ('Asset', 'asset')]:
    request_start = thumbnails.index('void LibraryManager::Request' + kind + 'Preview(')
    callback_start = thumbnails.index('Async::TaskSystem::Get().PostToMain(', request_start)
    guard_start = thumbnails.index('            if (!' + entry, callback_start)
    guard_end = thumbnails.index('            if (!preview.success', guard_start)
    guard = thumbnails[guard_start:guard_end]
    methods += ('bool LibraryManager::' + kind + 'PreviewAcceptance(const std::shared_ptr<' + kind + 'Entry>& ' + entry + ', std::uint64_t generation, std::uint64_t cancelGeneration) {\n'
        '    bool accepted=false;const auto callback=[&] {\n' + guard + '        accepted=true;\n    };callback();return accepted;\n}\n')

tests = r'''
std::shared_ptr<ProjectEntry> Project(std::string path,std::string id,unsigned texture,std::vector<unsigned char> cover={1,2,3}) {
    auto project=std::make_shared<ProjectEntry>();project->fileName=std::move(path);project->projectId=std::move(id);project->thumbnailTex=texture;project->thumbnailBytes=std::move(cover);return project;
}
int DeleteCount(unsigned texture) { return static_cast<int>(std::count(deletedTextures.begin(),deletedTextures.end(),texture)); }
void TransferAndRetirement() {
    LibraryManager manager;bool unlocked=true,sameContext=true;
    onDelete=[&] { unlocked &= MutexAvailable(manager.m_ProjectsMutex);sameContext &= currentContext==1; };
    auto unchanged=Project("a.stack","id-a",101);unchanged->sourcePreviewTex=102;unchanged->fullPreviewTex=103;unchanged->previewRequestGeneration=4;unchanged->previewTaskState=Async::TaskState::Running;
    auto changed=Project("b.stack","id-b",201);changed->sourcePreviewTex=202;changed->fullPreviewTex=203;
    auto removed=Project("c.stack","id-c",301);
    auto asset=std::make_shared<AssetEntry>();asset->thumbnailTex=401;asset->fullPreviewTex=402;asset->previewRequestGeneration=8;
    manager.m_Projects={unchanged,changed,removed};manager.m_Assets={asset};manager.m_DeferredTextureDeletions.push_back(501);
    auto accepted=Project("a.stack","id-a",0);accepted->projectName="New authoritative metadata";
    auto newCover=Project("b.stack","id-b",0,{9,9,9});
    auto distinctOwner=Project("different-folder/c.stack","id-c",0);
    std::vector<std::shared_ptr<ProjectEntry>> projects{accepted,newCover,distinctOwner};std::vector<std::shared_ptr<AssetEntry>> assets;
    manager.ApplyLibraryScanEntries(projects,assets,77);
    Require(manager.m_Projects[0]==accepted && accepted->thumbnailTex==101 && accepted->projectName=="New authoritative metadata","exact cover transfers only texture into new catalog pointer/metadata");
    Require(newCover->thumbnailTex==0 && distinctOwner->thumbnailTex==0 && accepted->fullPreviewTex==0 && accepted->sourcePreviewTex==0,"changed cover, other path and all full previews are not reused");
    Require(unchanged->retiredFromCatalog && changed->retiredFromCatalog && removed->retiredFromCatalog && asset->retiredFromCatalog,"every replaced/removed entry is retired even with external shared owners");
    Require(unchanged->previewRequestGeneration==5 && asset->previewRequestGeneration==9 && unchanged->previewTaskState==Async::TaskState::Idle,"retirement invalidates late fullscreen request generations");
    Require(unchanged->thumbnailTex==0 && unchanged->fullPreviewTex==0 && asset->thumbnailTex==0 && manager.m_LastLibrarySignature==77,"old runtime handles detached and scan accepted");
    Require(deletedTextures.empty() && manager.m_DeferredTextureDeletions.size()==9,"apply does not delete GL names in publication callback");
    while(!manager.m_DeferredTextureDeletions.empty()) {
        auto before=deletedTextures.size();manager.ProcessDeferredDeletions();
        Require(deletedTextures.size()>before && deletedTextures.size()-before<=8,"one frame retires at most eight actual GL names");
    }
    for(unsigned name:{102u,103u,201u,202u,203u,301u,401u,402u,501u}) Require(DeleteCount(name)==1,"each discarded name deleted exactly once");
    Require(DeleteCount(101)==0 && unlocked && sameContext,"transferred thumbnail survives and all deletion runs unlocked on UI context");
    manager.ReleaseProjectTextures(accepted);Require(DeleteCount(101)==1,"transferred texture retains exactly one live owner");onDelete={};
    LibraryManager different;different.m_Projects={Project("same.stack","old-id",601)};projects={Project("same.stack","replacement-id",0)};different.ApplyLibraryScanEntries(projects,assets,1);Require(different.m_Projects[0]->thumbnailTex==0,"same path/cover cannot transfer across project identity");different.ProcessDeferredDeletions(true);
}
void LateThumbnailCompletion() {
    LibraryManager manager;auto& tasks=Async::TaskSystem::Get();
    auto project=Project("late.stack","late-id",0);auto asset=std::make_shared<AssetEntry>();asset->fileName="late.png";
    manager.m_Projects={project};manager.m_Assets={asset};
    manager.QueueProjectThumbnailDecode(project);manager.QueueAssetThumbnailDecode(asset);tasks.RunWorkers();
    Require(tasks.main.size()==2,"two real production worker completions are waiting");
    std::vector<std::shared_ptr<ProjectEntry>> projects{Project("late.stack","late-id",0)};std::vector<std::shared_ptr<AssetEntry>> assets;
    manager.ApplyLibraryScanEntries(projects,assets,2);tasks.PumpMain();
    Require(!manager.ProjectPreviewAcceptance(project,0,0) && !manager.AssetPreviewAcceptance(asset,0,0),"actual fullscreen acceptance guards reject retired entries and old request generations");
    Require(manager.ProjectPreviewAcceptance(manager.m_Projects.front(),0,0),"current fullscreen acceptance guard remains available");
    Require(project->thumbnailPixels.empty() && asset->thumbnailPixels.empty() && project->thumbnailDecodeState==Async::TaskState::Idle && asset->thumbnailDecodeState==Async::TaskState::Idle,"retired thumbnail callbacks cannot publish orphaned decoded pixels");
    manager.QueueProjectThumbnailDecode(project);manager.QueueAssetThumbnailDecode(asset);Require(tasks.workers.empty(),"retired entry cannot start fresh decode work");
    auto current=manager.m_Projects.front();manager.QueueProjectThumbnailDecode(current);tasks.RunWorkers();tasks.PumpMain();Require(current->thumbnailDecodeState==Async::TaskState::Ready && !current->thumbnailPixels.empty(),"current catalog still accepts its own asynchronous pixels");
}
void GenerationAndPumpBounds() {
    LibraryManager manager;auto& tasks=Async::TaskSystem::Get();
    for(unsigned i=0;i<25;++i) manager.m_Projects.push_back(Project(std::to_string(i),std::to_string(i),700+i));
    std::vector<std::shared_ptr<ProjectEntry>> projects{Project("fresh.stack","fresh",0)};std::vector<std::shared_ptr<AssetEntry>> assets;
    manager.ApplyLibraryScanEntries(projects,assets,3);
    auto current=manager.m_Projects[0];current->thumbnailDecodeState=Async::TaskState::Ready;current->thumbnailPixels={1,2,3,255};current->thumbnailPixelWidth=current->thumbnailPixelHeight=1;
    const int previousUploads=uploads;
    const auto paused=manager.UploadLibraryTextures();Require(paused.budgetHit && uploads==previousUploads && current->thumbnailTex==0 && tasks.workers.empty(),"no second thumbnail-cache generation while retirement backlog drains");
    projects={Project("next.stack","next",0)};const auto backlog=manager.m_DeferredTextureDeletions.size();manager.ApplyLibraryScanEntries(projects,assets,4);Require(manager.m_DeferredTextureDeletions.size()==backlog,"repeated refresh cannot accumulate additional thumbnail-cache generations");
    tasks.PostToMain([&] { manager.ProcessDeferredDeletions(); });tasks.PumpMain();
    Require(!manager.m_DeferredTextureDeletions.empty() && tasks.main.empty(),"unlimited task pump cannot self-enqueue and drain the entire retirement queue");
    manager.ProcessDeferredDeletions(true);current=manager.m_Projects[0];current->thumbnailDecodeState=Async::TaskState::Ready;current->thumbnailPixels={1,2,3,255};current->thumbnailPixelWidth=current->thumbnailPixelHeight=1;
    Require(manager.UploadLibraryTextures().projectUploads==1 && current->thumbnailTex!=0,"normal visual upload resumes after previous names retire");manager.ReleaseProjectTextures(current);
}
void QueuedRefreshThenFullscreenOpen() {
    LibraryManager manager;LibraryManager::selected=&manager;LibraryModule ui;
    auto old=Project("selected.stack","selected-id",950);old->fullPreviewTex=951;manager.m_Projects={old};
    ui.m_PreviewProject=old; // The scan was queued before this fullscreen selection opened.
    std::vector<std::shared_ptr<ProjectEntry>> projects{Project("selected.stack","selected-id",0)};std::vector<std::shared_ptr<AssetEntry>> assets;
    manager.ApplyLibraryScanEntries(projects,assets,5);ui.ReconcileRefreshedPreviews();
    Require(ui.m_PreviewProject==manager.m_Projects[0] && ui.m_PreviewProject!=old && ui.m_PreviewProject->thumbnailTex==950 && ui.m_PreviewProject->fullPreviewTex==0,"same-frame reconciliation adopts current selection and never stale full-preview names");
    Require(manager.projectPreviewRequests==1 && manager.lastProjectPreview==ui.m_PreviewProject && !ui.m_ProjectPreviewClosing,"current selected project re-requests its own full-quality preview once");
    ui.ReconcileRefreshedPreviews();Require(manager.projectPreviewRequests==1,"unchanged selected pointer cannot re-request every frame");
    projects={Project("selected.stack","different-id",0)};manager.ApplyLibraryScanEntries(projects,assets,6);ui.ReconcileRefreshedPreviews();
    Require(!ui.m_PreviewProject && ui.m_ProjectPreviewClosing && manager.projectPreviewRequests==1,"same-path replacement identity closes unavailable selection rather than opening another project");
    auto oldAsset=std::make_shared<AssetEntry>();oldAsset->fileName="selected.png";oldAsset->fullPreviewTex=952;manager.m_Assets={oldAsset};ui.m_PreviewAsset=oldAsset;
    projects.clear();assets={std::make_shared<AssetEntry>()};assets[0]->fileName="selected.png";manager.ApplyLibraryScanEntries(projects,assets,7);ui.ReconcileRefreshedPreviews();
    Require(ui.m_PreviewAsset==manager.m_Assets[0] && ui.m_PreviewAsset!=oldAsset && ui.m_PreviewAsset->fullPreviewTex==0 && manager.assetPreviewRequests==1,"selected asset retargets and requests current file without obsolete texture IDs");
    projects.clear();assets.clear();manager.ApplyLibraryScanEntries(projects,assets,8);ui.ReconcileRefreshedPreviews();Require(!ui.m_PreviewAsset && ui.m_AssetPreviewClosing && manager.assetPreviewRequests==1,"removed asset selection closes safely");manager.ProcessDeferredDeletions(true);
}
void CloseDrainAndQueueFailure() {
    LibraryManager manager;bool unlocked=true,sameContext=true;
    onDelete=[&] { unlocked &= MutexAvailable(manager.m_ProjectsMutex);sameContext &= currentContext==1; };
    for(unsigned i=0;i<19;++i) manager.m_DeferredTextureDeletions.push_back(800+i);
    manager.m_LibraryRefreshGeneration=9;manager.CancelLibraryRefreshRequests();Require(manager.m_DeferredTextureDeletions.empty() && manager.m_LibraryRefreshGeneration==10 && unlocked && sameContext,"close drains all names on still-live context before task/context teardown");
    manager.m_DeferredTextureDeletions.failNextPush=true;unsigned name=901;manager.ReleaseLibraryTexture(name,true);Require(name==0 && DeleteCount(901)==1 && unlocked && sameContext,"retirement allocation failure releases ownership outside lock without dropping a name");onDelete={};
}
int main() {
    try { TransferAndRetirement();LateThumbnailCompletion();GenerationAndPumpBounds();QueuedRefreshThenFullscreenOpen();CloseDrainAndQueueFailure();std::cout<<"library refresh lifetime and retirement: PASS\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
'''
args.output.write_text(pre + methods + tests)
