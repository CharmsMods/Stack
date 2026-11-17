#include "Renderer/ViewportSectionStore.h"
#include "Renderer/ViewportWorkloadTiming.h"
#include <atomic>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace Stack::Renderer;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool EqualBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}
std::vector<float> PixelsFor(const ImageRegion& r) {
    const std::uint32_t bits[] = {0, 0x80000000u, 1, 0x3f800000u, 0xc0000000u,
        0x7f800000u, 0xff800000u, 0x7fc12345u};
    std::vector<float> out(std::size_t(r.width) * r.height * 4);
    for (std::size_t i = 0; i < out.size(); ++i) std::memcpy(&out[i], &bits[i % 8], sizeof(float));
    return out;
}
void StoreChecked(ViewportSectionStore& store, int index, std::vector<float> pixels,
    std::vector<float> hdr, std::string& error) {
    if (!store.Store(index,std::move(pixels),std::move(hdr),error)) throw std::runtime_error(error);
}
void TestIndex() {
    ViewportSectionIndex index(2051, 1027);
    const ImageRegion views[] = {{2051,1027,2048,0,3,4}, {2051,1027,1023,1023,3,3},
        WholeImageRegion(2051,1027), {2051,1027,0,0,1024,1024}};
    std::vector<bool> ready(index.Count(), false);
    const auto check = [&] {
        for (const auto& view : views) {
            int shown = -1, background = -1;
            for (int i = 0; i < index.Count(); ++i) {
                if (ready[i]) continue;
                if (background < 0) background = i;
                const auto b = index.Bounds(i);
                if (shown < 0 && b.x < view.x + view.width && view.x < b.x + b.width &&
                    b.y < view.y + view.height && view.y < b.y + b.height) shown = i;
            }
            Require(index.Covers(view) == (shown < 0), "Indexed coverage differs from core intersection");
            Require(index.NextMissing(view,false) == shown, "Visible missing order changed");
            Require(index.NextMissing(view,true) == (shown < 0 ? background : shown), "Background cursor skipped work");
        }
    };
    check();
    for (int i : {2,5,1,0,4,3}) { ready[i] = true; index.Publish(i); index.Publish(i); check(); }
    Require(index.Complete(), "Duplicate publication broke completion");
    Require(!index.Covers({}) && !index.Covers(WholeImageRegion(2048,1027)), "Incompatible coverage accepted");
    Require(!index.Has(-1) && !index.Has(index.Count()), "Invalid index accepted");
    bool rejected = false;
    try { ViewportSectionIndex invalid(INT_MAX,INT_MAX); } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected, "Oversized native grid accepted");
}
void TestStore(bool disk) {
    ViewportSectionStore store(2051,4,disk ? 0 : 1024*1024);
    const ImageRegion right{2051,4,2048,0,3,4};
    Require(store.NextMissing(right,true) == 2, "Visible work lost priority");
    std::string error;
    Require(!store.Store(0,{1},{},error) && !store.Has(0), "Invalid payload was published");
    for (int i : {2,0,1}) {
        error.clear();
        auto pixels = PixelsFor(store.Raster(i));
        auto hdr = pixels; std::reverse(hdr.begin(),hdr.end());
        const auto expected = pixels, expectedHdr = hdr;
        StoreChecked(store,i,std::move(pixels),std::move(hdr),error);
        auto loaded = store.LoadShared(i,error);
        Require(loaded && EqualBits(loaded->pixels,expected) && EqualBits(loaded->hdr,expectedHdr), "Native float bits changed");
        std::vector<float> copied, copiedHdr;
        Require(store.Load(i,copied,copiedHdr,error) && EqualBits(copied,expected) && EqualBits(copiedHdr,expectedHdr), "Copy API changed pixels");
        Require(store.Covers(right), "Ready view was hidden by later background work");
    }
    Require(store.Complete() && store.NextMissing(right,true) == -1, "Complete image rescheduled");
    Require((store.DiskBytes() > 0) == disk, "RAM allowance was not honored");
    ViewportSectionStore other(2051,4);
    Require(!other.Has(2), "New revision inherited old pixels");
    std::vector<float> before{7}, beforeHdr{9};
    Require(!other.Load(2,before,beforeHdr,error) && before == std::vector<float>{7} &&
        beforeHdr == std::vector<float>{9}, "Failed load replaced the caller's pixels");
    ViewportSectionStore sdr(7,3,disk ? 0 : 1024*1024);
    auto sdrPixels = PixelsFor(sdr.Raster(0)); error.clear();
    StoreChecked(sdr,0,sdrPixels,{},error);
    auto sdrRead = sdr.LoadShared(0,error);
    Require(sdrRead && sdrRead->hdr.empty() && EqualBits(sdrRead->pixels,sdrPixels), "SDR acquired an invented HDR plane");
}
void TestLease() {
    const auto before = ViewportSectionStore::RetainedRamBytes();
    auto store = std::make_unique<ViewportSectionStore>(16,16);
    auto pixels = PixelsFor(store->Raster(0)); pixels.reserve(pixels.size() * 2);
    const auto bytes = pixels.capacity() * sizeof(float);
    std::string error;
    StoreChecked(*store,0,std::move(pixels),{},error);
    auto first = store->LoadShared(0,error), second = store->LoadShared(0,error);
    Require(first && first == second && first->pixels.data() == second->pixels.data(), "RAM hit duplicated native pixels");
    Require(ViewportSectionStore::RetainedRamBytes() == before + bytes, "Budget did not charge vector capacity once");
    store.reset();
    Require(ViewportSectionStore::RetainedRamBytes() == before + bytes && !first->pixels.empty(), "Store retirement freed a live upload lease");
    first.reset();
    Require(ViewportSectionStore::RetainedRamBytes() == before + bytes, "Intermediate borrower released the budget");
    second.reset();
    Require(ViewportSectionStore::RetainedRamBytes() == before, "Last borrower leaked the budget");
}
void TestCapacity() {
    ViewportSectionStore store(16,16,16*16*4*sizeof(float));
    auto pixels = PixelsFor(store.Raster(0)); pixels.reserve(pixels.size() * 2);
    std::string error;
    StoreChecked(store,0,std::move(pixels),{},error);
    Require(store.DiskBytes() > 0, "Spare vector capacity bypassed the RAM allowance");
}
void TestPublication() {
    ViewportSectionStore store(8192,8,0);
    std::atomic<bool> start{false}, finished{false};
    auto writer = std::async(std::launch::async,[&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        std::string error;
        for (int i : {7,0,6,1,5,2,4,3}) {
            auto pixels = PixelsFor(store.Raster(i));
            StoreChecked(store,i,std::move(pixels),{},error);
        }
        finished.store(true,std::memory_order_release);
    });
    start.store(true,std::memory_order_release);
    std::size_t queries = 0;
    do {
        (void)store.Covers({8192,8,7168,0,1024,8});
        (void)store.NextMissing({8192,8,7168,0,1024,8},true);
        (void)store.Complete(); (void)store.DiskBytes(); ++queries;
    } while (!finished.load(std::memory_order_acquire) && writer.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
    writer.get();
    Require(queries > 0 && store.Complete(), "Concurrent publication did not drain");
    for (int i = 0; i < store.Count(); ++i) {
        std::string error;
        auto payload = store.LoadShared(i,error);
        Require(payload && EqualBits(payload->pixels,PixelsFor(store.Raster(i))), "Published payload was partial");
    }
}
void TestTiming() {
    ViewportWorkloadTiming timing;
    const auto start = ViewportWorkloadTiming::Clock::now();
    timing.Record(1,1000000,0,10,false,start);
    timing.Record(1,2000000,0,12,false,start);
    timing.Record(1,3000000,0,14,false,start);
    timing.Record(1,3000000,0,14,false,start + std::chrono::seconds(20));
    timing.Record(1,3000000,0,28,false,start + std::chrono::seconds(31));
    const auto estimate = timing.Estimate(1,1000000,0,start + std::chrono::seconds(31));
    Require(estimate.costKnown && std::abs(estimate.predictedMs - 14) < 1e-6,
        "Expired fit survived rejected outlier; shrinking falsely promised old savings");
    Require(!timing.Estimate(1,1000000,0,start + std::chrono::seconds(51)).costKnown,
        "Rejected outlier refreshed old evidence");
    timing.Record(1,3000000,0,28,false,start + std::chrono::seconds(32));
    timing.Record(1,3000000,0,28,false,start + std::chrono::seconds(33));
    Require(timing.Estimate(1,3000000,0,start + std::chrono::seconds(33)).predictedMs == 28,
        "Sustained load change no longer relearns");
    timing.Reset();
    for (const auto& p : {std::array<int,3>{1,2,13},{2,2,15},{1,4,21},{2,4,23}})
        timing.Record(2,p[0]*1000000,p[1]*1000000,p[2],false,start);
    Require(std::abs(timing.Estimate(2,1500000,3000000,start).predictedMs - 18) < 1e-6,
        "Compatible regional/complete interpolation changed");
}
void TestPolicy() {
    ViewportPreviewPolicyInput input{2048,128,2048,60};
    std::vector<ViewportPreviewCandidate> plans{{256,5,true,true,100},
        {1024,12,true,true,300},{2048,80,true,true,600}};
    Require(SelectViewportPreviewCandidate(input,plans).edge == 1024, "Normal FPS choice changed");
    input.evaluationEdge = 2048;
    auto selected = SelectViewportPreviewCandidate(input,plans);
    Require(selected.edge == 2048 && selected.limit == ViewportPreviewLimit::FixedWork,
        "Actual slow plan retained the preferred fast plan's explanation");
    input.preserveDetail = true;
    Require(SelectViewportPreviewCandidate(input,plans).limit == ViewportPreviewLimit::DetailFloor,
        "Actual detail-limited plan has the wrong explanation");
    plans[2].costKnown = false;
    Require(SelectViewportPreviewCandidate(input,plans).limit == ViewportPreviewLimit::Learning,
        "Unmeasured actual plan claimed measured performance");
    input.memoryBudgetBytes = 200;
    Require(SelectViewportPreviewCandidate(input,plans).edge == 256, "Evaluation bypassed memory admission");
    input.memoryBudgetBytes = 50;
    Require(SelectViewportPreviewCandidate(input,plans).edge == 0, "No-memory case invented an admitted plan");
}
}
int main(int argc, char** argv) {
    try {
        Require(argc == 2,"Supply one focused case name");
        const std::string name = argv[1];
        if (name == "index") TestIndex();
        else if (name == "ram") TestStore(false);
        else if (name == "disk") TestStore(true);
        else if (name == "lease") TestLease();
        else if (name == "capacity") TestCapacity();
        else if (name == "publication") TestPublication();
        else if (name == "timing") TestTiming();
        else if (name == "policy") TestPolicy();
        else throw std::runtime_error("Unknown case");
        std::cout << "PASS " << name << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
