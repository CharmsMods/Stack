#include "Renderer/ViewportPreviewTypes.h"
#include "Renderer/ViewportSectionStore.h"
#include "Renderer/ViewportPreviewPolicy.h"
#include "Renderer/ViewportWorkloadTiming.h"
#include "Editor/ViewportCompletionPolicy.h"
#include "Raw/RawViewportPreferences.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <atomic>
#include <thread>

namespace {
using namespace Stack::Renderer;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void TestSectionStore() {
    for (std::uint64_t allowance : {std::uint64_t(0),std::uint64_t(1024*1024)}) {
        ViewportSectionStore store(2051,4,allowance);
        const ImageRegion right{2051,4,2048,0,3,4};
        Require(store.NextMissing(right,true)==2,"Visible missing sections must precede background completion");
        Require(!store.Covers(right) && !store.Complete(),"Unwritten native sections cannot claim coverage");
        for (int index : {2,0,1}) {
            const auto r=store.Raster(index);
            std::vector<float> pixels(std::size_t(r.width)*r.height*4),hdr(pixels.size());
            for(std::size_t i=0;i<pixels.size();++i) {pixels[i]=float(int(i%71)-20)/7.f;hdr[i]=pixels[i]*4.f;}
            std::string error;
            Require(store.Store(index,pixels,hdr,error),"Native section could not be stored");
            std::vector<float> restored,restoredHdr;
            Require(store.Load(index,restored,restoredHdr,error) && restored==pixels && restoredHdr==hdr,
                "RAM and disk reload must preserve every float, including negative and HDR values");
            Require(store.Covers(right),"Completed visible coverage must remain ready during background work");
        }
        Require(store.Complete() && store.NextMissing(right,true)<0,"Completed sections must never be scheduled again");
        Require((store.DiskBytes()>0)==(allowance==0),"Cache must honor its RAM allowance and spill when required");
        ViewportSectionStore otherRevision(2051,4,allowance);
        Require(!otherRevision.Has(2),"Different revision stores must not reuse pixels without matching identity");
    }
}

void TestSectionPublication() {
    // Exercise the public store while coverage is polled as the UI does. The
    // disk and retained-RAM paths must publish only complete immutable pairs.
    for (std::uint64_t allowance : {std::uint64_t(0), std::uint64_t(1024*1024)}) {
        ViewportSectionStore store(2051,4,allowance);
        const auto whole = WholeImageRegion(2051,4);
        std::atomic<bool> finished{false};
        bool stored = true;
        std::thread producer([&] {
            for (int index = 0; index < store.Count(); ++index) {
                const auto raster = store.Raster(index);
                std::vector<float> pixels(std::size_t(raster.width)*raster.height*4, float(index)-2.5f);
                std::vector<float> hdr(pixels.size(), float(index)+4.25f);
                std::string error;
                stored = stored && store.Store(index,std::move(pixels),std::move(hdr),error);
            }
            finished.store(true,std::memory_order_release);
        });
        bool publicationConsistent = true;
        std::vector<bool> seen(store.Count(),false);
        do {
            for (int i = 0; i < store.Count(); ++i) {
                const bool ready = store.Has(i);
                publicationConsistent &= !seen[i] || ready;
                seen[i] = ready;
            }
            // Completion is published after the final section, so these
            // following observations must agree once it becomes true.
            if (store.Complete())
                publicationConsistent &= store.Covers(whole) && store.NextMissing(whole,true) < 0;
            (void)store.DiskBytes();
            std::this_thread::yield();
        } while (!finished.load(std::memory_order_acquire));
        producer.join();
        Require(stored && publicationConsistent && store.Complete(),
            "Concurrent section coverage must be monotonic and follow successful publication");
        for (int index = 0; index < store.Count(); ++index) {
            std::vector<float> pixels,hdr;
            std::string error;
            Require(store.Load(index,pixels,hdr,error) && !pixels.empty() && pixels.size()==hdr.size(),
                "A published section must contain both complete renditions");
            for (std::size_t i=0; i<pixels.size(); ++i)
                Require(pixels[i]==float(index)-2.5f && hdr[i]==float(index)+4.25f,
                    "Section publication must not expose mixed revisions or partial float payloads");
        }
    }
    ViewportSectionStore invalid(4,4);
    std::string error;
    Require(!invalid.Store(0,{1.f},{},error) && !invalid.Has(0) && !invalid.Complete(),
        "Invalid payloads must leave section availability unpublished");
}

void TestGeometry() {
    Raw::ViewportPreferences preferences;
    preferences.targetFps = 5;
    Require(Raw::EffectiveViewportTargetFps(preferences,144) == 5,
        "Auto detail must honor the saved target even with an inactive 100 percent floor");
    preferences.mode = Raw::ViewportInteractionMode::PreserveDetail;
    Require(Raw::EffectiveViewportTargetFps(preferences,144) == 144 && preferences.targetFps == 5,
        "Full screen detail must use the display ceiling and preserve the inactive target");
    preferences.minimumDetailPercent = 99;
    Require(Raw::EffectiveViewportTargetFps(preferences,144) == 5,
        "Reducing the floor must restore the saved FPS target");

    Stack::EditorRendering::ViewportCompletionPolicy completion;
    const ImageRegion most{1000,1000,0,0,800,1000};
    const ImageRegion detail{1000,1000,400,400,100,100};
    completion.ImageChanged(1.0);
    Require(!completion.Ready(1.1,false,most) && !completion.wholeImagePending,
        "Whole image completion must wait through the edit quiet interval");
    Require(completion.Ready(1.3,false,most) && completion.wholeImagePending,
        "Eighty percent visibility must arm whole image completion");
    Require(completion.Ready(1.4,false,detail) && completion.wholeImagePending,
        "Zooming in must preserve whole image completion intent");
    completion.ImageChanged(2.0);
    Require(completion.Ready(2.3,false,detail) && !completion.wholeImagePending,
        "A new edit at magnification must start with regional completion");
    Require(!completion.Ready(3.0,true,most) && !completion.Ready(3.1,false,most),
        "Holding an image control must postpone completion until after release");
    Require(!ImageRegion{}.Valid() && !WholeImageRegion(0, 10).Valid(), "Missing geometry must remain unavailable");
    const ImageRegion native{4033, 3017, 1001, 731, 1821, 1502};
    const auto sampled = ScaleImageRegion(native, 1291, 966);
    Require(sampled == ImageRegion{1291, 966, 320, 234, 584, 481}, "Fractional scale must round coverage outward on the full grid");
    Require(ContainsImageRegion(sampled, native), "Scaled coverage must include all requested logical pixels");
    Require(!ContainsImageRegion(native, sampled), "Outward rounding must not claim the reverse coverage");
    const auto full = WholeImageRegion(1291, 966);
    Require(ContainsImageRegion(full, native), "Full proxy coverage must cover a native-coordinate crop");
    auto moved = native;
    moved.x += 1;
    Require(native.Fingerprint() != moved.Fingerprint(), "Equal-sized crops at different origins need different cache identities");
    Require(!MapImageRegionUv(native, moved).valid, "Missing one edge cannot be stretched into complete coverage");
    const auto uv = MapImageRegionUv(WholeImageRegion(100, 80), ImageRegion{100, 80, 0, 40, 50, 40});
    Require(uv.valid && uv.x == 0 && uv.y == 0 && uv.width == 0.5 && uv.height == 0.5,
        "Top-left lower-half coverage must map to bottom-left texture UVs");
    const auto upperUv = MapImageRegionUv(WholeImageRegion(100, 80), ImageRegion{200, 160, 50, 0, 100, 80});
    Require(upperUv.valid && upperUv.x == 0.25 && upperUv.y == 0.5 && upperUv.width == 0.5 && upperUv.height == 0.5,
        "Mapping across sample grids must retain normalized position and orientation");

    ViewportPreviewGeometry patch{4033, 3017, sampled, sampled.width, sampled.height};
    Require(patch.Valid() && !patch.NativeSamples(), "Logical canvas and cropped proxy storage must be distinct");
    for (int local : {0, 113, sampled.width - 1}) {
        const double patchCenter = patch.SampleOriginX() + (local + 0.5) * patch.SampleSpacingX();
        const double fullCenter = (sampled.x + local + 0.5) * 4033.0 / 1291;
        Require(std::abs(patchCenter - fullCenter) < 1e-9, "Cropping must not reset the global pixel-center phase");
    }
    patch.storageWidth--;
    Require(!patch.Valid(), "A texture independently resized after cropping cannot claim the original sample lattice");
    patch = {4033, 3017, native, native.width, native.height};
    Require(patch.NativeSamples(), "Native crop samples must remain identifiable for authoritative inspection");

    const auto zoomedOut = WholeImageRegion(4033, 3017);
    const ViewportPreviewGeometry overview{4033, 3017, WholeImageRegion(512, 383), 512, 383};
    Require(ViewportPreviewCoversDisplay(patch, native, 650, 536) &&
        !ViewportPreviewCoversDisplay(patch, zoomedOut, 512, 383),
        "A native patch must satisfy its zoomed-in view and leave wider coverage to the overview");
    Require(ViewportPreviewCoversDisplay(overview, zoomedOut, 512, 383) &&
        !ViewportPreviewCoversDisplay(overview, zoomedOut, 513, 383) &&
        !ViewportPreviewCoversDisplay(overview, zoomedOut, 512, 384),
        "Overview reuse must require enough samples on both display axes");
    Require(!ViewportPreviewCoversDisplay(overview, native, 512, 383),
        "Only the overview samples within the visible crop count toward display coverage");

    const int maximum = std::numeric_limits<int>::max();
    const ImageRegion edge{maximum, maximum, maximum - 1, maximum - 1, 1, 1};
    Require(ScaleImageRegion(edge, maximum, maximum) == edge, "Large edge arithmetic must retain its last pixel without overflow");
    auto invalid = edge;
    invalid.width = 2;
    Require(!invalid.Valid() && !ScaleImageRegion(invalid, 10, 10).Valid(), "Out-of-canvas regions must fail geometry validation");
    ViewportPreviewRequest request{{}, 1.0, 7, true, true};
    Require(request.enabled && request.empty && !request.visible.Valid(), "Empty detail demand must remain distinct from full demand");
}

void TestCandidateSelection() {
    ViewportPreviewPolicyInput input{2048, 128, 2048, 60};
    std::vector<ViewportPreviewCandidate> plans{{256, 15, true, true, 100, 1},
        {512, 70, true, true, 200, 2}, {1024, 12, true, true, 300, 3}, {2048, 80, true, true, 400, 4}};
    auto selected = SelectViewportPreviewCandidate(input, plans);
    Require(selected.edge == 1024 && selected.candidateIndex == 2 && selected.limit == ViewportPreviewLimit::FrameBudget,
        "Selection must compare actual cache-discontinuous plans, not binary-search their edges");
    input.targetFps = 10;
    Require(SelectViewportPreviewCandidate(input, plans).edge == 2048, "A relaxed FPS target must admit higher-detail plans");
    input.targetFps = 60;
    input.memoryBudgetBytes = 250;
    selected = SelectViewportPreviewCandidate(input, plans);
    Require(selected.edge == 256 && selected.limit == ViewportPreviewLimit::Memory,
        "A faster cached plan exceeding the working budget must not be admitted");
    input.memoryBudgetBytes = 50;
    selected = SelectViewportPreviewCandidate(input, plans);
    Require(selected.edge == 0 && selected.limit == ViewportPreviewLimit::Memory,
        "No admissible allocation must return no replacement plan");

    input = {2048, 128, 2048, 60};
    plans = {{256, 50, true, true}, {1024, 51, true, true}, {2048, 90, true, true}};
    selected = SelectViewportPreviewCandidate(input, plans);
    Require(selected.edge == 1024 && selected.limit == ViewportPreviewLimit::FixedWork,
        "Unavoidable fixed work must retain detail within five percent of the fastest plan");
    input.minimumEdge = 2048;
    input.preserveDetail = true;
    selected = SelectViewportPreviewCandidate(input, plans);
    Require(selected.edge == 2048 && selected.limit == ViewportPreviewLimit::DetailFloor,
        "A physical detail floor must remain explicit when its work exceeds the FPS target");
    input.maximumEdge = 1024;
    selected = SelectViewportPreviewCandidate(input, plans);
    Require(selected.edge == 1024 && selected.limit == ViewportPreviewLimit::Memory,
        "A memory or texture cap must explain an unmet detail floor");

    input = {2048, 128, 2048, 60};
    plans = {{320}, {960}, {1600}};
    selected = SelectViewportPreviewCandidate(input, plans);
    Require(selected.edge == 960 && !selected.measured && selected.provisional && selected.predictedMs == 0,
        "Unknown costs must choose an actual provisional candidate and report no invented timing");
    input.evaluationEdge = 777;
    Require(SelectViewportPreviewCandidate(input, plans).edge == 960, "Stabilization must never invent a missing executable edge");
    input.evaluationEdge = 320;
    Require(SelectViewportPreviewCandidate(input, plans).edge == 320, "An admitted dispatched candidate must be reportable during stabilization");
    plans[1].costKnown = true;
    plans[1].predictedMs = std::numeric_limits<double>::quiet_NaN();
    Require(!SelectViewportPreviewCandidate(input, plans).measured, "Nonfinite cost evidence must remain unknown");
}

void TestCrossZoomTiming() {
    ViewportWorkloadTiming timings;
    const auto now=ViewportWorkloadTiming::Clock::now();
    // Same graph at fit and two crops: fixed 3 ms + 2 ms/M regional + 4 ms/M complete.
    timings.Record(1,1000000,2000000,13,false,now);
    timings.Record(1,2000000,2000000,15,false,now);
    timings.Record(1,1000000,4000000,21,false,now);
    timings.Record(1,2000000,4000000,23,false,now);
    auto estimate=timings.Estimate(1,1500000,3000000,now);
    Require(estimate.costKnown && !estimate.timingCovered && std::abs(estimate.predictedMs-18)<.001,
        "cross-zoom fit separates fixed, regional and complete costs");
    Require(timings.Estimate(1,1000000,2000000,now).timingCovered,"measured workload remains exact");
    Require(!timings.Estimate(2,1000000,2000000,now).costKnown,"other kernels cannot borrow timing");
    Require(!timings.Estimate(1,1000000,2000000,now+std::chrono::seconds(31)).costKnown,"cross-zoom evidence expires");
    timings.Record(2,1000000,0,10,false,now);
    Require(timings.Estimate(2,250000,0,now).predictedMs==10,"single crop cannot invent fixed cost savings");
    Require(!timings.Estimate(2,250000,1000000,now).costKnown,"new whole-input dependency remains unknown");
    timings.Record(4,1000000,4000000,20,false,now);
    timings.Record(4,2000000,4000000,22,false,now);
    timings.Record(4,3000000,4000000,24,false,now);
    Require(std::abs(timings.Estimate(4,1500000,4000000,now).predictedMs-21)<.001,
        "constant complete dependency allows regional interpolation");
    Require(timings.Estimate(4,1500000,8000000,now).predictedMs>=40,
        "constant dependency does not prove the cost of growing it");
    timings.Record(3,1000000,0,1,true,now);
    Require(!timings.Estimate(3,1000000,0,now).costKnown,"cold timings excluded");
}

void TestTimingCurve() {
    ViewportPreviewTimingCurve timings;
    timings.Record(1, 512, 10, true);
    Require(!timings.Estimate(1, 512).costKnown, "Cold preparation must not train steady editing costs");
    timings.Record(1, 512, 10);
    const auto single = timings.Estimate(1, 256);
    Require(single.costKnown && single.predictedMs == 10 && !single.timingCovered && single.provisional,
        "One completed size cannot pretend fixed dependencies scale by viewport area");
    Require(timings.Estimate(1, 2048).predictedMs == 160,
        "One cheap small render must not make a sixteen-times-larger raster look equally cheap");
    Require(timings.Estimate(1, 512).timingCovered && !timings.Estimate(2, 512).costKnown,
        "Exact sizes are covered only within their execution context");
    timings.Record(1, 1024, 22);
    Require(std::abs(timings.Estimate(1, 256).predictedMs - 7) < 1e-9,
        "Two sizes must retain the measured fixed component while scaling pixel work");
    Require(std::abs(timings.Estimate(1, 2048).predictedMs - 70) < 1e-9,
        "Extrapolation must use the compatible fixed-plus-pixel curve");
    timings.Record(2, 512, 10);
    timings.Record(2, 1024, 8);
    Require(timings.Estimate(2, 2048).predictedMs == 40, "A negative pixel slope must not make larger unmeasured rasters free");
    for (int i = 0; i < 3; ++i) timings.Record(1, 512, 10);
    timings.Record(1, 512, 500);
    Require(timings.Estimate(1, 512).predictedMs == 10, "An isolated service stall must not replace a stable timing point");
    timings.Record(1, 512, 500);
    timings.Record(1, 512, 500);
    Require(timings.Estimate(1, 512).predictedMs > 10, "Repeated slow work must still adapt the curve");
    Require(!timings.Estimate(1, 1024).timingCovered,
        "A sustained cost change must retire the other old sizes, not just update the currently used size");
    timings.Record(1, 512, std::numeric_limits<double>::infinity());
    Require(std::isfinite(timings.Estimate(1, 512).predictedMs), "Invalid completed timing must not poison the curve");
    for (std::size_t identity = 3; identity <= 66; ++identity) timings.Record(identity, 512, 5);
    Require(!timings.Estimate(1, 512).costKnown && timings.Estimate(66, 512).timingCovered,
        "Timing contexts must remain bounded without discarding their newest evidence");
    for (int edge = 1; edge <= 17; ++edge) timings.Record(66, edge, 5);
    Require(!timings.Estimate(66, 512).timingCovered && timings.Estimate(66, 17).timingCovered,
        "Sizes within one timing context must remain bounded");
    timings.Reset();
    Require(!timings.Estimate(66, 17).costKnown, "Reset must discard only this controller's derived evidence");

    const auto start = ViewportPreviewTimingCurve::Clock::now();
    timings.Record(9, 512, 10, false, start);
    timings.Record(9, 1024, 30, false, start + std::chrono::seconds(20));
    timings.Record(9, 1024, 300, false, start + std::chrono::seconds(10));
    timings.Record(9, 256, 8, false, start + std::chrono::seconds(10));
    Require(timings.Estimate(9, 1024, start + std::chrono::seconds(21)).predictedMs == 30,
        "Delayed older evidence must not overwrite a newer point at the same size");
    Require(!timings.Estimate(9, 512, start + std::chrono::seconds(31)).timingCovered &&
        timings.Estimate(9, 1024, start + std::chrono::seconds(31)).timingCovered,
        "Using one size must not keep an older size's exact evidence alive");
    Require(!timings.Estimate(9, 512, start + std::chrono::seconds(51)).costKnown,
        "An idle workload must return to provisional learning after expiry");
    for (int i = 0; i < 3; ++i) timings.Record(10, 512, 40);
    timings.Record(10, 1024, 160);
    for (int i = 0; i < 3; ++i) timings.Record(10, 512, 10);
    Require(timings.Estimate(10, 512).predictedMs == 10 && !timings.Estimate(10, 1024).timingCovered,
        "Sustained faster work must also discard old slow sizes and recover detail");
}
} // namespace

int main() {
    try {
        TestSectionStore();
        TestSectionPublication();
        TestGeometry();
        TestCandidateSelection();
        TestTimingCurve();
        TestCrossZoomTiming();
    } catch (const std::exception& error) {
        std::cerr << "Viewport preview checks failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Viewport preview geometry, candidate selection, and timing checks passed.\n";
    return 0;
}
