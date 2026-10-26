#!/usr/bin/env python3
"""Generate bounded no-GPU checks from production worker/service method bodies.

The request/result payloads are reduced to lifecycle identity; platform, GPU and
model operations are fakes. Worker failure/publication, owner validation, drain,
service scheduling/routing and explicit shutdown are production method bodies.
Selected failure consumers and the fatal-idle scheduling gate are also copied
unchanged. Shared/dedicated continuation purpose, polling and auxiliary-only
dispatch branches are exercised with promise-backed model readiness. This checks
lifecycle decisions, not the full worker/UI render loop. Resource-delete stand-ins
check real worker/service retirement while a separate thread probes both locks,
including owner isolation, exactly-once tiled cleanup, failure and context order.
A single queue-push failure exercises the existing allocation-free fallback; this
is not a process-wide OOM proof. No private-state hook is added to production.
"""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
worker = (root / 'src/Editor/EditorRenderWorker.cpp').read_text()
sessions = (root / 'src/Editor/Internal/EditorRenderWorkerSessions.cpp').read_text()
timing = (root / 'src/Editor/Internal/EditorRenderWorkerViewportTiming.cpp').read_text()
service = (root / 'src/Editor/RawRenderService.cpp').read_text()

def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    pos = brace + 1
    while depth:
        if source[pos] == '{': depth += 1
        elif source[pos] == '}': depth -= 1
        pos += 1
    return source[start:pos] + '\n'

pre = r'''
#include "Editor/RawRenderPurpose.h"
#include "Editor/GraphRenderPolicy.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <list>
#include <type_traits>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct GLFWwindow {};
using GLsync = void*;
int deletedTextures=0, deletedFences=0, destroyedWindows=0, drainedModels=0;
std::function<void()> onResourceDelete;
std::unordered_map<unsigned,int> textureDeletes;
std::unordered_map<GLsync,int> fenceDeletes;
GLFWwindow* currentContext=nullptr;
void glfwMakeContextCurrent(GLFWwindow* context) { currentContext=context; }
GLFWwindow* glfwGetCurrentContext() { return currentContext; }
void glfwDestroyWindow(GLFWwindow*) { ++destroyedWindows; }
void glDeleteTextures(int n, const unsigned* names) { if(onResourceDelete) onResourceDelete();deletedTextures+=n;for(int i=0;i<n;++i) ++textureDeletes[names[i]]; }
void glDeleteSync(GLsync fence) { if(onResourceDelete) onResourceDelete();++deletedFences;++fenceDeletes[fence]; }
namespace Raw {
enum class OpenGlTaskPriority { Foreground, Background };
namespace ViewportGpuTiming { void Release(int&) {} }
}
struct FakeDenoise {
    bool pending=false,deferred=false,canceled=false;
    std::future<int> future;
    bool IsCompletionReady() const { return pending && future.valid() && future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready; }
    void RequestCancellation() { canceled=true;deferred=false; }
    void CancelAndWait() { ++drainedModels;pending=false; }
};
struct RenderPipeline {};
struct ViewportRequest { bool enabled=false,empty=false; std::uint64_t generation=0; };
struct ViewportTilingSettings {};
struct PreviewRequest { std::optional<int> assetCapture; bool assetCaptureInput=false; std::string rawLayerId,rawMaskId; int previewNodeId=-1; std::uint64_t dirtyGeneration=0; };
struct PreviewResult { bool assetCapture=false,assetCaptureInput=false,assetCaptureSuperseded=false,success=false; std::shared_ptr<int> assetFeedback;std::optional<int> capturedAsset; std::string rawLayerId,rawMaskId,error; int previewNodeId=-1; std::uint64_t dirtyGeneration=0; };
struct SolveRequest { std::uint64_t identity=0; };
struct SolveResult { bool attempted=false,failed=false,canceled=false; struct Candidate { std::uint64_t identity=0; std::string reason; } candidate; std::string reason; };
struct RawWorkspaceSnapshot { std::string sourceKey; std::uint64_t sourceHash=0; std::size_t recipeRevision=0; bool hasRecipe=false; int recipe=123,viewportDependencyEdge=-1; ViewportRequest viewport; std::optional<int> cachePrewarmStage; std::size_t cachePrewarmFingerprint=0; std::optional<SolveRequest> preciseSolveRequest; };
struct RawWorkspaceResult { std::string sourceKey; std::uint64_t sourceHash=0,viewportGeneration=0; std::size_t recipeRevision=0,presentationFingerprint=0; std::optional<int> cachePrewarmStage; std::size_t cachePrewarmFingerprint=0; std::optional<SolveResult> preciseSolveResult; };
struct SharedTexture {
    unsigned texture=0; GLsync readyFence=nullptr;
    SharedTexture()=default;
    SharedTexture(SharedTexture&& other) noexcept : texture(std::exchange(other.texture,0)),readyFence(std::exchange(other.readyFence,nullptr)) {}
    SharedTexture& operator=(SharedTexture&& other) noexcept { if(this!=&other) { Reset();texture=std::exchange(other.texture,0);readyFence=std::exchange(other.readyFence,nullptr); } return *this; }
    ~SharedTexture() { Reset(); }
    void Reset() { if(texture) glDeleteTextures(1,&texture); if(readyFence) glDeleteSync(readyFence); texture=0;readyFence=nullptr; }
};
struct WorkspaceResult : RawWorkspaceResult { SharedTexture localRangeOverlayTexture; };
template<class T> struct FailureList : std::list<T> {
    bool failNextPush=false;
    void push_back(T&& value) { if(std::exchange(failNextPush,false)) throw std::bad_alloc(); std::list<T>::push_back(std::move(value)); }
};
class EditorRenderWorker {
public:
    using PreviewRequest=::PreviewRequest;
    using PreviewResult=::PreviewResult;
    using RawWorkspaceSnapshot=::RawWorkspaceSnapshot;
    using SharedTextureResult=SharedTexture;
    struct SharedTextureTile { unsigned texture=0; };
    struct Tiles { GLsync readyFence=nullptr; std::vector<SharedTextureTile> tiles; };
    struct RawRenderTelemetry {
        std::chrono::steady_clock::time_point queuedAt{},snapshotReadyAt{};
        double commandEnqueueMs=0; bool superseded=false,workerStarted=false,overloadSkipped=false;
        std::uint64_t gestureId=0,timingHistoryEpoch=0; RawPreviewSkipReason skipReason=RawPreviewSkipReason::None;
    };
    struct Snapshot {
        std::uint64_t ownerId=0,generation=0,schedulingSerial=0,lastAcceptedGeneration=0;
        Stack::GraphRendering::RequestTag graphRequest;
        RawRenderPurpose rawRenderPurpose=RawRenderPurpose::InteractivePresentation;
        RawRenderTelemetry telemetry; int previewMaxDimension=0;
        RawWorkspaceSnapshot rawWorkspace; ViewportRequest viewportPreview;
        int viewportViewId=0; std::size_t viewportExecutionIdentity=0,viewportContentIdentity=0;
        bool prepareViewportOverview=false,outputConnected=false; int graph=0; ViewportTilingSettings viewportTiling;
        std::vector<PreviewRequest> previews;
        std::shared_ptr<const Snapshot> rawSessionTemplate;
        std::uint64_t rawSessionContractRevision=0; bool rawSessionCommand=false;
    };
    struct Result {
        std::uint64_t ownerId=0,generation=0,schedulingSerial=0,lastAcceptedGeneration=0;
        Stack::GraphRendering::RequestTag graphRequest;
        RawRenderPurpose rawRenderPurpose=RawRenderPurpose::InteractivePresentation;
        RawRenderTelemetry telemetry; int previewMaxDimension=0;
        WorkspaceResult rawWorkspace; ViewportRequest viewportPreview;
        int viewportViewId=0; std::size_t viewportExecutionIdentity=0,viewportContentIdentity=0;
        struct Calibration { double renderMs=0; } calibration;
        std::array<std::size_t,1> workloadKeys{};std::string timingRepresentation;
        bool denoisePending=false,timingOnly=false,success=false,workerFailed=false; std::string error; std::vector<unsigned char> pixels; int width=0,height=0;
        SharedTextureResult outputTexture,viewportOverview; Tiles outputTiles;
        std::vector<PreviewResult> previews;
    };
    struct RenderProgress { bool busy=false; int completedSteps=0,totalSteps=0; std::string label; };
    struct OwnerRenderState {
        std::shared_ptr<FakeDenoise> denoise; std::optional<Snapshot> continuation;
        bool continuationReady=false,resumeOriginalPurpose=false,denoiseInFlight=false,released=false,resetDenoiseRequested=false;
        RawRenderPurpose denoisePurpose=RawRenderPurpose::InteractivePresentation;
        std::uint64_t invalidBeforeGeneration=0,invalidBeforeSerial=0,latestGeneration=0,latestSerial=0;
        std::unordered_map<RawRenderPurpose,std::uint64_t> canceledPurposeBeforeSerial;
        Stack::GraphRendering::RequestTag latestGraphRequest;
    };
    struct DrainingDenoiseState { std::uint64_t ownerId=0;std::shared_ptr<FakeDenoise> state; };
    using OpenGlTask=std::function<bool(std::string&)>;
    struct OpenGlTaskState { OpenGlTask task;std::mutex mutex;std::condition_variable cv;bool success=false,completed=false;std::string error; };
    using OpenGlTaskQueue=std::priority_queue<std::shared_ptr<OpenGlTaskState>>;
    GLFWwindow* m_WorkerWindow=nullptr; std::thread m_Thread;
    mutable std::mutex m_Mutex;std::condition_variable m_Cv;
    bool m_StopRequested=false,m_InitComplete=true,m_InitSucceeded=true,m_ThreadFailed=false,m_HasPending=false,m_Rendering=false;
    std::string m_InitError,m_ThreadFailure;std::atomic<bool> m_Busy=false;
    std::uint64_t m_RenderingOwner=0,m_InvalidBeforeGeneration=0,m_InvalidBeforeSchedulingSerial=0,m_NextSchedulingSerial=1;
    Stack::GraphRendering::RequestTag m_LatestGraphRequest;
    Snapshot m_Pending;OpenGlTaskQueue m_OpenGlTasks;std::shared_ptr<OpenGlTaskState> m_ActiveOpenGlTask;
    FailureList<Result> m_Completed;std::list<Result> m_CompletedTimings,m_CompletedViewportTimings;std::optional<Result> m_FallbackCompleted;
    std::unordered_map<std::uint64_t,OwnerRenderState> m_OwnerStates;std::vector<DrainingDenoiseState> m_DrainingDenoise;
    int m_ProgressCompletedSteps=0,m_ProgressTotalSteps=0;std::string m_ProgressLabel;
    std::unique_ptr<RenderPipeline> m_CalibrationPipeline,m_PersistentPipeline;
    struct Timing { int batch=0; };std::vector<Timing> m_PendingViewportTimings;
    bool Initialize(GLFWwindow* window) { m_WorkerWindow=window;return true; }
    bool ExecuteOpenGlTaskBlocking(OpenGlTask,std::string&,Raw::OpenGlTaskPriority) { return false; }
    void CancelOwnerSnapshots(std::uint64_t,std::optional<RawRenderPurpose> = {});
    void InvalidateOwnerSnapshotsBefore(std::uint64_t,std::uint64_t);
    void ReleaseOwner(std::uint64_t);
    void PublishCompletedResult(Snapshot&,Result&);
    void RetainDenoiseContinuationLocked(Snapshot&,bool);
    bool IsOwnerResultStaleLocked(const Result&) const;
    void HandleThreadFailure(const char*,const Snapshot*) noexcept;
    bool GetThreadFailure(std::string* = nullptr) const noexcept;
    static Result MakeFailedResult(const Snapshot&,const std::string&);
    bool Submit(Snapshot);bool TryConsumeCompleted(Result&);bool TryConsumeViewportTiming(Result&);
    bool IsOwnerSnapshotInvalidLocked(std::uint64_t,std::uint64_t,std::uint64_t,RawRenderPurpose) const;
    bool HasPendingForOwner(std::uint64_t) const;bool HasSnapshotWorkFor(std::uint64_t) const;
    bool HasPendingSnapshotOrRender() const;bool HasDenoiseWorkLocked() const;
    bool IsWaitingForRawDenoiseCompletion() const;bool IsAvailable() const;
    bool TryConsumeReadyDenoiseSnapshot(Snapshot&);RenderProgress GetProgress() const;
    bool TakeDedicatedContinuationForTest(Snapshot&);
    bool CanStartAuthoritativeRenderLocked(std::uint64_t) const;
    void PollDenoiseContinuationsLocked();
    Result RenderAuxiliaryForTest(const Snapshot&);
    int mainRenderCalls=0,previewRenderCalls=0;
    void DrainDenoiseForShutdown() noexcept;void RequestStopForShutdown();void Shutdown();
};
int EstimateRenderProgressStepCount(const EditorRenderWorker::Snapshot&) { return 1; }
void ReleaseResultResources(EditorRenderWorker::Result& result) {
    result.outputTexture.Reset();result.viewportOverview.Reset();result.rawWorkspace.localRangeOverlayTexture.Reset();
    if(result.outputTiles.readyFence) glDeleteSync(result.outputTiles.readyFence);
    result.outputTiles.readyFence=nullptr;
    for(auto& tile:result.outputTiles.tiles) if(tile.texture) glDeleteTextures(1,&tile.texture);
    result.outputTiles.tiles.clear();
}
namespace Stack::Renderer { using ViewportPreviewRequest=::ViewportRequest; }
namespace Stack::EditorRendering { std::size_t BuildRawPresentationFingerprint(int recipe,int) { return recipe; } }
'''
retirement = (root / 'src/Editor/Internal/EditorRenderResultRetirement.h').read_text()
retirement = '\n'.join(line for line in retirement.splitlines() if not line.startswith('#include') and line != '#pragma once')
methods = retirement + '\nusing Stack::EditorRendering::Internal::RenderResultRetirement;\nusing Stack::EditorRendering::Internal::RenderResultResourceGuard;\n'
for signature in [
    'void EditorRenderWorker::HandleThreadFailure(',
    'void EditorRenderWorker::RequestStopForShutdown()',
    'void EditorRenderWorker::Shutdown()',
    'bool EditorRenderWorker::GetThreadFailure(',
    'EditorRenderWorker::Result EditorRenderWorker::MakeFailedResult(',
    'void EditorRenderWorker::PublishCompletedResult(',
    'bool EditorRenderWorker::Submit(',
    'bool EditorRenderWorker::TryConsumeCompleted(',
    'bool EditorRenderWorker::HasPendingSnapshotOrRender()',
    'bool EditorRenderWorker::IsWaitingForRawDenoiseCompletion()',
    'bool EditorRenderWorker::IsAvailable()',
    'EditorRenderWorker::RenderProgress EditorRenderWorker::GetProgress()']:
    methods += method(worker, signature)
for signature in [
    'void EditorRenderWorker::CancelOwnerSnapshots(',
    'void EditorRenderWorker::InvalidateOwnerSnapshotsBefore(',
    'void EditorRenderWorker::ReleaseOwner(',
    'void EditorRenderWorker::PollDenoiseContinuationsLocked()',
    'bool EditorRenderWorker::CanStartAuthoritativeRenderLocked(',
    'bool EditorRenderWorker::IsOwnerSnapshotInvalidLocked(',
    'bool EditorRenderWorker::IsOwnerResultStaleLocked(',
    'void EditorRenderWorker::RetainDenoiseContinuationLocked(',
    'bool EditorRenderWorker::HasDenoiseWorkLocked()',
    'bool EditorRenderWorker::HasPendingForOwner(',
    'bool EditorRenderWorker::HasSnapshotWorkFor(',
    'bool EditorRenderWorker::TryConsumeReadyDenoiseSnapshot(',
    'void EditorRenderWorker::DrainDenoiseForShutdown()']:
    methods += method(sessions, signature)
dedicated_start = worker.index('                auto& owner = dedicated->second;', worker.index('} else if (denoiseCompletionReady) {'))
dedicated_end = worker.index('            } else {', dedicated_start)
dedicated = worker[dedicated_start:dedicated_end]
methods += '''bool EditorRenderWorker::TakeDedicatedContinuationForTest(Snapshot& snapshot) {
    auto dedicated=m_OwnerStates.find(0);
    if(dedicated==m_OwnerStates.end() || !dedicated->second.continuationReady || !dedicated->second.continuation) return false;
''' + dedicated + '    return true;\n}\n'
auxiliary_start = worker.index('        if (!snapshot.outputConnected) {', worker.index('EditorRenderWorker::Result EditorRenderWorker::RenderSnapshot('))
auxiliary = method(worker[auxiliary_start:], '        if (!snapshot.outputConnected) {')
methods += '''EditorRenderWorker::Result EditorRenderWorker::RenderAuxiliaryForTest(const Snapshot& snapshot) {
    Result result;
    result.rawRenderPurpose=snapshot.rawRenderPurpose;
    const auto renderDevelopCandidateRequests=[]{};
    const auto renderPreviewRequests=[&]{++previewRenderCalls;};
''' + auxiliary + '    ++mainRenderCalls;return result;\n}\n'
methods += method(timing, 'bool EditorRenderWorker::TryConsumeViewportTiming(')
header = (root / 'src/Editor/RawRenderService.h').read_text()
header = '\n'.join(line for line in header.splitlines() if not line.startswith('#include') and line != '#pragma once')
# Production service layout, made constructible solely in this generated fixture.
header = header.replace('private:', 'public:')
service = '\n'.join(line for line in service.splitlines() if not line.startswith('#include'))
planning = (root / 'src/Editor/RawRenderPlanning.h').read_text()
methods += 'namespace Stack::EditorRendering {\n' + method(planning, 'inline bool ShouldAdoptRawPresentation(') + '}\n'
adoption = (root / 'src/Editor/Internal/EditorModuleRenderAdoption.cpp').read_text()
first_start = adoption.index('        const bool awaitedFirstPresentationFailed =')
first_end = adoption.index('        if (result.graphRequest.enabled)', first_start)
first_failure = adoption[first_start:first_end]
diagnostic_start = adoption.index('            if (!result.success && result.graphRequest.enabled &&')
diagnostic_end = adoption.index('            FinishViewportPreviewRequest(result, false);', diagnostic_start)
diagnostic = adoption[diagnostic_start:diagnostic_end]
raw_failure_feedback = method(adoption, 'if (!result.success && !result.rawWorkspace.sourceKey.empty()) {')
persistence = (root / 'src/Editor/Internal/EditorModulePersistence.cpp').read_text()
export_start = persistence.index('void EditorModule::CompleteRawWorkspaceExportRender(')
export_end = persistence.index('    std::string path =', export_start)
export_failure = persistence[export_start:export_end] + '}\n'
scheduling = (root / 'src/Editor/Internal/EditorModuleRenderScheduling.cpp').read_text()
gate_start = scheduling.index('    const bool rawWorkspaceActive = IsRawWorkspaceProjectActive();')
gate_end = scheduling.index('    if ((!m_RawWorkspaceRootTabActive', gate_start)
scheduler_gate = scheduling[gate_start:gate_end]
asset_source = (root / 'src/Editor/Internal/RawLab/EditorModuleRawAssets.cpp').read_text()
asset_start = asset_source.index('void EditorModule::AdoptRawAssetCapture(')
asset_end = asset_source.index('    auto asset=', asset_start)
asset_failure = asset_source[asset_start:asset_end] + '}\n'
preview_source = (root / 'src/Editor/Internal/EditorModulePreviewPublication.cpp').read_text()
preview_retry = method(preview_source, 'void EditorModule::ResetPreviewRequestForRetry(') + method(preview_source, 'void EditorModule::ResetIncompletePreviewRequestsForRetry()')
precise_source = (root / 'src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp').read_text()
precise_start = precise_source.index('    if (!decision.allowed || !source) {', precise_source.index('void EditorModule::HandleRawWorkspacePreciseSolveResult('))
precise_end = precise_source.index('    const Stack::PreciseIntegration::ProjectionSummary projection', precise_start)
precise_failure = precise_source[precise_start:precise_end]

tests = r'''
using Service=Stack::EditorRendering::RawRenderService;
using Snapshot=EditorRenderWorker::Snapshot;
using Result=EditorRenderWorker::Result;
Snapshot Request(std::uint64_t owner,std::uint64_t generation,RawRenderPurpose purpose=RawRenderPurpose::InteractivePresentation) {
    Snapshot value;value.ownerId=owner;value.generation=generation;value.schedulingSerial=generation;
    value.rawRenderPurpose=purpose;value.rawWorkspace.sourceKey="source-"+std::to_string(owner);
    value.rawWorkspace.sourceHash=100+owner;value.rawWorkspace.recipeRevision=200+generation;
    value.rawWorkspace.viewport.generation=300+generation;value.viewportViewId=1;
    value.viewportExecutionIdentity=400+generation;value.viewportContentIdentity=500+generation;
    return value;
}
void CheckFailure(const Result& result,const Snapshot& source,RawRenderPurpose purpose) {
    Require(!result.success && !result.error.empty() && !result.telemetry.superseded,"truthful terminal failure");
    Require(result.ownerId==source.ownerId && result.generation==source.generation && result.schedulingSerial==source.schedulingSerial,"owner/generation/attempt retained");
    Require(result.rawRenderPurpose==purpose && result.rawWorkspace.sourceKey==source.rawWorkspace.sourceKey && result.rawWorkspace.sourceHash==source.rawWorkspace.sourceHash && result.rawWorkspace.recipeRevision==source.rawWorkspace.recipeRevision,"purpose/source/recipe retained");
    Require(result.rawWorkspace.viewportGeneration==(source.viewportPreview.enabled?source.viewportPreview.generation:source.rawWorkspace.viewport.generation) && result.viewportContentIdentity==source.viewportContentIdentity,"viewport acceptance identity retained");
}
bool MutexAvailable(std::mutex& mutex) {
    bool acquired=false;
    std::thread contender([&] { acquired=mutex.try_lock();if(acquired) mutex.unlock(); });
    contender.join();
    return acquired;
}
Result ResourcePacket(const Snapshot& request,unsigned base) {
    auto result=EditorRenderWorker::MakeFailedResult(request,"completed");
    result.outputTexture.texture=base;result.outputTexture.readyFence=reinterpret_cast<void*>(base+10000);
    result.viewportOverview.texture=base+1;result.viewportOverview.readyFence=reinterpret_cast<void*>(base+10001);
    result.rawWorkspace.localRangeOverlayTexture.texture=base+2;result.rawWorkspace.localRangeOverlayTexture.readyFence=reinterpret_cast<void*>(base+10002);
    result.outputTiles.tiles.push_back({base+3});result.outputTiles.tiles.push_back({base+4});result.outputTiles.readyFence=reinterpret_cast<void*>(base+10003);
    return result;
}
void CheckPacketDeletes(unsigned base,int count) {
    for(unsigned i=0;i<5;++i) Require(textureDeletes[base+i]==count,"every packet texture retired exactly once");
    for(unsigned i=0;i<4;++i) Require(fenceDeletes[reinterpret_cast<void*>(base+10000+i)]==count,"every packet fence retired exactly once");
}
void WorkerRetirementLockBoundary() {
    GLFWwindow context;currentContext=&context;
    EditorRenderWorker worker;bool unlocked=true,correctContext=true;
    onResourceDelete=[&] { unlocked &= MutexAvailable(worker.m_Mutex);correctContext &= currentContext==&context; };
    auto request=Request(31,310);worker.m_OwnerStates[31].latestSerial=request.schedulingSerial;
    worker.m_Completed.push_back(ResourcePacket(Request(31,301),1000));
    worker.m_Completed.push_back(ResourcePacket(Request(32,302),1100));
    auto ack=ResourcePacket(Request(31,303),1200);ack.rawWorkspace.preciseSolveResult.emplace();ack.rawWorkspace.preciseSolveResult->canceled=true;worker.m_Completed.push_back(std::move(ack));
    auto newestAck=ResourcePacket(Request(31,304),1300);newestAck.rawWorkspace.preciseSolveResult.emplace();newestAck.rawWorkspace.preciseSolveResult->canceled=true;worker.m_Completed.push_back(std::move(newestAck));
    worker.m_FallbackCompleted.emplace(ResourcePacket(Request(31,305),1400));
    auto result=ResourcePacket(request,1500);worker.PublishCompletedResult(request,result);
    Require(unlocked && correctContext && request.generation==0,"worker supersession retires after unlock on current context");
    CheckPacketDeletes(1000,1);CheckPacketDeletes(1200,1);CheckPacketDeletes(1400,1);CheckPacketDeletes(1100,0);CheckPacketDeletes(1300,0);CheckPacketDeletes(1500,0);
    worker.HandleThreadFailure("later housekeeping failure",&request);
    currentContext=&context; // Fatal cleanup releases its context; published packets remain intact.
    Result consumed;consumed.outputTexture.texture=1550;for(auto generation:{302u,304u,310u}) {
        Require(worker.TryConsumeCompleted(consumed) && consumed.generation==generation,"other owners and newest cancellation acknowledgement preserve queue order");
        ReleaseResultResources(consumed);
    }
    Require(textureDeletes[1550]==1 && unlocked,"worker dequeuing destroys prior caller texture only after unlocking");
    Require(!worker.TryConsumeCompleted(consumed),"late failure cannot duplicate a published completion");
    CheckPacketDeletes(1100,1);CheckPacketDeletes(1300,1);CheckPacketDeletes(1500,1);
    Require(unlocked && correctContext,"all worker retirement callbacks observe released scheduling mutex");
    onResourceDelete={};
}
void WorkerRetirementFailureAndStop() {
    GLFWwindow context;currentContext=&context;
    EditorRenderWorker worker;bool unlocked=true;
    onResourceDelete=[&] { unlocked &= MutexAvailable(worker.m_Mutex); };
    worker.m_FallbackCompleted.emplace(ResourcePacket(Request(42,420),1600));
    worker.m_Completed.failNextPush=true;
    auto request=Request(41,410);auto result=ResourcePacket(request,1700);worker.PublishCompletedResult(request,result);
    Require(unlocked && worker.m_Completed.empty() && worker.m_FallbackCompleted->ownerId==42,"failed queue publication preserves another owner's fallback");
    CheckPacketDeletes(1700,1);CheckPacketDeletes(1600,0);
    worker.RequestStopForShutdown();auto stopped=Request(41,411);auto stoppedResult=ResourcePacket(stopped,1800);worker.PublishCompletedResult(stopped,stoppedResult);
    Require(stopped.generation==0 && unlocked,"requested stop retires unpublished packet after unlocking");CheckPacketDeletes(1800,1);
    worker.m_CompletedTimings.push_back(ResourcePacket(Request(41,412),1900));
    worker.HandleThreadFailure("shutdown drain",&stopped);CheckPacketDeletes(1900,1);
    currentContext=&context;
    worker.m_WorkerWindow=&context;GLFWwindow ui;currentContext=&ui;
    bool shutdownContext=true;onResourceDelete=[&] { unlocked &= MutexAvailable(worker.m_Mutex);shutdownContext &= currentContext==&context; };
    worker.m_CompletedViewportTimings.push_back(ResourcePacket(Request(41,413),2000));
    worker.Shutdown();CheckPacketDeletes(1600,1);CheckPacketDeletes(2000,1);
    Require(unlocked && shutdownContext && currentContext==&ui,"shutdown retirement unlocks before GL deletion and restores context afterwards");
    onResourceDelete={};
}
void ServiceRetirementLockBoundary() {
    Service service;GLFWwindow context;currentContext=&context;auto owner=service.Acquire(&context),other=service.Acquire(&context);
    bool unlocked=true,correctContext=true;
    onResourceDelete=[&] { unlocked &= MutexAvailable(service.m_Mutex) && MutexAvailable(service.m_Worker.m_Mutex);correctContext &= currentContext==&context; };
    auto old=ResourcePacket(Request(owner,510),2100);old.graphRequest.enabled=true;service.m_Completed[owner].push_back(std::move(old));
    service.m_Completed[other].push_back(ResourcePacket(Request(other,511),2200));
    auto fresh=ResourcePacket(Request(owner,512),2300);fresh.graphRequest.enabled=true;service.m_Worker.m_Completed.push_back(std::move(fresh));
    service.m_Worker.m_Completed.push_back(ResourcePacket(Request(999,513),2400));
    service.Pump();CheckPacketDeletes(2100,1);CheckPacketDeletes(2400,1);CheckPacketDeletes(2200,0);CheckPacketDeletes(2300,0);
    Require(unlocked && correctContext && service.m_Completed[owner].size()==1 && service.m_Completed[other].size()==1,"service routing/coalescing retires outside both locks on caller context");
    for(unsigned i=0;i<32;++i) { auto timing=EditorRenderWorker::MakeFailedResult(Request(owner,520+i),"timing");timing.timingOnly=true;service.m_Timings[owner].push_back(std::move(timing)); }
    service.m_Timings[owner].front()=ResourcePacket(Request(owner,520),2500);
    auto timing=EditorRenderWorker::MakeFailedResult(Request(owner,553),"timing");timing.timingOnly=true;service.m_Worker.m_Completed.push_back(std::move(timing));
    service.Pump();CheckPacketDeletes(2500,1);Require(service.m_Timings[owner].size()==32,"timing queue remains bounded after retirement");
    Result consumed;consumed.outputTexture.texture=2600;
    Require(service.TryConsumeCompleted(owner,consumed) && consumed.generation==512,"current service packet is delivered");
    Require(textureDeletes[2600]==1 && unlocked,"caller-owned previous texture is destroyed only after dequeue unlock");
    ReleaseResultResources(consumed);CheckPacketDeletes(2300,1);
    service.Release(other);CheckPacketDeletes(2200,1);
    service.Release(owner);
    Require(unlocked && correctContext,"service owner release never deletes under either scheduling mutex");onResourceDelete={};
}
void WorkerLifecycle() {
    EditorRenderWorker worker;
    auto active=Request(1,11);auto pending=Request(2,22);
    worker.m_Pending=pending;worker.m_HasPending=true;worker.m_Rendering=true;worker.m_Busy=true;
    auto completed=EditorRenderWorker::MakeFailedResult(Request(3,33),"ignored");
    completed.success=true;completed.error.clear();completed.outputTexture.texture=71;completed.outputTexture.readyFence=reinterpret_cast<void*>(72);
    worker.m_Completed.push_back(std::move(completed));
    worker.m_ActiveOpenGlTask=std::make_shared<EditorRenderWorker::OpenGlTaskState>();auto task=worker.m_ActiveOpenGlTask;
    worker.HandleThreadFailure("GPU host dispatch failed",&active);
    Require(worker.GetThreadFailure() && !worker.IsAvailable() && !worker.m_Rendering && !worker.m_Busy && !worker.m_HasPending,"fatal status and worker slot released");
    Require(task->completed && !task->success && task->error.find("GPU host dispatch failed")!=std::string::npos,"blocked GL caller released");
    Result result;Require(worker.TryConsumeCompleted(result) && result.ownerId==3 && result.success && result.outputTexture.texture==71,"valid completed result preserved");
    ReleaseResultResources(result);
    Require(worker.TryConsumeCompleted(result),"active completion exists");CheckFailure(result,active,active.rawRenderPurpose);
    Require(worker.TryConsumeCompleted(result),"pending completion exists");CheckFailure(result,pending,pending.rawRenderPurpose);
    Require(!worker.TryConsumeCompleted(result),"exactly one terminal per request");
    worker.m_ThreadFailure.clear();std::string message;Require(worker.GetThreadFailure(&message) && !message.empty(),"fatal bool survives missing allocated diagnostic");
}
void ParkedAndDedupe() {
    EditorRenderWorker worker;
    auto exported=Request(4,44,RawRenderPurpose::ExplicitExport);
    auto current=exported;worker.m_OwnerStates[4].continuation=std::move(current);worker.m_OwnerStates[4].resumeOriginalPurpose=true;
    worker.m_OwnerStates[4].denoise=std::make_shared<FakeDenoise>();
    auto denoised=Request(5,55);worker.m_OwnerStates[5].continuation=denoised;
    auto previous=EditorRenderWorker::MakeFailedResult(denoised,"ignored");previous.success=true;previous.error.clear();worker.m_Completed.push_back(std::move(previous));
    worker.HandleThreadFailure("fatal",&current);
    Result result;bool sawExport=false,sawDenoise=false;int count=0;
    while(worker.TryConsumeCompleted(result)) { ++count;if(result.success) continue;
        if(result.ownerId==4){CheckFailure(result,exported,RawRenderPurpose::ExplicitExport);sawExport=true;}
        if(result.ownerId==5){CheckFailure(result,denoised,RawRenderPurpose::DenoiseCompletion);sawDenoise=true;}
    }
    Require(sawExport && sawDenoise && count==3 && drainedModels>0,"parked exports and same-generation denoise complete, no moved-slot duplicate");
    EditorRenderWorker exact;auto request=Request(8,88);auto done=EditorRenderWorker::MakeFailedResult(request,"already failed");exact.m_Completed.push_back(std::move(done));exact.HandleThreadFailure("fatal",&request);Require(exact.m_Completed.size()==1,"already-published exact request not duplicated");
    EditorRenderWorker distinct;auto older=Request(8,88);older.schedulingSerial=1;distinct.m_Completed.push_back(EditorRenderWorker::MakeFailedResult(older,"old"));auto newer=older;newer.schedulingSerial=2;distinct.HandleThreadFailure("fatal",&newer);Require(distinct.m_Completed.size()==2,"same generation distinct attempt not dropped");
}
void CancelFallbackAndShutdown() {
    EditorRenderWorker canceled;auto request=Request(9,99);canceled.m_OwnerStates[9].released=true;canceled.HandleThreadFailure("fatal",&request);Require(canceled.m_Completed.empty(),"released owner not revived");
    EditorRenderWorker purposeCanceled;purposeCanceled.m_OwnerStates[9].continuation=request;purposeCanceled.m_OwnerStates[9].canceledPurposeBeforeSerial[RawRenderPurpose::DenoiseCompletion]=request.schedulingSerial+1;purposeCanceled.HandleThreadFailure("fatal",nullptr);Require(purposeCanceled.m_Completed.empty(),"canceled effective continuation purpose not revived");
    EditorRenderWorker stopped;stopped.RequestStopForShutdown();stopped.HandleThreadFailure("during shutdown",&request);Require(!stopped.GetThreadFailure() && stopped.m_Completed.empty(),"requested stop not relabeled fatal");
    EditorRenderWorker fallback;fallback.m_Completed.failNextPush=true;fallback.HandleThreadFailure("fatal",&request);Result result;Require(fallback.TryConsumeCompleted(result),"queue allocation failure uses fallback slot");CheckFailure(result,request,request.rawRenderPurpose);
    EditorRenderWorker cleanup;GLFWwindow ui,workerWindow;cleanup.m_WorkerWindow=&workerWindow;currentContext=&ui;
    auto kept=EditorRenderWorker::MakeFailedResult(request,"done");kept.outputTexture.texture=81;kept.outputTexture.readyFence=reinterpret_cast<void*>(82);kept.outputTiles.tiles.push_back({83});cleanup.m_Completed.push_back(std::move(kept));
    int textures=deletedTextures,fences=deletedFences;cleanup.Shutdown();Require(deletedTextures==textures+2 && deletedFences==fences+1 && currentContext==&ui && cleanup.m_Completed.empty(),"explicit shutdown releases retained publication and restores context");
}
void ServiceRouting() {
    Service service;GLFWwindow window;auto a=service.Acquire(&window),b=service.Acquire(&window);
    auto requestA=Request(a,101),requestB=Request(b,102,RawRenderPurpose::ExplicitExport);
    Require(service.Submit(a,requestA),"active accepted");auto active=std::move(service.m_Worker.m_Pending);service.m_Worker.m_HasPending=false;service.m_Worker.m_Rendering=true;service.m_Worker.m_RenderingOwner=a;
    Require(service.Submit(b,requestB),"second owner queued");
    service.m_Worker.HandleThreadFailure("driver boundary exception",&active);
    Result result;Require(service.TryConsumeCompleted(a,result),"active owner receives failure");CheckFailure(result,active,active.rawRenderPurpose);
    Require(service.TryConsumeCompleted(b,result),"queued owner receives failure");CheckFailure(result,requestB,requestB.rawRenderPurpose);
    Require(!service.TryConsumeCompleted(a,result) && !service.TryConsumeCompleted(b,result) && !service.IsBusyFor(a) && !service.IsBusyFor(b),"no duplicate or cross-owner completion, idle after failure");
    auto later=Request(b,103);later.graphRequest.enabled=true;Require(service.Submit(b,later),"dead worker accepts terminal result, not GPU work");Require(service.TryConsumeCompleted(b,result),"future request fails truthfully");CheckFailure(result,later,later.rawRenderPurpose);Require(result.graphRequest.enabled && !service.m_Worker.m_HasPending,"graph failure preserved without restart");
    // Model the publication/drain window: fatal status is set while model
    // owner state still exists. Service must not re-latch editor busy.
    service.m_Worker.m_Rendering=true;service.m_Worker.m_RenderingOwner=b;service.m_Worker.m_OwnerStates[b].denoiseInFlight=true;
    Require(!service.IsBusyFor(b) && !service.HasSnapshotWorkFor(b) && !service.GetProgressFor(b).busy,"fatal completion cannot re-latch pending before cleanup drain");
    service.Release(a);service.Release(b);
}
void NormalSupersession() {
    Service service;GLFWwindow window;auto id=service.Acquire(&window);
    auto old=Request(id,201,RawRenderPurpose::ExplicitInspection);Require(service.Submit(id,old),"old accepted");auto newer=Request(id,202);Require(service.Submit(id,newer),"new accepted");Result result;
    Require(service.TryConsumeCompleted(id,result) && result.generation==201 && result.telemetry.superseded && result.error=="Render superseded by a newer snapshot.","real supersession unchanged");
    service.Release(id);
    Service stopping;auto sid=stopping.Acquire(&window);stopping.m_Worker.RequestStopForShutdown();Require(!stopping.Submit(sid,Request(sid,301)),"normal shutdown rejected, not falsely accepted failure");stopping.Release(sid);
}
void PublicationRetirement() {
    EditorRenderWorker worker;auto request=Request(12,120);auto result=EditorRenderWorker::MakeFailedResult(request,"completed");
    worker.PublishCompletedResult(request,result);Require(request.generation==0,"publication retires request before consumer can take it");
    Result consumed;Require(worker.TryConsumeCompleted(consumed),"published result consumed");worker.HandleThreadFailure("later housekeeping failure",&request);Require(!worker.TryConsumeCompleted(consumed),"consumed publication not failed again");
    EditorRenderWorker fallback;auto second=Request(12,121);fallback.m_Completed.failNextPush=true;auto fallbackResult=EditorRenderWorker::MakeFailedResult(second,"completed");fallback.PublishCompletedResult(second,fallbackResult);Require(second.generation==0 && fallback.m_FallbackCompleted.has_value(),"fallback publication also retires identity");
}
void GraphAndOverview() {
    Service service;GLFWwindow window;auto owner=service.Acquire(&window);
    auto request=Request(owner,111);request.graphRequest.enabled=true;
    auto completed=EditorRenderWorker::MakeFailedResult(request,"ignored");completed.success=true;completed.error.clear();completed.outputTexture.texture=95;service.m_Worker.m_Completed.push_back(std::move(completed));
    service.m_Worker.m_OwnerStates[owner].continuation=request;service.m_Worker.m_OwnerStates[owner].continuationReady=true;
    service.m_Worker.HandleThreadFailure("fatal",nullptr);
    Result result;Require(service.TryConsumeCompleted(owner,result) && result.success && result.outputTexture.texture==95,"fatal routing preserves already-published graph success");ReleaseResultResources(result);
    Require(service.TryConsumeCompleted(owner,result) && !result.success && result.rawRenderPurpose==RawRenderPurpose::DenoiseCompletion,"distinct graph continuation failure survives");Require(!service.TryConsumeCompleted(owner,result),"ready continuation not failed twice");
    auto overview=Request(owner,112,RawRenderPurpose::ViewportOverview);overview.rawWorkspace.hasRecipe=true;overview.rawWorkspace.recipe=876;
    Require(service.Submit(owner,overview) && service.TryConsumeCompleted(owner,result) && result.rawWorkspace.presentationFingerprint==876,"overview failure carries fingerprint to suppress idle retry loop");service.Release(owner);
}
void AuxiliaryIdentity() {
    auto request=Request(7,77);request.viewportPreview.enabled=true;request.viewportPreview.generation=901;
    PreviewRequest preview;preview.assetCapture=1;preview.assetCaptureInput=true;preview.dirtyGeneration=654;preview.rawLayerId="layer";request.previews.push_back(preview);request.rawWorkspace.preciseSolveRequest=SolveRequest{987};
    auto result=EditorRenderWorker::MakeFailedResult(request,"");CheckFailure(result,request,request.rawRenderPurpose);
    Require(result.previews.size()==1 && result.previews[0].assetCapture && result.previews[0].assetCaptureInput && result.previews[0].dirtyGeneration==654,"capture terminal identity retained");
    Require(result.rawWorkspace.preciseSolveResult && result.rawWorkspace.preciseSolveResult->failed && result.rawWorkspace.preciseSolveResult->candidate.identity==987,"precise solve terminal identity retained");
}
void ContinuationPurposeOwnership() {
    for(bool dedicated:{false,true}) for(auto purpose:{RawRenderPurpose::Thumbnail,
            RawRenderPurpose::AnalysisScopes,RawRenderPurpose::InteractiveSample,
            RawRenderPurpose::CachePrewarm,RawRenderPurpose::ViewportOverview,
            RawRenderPurpose::InteractivePresentation,RawRenderPurpose::ViewportRefinement,
            RawRenderPurpose::DenoiseCompletion}) {
        EditorRenderWorker worker;auto request=Request(dedicated?0:7,77,purpose);
        request.rawSessionCommand=RawRenderPurposeMayPublishPresentation(purpose);
        if(request.rawSessionCommand) {
            auto mainTemplate=std::make_shared<Snapshot>();mainTemplate->outputConnected=true;
            request.rawSessionTemplate=mainTemplate;
        }
        Require(!request.outputConnected,"compact main and auxiliary payloads both default disconnected before expansion");
        PreviewRequest preview;preview.previewNodeId=123;preview.dirtyGeneration=456;request.previews.push_back(preview);
        auto& owner=worker.m_OwnerStates[request.ownerId];owner.continuation=request;
        owner.denoise=std::make_shared<FakeDenoise>();owner.denoise->pending=true;owner.latestSerial=request.schedulingSerial;
        std::promise<int> promise;owner.denoise->future=promise.get_future();worker.m_NextSchedulingSerial=100;
        const auto take=[&](Snapshot& next){return dedicated?worker.TakeDedicatedContinuationForTest(next):worker.TryConsumeReadyDenoiseSnapshot(next);};
        Snapshot next;
        for(int tick=0;tick<4;++tick) {
            worker.PollDenoiseContinuationsLocked();
            Require(!owner.continuationReady && !owner.resumeOriginalPurpose && !take(next),"pending auxiliary model must not hot-replay or bypass its future");
        }
        Require(worker.HasPendingForOwner(request.ownerId) && worker.IsWaitingForRawDenoiseCompletion(),"parked auxiliary continuation remains visible as busy work");
        promise.set_value(1);worker.PollDenoiseContinuationsLocked();
        Require(take(next) && !take(next),"ready continuation dequeues exactly once");
        const auto expected=RawRenderPurposeMayPublishPresentation(purpose)?RawRenderPurpose::DenoiseCompletion:purpose;
        Require(next.rawRenderPurpose==expected && owner.denoisePurpose==expected && !owner.resumeOriginalPurpose,"effective continuation purpose preserves the original consumer");
        Require(next.ownerId==request.ownerId && next.generation==request.generation && next.rawWorkspace.sourceKey==request.rawWorkspace.sourceKey && next.previews.size()==1 && next.previews[0].dirtyGeneration==456,"continuation retains owner/source/generation and preview ownership");
        Require(next.schedulingSerial==(dedicated?100:request.schedulingSerial),"dedicated replay renews serial and shared replay leaves renewal to service");
        if(!RawRenderPurposeMayPublishPresentation(purpose)) {
            auto result=worker.RenderAuxiliaryForTest(next);
            Require(result.success && result.error.empty() && worker.previewRenderCalls==1 && worker.mainRenderCalls==0,"ready RAW auxiliary branch renders feedback without inventing a missing-main failure or rerendering main");
        } else {
            Require(next.rawSessionCommand && next.rawSessionTemplate && next.rawSessionTemplate->outputConnected,"compact main command still becomes presentation completion before template expansion");
        }
    }
    for(bool dedicated:{false,true}) for(auto purpose:{RawRenderPurpose::ExplicitExport,RawRenderPurpose::ExplicitInspection}) {
        EditorRenderWorker worker;auto request=Request(dedicated?0:7,78,purpose);
        auto& owner=worker.m_OwnerStates[request.ownerId];owner.continuation=request;owner.resumeOriginalPurpose=true;
        worker.PollDenoiseContinuationsLocked();Snapshot next;
        Require((dedicated?worker.TakeDedicatedContinuationForTest(next):worker.TryConsumeReadyDenoiseSnapshot(next)) && next.rawRenderPurpose==purpose,"authoritative deferred resume retains original purpose");
    }
    for(int invalid=0;invalid<3;++invalid) {
        EditorRenderWorker worker;auto request=Request(9,79,RawRenderPurpose::Thumbnail);
        auto& owner=worker.m_OwnerStates[9];owner.continuation=request;owner.denoisePurpose=request.rawRenderPurpose;owner.denoise=std::make_shared<FakeDenoise>();owner.denoise->pending=true;
        std::promise<int> promise;owner.denoise->future=promise.get_future();
        worker.m_NextSchedulingSerial=request.schedulingSerial+1;
        if(invalid==0)worker.CancelOwnerSnapshots(9,RawRenderPurpose::Thumbnail);
        if(invalid==1)worker.InvalidateOwnerSnapshotsBefore(9,request.generation+1);
        if(invalid==2)worker.ReleaseOwner(9);
        promise.set_value(1);worker.PollDenoiseContinuationsLocked();Snapshot next;
        Require(!worker.TryConsumeReadyDenoiseSnapshot(next) && !worker.HasDenoiseWorkLocked(),"canceled, newer-generation or released auxiliary ownership cannot revive after readiness");
    }
    for(auto purpose:{RawRenderPurpose::Thumbnail,RawRenderPurpose::AnalysisScopes,
            RawRenderPurpose::InteractiveSample,RawRenderPurpose::CachePrewarm,RawRenderPurpose::ViewportOverview}) {
        EditorRenderWorker worker;auto request=Request(10,80,purpose);worker.m_OwnerStates[10].continuation=request;
        worker.HandleThreadFailure("model worker unavailable",nullptr);Result result;
        Require(worker.TryConsumeCompleted(result),"parked auxiliary gets its terminal failure");CheckFailure(result,request,purpose);
        Require(!worker.TryConsumeCompleted(result),"parked auxiliary terminal failure reports once");
        EditorRenderWorker canceled;canceled.m_OwnerStates[10].continuation=request;
        canceled.m_OwnerStates[10].canceledPurposeBeforeSerial[purpose]=request.schedulingSerial+1;
        canceled.HandleThreadFailure("model worker unavailable",nullptr);
        Require(canceled.m_Completed.empty(),"fatal reporting honors original auxiliary purpose cancellation");
    }
}
'''
consumer = r'''
namespace Async { enum class TaskState { Failed }; }
namespace Stack::PreciseIntegration {
enum class LifecycleState { Failed,Blocked };
struct State { bool active=true;LifecycleState state=LifecycleState::Blocked; };
void MarkTerminal(State& state,LifecycleState result,const std::string&) { state.active=false;state.state=result; }
}
enum class UiNotificationSeverity { Error };
namespace Stack::PixelBuffer { bool HasCompletePixelBuffer(std::size_t,int,int,int) { return false; } }
namespace Stack::EditorModuleTypes {
struct StartingPointQueueProbe { int checks=0,clears=0; };
bool ShouldClearRawStartingPointCandidateRenderQueueForFailedResult(StartingPointQueueProbe& queue,const std::string&,const std::string&,std::uint64_t,std::uint64_t) { ++queue.checks;return true; }
void ClearRawStartingPointCandidateRenderQueue(StartingPointQueueProbe& queue) { ++queue.clears; }
template<class T> bool MarkRawStartingPointRenderFailure(T& ui,const std::string&) { ++ui.failureChecks;return false; }
}
struct DeferredLoadedProjectApplyState { enum class Step { WaitForFirstRender }; bool active=true; Step step=Step::WaitForFirstRender; std::uint64_t acceptedRenderGenerationAtStart=0; };
class EditorModule {
public:
    bool m_RenderPending=true,m_RawWorkspaceExportRenderRequested=true,m_RenderDirty=false;
    struct Capture { bool pending=false,armed=false,drawing=false,feedbackNeeded=false,feedbackDirty=false;std::optional<int> target=7,application;std::uint64_t serial=0;std::shared_ptr<int> feedback;std::string status; } m_RawAssetCapture;
    Capture QueryRawOperationMaskAvailability() { return {}; }
    bool RawAssetCaptureInputMatches() { return true; }
    bool IsRawWorkspaceProjectActive() { return true; }
    std::unordered_map<int,std::uint64_t> m_PreviewRequestedGenerations,m_PreviewCompletedGenerations;
    struct AutoUi { std::string summary;Stack::PreciseIntegration::State preciseStartingPoint;int failureChecks=0; } m_RawWorkspaceAutoBaseUi;
    Stack::EditorModuleTypes::StartingPointQueueProbe m_RawWorkspaceStartPointCandidateRenderQueue;
    std::vector<int> m_RawWorkspaceStartPointCandidateRenderResults;
    std::string m_RawWorkspaceStaleRenderStatusText,lastNotice,lastNoticeId;
    std::uint64_t m_RenderRevision=9,m_LastSubmittedRenderRevision=9;
    template<class... T> void SetStartingPointUiResult(T&&...) {}
    int preparationCount=0;
    void SchedulerGate();
    void AdoptRawAssetCapture(PreviewResult&);
    void ResetPreviewRequestForRetry(int,std::uint64_t);
    void ResetIncompletePreviewRequestsForRetry();
    void AdoptPreciseFailure(const SolveResult&);
    std::uint64_t m_RawWorkspaceExportRenderGeneration=0;
    std::string m_RawWorkspaceExportPath="unused",m_RawWorkspaceExportColorChunks,m_RawWorkspaceExportFileTarget,m_ExportStatusText;
    Async::TaskState m_ExportTaskState=Async::TaskState::Failed;
    DeferredLoadedProjectApplyState m_DeferredLoadedProjectApply;
    std::unordered_map<std::uint64_t,int> m_HdrMergeSubmittedNodesByGeneration;
    bool failedLoad=false,currentViewport=true,graphViewport=true,m_RawWorkspaceRootTabActive=true; int notices=0;
    struct PreviewState { ViewportRequest request; std::size_t contentIdentity=0; } currentView;
    std::uint64_t m_RawRenderClientId=15,m_RenderGeneration=150,m_GraphAcceptedResultGeneration=150;
    Stack::GraphRendering::RequestTag currentTag;
    bool IsViewportPreviewResultCurrent(const Result&) { return currentViewport; }
    bool UsesGraphViewportPreview() const { return graphViewport; }
    const PreviewState& ActiveViewportPreviewState() const { return currentView; }
    Stack::GraphRendering::RequestTag CurrentGraphRenderTag() { return currentTag; }
    void ReportGraphRenderFailure(const Result&) { ++notices; }
    void DiagnoseStaleFailure(const Result& result);
    bool IsAnyRenderBackendBusy() { return false; }
    void QueueViewportOutputTextureRelease(SharedTexture& value) { value.Reset(); }
    void QueueViewportOutputTileSetRelease(EditorRenderWorker::Tiles&) {}
    void ResetIncompleteCompositeOutputRequestsForRetry() {}
    void FailDeferredLoadedProjectApply(const std::string&) { failedLoad=true;m_DeferredLoadedProjectApply.active=false; }
    void PostNotification(UiNotificationSeverity,const std::string& text,const char* id) { ++notices;lastNotice=text;lastNoticeId=id; }
    void RawFailureFeedback(const Result&,bool);
    void CompleteRawWorkspaceExportRender(EditorRenderWorker::Result&,bool);
    void AdoptFirstFailure(Result result) { for(int once=0;once<1;++once) {
''' + first_failure + r'''
    } }
};
''' + asset_failure + preview_retry + 'void EditorModule::SchedulerGate() {\n' + scheduler_gate + '++preparationCount; }\n' + 'void EditorModule::AdoptPreciseFailure(const SolveResult& result) { auto& precise=m_RawWorkspaceAutoBaseUi.preciseStartingPoint;struct Decision { bool allowed=false;std::string reason; } decision; bool source=false;\n' + precise_failure + '}\n' + export_failure + 'void EditorModule::DiagnoseStaleFailure(const Result& result) { const bool rawResult=!result.rawWorkspace.sourceKey.empty();\n' + diagnostic + '}\n' + '''void EditorModule::RawFailureFeedback(const Result& result,bool rawWorkspaceResultMatchesActive) {
    const bool resultMayPublishPresentation=RawRenderPurposeMayPublishPresentation(result.rawRenderPurpose);
    const std::string activePreviewIdentity="source-15";
    const std::uint64_t activeRawWorkspaceSourceHash=115;
''' + raw_failure_feedback + '\n}\n' + r'''
void AuxiliaryFailureFeedback() {
    auto request=Request(15,150);auto result=EditorRenderWorker::MakeFailedResult(request,"main render failed");
    EditorModule main;main.RawFailureFeedback(result,true);
    Require(main.m_RawWorkspaceStaleRenderStatusText==result.error && main.lastNotice=="Preview retained as stale: main render failed" && main.lastNoticeId=="raw-workspace-render-stale","presentation failure retains existing stale-main diagnostic");
    Require(main.m_RawWorkspaceStartPointCandidateRenderQueue.clears==1 && main.m_RawWorkspaceAutoBaseUi.failureChecks==1,"presentation failure keeps existing matching starting-point cleanup");
    for(auto purpose:{RawRenderPurpose::Thumbnail,RawRenderPurpose::AnalysisScopes,RawRenderPurpose::InteractiveSample}) {
        result.rawRenderPurpose=purpose;result.error="auxiliary compute failed";
        EditorModule auxiliary;auxiliary.RawFailureFeedback(result,true);
        Require(auxiliary.m_RawWorkspaceStaleRenderStatusText.empty() && auxiliary.lastNotice=="RAW feedback render failed: auxiliary compute failed" && auxiliary.lastNoticeId=="raw-workspace-feedback-failed","matching failed auxiliary reports without falsely marking main image stale");
        Require(auxiliary.m_RawWorkspaceStartPointCandidateRenderQueue.clears==1 && auxiliary.m_RawWorkspaceAutoBaseUi.failureChecks==1,"auxiliary diagnostic classification does not bypass existing matching consumer cleanup");
        auxiliary.m_RawWorkspaceStaleRenderStatusText="earlier main failure";result.error.clear();auxiliary.RawFailureFeedback(result,true);
        Require(auxiliary.m_RawWorkspaceStaleRenderStatusText=="earlier main failure" && auxiliary.lastNotice=="RAW feedback render failed.","auxiliary default diagnostic preserves an existing main failure instead of erasing it");
        EditorModule superseded;result.error="Render superseded by a newer snapshot.";superseded.RawFailureFeedback(result,true);
        Require(!superseded.notices && superseded.m_RawWorkspaceStaleRenderStatusText.empty() && superseded.m_RenderDirty && superseded.m_LastSubmittedRenderRevision==8,"normal supersession keeps silent status and existing retry bookkeeping");
        Require(superseded.m_RawWorkspaceStartPointCandidateRenderQueue.clears==1 && superseded.m_RawWorkspaceAutoBaseUi.failureChecks==1,"superseded starting-point cleanup remains unchanged");
        EditorModule mismatch;result.error="other source failed";mismatch.RawFailureFeedback(result,false);
        Require(!mismatch.notices && mismatch.m_RawWorkspaceStaleRenderStatusText.empty() && mismatch.m_RawWorkspaceStartPointCandidateRenderQueue.clears==1 && !mismatch.m_RawWorkspaceAutoBaseUi.failureChecks,"source mismatch remains silent and retains original cleanup gates");
        EditorModule success;result.success=true;result.error.clear();success.RawFailureFeedback(result,true);
        Require(!success.notices && success.m_RawWorkspaceStaleRenderStatusText.empty() && !success.m_RawWorkspaceStartPointCandidateRenderQueue.checks && !success.m_RawWorkspaceAutoBaseUi.failureChecks,"successful auxiliary baseline bypasses failure feedback and cleanup");result.success=false;
    }
}
void ContinuationFailureDiagnostic() {
    auto request=Request(15,150,RawRenderPurpose::DenoiseCompletion);request.graphRequest.enabled=true;request.graphRequest.sourceIdentity=77;request.graphRequest.outputNodeId=9;request.graphRequest.revision=123;
    request.viewportPreview.enabled=true;request.viewportPreview.generation=901;
    auto result=EditorRenderWorker::MakeFailedResult(request,"fatal");EditorModule editor;editor.currentTag=request.graphRequest;
    editor.currentView.request.generation=result.viewportPreview.generation;editor.currentView.contentIdentity=result.viewportContentIdentity;
    editor.DiagnoseStaleFailure(result);Require(editor.notices==1 && editor.m_GraphAcceptedResultGeneration==150,"accepted provisional failure emits diagnostic only");
    editor.notices=0;result.ownerId=99;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"other owner fatal suppressed");result.ownerId=15;
    result.graphRequest.sourceIdentity=78;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"replaced graph source suppressed");result.graphRequest.sourceIdentity=77;
    result.graphRequest.revision=122;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"old graph edit suppressed");result.graphRequest.revision=123;
    result.generation=149;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"old generation suppressed");result.generation=150;
    result.telemetry.superseded=true;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"superseded failure suppressed");result.telemetry.superseded=false;
    ++editor.currentView.request.generation;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"old fatal viewport suppressed");--editor.currentView.request.generation;
    result.workerFailed=false;result.error="AI denoise inference failed";
    Require(!Stack::GraphRendering::MayAdopt(result.graphRequest,editor.currentTag,result.generation,editor.m_GraphAcceptedResultGeneration),"same-generation continuation pixels remain inadmissible");
    editor.DiagnoseStaleFailure(result);Require(editor.notices==1 && editor.m_GraphAcceptedResultGeneration==150,"ordinary denoise continuation reports without adopting pixels");editor.notices=0;
    // Failures without a regional payload still retain the original view ID,
    // camera generation and content identity. The flag cannot erase that owner.
    result.viewportPreview.enabled=false;
    editor.DiagnoseStaleFailure(result);Require(editor.notices==1,"current nonfatal continuation reports without a regional payload");editor.notices=0;
    ++editor.currentView.request.generation;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"disabled viewport flag cannot hide obsolete camera");--editor.currentView.request.generation;
    ++editor.currentView.contentIdentity;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"disabled viewport flag cannot hide obsolete content");--editor.currentView.contentIdentity;
    editor.m_RawWorkspaceRootTabActive=false;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"other view suppressed");editor.m_RawWorkspaceRootTabActive=true;
    editor.currentView.request.empty=true;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"empty viewport suppressed");editor.currentView.request.empty=false;
    editor.graphViewport=false;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"disabled graph viewport suppressed");editor.graphViewport=true;
    result.viewportViewId=9;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"unknown view suppressed");result.viewportViewId=1;
    result.success=true;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"successful continuation never reported as failure");result.success=false;
    result.rawRenderPurpose=RawRenderPurpose::InteractivePresentation;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"unrelated ordinary stale failure stays discarded");result.rawRenderPurpose=RawRenderPurpose::DenoiseCompletion;
    result.error="Render superseded by a newer snapshot.";editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"cancellation error remains silent even without telemetry flag");result.error="AI denoise inference failed";
    result.telemetry.superseded=true;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"superseded nonfatal continuation stays silent");result.telemetry.superseded=false;
    editor.m_RenderGeneration=151;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"newer requested generation suppresses continuation failure");editor.m_RenderGeneration=150;
    editor.m_GraphAcceptedResultGeneration=151;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"newer accepted generation suppresses continuation failure");editor.m_GraphAcceptedResultGeneration=150;
    result.viewportViewId=0;result.ownerId=0;result.rawWorkspace.sourceKey.clear();editor.DiagnoseStaleFailure(result);Require(editor.notices==1,"dedicated full-image graph failure reports without viewport identity");editor.notices=0;
    editor.currentViewport=false;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"existing non-view currentness gate retained");
}
void FatalIdleGuard() {
    auto& service=Service::Get();GLFWwindow window;auto owner=service.Acquire(&window);service.m_Worker.HandleThreadFailure("fatal",nullptr);
    EditorModule editor;editor.m_RawRenderClientId=owner;editor.m_RenderPending=false;
    editor.m_PreviewRequestedGenerations[9]=1;editor.ResetPreviewRequestForRetry(9,1);editor.ResetIncompletePreviewRequestsForRetry();
    Require(!editor.m_RenderDirty && editor.m_PreviewRequestedGenerations.empty(),"ordinary retry reset does not dirty render and defeat fatal idle gate");
    for(int frame=0;frame<100;++frame) editor.SchedulerGate();
    Require(editor.preparationCount==0 && editor.notices==0 && service.m_Pending.empty() && !service.m_Worker.m_HasPending,"100 fatal idle frames neither prepare nor submit/notify");
    auto submitCapture=[&](bool feedback,std::uint64_t generation) {
        editor.m_RawAssetCapture.pending=!feedback;editor.m_RawAssetCapture.armed=feedback;editor.m_RawAssetCapture.feedbackNeeded=feedback;editor.m_RawAssetCapture.serial=generation;
        int prepared=editor.preparationCount;editor.SchedulerGate();Require(editor.preparationCount==prepared+1,"explicit post-failure capture bypasses idle gate once");
        auto capture=Request(owner,generation,RawRenderPurpose::Thumbnail);PreviewRequest preview;preview.assetCapture=7;preview.assetCaptureInput=feedback;preview.dirtyGeneration=generation;capture.previews.push_back(preview);
        Require(service.Submit(owner,capture),"post-failure capture accepted terminal");Result result;Require(service.TryConsumeCompleted(owner,result) && result.previews.size()==1,"post-failure capture completion arrives");editor.AdoptRawAssetCapture(result.previews[0]);
        Require(!editor.m_RawAssetCapture.pending && !editor.m_RawAssetCapture.feedbackNeeded && !editor.m_RawAssetCapture.status.empty(),"exact capture failure adoption releases pending/feedback");
        int notices=editor.notices;prepared=editor.preparationCount;for(int frame=0;frame<100;++frame) editor.SchedulerGate();Require(editor.preparationCount==prepared && editor.notices==notices,"terminal capture does not repeatedly bypass gate");
    };
    submitCapture(false,401);submitCapture(true,402);
    editor.m_RenderDirty=true;int prepared=editor.preparationCount;editor.SchedulerGate();Require(editor.preparationCount==prepared+1,"dirty explicit export permitted");auto exported=Request(owner,403,RawRenderPurpose::ExplicitExport);Require(service.Submit(owner,exported),"dirty export terminal accepted");Result result;Require(service.TryConsumeCompleted(owner,result),"dirty export result");editor.m_RawWorkspaceExportRenderGeneration=403;editor.CompleteRawWorkspaceExportRender(result,true);editor.m_RenderDirty=false;Require(!editor.m_RawWorkspaceExportRenderRequested,"explicit export releases pending");
    editor.m_RenderDirty=true;prepared=editor.preparationCount;editor.SchedulerGate();Require(editor.preparationCount==prepared+1,"dirty precise solve permitted");auto precise=Request(owner,404);precise.rawWorkspace.preciseSolveRequest=SolveRequest{55};Require(service.Submit(owner,precise) && service.TryConsumeCompleted(owner,result) && result.rawWorkspace.preciseSolveResult,"precise terminal arrives");editor.AdoptPreciseFailure(*result.rawWorkspace.preciseSolveResult);editor.m_RenderDirty=false;Require(!editor.m_RawWorkspaceAutoBaseUi.preciseStartingPoint.active && editor.m_RawWorkspaceAutoBaseUi.preciseStartingPoint.state==Stack::PreciseIntegration::LifecycleState::Failed,"precise failure branch releases expected operation");
    prepared=editor.preparationCount;for(int frame=0;frame<100;++frame) editor.SchedulerGate();Require(editor.preparationCount==prepared,"explicit dirty request does not arm idle retry");
    service.Release(owner);
    auto liveOwner=service.Acquire(&window);service.m_Worker.m_ThreadFailed=false;service.m_Worker.m_StopRequested=false;editor.m_RawRenderClientId=liveOwner;prepared=editor.preparationCount;editor.SchedulerGate();Require(editor.preparationCount==prepared+1,"nonfatal idle preview retry unchanged");service.Release(liveOwner);
}
void AdoptionTerminalState() {
    auto request=Request(15,150);auto result=EditorRenderWorker::MakeFailedResult(request,"fatal");
    Require(Stack::EditorRendering::ShouldAdoptRawPresentation(result.generation,result.rawWorkspace.recipeRevision,request.generation,request.rawWorkspace.recipeRevision,0),"failed RAW packet passes current presentation predicate");
    EditorModule first;first.AdoptFirstFailure(std::move(result));Require(!first.m_RenderPending && first.failedLoad && !first.m_DeferredLoadedProjectApply.active,"awaited first-render failure releases logical pending and load wait");
    auto exported=Request(15,151,RawRenderPurpose::ExplicitExport);auto failure=EditorRenderWorker::MakeFailedResult(exported,"worker stopped");
    EditorModule editor;editor.m_RawWorkspaceExportRenderGeneration=151;editor.CompleteRawWorkspaceExportRender(failure,true);Require(!editor.m_RawWorkspaceExportRenderRequested && editor.m_RawWorkspaceExportRenderGeneration==0 && editor.m_ExportStatusText=="worker stopped","export failure releases expected request and preserves reason");
}
'''
main = r'''
int main() {
    try { WorkerRetirementLockBoundary();WorkerRetirementFailureAndStop();ServiceRetirementLockBoundary();WorkerLifecycle();ParkedAndDedupe();CancelFallbackAndShutdown();ServiceRouting();NormalSupersession();PublicationRetirement();GraphAndOverview();AuxiliaryIdentity();ContinuationPurposeOwnership();AdoptionTerminalState();AuxiliaryFailureFeedback();ContinuationFailureDiagnostic();FatalIdleGuard();std::cout<<"render worker failure and retirement lifecycle: PASS\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
'''
args.output.write_text(pre + header + '\n' + methods + service + '\n' + tests + consumer + main)
