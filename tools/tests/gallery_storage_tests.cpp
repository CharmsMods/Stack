#include "Raw/GalleryTileCache.h"
#include "Async/TaskGroup.h"
#include "Storage/ScratchBudget.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
std::filesystem::path root;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
// The path provider is the boundary double. Cache encoding, persistence,
// admission, eviction and task ownership are the production implementations.
namespace AppPaths {const std::filesystem::path& GetCacheDirectory(){return root;}}
int main() {
    using namespace Stack;
    try {
        root=std::filesystem::temp_directory_path()/("stack-gallery-storage-"+std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        std::ofstream(root/"original.keep")<<"not a cache entry";
        GalleryTileCache::Initialize();GalleryTileCache::SetLimitMiB(1);
        std::vector<unsigned char> pixels(512*512*4);
        std::uint32_t random=12345;
        for(auto& value:pixels){random^=random<<13;random^=random>>17;random^=random<<5;value=static_cast<unsigned char>(random);}
        // An incompressible RGBA tile is just above 1 MiB: reject before write.
        auto key=GalleryTileCache::Key(root/"image.raw",123,456,512);
        GalleryTileCache::Write(key,pixels,512,512);
        Require(GalleryTileCache::Status().bytes<=1024*1024,"Tile cache exceeded its byte limit");
        pixels.resize(384*384*4);
        auto first=GalleryTileCache::Key(root/"first.raw",123,456,512);
        auto second=GalleryTileCache::Key(root/"second.raw",123,456,512);
        GalleryTileCache::Write(first,pixels,384,384);
        std::vector<unsigned char> decoded;int w=0,h=0;
        Require(GalleryTileCache::Read(first,decoded,w,h)&&decoded==pixels&&w==384&&h==384,"Local tile pixels changed");
        GalleryTileCache::Write(second,pixels,384,384);
        Require(GalleryTileCache::Status().bytes<=1024*1024 && GalleryTileCache::Status().entries==1,"Admission did not evict before writing");
        Require(!GalleryTileCache::Read(first,decoded,w,h)&&GalleryTileCache::Read(second,decoded,w,h),"Evicted the newest tile");
        Require(!GalleryTileCache::Read(GalleryTileCache::Key(root/"second.raw",123,457,512),decoded,w,h),"A replaced source reused stale pixels");
        GalleryTileCache::SetLimitMiB(0);GalleryTileCache::Write(first,pixels,384,384);
        Require(GalleryTileCache::Status().bytes==0&&std::filesystem::exists(root/"original.keep"),"Disabled cache wrote data or removed an original");

        Async::TaskSystem::Get().Initialize();Async::TaskGroup owner;
        std::promise<void> started,release,opened,latest;
        auto blocked=release.get_future().share();auto running=started.get_future();
        auto firstCancel=std::make_shared<std::atomic<bool>>(false);
        Require(owner.SubmitThumbnail(firstCancel,[&,blocked]{started.set_value();blocked.wait();}),"Could not start thumbnail lane");
        bool queueValid=running.wait_for(std::chrono::seconds(3))==std::future_status::ready;
        std::vector<std::shared_ptr<std::atomic<bool>>> canceled;
        std::atomic<unsigned> staleRuns{0};
        for(int i=0;i<24;++i){auto flag=std::make_shared<std::atomic<bool>>(false);canceled.push_back(flag);queueValid=owner.SubmitThumbnail(flag,[&]{++staleRuns;})&&queueValid;}
        queueValid=!owner.SubmitThumbnail(std::make_shared<std::atomic<bool>>(false),[]{})&&queueValid;
        for(auto& flag:canceled)flag->store(true);
        auto latestFuture=latest.get_future(),openFuture=opened.get_future();
        queueValid=owner.SubmitThumbnail(std::make_shared<std::atomic<bool>>(false),[&]{latest.set_value();})&&queueValid;
        queueValid=owner.SubmitHighPriority([&]{opened.set_value();})&&queueValid;
        const bool openReady=openFuture.wait_for(std::chrono::seconds(3))==std::future_status::ready;
        release.set_value();
        const bool latestReady=latestFuture.wait_for(std::chrono::seconds(3))==std::future_status::ready;
        owner.Stop();Async::TaskSystem::Get().Shutdown();
        Require(queueValid&&openReady&&latestReady&&staleRuns==0&&owner.IsIdle(),"Queue bounds, latest demand, foreground lane or owner release failed");
        std::cout<<"Gallery cache admission, eviction, identity, queue replacement and foreground isolation passed.\n";
        // The cache owner holds a lock until process exit. The small isolated
        // test directory is intentionally left for the harness to remove.
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
