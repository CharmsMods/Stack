#include "App/UiPerformanceProbe.h"
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <vector>
#include <cstdlib>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#endif

namespace Stack::UiPerformance::Probe {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto count = static_cast<std::size_t>(Visual::Count);
struct Pending {
    bool active = false;
    std::uint64_t baseline = 0, required = 0;
    Clock::time_point started;
    std::string scenario, action, dispatch;
};
struct Record { std::string scenario, action, dispatch; double milliseconds; bool changed; };
struct ImageEvent { const char* kind; std::uint64_t generation, revision; };
struct Invalidation { const char* kind; std::array<void*,12> addresses {}; unsigned short count = 0; };
struct RefinementEvent { std::uint64_t content,accepted; bool native,covered; int logical,storage; };
struct AcceptedImageTag { std::uint64_t generation = 0, revision = 0; bool settledKernels = false; };
struct State {
    bool active = false;
    int node = -1;
    const void* editor = nullptr;
    std::filesystem::path output;
    std::array<Observation, count> last {}, current {};
    std::array<Pending, count> pending {};
    std::array<AcceptedImageTag,64> images {};
    std::size_t nextImage = 0;
    std::vector<ImageEvent> imageEvents;
    std::uint64_t observedImage = 0;
    unsigned int frame = 0;
    std::vector<Invalidation> invalidations;
    std::vector<RefinementEvent> refinements;
    std::vector<ViewportTiming> viewportTimings;
    std::vector<Record> records;
};
State& Get() { static State state; return state; }
}
void Enable(const std::filesystem::path& output) {
    auto& s = Get(); s = {}; s.active = true; s.output = output; s.records.reserve(512);
}
bool Active() { return Get().active; }
void BeginFrame() { if (Active()) { Get().current = {}; ++Get().frame; } }
void Drawn(Visual visual, std::uint64_t key, Region region) {
    if (Active()) Get().current[static_cast<std::size_t>(visual)] = {key, region, true};
}
void AcceptedImage(std::uint64_t generation,std::uint64_t revision,const void* owner,bool settledKernels) {
    if (Active()) { auto& s=Get();
        if (owner==s.editor) s.images[s.nextImage++%s.images.size()]={generation,revision,settledKernels};
        if(s.imageEvents.size()<512)s.imageEvents.push_back({owner==s.editor?"accepted_primary":"accepted_background",generation,revision}); }
}
void ImageDrawn(std::uint64_t generation,Region region) {
    if (!Active()) return;
    Drawn(Visual::ImageCamera,Key(region.x,region.y,region.width),region);
    if(Get().observedImage!=generation) {
        auto& s=Get();s.observedImage=generation;
        if(s.imageEvents.size()<512)s.imageEvents.push_back({"drawn",generation,0});
    }
    for (const auto& image:Get().images) if (image.generation==generation && image.revision) {
        Drawn(Visual::Image,image.revision,region);
        Get().current[static_cast<std::size_t>(Visual::Image)].settledKernels=image.settledKernels;
        return;
    }
}
Observation Last(Visual visual) { return Active() ? Get().last[static_cast<std::size_t>(visual)] : Observation{}; }
void WatchNode(int nodeId) { Get().node = nodeId; }
void WatchEditor(const void* editor) { Get().editor = editor; Get().images = {}; Get().nextImage = 0; }
int WatchedNode() { return Active() ? Get().node : -1; }
std::uint64_t Key(float a, float b, float c) {
    std::uint32_t bits[3]; const float values[]{a,b,c}; std::memcpy(bits,values,sizeof(bits));
    std::uint64_t hash = 1469598103934665603ull;
    for (auto value : bits) hash = (hash ^ value) * 1099511628211ull;
    return hash;
}
std::uint64_t Key(const std::string& value) { return std::hash<std::string>{}(value); }
bool BeginInteraction(Visual visual, const std::string& scenario, const std::string& action, const char* dispatch) {
    if (!Active()) return false;
    auto& s = Get(); const auto index = static_cast<std::size_t>(visual);
    auto& pending = s.pending[index];
    if (pending.active || !s.last[index].valid || s.records.size() >= 512) return false;
    pending = {true,s.last[index].key,0,Clock::now(),scenario,action,dispatch}; return true;
}
void RequireKey(Visual visual, std::uint64_t key) {
    Get().pending[static_cast<std::size_t>(visual)].required = key;
    if(visual==Visual::Image && Get().imageEvents.size()<512)Get().imageEvents.push_back({"required",0,key});
}
void Invalidated(const char* kind,const void* owner) {
#if defined(_WIN32)
    if(!Active() || owner!=Get().editor || Get().frame<60 || Get().invalidations.size()>=8)return;
    const char* enabled=std::getenv("STACK_UI_PERF_STACKS");if(!enabled || enabled[0]!='1')return;
    Invalidation event;event.kind=kind;event.count=CaptureStackBackTrace(1,static_cast<DWORD>(event.addresses.size()),event.addresses.data(),nullptr);
    Get().invalidations.push_back(event);
#else
    (void)kind;
    (void)owner;
#endif
}
void Refinement(std::uint64_t content,std::uint64_t accepted,bool native,bool covered,int logicalWidth,int storageWidth,const void* owner) {
    if(Active() && owner==Get().editor && Get().refinements.size()<16)
        Get().refinements.push_back({content,accepted,native,covered,logicalWidth,storageWidth});
}
void Presented() {
    if (!Active()) return;
    auto& s = Get(); const auto now = Clock::now();
    for (std::size_t index = 0; index < count; ++index) {
        auto& pending = s.pending[index]; const auto& observation = s.current[index];
        if (pending.active) {
            const double elapsed = std::chrono::duration<double,std::milli>(now-pending.started).count();
            const bool changed = observation.valid && observation.key != pending.baseline &&
                (!pending.required || (index==static_cast<std::size_t>(Visual::Image)
                    ? observation.key>=pending.required : observation.key==pending.required));
            if (changed || elapsed > 2000) {
                s.records.push_back({pending.scenario,pending.action,pending.dispatch,elapsed,changed});
                pending.active = false;
            }
        }
        if (observation.valid) s.last[index] = observation;
        else s.last[index].valid = false;
    }
}
void CompletedViewport(const ViewportTiming& timing, const void* owner) {
    if (Active() && owner == Get().editor && Get().viewportTimings.size() < 512)
        Get().viewportTimings.push_back(timing);
}
void Finish() {
    if (!Active()) return;
    auto& s = Get(); std::filesystem::create_directories(s.output);
    std::ofstream out(s.output / "interactions.csv");
    out << "scenario,action,dispatch,response_ms,status\n" << std::fixed << std::setprecision(6);
    for (const auto& record : s.records) out << record.scenario << ',' << record.action << ',' << record.dispatch
        << ',' << record.milliseconds << ',' << (record.changed ? "presented" : "timeout") << '\n';
    for (const auto& pending : s.pending) if (pending.active)
        out << pending.scenario << ',' << pending.action << ',' << pending.dispatch << ",,incomplete\n";
    std::ofstream images(s.output / "images.csv");images<<"kind,generation,revision\n";
    for(const auto& event:s.imageEvents)images<<event.kind<<','<<event.generation<<','<<event.revision<<'\n';
    std::ofstream refinements(s.output/"refinements.csv");refinements<<"content,accepted,native,covered,logical_width,storage_width\n";
    for(const auto& event:s.refinements)refinements<<event.content<<','<<event.accepted<<','<<event.native<<','<<event.covered<<','<<event.logical<<','<<event.storage<<'\n';
    std::ofstream timings(s.output/"viewport-timings.csv");
    timings << "generation,execution,edge,storage_width,storage_height,predicted_ms,queue_ms,cpu_ms,gpu_ms,completed_ms,interactive,cold,refinement,trained,regional_pixels,complete_pixels\n";
    for (const auto& t:s.viewportTimings)
        timings << t.generation<<','<<t.execution<<','<<t.edge<<','<<t.storageWidth<<','<<t.storageHeight<<','
            <<t.predictedMs<<','<<t.queueMs<<','<<t.cpuMs<<','<<t.gpuMs<<','<<t.completedMs<<','
            <<t.interactive<<','<<t.cold<<','<<t.refinement<<','<<t.trained<<','
            <<t.regionalPixels<<','<<t.completePixels<<'\n';
#if defined(_WIN32)
    if(!s.invalidations.empty()) {
        std::ofstream stacks(s.output/"invalidations.txt");
        const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        for (const auto& event:s.invalidations) {
            stacks<<event.kind<<'\n';
            for(unsigned short index=0;index<event.count;++index)
                stacks<<"  offset=0x"<<std::hex<<(reinterpret_cast<std::uintptr_t>(event.addresses[index])-module)<<std::dec<<'\n';
        }
    }
#endif
    s.active = false;
}
}
