#include "Raw/RawViewportController.h"
#include "Raw/RawViewportPreferences.h"
#include <fstream>
#include "Raw/RawViewportTimingHistory.h"
#include "Raw/RawViewportDetail.h"
#include "Renderer/RawGraphViewportWorkload.h"
#include "Editor/RawRenderPlanning.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Async/TaskSystem.h"
#include <chrono>
#include <stdexcept>
#include <thread>

void TestRawViewportTimingLearning() {
    const auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
    using namespace Stack::EditorRenderScheduling;
    auto recipe = Stack::RawRecipe::MakeDefaultRecipe("timing-test");
    recipe.preToneExposureEv = 0.5f;
    const auto keys = Raw::ViewportWorkloadKeys(recipe);
    {
        Stack::EditorRendering::RawRenderPlanInput input;
        input.fullFrameWidth=4000; input.fullFrameHeight=3000;
        input.maximumTextureSize=8192; input.workingBudgetBytes=1024ull*1024*1024;
        input.fullResolutionPreviewRequested=true; input.interactiveTargetEdge=512;
        require(Stack::EditorRendering::BuildRawRenderPlan(input).nativePresentationAllowed,
            "The 16-bit reference raster should fit the specified budget");
        input.surfaceBytesPerPixel=16;
        const auto plan=Stack::EditorRendering::BuildRawRenderPlan(input);
        require(!plan.nativePresentationAllowed && plan.targetEdge==512 &&
            plan.fullFrameDecision.estimatedWorkingSetBytes==4000ull*3000*7*16 &&
            plan.minimumRawStageCacheBytes==512ull*384*16,
            "32-bit RAW surfaces must affect native admission and input-cache reservation");
    }
    {
        RenderGraphSnapshot graph;
        RenderGraphNode exposure;
        exposure.nodeId=3; exposure.kind=RenderGraphNodeKind::RawOperation;
        exposure.rawOperation=Stack::RawRecipe::MakeGraphOperation(Stack::RawRecipe::GraphOperationKind::Exposure);
        Stack::RawRecipe::WriteGraphOperation(exposure.rawOperation,recipe);
        graph.nodes.push_back(exposure); graph.outputNodeId=3; graph.outputSocketId="imageOut";
        const auto graphKeys=Stack::Renderer::BuildRawGraphViewportWorkloadKeys(recipe,graph,3);
        auto moved=recipe; moved.preToneExposureEv=1.5f;
        Stack::RawRecipe::WriteGraphOperation(graph.nodes[0].rawOperation,moved);
        require(graphKeys==Stack::Renderer::BuildRawGraphViewportWorkloadKeys(recipe,graph,3),
            "Exposure values in the same graph must share learned rendering costs");
        graph.nodes.push_back(exposure); graph.nodes.back().nodeId=4;
        graph.links.push_back({3,"imageOut",4,"imageIn"}); graph.outputNodeId=4;
        require(graphKeys!=Stack::Renderer::BuildRawGraphViewportWorkloadKeys(recipe,graph,3),
            "A repeated operation and changed topology must change rendering cost identity");
        Raw::ViewportTimingBank graphBank;
        Raw::ViewportCalibrationSample graphSample;
        graphSample.edge=1024; graphSample.renderMs=graphSample.startupMs=100;
        graphSample.stages.back()=100;
        graphBank.Record(graphKeys,graphSample);
        Raw::ViewportDecisionInput input;
        input.nativeWidth=4000; input.nativeHeight=3000;
        input.physicalWidth=1920; input.physicalHeight=1080; input.maximumEdge=4000;
        input.visible={4000,3000,0,0,4000,3000}; input.keys=graphKeys; input.completeGraph=true;
        Raw::ViewportController controller;
        input.fps=15; const auto low=controller.Choose(input,graphBank,recipe);
        input.fps=120; const auto high=controller.Choose(input,graphBank,recipe);
        require(high.edge<low.edge && high.predictedMs<=1000.0/120 && high.provisional,
            "Complete graph timing must reduce resolution for a higher FPS target before a second size is learned");
        graphSample.edge=256; graphSample.stages.back()=graphSample.renderMs=graphSample.startupMs=8;
        graphBank.Record(graphKeys,graphSample);
        require(controller.Choose(input,graphBank,recipe).edge<low.edge,
            "A second graph size must replace the provisional pixel estimate with measured costs");
        input.preferences.mode=Raw::ViewportInteractionMode::PreserveDetail;
        input.preferences.minimumDetailPercent=100;
        require(controller.Choose(input,graphBank,recipe).edge==Raw::ViewportDisplayDetailEdge(input),
            "Graph adaptation must respect the requested physical display detail floor");
    }
    Raw::ViewportTimingBank bank;
    Raw::ViewportCalibrationSample sample {1000,500,20,false};
    sample.stages = {8,2,2,2,2,2,1,1};
    bank.Record(keys, sample);
    sample.edge = 2000;
    sample.renderMs = 80;
    for (auto& stage : sample.stages) stage *= 4;
    bank.Record(keys,sample);
    require(bank.EdgeForFps(keys,5,3000,Raw::ViewportStage::RawBase,false) == 3000,
        "Two measured sizes should spend a 5 FPS budget on a larger estimated raster");
    require(bank.EdgeForFps(keys,30,3000,Raw::ViewportStage::RawBase,false) < 2000,
        "A tighter FPS budget must select fewer pixels");
    const auto cleanCost = bank.Cost(keys,2000,Raw::ViewportStage::RawBase,false);
    for (int i=0;i<8;++i) bank.Record(keys,sample);
    auto delayed = sample;
    delayed.renderMs = 500;
    delayed.stages[7] += 420;
    bank.Record(keys,delayed);
    require(bank.Cost(keys,2000,Raw::ViewportStage::RawBase,false) < cleanCost * 1.05,
        "One 500 ms stall must not replace learned frame costs");
    const auto saved = Raw::ViewportTimingHistory::Encode(bank);
    auto restored = Raw::ViewportTimingHistory::Decode(saved);
    require(restored.Cost(keys,2000,Raw::ViewportStage::RawBase,false) ==
        bank.Cost(keys,2000,Raw::ViewportStage::RawBase,false), "Timing history must retain averages and sample counts");
    auto invalid = saved;
    invalid["version"] = 0;
    require(Raw::ViewportTimingHistory::Decode(invalid).EdgeForFps(keys,5,3000,Raw::ViewportStage::RawBase,false) == 0,
        "Old timing contracts cannot constrain the current renderer");
    invalid = saved; invalid["profiles"][0]["points"][0][1] = "bad";
    require(Raw::ViewportTimingHistory::Decode(invalid).EdgeForFps(keys,5,3000,Raw::ViewportStage::RawBase,false) == 0,
        "Malformed history must be ignored");
    for (int fps : {5,10,30,60,144}) {
        const float quality = RawPreviewScaleAfterTargetChange(0.25f,20,4,fps);
        require(quality == RawPreviewScaleForCost(20,4,fps), "FPS changes must use the requested frame budget in both directions");
    }
    require(!Raw::CanLearnViewportEdit(true,false,true,true,false,false,0,2,2) &&
        !Raw::CanLearnViewportEdit(true,false,false,true,false,false,1,0,2) &&
        !Raw::CanLearnViewportEdit(true,true,false,true,false,false,0,2,2) &&
        Raw::CanLearnViewportEdit(true,false,false,true,false,false,0,2,2),
        "Startup, denoise preparation and canceled frames cannot train steady edit timing");
    Raw::ViewportEditTimingWindow edits;
    int healthy = 0, slow = 0, cooldown = 0;
    float scale = 1;
    double average = 20;
    for (double cost : {20.,20.,20.,500.,20.,20.,20.,20.}) {
        const double steady = edits.Observe(cost);
        if (steady > 0) {
            average = UpdateRawInteractiveFrameTimeEma(average,steady);
            scale = StabilizeRawPreviewScale(scale,RawPreviewScaleForCost(average,1,50),healthy,slow,cooldown,false);
        }
    }
    require(scale == 1 && average == 20, "An isolated 500 ms stall must not shrink later frames while the EMA recovers");
    for (int i=0;i<20;++i) {
        const double steady = edits.Observe(100);
        average = UpdateRawInteractiveFrameTimeEma(average,steady);
        scale = StabilizeRawPreviewScale(scale,RawPreviewScaleForCost(average,1,50),healthy,slow,cooldown,false);
    }
    require(scale < 0.9f, "Sustained slow completed rendering must still reduce resolution");
    // Feed completed render costs back through the controller, rather than
    // only testing the cost-to-scale formula in isolation.
    float lastQuality = 4;
    for (int fps : {5,10,30,60,120,240}) {
        for (float initial : {0.1f,2.0f}) {
            float quality = initial;
            int headroom = 0, overloaded = 0, settling = 0;
            double learned = 0;
            Raw::ViewportEditTimingWindow window;
            for (int frame = 0; frame < 100; ++frame) {
                const double cost = 2.0+80.0*quality*quality;
                const double sampleCost = window.Observe(RawViewportEquivalentRenderMs(cost,quality,2));
                if (sampleCost <= 0) continue;
                learned = UpdateRawInteractiveFrameTimeEma(learned,sampleCost);
                quality = StabilizeRawPreviewScale(quality,RawPreviewScaleForCost(learned,4,fps,2),
                    headroom,overloaded,settling,true);
            }
            const double budget = 1000.0/fps;
            const double measured = 2.0+80.0*quality*quality;
            require(measured <= budget*1.30 && measured >= budget*0.68,
                "Completed-frame feedback must converge toward both slow and fast FPS targets from either direction");
            if (initial == 2.0f) {
                require(quality < lastQuality,"Higher FPS targets must produce smaller rasters for the same measured workload");
                lastQuality = quality;
            }
        }
    }
    Raw::ViewportController controller;
    Raw::ViewportDecisionInput input;
    input.nativeWidth=4000; input.nativeHeight=3000;
    input.physicalWidth=1000; input.physicalHeight=750;
    input.maximumEdge=4000; input.keys=keys;
    input.visible={4000,3000,0,0,4000,3000};
    input.fps=60;
    const auto responsive=controller.Choose(input,bank,recipe);
    require(responsive.edge<1000,"Target FPS must reduce detail for a measured slow workload");
    input.preferences.mode=Raw::ViewportInteractionMode::PreserveDetail;
    const auto detail=controller.Choose(input,bank,recipe);
    require(detail.edge==1000 && detail.limit==Raw::ViewportLimit::DetailFloor,"100% detail floor must preserve physical display detail");
    input.visible={4000,3000,1000,1000,250,188};
    require(Raw::ViewportDisplayDetailEdge(input,0.5)==4000,"A half-display floor at 400% zoom still requires native samples");
    input.maximumEdge=1024;
    require(controller.Choose(input,bank,recipe).limit==Raw::ViewportLimit::Memory,"Memory restrictions must override and explain an unmet detail floor");
    input.maximumEdge=4000;
    input.regionalAllowed=false;
    require(!controller.Choose(input,bank,recipe).region.Valid(),"Unmapped graph output cannot claim regional rendering");
    input.regionalAllowed=true;
    require(controller.Choose(input,bank,recipe).region.Valid(),"A validated RAW stage must retain its requested region");
    auto regionalSample=sample;
    regionalSample.edge=4000;
    regionalSample.stageEdges.fill(1000);
    regionalSample.stages.fill(1);
    regionalSample.renderMs=8;
    regionalSample.startupMs=8;
    Raw::ViewportTimingBank regionalBank;
    regionalBank.Record(keys,regionalSample);
    require(std::abs(regionalBank.Cost(keys,1000,Raw::ViewportStage::RawBase,false)-8)<0.001,
        "Regional observations must record processed pixel extent, not pretend they processed the whole source");
    Raw::ViewportTimingBank fixedBank;
    auto fixed=sample; fixed.edge=500; fixed.stages.fill(2.5); fixed.renderMs=fixed.startupMs=20;
    fixedBank.Record(keys,fixed); fixed.edge=1000; fixedBank.Record(keys,fixed);
    input.preferences.mode=Raw::ViewportInteractionMode::TargetFps;
    input.visible={4000,3000,0,0,4000,3000}; input.regionalAllowed=false;
    const auto fixedDecision=controller.Choose(input,fixedBank,recipe);
    require(fixedDecision.edge==1000 && fixedDecision.limit==Raw::ViewportLimit::FixedWork,
        "Fixed work beyond the FPS budget must retain detail within 5% of the fastest plan");
    require(controller.Choose(input,regionalBank,recipe).provisional==false,
        "An exactly measured size must report measured coverage");
    auto coldOnly=fixed; coldOnly.coldOnly=true; coldOnly.stages.fill(100); coldOnly.renderMs=coldOnly.startupMs=800;
    Raw::ViewportTimingBank coldBank; coldBank.Record(keys,coldOnly);
    bool warmKnown=true; coldBank.Cost(keys,1000,Raw::ViewportStage::RawBase,false,&warmKnown);
    require(!warmKnown && !coldBank.Covers(keys,1000,Raw::ViewportStage::RawBase),
        "Cold preparation cannot masquerade as a measured repeated edit");
    controller.Reset(); controller.SetContext(42);
    const auto feedbackKey=Raw::ViewportFeedbackKey(42,Raw::ViewportStage::RawBase);
    auto before=controller.Choose(input,bank,recipe);
    for (int frame=0;frame<30;++frame) {
        auto current=controller.Choose(input,bank,recipe);
        const double actual=1.4*bank.Cost(keys,current.edge,Raw::ViewportStage::RawBase,false);
        controller.Observe(feedbackKey,current.predictedMs,actual);
    }
    auto after=controller.Choose(input,bank,recipe);
    require(after.edge<before.edge && after.predictedMs<=1000.0/input.fps*1.05,
        "Matching completed render error must correct the complete plan toward its frame budget");
    controller.Observe(feedbackKey,after.predictedMs,500);
    require(controller.Choose(input,bank,recipe).edge==after.edge,
        "One stalled completion must not permanently reduce the chosen detail");
    const Raw::ViewportRegion sharp {4000,3000,1000,750,2000,1500};
    const Raw::ViewportRegion proxy {1000,750,0,0,1000,750};
    require(Raw::RetainViewportDetail(1,1,sharp,2000,1500,proxy,1000,750) &&
        !Raw::RetainViewportDetail(1,2,sharp,2000,1500,proxy,1000,750) &&
        !Raw::RetainViewportDetail(1,1,sharp,2000,1500,{},4000,3000),
        "Zoom must retain sharp coverage until replacement detail arrives, without retaining an old edit");
    const Raw::ViewportRegion whole {4000,3000,0,0,4000,3000};
    require(Raw::ViewportDetailCoversDisplayRegion({},1200,900,whole,1000,750) &&
        Raw::ViewportDetailCoversDisplayRegion(proxy,1000,750,whole,1000,750),
        "A full-image frame must be reusable at its displayed sample density, across raster coordinate grids");
    require(!Raw::ViewportDetailCoversDisplayRegion({},1200,900,sharp,1000,750) &&
        Raw::ViewportDetailCoversDisplayRegion({},1200,900,sharp,500,375),
        "Zoom requests more detail only when the accepted frame no longer has enough displayed samples");
    require(!Raw::ViewportDetailCoversDisplayRegion({},1200,600,whole,1000,750),
        "Both image axes must satisfy display density");
    require(Raw::ViewportDetailCoversDisplayRegion(sharp,2000,1500,sharp,2500,1875) &&
        !Raw::ViewportDetailCoversDisplayRegion(sharp,2000,1500,{4000,3000,900,750,2000,1500},500,375),
        "Native pixels remain authoritative above 1:1, but an uncovered edge still needs refinement");
    const auto directory = std::filesystem::temp_directory_path() /
        ("stack-viewport-timing-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Async::TaskSystem::Get().Initialize();
    Raw::ViewportTimingHistory history;
    history.Open(directory,"test-gpu",4000,3000);
    for (int i=0;i<1000 && !history.MergeLoaded(restored);++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    history.Save(bank);
    for (int i=0;i<1000 && Async::TaskSystem::Get().HasPendingWork();++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    history.Reset();
    history.Open(directory,"test-gpu",4000,3000);
    Raw::ViewportTimingBank disk;
    bool loaded = false;
    for (int i=0;i<1000 && !(loaded = history.MergeLoaded(disk));++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(loaded && Raw::ViewportTimingHistory::Encode(disk) == saved, "Asynchronous disk history must survive reopening");
    history.Open(directory,"different-gpu",4000,3000);
    Raw::ViewportTimingBank other;
    for (int i=0;i<1000 && !history.MergeLoaded(other);++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(other.EdgeForFps(keys,5,4000,Raw::ViewportStage::RawBase,false) == 0, "Different GPUs must not share timing estimates");
    Raw::ViewportTimingHistory peerA,peerB;
    peerA.Open(directory,"shared-gpu",4000,3000); peerB.Open(directory,"shared-gpu",4000,3000);
    Raw::ViewportTimingBank peerBank;
    for(int i=0;i<1000 && !peerA.MergeLoaded(peerBank);++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto firstPoint=sample; firstPoint.edge=1000;
    peerA.Record(keys,firstPoint);
    auto secondPoint=sample; secondPoint.edge=2000;
    peerB.Record(keys,secondPoint);
    require(peerA.MergeLoaded(peerBank) && Raw::ViewportTimingHistory::Encode(peerBank)["profiles"][0]["points"].size()==2,
        "Independent projects must merge their individual learned sizes");
    peerA.Reset(); peerB.Reset();
    for(int i=0;i<2000 && Async::TaskSystem::Get().HasPendingWork();++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bool mergedOnDisk=false;
    for (const auto& file : std::filesystem::directory_iterator(directory)) {
        if (file.path().extension()!=".json") continue;
        std::ifstream stream(file.path()); const auto document=nlohmann::json::parse(stream);
        if (document.value("hardware",std::string{})=="shared-gpu")
            mergedOnDisk=document["profiles"][0]["points"].size()==2;
    }
    require(mergedOnDisk,"Closing both projects during a pending write must persist their merged final revision");
    peerA.Open(directory,"shared-gpu",4000,3000); peerB.Open(directory,"shared-gpu",4000,3000);
    const auto oldEpoch=peerB.Epoch();
    peerA.ResetHardware();
    peerB.Record(keys,secondPoint,oldEpoch);
    require(peerB.MergeLoaded(peerBank) && Raw::ViewportTimingHistory::Encode(peerBank)["profiles"].empty(),
        "Resetting hardware timings must invalidate all live readers");
    std::filesystem::create_directories(directory);
    { std::ofstream legacy(directory/"RawWorkspaceState.json"); legacy << R"({"rawViewportTargetFps":144,"smoothRawViewportUpdates":false,"rawViewportFadeBelowFps":90})"; }
    auto preferences=Raw::ViewportPreferencesStore::Open(directory);
    auto secondEditor=Raw::ViewportPreferencesStore::Open(directory);
    require(preferences==secondEditor && preferences->Read().targetFps==144 && !preferences->Read().smoothUpdates,
        "Editors share application preferences and import the existing values");
    auto value=preferences->Read(); value.targetFps=90; value.mode=Raw::ViewportInteractionMode::PreserveDetail; value.minimumDetailPercent=100;
    preferences->Set(value);
    require(secondEditor->Read().targetFps==90 && secondEditor->Read().mode==Raw::ViewportInteractionMode::PreserveDetail,
        "Changing FPS or mode must immediately affect another editor");
    for(int i=0;i<2000 && !preferences->IsPersisted();++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    { std::ifstream file(directory/"RawViewportSettings.json"); const auto persisted=nlohmann::json::parse(file);
      require(persisted.at("targetFps")==90 && persisted.at("minimumDetailPercent")==100,"Shared preferences must persist their own record"); }
    peerA.Reset(); peerB.Reset(); history.Reset();
    for(int i=0;i<2000 && Async::TaskSystem::Get().HasPendingWork();++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::error_code error;
    std::filesystem::remove_all(directory,error);
}
