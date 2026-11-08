#include "App/UiPerformanceProbe.h"
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <vector>
#include <cstdlib>
#include "imgui.h"
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
    double acceptedMs = -1, geometryMs = -1, presentedMs = -1;
    std::uint64_t inputQpc = 0;
    bool tracksMotion = false;
    unsigned int earliestFrame = 0;
    double submittedMs = -1;
};
struct Record { std::string scenario, action, dispatch; double milliseconds; bool changed;
    double acceptedMs = -1, geometryMs = -1, settledMs = -1; std::uint64_t inputQpc = 0; const char* outcome = "presented";
    double submittedMs = -1; };
struct ImageEvent { const char* kind; std::uint64_t generation, revision; };
struct Invalidation { const char* kind; std::array<void*,12> addresses {}; unsigned short count = 0; };
struct RefinementEvent { std::uint64_t content,accepted; bool native,covered; int logical,storage; };
struct FilmstripRecord { std::uint64_t qpc; float nativePosition,position,velocity; int direction; bool active; float maximum; };
struct AcceptedImageTag { std::uint64_t generation = 0, revision = 0; bool settledKernels = false; };
struct State {
    bool active = false;
    int node = -1;
    const void* editor = nullptr;
    std::filesystem::path output;
    std::array<Observation, count> last {}, current {};
    std::array<Pending, count> pending {};
    std::array<bool, count> moving {};
    std::array<AcceptedImageTag,64> images {};
    std::size_t nextImage = 0;
    std::vector<ImageEvent> imageEvents;
    std::uint64_t observedImage = 0;
    unsigned int frame = 0;
    std::vector<Invalidation> invalidations;
    std::vector<RefinementEvent> refinements;
    std::vector<ViewportTiming> viewportTimings;
    std::vector<Record> records;
    std::vector<FilmstripRecord> filmstrip;
};
State& Get() { static State state; return state; }
}
void Enable(const std::filesystem::path& output) {
    auto& s = Get(); s = {}; s.active = true; s.output = output; s.records.reserve(512); s.filmstrip.reserve(60000);
}
bool Active() { return Get().active; }
void BeginFrame() { if (Active()) { Get().current = {}; Get().moving = {}; ++Get().frame; } }
std::uint64_t Timestamp() {
#if defined(_WIN32)
    LARGE_INTEGER value; QueryPerformanceCounter(&value); return static_cast<std::uint64_t>(value.QuadPart);
#else
    return static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
#endif
}
static double Elapsed(const Pending& p) { return std::chrono::duration<double,std::milli>(Clock::now()-p.started).count(); }
void Accepted(Visual visual) {
    if (!Active()) return; auto& p=Get().pending[static_cast<std::size_t>(visual)];
    if (p.active && p.acceptedMs<0) p.acceptedMs=Elapsed(p);
}
void Motion(Visual visual,bool active) {
    if (!Active()) return; const auto i=static_cast<std::size_t>(visual); Get().moving[i]=active;
    if (Get().pending[i].active) Get().pending[i].tracksMotion=true;
}
void FilmstripSample(float nativePosition,float position,float velocity,int direction,bool active,float maximum) {
    if(!Active())return;
    auto& samples=Get().filmstrip;
    if(samples.size()<60000)samples.push_back({Timestamp(),nativePosition,position,velocity,direction,active,maximum});
}
void Item(Visual visual,std::uint64_t key) {
    if (!Active()) return; const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    Drawn(visual,key,{lo.x,lo.y,hi.x-lo.x,hi.y-lo.y});
}
void Drawn(Visual visual, std::uint64_t key, Region region) {
    if (!Active()) return;
    const auto i=static_cast<std::size_t>(visual); auto& s=Get();
    s.current[i] = {key, region, true};
    auto& p=s.pending[i];
    if (p.active && s.frame>=p.earliestFrame && p.geometryMs<0 && key!=p.baseline && (!p.required ||
        (visual==Visual::Image ? key>=p.required : key==p.required))) p.geometryMs=Elapsed(p);
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
    if (!s.last[index].valid || s.records.size() >= 2048) return false;
    if (pending.active) s.records.push_back({pending.scenario,pending.action,pending.dispatch,
        pending.presentedMs,pending.presentedMs>=0,pending.acceptedMs,pending.geometryMs,-1,pending.inputQpc,"interrupted",pending.submittedMs});
    pending = {true,s.last[index].key,0,Clock::now(),scenario,action,dispatch};
    pending.inputQpc=Timestamp();
    // Scroll geometry at the start of this frame may still reflect momentum
    // from the previous input. Native scroll adoption happens next frame.
    pending.earliestFrame=s.frame+(std::strcmp(dispatch,"imgui-wheel")==0 ? 1u : 0u);
    if (std::strcmp(dispatch,"ui-command")==0) pending.acceptedMs=0;
    return true;
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
void Submitted() {
    if(!Active())return;
    auto& s=Get();
    for(std::size_t i=0;i<count;++i) {
        auto& pending=s.pending[i];const auto& observation=s.current[i];
        if(pending.active && pending.submittedMs<0 && s.frame>=pending.earliestFrame && observation.valid &&
            observation.key!=pending.baseline && (!pending.required || (i==static_cast<std::size_t>(Visual::Image)
                ? observation.key>=pending.required : observation.key==pending.required)))
            pending.submittedMs=Elapsed(pending);
    }
}
void Presented() {
    if (!Active()) return;
    auto& s = Get(); const auto now = Clock::now();
    for (std::size_t index = 0; index < count; ++index) {
        auto& pending = s.pending[index]; const auto& observation = s.current[index];
        if (pending.active) {
            const double elapsed = std::chrono::duration<double,std::milli>(now-pending.started).count();
            const bool changed = s.frame>=pending.earliestFrame && observation.valid && observation.key != pending.baseline &&
                (!pending.required || (index==static_cast<std::size_t>(Visual::Image)
                    ? observation.key>=pending.required : observation.key==pending.required));
            if (changed && pending.presentedMs<0) pending.presentedMs=elapsed;
            const bool settled = pending.presentedMs>=0 && (!pending.tracksMotion || !s.moving[index]);
            if (settled || elapsed > 2500) {
                s.records.push_back({pending.scenario,pending.action,pending.dispatch,
                    pending.presentedMs,pending.presentedMs>=0,pending.acceptedMs,pending.geometryMs,
                    settled && pending.tracksMotion ? elapsed : -1,pending.inputQpc,settled?"presented":"timeout",pending.submittedMs});
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
    out << "scenario,action,dispatch,response_ms,status,accepted_ms,geometry_ms,settled_ms,input_qpc,submitted_ms\n" << std::fixed << std::setprecision(6);
    for (const auto& record : s.records) out << record.scenario << ',' << record.action << ',' << record.dispatch
        << ',' << record.milliseconds << ',' << record.outcome << ',' << record.acceptedMs << ',' << record.geometryMs << ',' << record.settledMs << ',' << record.inputQpc << ',' << record.submittedMs << '\n';
    for (const auto& pending : s.pending) if (pending.active)
        out << pending.scenario << ',' << pending.action << ',' << pending.dispatch << ',' << pending.presentedMs << ",incomplete," << pending.acceptedMs << ',' << pending.geometryMs << ",-1," << pending.inputQpc << ',' << pending.submittedMs << '\n';
    std::ofstream images(s.output / "images.csv");images<<"kind,generation,revision\n";
    std::ofstream filmstrip(s.output/"filmstrip.csv");
    filmstrip<<"qpc,native_position,position,velocity,direction,active,maximum\n"<<std::fixed<<std::setprecision(6);
    for(const auto& row:s.filmstrip)filmstrip<<row.qpc<<','<<row.nativePosition<<','<<row.position<<','<<row.velocity
        <<','<<row.direction<<','<<row.active<<','<<row.maximum<<'\n';
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
