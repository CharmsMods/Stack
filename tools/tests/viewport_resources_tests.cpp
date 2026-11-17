#include "Renderer/ViewportSectionIndex.h"
#include "Renderer/Internal/GraphTransientRetentionPolicy.h"
#ifdef _WIN32
#include "Renderer/ViewportSectionStore.h"
#endif
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace Stack::Renderer;
int checks = 0;
void Require(bool pass, const char* message) {
    ++checks;
    if (!pass) throw std::runtime_error(message);
}
void GeometryAndCoverage() {
    ViewportSectionIndex index(2049, 1025);
    Require(index.Count() == 6, "partial row/column count");
    Require(index.Bounds(5) == ImageRegion{2049,1025,2048,1024,1,1}, "last core pixel");
    Require(index.Raster(5) == ImageRegion{2049,1025,2044,1020,5,5}, "last raster guard");
    Require(index.Raster(0) == ImageRegion{2049,1025,0,0,1028,1025}, "first raster guard");
    Require(!index.Bounds(-1).Valid() && !index.Raster(6).Valid(), "invalid index geometry");
    Require(!index.Has(-1) && !index.Has(6) && !index.Complete(), "empty index readiness");
    Require(!index.Publish(-1) && !index.Publish(6), "invalid publication");
    Require(index.Publish(0) && !index.Publish(0), "publish once");
    Require(index.Covers({2049,1025,0,0,1024,1024}), "exact core boundary");
    Require(!index.Covers({2049,1025,1023,0,2,1}), "one-pixel crossing needs adjacent core");
    Require(!index.Covers({2048,1025,0,0,1,1}) && !index.Covers({}), "foreign/empty coverage rejected");
    for (int i=1; i<index.Count(); ++i) Require(index.Publish(i), "publish remaining cores");
    Require(index.Complete() && index.Covers(WholeImageRegion(2049,1025)), "whole coverage complete");
    Require(index.NextMissing(WholeImageRegion(2049,1025),true) == -1, "completion stops work");
    ViewportSectionIndex other(2049,1025);
    Require(!other.Has(0) && !other.Complete(), "independent content revision");
    for (const auto extent : std::array<std::array<int,2>,4>{{{{0,1}},{{1,-1}},{{1048577,1}},{{65536,65536}}}}) {
        bool rejected = false;
        try { ViewportSectionIndex invalid(extent[0],extent[1]); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "invalid image extent rejected before allocation");
    }
    ViewportSectionIndex large(1048576,2047);
    Require(large.Raster(large.Count()-1).Valid(), "maximum supported coordinate arithmetic");
}
void Scheduling() {
    ViewportSectionIndex index(5120,3072);
    const auto all = WholeImageRegion(5120,3072);
    Require(index.NextMissing(all,false) == 7, "center first, not top-left scan");
    const ImageRegion right{5120,3072,4096,1024,1024,1024};
    Require(index.NextMissing(right,false) == 9, "new camera reprioritizes visible work");
    Require(index.Publish(9), "cache right-hand section");
    Require(index.NextMissing(right,false) == -1, "no redundant graph execution");
    Require(index.NextMissing(right,true) == 0, "whole completion continues offscreen");
    Require(index.Prefer(9,7,all), "reuse completed section before recomputation");
    Require(index.Prefer(7,0,all), "center precedence for equally uncached sections");
    Require(!index.Prefer(-1,7,all) && index.Prefer(7,-1,all), "candidate sentinels");
    Require(index.NextMissing({5120,3073,0,0,1,1},false) == -1, "mismatched camera cannot select foreground");
}
void Publication() {
    constexpr int count = 32;
    ViewportSectionIndex index(count*1024,1);
    std::array<int,count> payload{};
    std::thread writer([&] {
        for (int i=0; i<count; ++i) {
            payload[i] = i+100;
            index.Publish(i);
        }
    });
    bool payloadValid = true;
    for (int i=0; i<count; ++i) {
        while (!index.Has(i)) std::this_thread::yield();
        payloadValid &= payload[i] == i+100;
    }
    writer.join();
    Require(payloadValid, "acquire readiness publishes immutable payload");
    Require(index.Complete(), "completion after all successful publications");
    Require(!index.Publish(0) && index.Complete(), "duplicate does not overcount completion");
}
void RetentionAdmission() {
    using GraphExecution::CanRetainGraphTransientTarget;
    constexpr std::uint64_t mib=1024ull*1024;
    Require(CanRetainGraphTransientTarget(1*mib,0,8*mib,0,8), "small interactive allocations are reusable");
    Require(CanRetainGraphTransientTarget(8*mib,0,8*mib,7,8), "exact byte and count boundary");
    Require(!CanRetainGraphTransientTarget(1,8*mib,8*mib,0,8), "no byte overshoot");
    Require(!CanRetainGraphTransientTarget(1,9*mib,8*mib,0,8), "over-budget resident cannot underflow");
    Require(!CanRetainGraphTransientTarget(1,0,8*mib,8,8), "object-count bound before retirement");
    Require(!CanRetainGraphTransientTarget(0,0,8*mib,0,8), "zero allocation rejected");
    Require(!CanRetainGraphTransientTarget(1,0,0,0,8), "zero allowance rejects retention");
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    Require(!CanRetainGraphTransientTarget(2,maximum-1,maximum,0,8), "overflow-safe near maximum");
    Require(CanRetainGraphTransientTarget(1,maximum-1,maximum,0,8), "maximum exact fit");
}
#ifdef _WIN32
void NativeStorage() {
    for (const std::uint64_t allowance : {0ull,1024ull*1024}) {
        ViewportSectionStore store(8,8,allowance);
        std::vector<float> sdr(8*8*4,.25f), hdr(8*8*4,4.f), loaded, loadedHdr;
        std::string error;
        Require(store.Store(0,sdr,hdr,error), "native RAM/disk store");
        Require(store.Complete() && store.Has(0), "publish stored SDR/HDR pair");
        Require(store.Load(0,loaded,loadedHdr,error) && loaded==sdr && loadedHdr==hdr, "lossless native round trip");
        Require((store.DiskBytes()>0) == (allowance==0), "explicit RAM/disk allowance");
        Require(store.Store(0,std::vector<float>(sdr.size(),9.f),{},error), "duplicate store idempotent");
        Require(store.Load(0,loaded,loadedHdr,error) && loaded==sdr && loadedHdr==hdr, "published payload immutable");
    }
    // Capacity, including an empty HDR vector's allocation, is retained by move.
    ViewportSectionStore tight(1,1,64);
    std::vector<float> sdr(4,.5f), hdr;
    hdr.reserve(1024);
    std::string error;
    Require(tight.Store(0,std::move(sdr),std::move(hdr),error) && tight.DiskBytes()==16, "capacity counted, not just size");
    ViewportSectionStore incomplete(1,1);
    Require(!incomplete.Store(0,{1.f},{},error) && !incomplete.Has(0) && !incomplete.Complete(), "invalid payload never published");
}
#endif
}
int main() {
    try {
        GeometryAndCoverage(); Scheduling(); Publication(); RetentionAdmission();
#ifdef _WIN32
        NativeStorage();
#else
        std::cout << "Win32 native-storage integration not run on this platform.\n";
#endif
        std::cout << checks << " viewport-resource checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Viewport resources FAILED: " << error.what() << '\n';
        return 1;
    }
}
