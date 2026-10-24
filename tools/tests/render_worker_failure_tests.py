#!/usr/bin/env python3
"""Generate bounded no-GPU checks from production worker/service method bodies.

The request/result payloads are reduced to lifecycle identity; platform, GPU and
model operations are fakes. Worker failure/publication, owner validation, drain,
service scheduling/routing and explicit shutdown are production method bodies.
Selected failure consumers and the fatal-idle scheduling gate are also copied
unchanged. This checks lifecycle decisions, not the full worker/UI render loop.
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
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
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
GLFWwindow* currentContext=nullptr;
void glfwMakeContextCurrent(GLFWwindow* context) { currentContext=context; }
GLFWwindow* glfwGetCurrentContext() { return currentContext; }
void glfwDestroyWindow(GLFWwindow*) { ++destroyedWindows; }
void glDeleteTextures(int n, const unsigned*) { deletedTextures+=n; }
void glDeleteSync(GLsync) { ++deletedFences; }
namespace Raw {
enum class OpenGlTaskPriority { Foreground, Background };
namespace ViewportGpuTiming { void Release(int&) {} }
}
struct FakeDenoise { void RequestCancellation() {} void CancelAndWait() { ++drainedModels; } };
struct RenderPipeline {};
struct ViewportRequest { bool enabled=false; std::uint64_t generation=0; };
struct ViewportTilingSettings {};
struct PreviewRequest { std::optional<int> assetCapture; bool assetCaptureInput=false; std::string rawLayerId,rawMaskId; int previewNodeId=-1; std::uint64_t dirtyGeneration=0; };
struct PreviewResult { bool assetCapture=false,assetCaptureInput=false,assetCaptureSuperseded=false,success=false; std::shared_ptr<int> assetFeedback;std::optional<int> capturedAsset; std::string rawLayerId,rawMaskId,error; int previewNodeId=-1; std::uint64_t dirtyGeneration=0; };
struct SolveRequest { std::uint64_t identity=0; };
struct SolveResult { bool attempted=false,failed=false; struct Candidate { std::uint64_t identity=0; std::string reason; } candidate; std::string reason; };
struct RawWorkspaceSnapshot { std::string sourceKey; std::uint64_t sourceHash=0; std::size_t recipeRevision=0; bool hasRecipe=false; int recipe=123; ViewportRequest viewport; std::optional<int> cachePrewarmStage; std::size_t cachePrewarmFingerprint=0; std::optional<SolveRequest> preciseSolveRequest; };
struct RawWorkspaceResult { std::string sourceKey; std::uint64_t sourceHash=0,viewportGeneration=0; std::size_t recipeRevision=0,presentationFingerprint=0; std::optional<int> cachePrewarmStage; std::size_t cachePrewarmFingerprint=0; std::optional<SolveResult> preciseSolveResult; };
struct SharedTexture {
    unsigned texture=0; GLsync readyFence=nullptr;
    SharedTexture()=default;
    SharedTexture(SharedTexture&& other) noexcept : texture(std::exchange(other.texture,0)),readyFence(std::exchange(other.readyFence,nullptr)) {}
    SharedTexture& operator=(SharedTexture&& other) noexcept { if(this!=&other) { Reset();texture=std::exchange(other.texture,0);readyFence=std::exchange(other.readyFence,nullptr); } return *this; }
    void Reset() { if(texture) glDeleteTextures(1,&texture); if(readyFence) glDeleteSync(readyFence); texture=0;readyFence=nullptr; }
};
struct WorkspaceResult : RawWorkspaceResult { SharedTexture localRangeOverlayTexture; };
template<class T> struct FailureDeque : std::deque<T> {
    bool failNextPush=false;
    void push_back(T&& value) { if(std::exchange(failNextPush,false)) throw std::bad_alloc(); std::deque<T>::push_back(std::move(value)); }
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
        std::uint64_t gestureId=0; RawPreviewSkipReason skipReason=RawPreviewSkipReason::None;
    };
    struct Snapshot {
        std::uint64_t ownerId=0,generation=0,schedulingSerial=0,lastAcceptedGeneration=0;
        Stack::GraphRendering::RequestTag graphRequest;
        RawRenderPurpose rawRenderPurpose=RawRenderPurpose::InteractivePresentation;
        RawRenderTelemetry telemetry; int previewMaxDimension=0;
        RawWorkspaceSnapshot rawWorkspace; ViewportRequest viewportPreview;
        int viewportViewId=0; std::size_t viewportExecutionIdentity=0,viewportContentIdentity=0;
        bool prepareViewportOverview=false; int graph=0; ViewportTilingSettings viewportTiling;
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
        bool timingOnly=false,success=false,workerFailed=false; std::string error; std::vector<unsigned char> pixels; int width=0,height=0;
        SharedTextureResult outputTexture,viewportOverview; Tiles outputTiles;
        std::vector<PreviewResult> previews;
    };
    struct RenderProgress { bool busy=false; int completedSteps=0,totalSteps=0; std::string label; };
    struct OwnerRenderState {
        std::shared_ptr<FakeDenoise> denoise; std::optional<Snapshot> continuation;
        bool continuationReady=false,resumeOriginalPurpose=false,denoiseInFlight=false,released=false;
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
    FailureDeque<Result> m_Completed;std::queue<Result> m_CompletedTimings,m_CompletedViewportTimings;std::optional<Result> m_FallbackCompleted;
    std::unordered_map<std::uint64_t,OwnerRenderState> m_OwnerStates;std::vector<DrainingDenoiseState> m_DrainingDenoise;
    int m_ProgressCompletedSteps=0,m_ProgressTotalSteps=0;std::string m_ProgressLabel;
    std::unique_ptr<RenderPipeline> m_CalibrationPipeline,m_PersistentPipeline;
    struct Timing { int batch=0; };std::vector<Timing> m_PendingViewportTimings;
    bool Initialize(GLFWwindow* window) { m_WorkerWindow=window;return true; }
    bool ExecuteOpenGlTaskBlocking(OpenGlTask,std::string&,Raw::OpenGlTaskPriority) { return false; }
    void CancelOwnerSnapshots(std::uint64_t,std::optional<RawRenderPurpose> = {}) {}
    void InvalidateOwnerSnapshotsBefore(std::uint64_t,std::uint64_t) {}
    void ReleaseOwner(std::uint64_t owner) { m_OwnerStates[owner].released=true; }
    void PublishForTest(Snapshot&,Result);
    void HandleThreadFailure(const char*,const Snapshot*) noexcept;
    bool GetThreadFailure(std::string* = nullptr) const noexcept;
    static Result MakeFailedResult(const Snapshot&,const std::string&);
    bool Submit(Snapshot);bool TryConsumeCompleted(Result&);bool TryConsumeViewportTiming(Result&);
    bool IsOwnerSnapshotInvalidLocked(std::uint64_t,std::uint64_t,std::uint64_t,RawRenderPurpose) const;
    bool HasPendingForOwner(std::uint64_t) const;bool HasSnapshotWorkFor(std::uint64_t) const;
    bool HasPendingSnapshotOrRender() const;bool HasDenoiseWorkLocked() const;
    bool IsWaitingForRawDenoiseCompletion() const;bool IsAvailable() const;
    bool TryConsumeReadyDenoiseSnapshot(Snapshot&);RenderProgress GetProgress() const;
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
methods = ''
for signature in [
    'void EditorRenderWorker::HandleThreadFailure(',
    'void EditorRenderWorker::RequestStopForShutdown()',
    'void EditorRenderWorker::Shutdown()',
    'bool EditorRenderWorker::GetThreadFailure(',
    'EditorRenderWorker::Result EditorRenderWorker::MakeFailedResult(',
    'bool EditorRenderWorker::Submit(',
    'bool EditorRenderWorker::TryConsumeCompleted(',
    'bool EditorRenderWorker::HasPendingSnapshotOrRender()',
    'bool EditorRenderWorker::IsWaitingForRawDenoiseCompletion()',
    'bool EditorRenderWorker::IsAvailable()',
    'EditorRenderWorker::RenderProgress EditorRenderWorker::GetProgress()']:
    methods += method(worker, signature)
for signature in [
    'bool EditorRenderWorker::IsOwnerSnapshotInvalidLocked(',
    'bool EditorRenderWorker::HasDenoiseWorkLocked()',
    'bool EditorRenderWorker::HasPendingForOwner(',
    'bool EditorRenderWorker::HasSnapshotWorkFor(',
    'bool EditorRenderWorker::TryConsumeReadyDenoiseSnapshot(',
    'void EditorRenderWorker::DrainDenoiseForShutdown()']:
    methods += method(sessions, signature)
methods += method(timing, 'bool EditorRenderWorker::TryConsumeViewportTiming(')
header = (root / 'src/Editor/RawRenderService.h').read_text()
header = '\n'.join(line for line in header.splitlines() if not line.startswith('#include') and line != '#pragma once')
# Production service layout, made constructible solely in this generated fixture.
header = header.replace('private:', 'public:')
service = '\n'.join(line for line in service.splitlines() if not line.startswith('#include'))
publication_start = worker.index('            bool cancellationAcknowledgementQueued = false;')
publication_end = worker.index('            m_Busy = ', publication_start)
publication = worker[publication_start:publication_end]
methods += 'void EditorRenderWorker::PublishForTest(Snapshot& snapshot, Result result) { std::optional<Result> cancellationAcknowledgement;\n' + publication + 'Require(resultQueued != resultStoredWithoutQueueAllocation, "exactly one publication path owns the result");\n}\n'
planning = (root / 'src/Editor/RawRenderPlanning.h').read_text()
methods += 'namespace Stack::EditorRendering {\n' + method(planning, 'inline bool ShouldAdoptRawPresentation(') + '}\n'
adoption = (root / 'src/Editor/Internal/EditorModuleRenderAdoption.cpp').read_text()
first_start = adoption.index('        const bool awaitedFirstPresentationFailed =')
first_end = adoption.index('        if (result.graphRequest.enabled)', first_start)
first_failure = adoption[first_start:first_end]
diagnostic_start = adoption.index('            if (result.workerFailed && result.graphRequest.enabled &&')
diagnostic_end = adoption.index('            FinishViewportPreviewRequest(result, false);', diagnostic_start)
diagnostic = adoption[diagnostic_start:diagnostic_end]
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
    worker.PublishForTest(request,std::move(result));Require(request.generation==0,"publication retires request before consumer can take it");
    Result consumed;Require(worker.TryConsumeCompleted(consumed),"published result consumed");worker.HandleThreadFailure("later housekeeping failure",&request);Require(!worker.TryConsumeCompleted(consumed),"consumed publication not failed again");
    EditorRenderWorker fallback;auto second=Request(12,121);fallback.m_Completed.failNextPush=true;fallback.PublishForTest(second,EditorRenderWorker::MakeFailedResult(second,"completed"));Require(second.generation==0 && fallback.m_FallbackCompleted.has_value(),"fallback publication also retires identity");
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
struct DeferredLoadedProjectApplyState { enum class Step { WaitForFirstRender }; bool active=true; Step step=Step::WaitForFirstRender; std::uint64_t acceptedRenderGenerationAtStart=0; };
class EditorModule {
public:
    bool m_RenderPending=true,m_RawWorkspaceExportRenderRequested=true,m_RenderDirty=false;
    struct Capture { bool pending=false,armed=false,drawing=false,feedbackNeeded=false,feedbackDirty=false;std::optional<int> target=7,application;std::uint64_t serial=0;std::shared_ptr<int> feedback;std::string status; } m_RawAssetCapture;
    Capture QueryRawOperationMaskAvailability() { return {}; }
    bool RawAssetCaptureInputMatches() { return true; }
    bool IsRawWorkspaceProjectActive() { return true; }
    std::unordered_map<int,std::uint64_t> m_PreviewRequestedGenerations,m_PreviewCompletedGenerations;
    struct AutoUi { std::string summary;Stack::PreciseIntegration::State preciseStartingPoint; } m_RawWorkspaceAutoBaseUi;
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
    bool failedLoad=false,currentViewport=true; int notices=0;
    std::uint64_t m_RawRenderClientId=15,m_RenderGeneration=150,m_GraphAcceptedResultGeneration=150;
    Stack::GraphRendering::RequestTag currentTag;
    bool IsViewportPreviewResultCurrent(const Result&) { return currentViewport; }
    Stack::GraphRendering::RequestTag CurrentGraphRenderTag() { return currentTag; }
    void ReportGraphRenderFailure(const Result&) { ++notices; }
    void DiagnoseStaleFailure(const Result& result);
    bool IsAnyRenderBackendBusy() { return false; }
    void QueueViewportOutputTextureRelease(SharedTexture& value) { value.Reset(); }
    void QueueViewportOutputTileSetRelease(EditorRenderWorker::Tiles&) {}
    void ResetIncompleteCompositeOutputRequestsForRetry() {}
    void FailDeferredLoadedProjectApply(const std::string&) { failedLoad=true;m_DeferredLoadedProjectApply.active=false; }
    void PostNotification(UiNotificationSeverity,const std::string&,const char*) { ++notices; }
    void CompleteRawWorkspaceExportRender(EditorRenderWorker::Result&,bool);
    void AdoptFirstFailure(Result result) { for(int once=0;once<1;++once) {
''' + first_failure + r'''
    } }
};
''' + asset_failure + preview_retry + 'void EditorModule::SchedulerGate() {\n' + scheduler_gate + '++preparationCount; }\n' + 'void EditorModule::AdoptPreciseFailure(const SolveResult& result) { auto& precise=m_RawWorkspaceAutoBaseUi.preciseStartingPoint;struct Decision { bool allowed=false;std::string reason; } decision; bool source=false;\n' + precise_failure + '}\n' + export_failure + 'void EditorModule::DiagnoseStaleFailure(const Result& result) { const bool rawResult=!result.rawWorkspace.sourceKey.empty();\n' + diagnostic + '}\n' + r'''
void FatalContinuationDiagnostic() {
    auto request=Request(15,150,RawRenderPurpose::DenoiseCompletion);request.graphRequest.enabled=true;request.graphRequest.sourceIdentity=77;request.graphRequest.outputNodeId=9;request.graphRequest.revision=123;
    auto result=EditorRenderWorker::MakeFailedResult(request,"fatal");EditorModule editor;editor.currentTag=request.graphRequest;
    editor.DiagnoseStaleFailure(result);Require(editor.notices==1 && editor.m_GraphAcceptedResultGeneration==150,"accepted provisional failure emits diagnostic only");
    editor.notices=0;result.ownerId=99;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"other owner fatal suppressed");result.ownerId=15;
    result.graphRequest.sourceIdentity=78;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"replaced graph source suppressed");result.graphRequest.sourceIdentity=77;
    result.graphRequest.revision=122;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"old graph edit suppressed");result.graphRequest.revision=123;
    result.generation=149;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"old generation suppressed");result.generation=150;
    result.telemetry.superseded=true;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"superseded failure suppressed");result.telemetry.superseded=false;
    editor.currentViewport=false;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"old viewport suppressed");editor.currentViewport=true;
    result.workerFailed=false;editor.DiagnoseStaleFailure(result);Require(editor.notices==0,"ordinary failure uses existing diagnostic path");
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
    try { WorkerLifecycle();ParkedAndDedupe();CancelFallbackAndShutdown();ServiceRouting();NormalSupersession();PublicationRetirement();GraphAndOverview();AuxiliaryIdentity();AdoptionTerminalState();FatalContinuationDiagnostic();FatalIdleGuard();std::cout<<"render worker failure lifecycle: PASS\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
'''
args.output.write_text(pre + header + '\n' + methods + service + '\n' + tests + consumer + main)
