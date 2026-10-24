#!/usr/bin/env python3
"""Exercise production presentation consumers and Queue terminal branches without GL.

The state methods, graph/view acceptance, adoption decision slice, explicit
request method, readiness predicate and Queue wait branches are copied unchanged.
Texture transfer, backend execution and unrelated RAW scheduling are fakes.
"""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]

def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, pos = 1, brace + 1
    while depth:
        if source[pos] == '{': depth += 1
        elif source[pos] == '}': depth -= 1
        pos += 1
    return source[start:pos] + '\n'

def read(path):
    return (root / path).read_text()

state = read('src/Editor/Internal/EditorModuleRenderPresentationState.cpp')
state = '\n'.join(line for line in state.splitlines() if not line.startswith('#include'))
graph = read('src/Editor/Internal/EditorModuleGraphRendering.cpp')
viewport = read('src/Editor/Internal/EditorModuleViewportPreview.cpp')
editor = read('src/Editor/EditorModule.cpp')
header = read('src/Editor/EditorModule.h')
adoption = read('src/Editor/Internal/EditorModuleRenderAdoption.cpp')
rendering = read('src/Editor/Internal/EditorModuleRendering.cpp')
scheduling = read('src/Editor/Internal/EditorModuleRenderScheduling.cpp')
queue = read('src/Queue/QueueRenderCoordinator.cpp')
cleanup_start = adoption.index('    auto releaseDeferredResultResources =')
cleanup_end = adoption.index('    auto deferResultUntilReady', cleanup_start)
cleanup = adoption[cleanup_start:cleanup_end]
sync_start = scheduling.index('        const bool authoritativeOutput =')
sync_end = scheduling.index('        m_GraphPerformanceStats.lastSnapshotBuildMs', sync_start)
sync_policy = scheduling[sync_start:sync_end]
viewport_guard_start = viewport.index('    if (!UsesGraphViewportPreview()', viewport.index('void EditorModule::ConfigureViewportPreview('))
viewport_guard_end = viewport.index('    auto& state', viewport_guard_start)
viewport_guard = viewport[viewport_guard_start:viewport_guard_end]
submission_start = editor.index('    const bool rawSnapshot =', editor.index('bool EditorModule::SubmitRenderSnapshot('))
submission_end = editor.index('    const std::uint64_t generation', submission_start)
submission = editor[submission_start:submission_end]
refresh_start = rendering.index('    // Some authored RAW edits', rendering.index('void EditorModule::MarkRenderRefreshDirty()'))
refresh_end = rendering.index('    for (auto& view', refresh_start)
refresh_cancel = rendering[refresh_start:refresh_end]
blocked_start = scheduling.index('        FailFullQualityRenderRequest("Full-quality rendering is blocked by incompatible or missing HDR inputs.");')
blocked_end = scheduling.index('        submitPreviewOnlyRequests', blocked_start)
blocked_terminal = scheduling[blocked_start:blocked_end]
decision_start = adoption.index('        const bool staleResult =')
decision_end = adoption.index('        const bool ownedRawNativeRefinementResult', decision_start)
decision = adoption[decision_start:decision_end]
accepted_start = adoption.index('        if (result.graphRequest.enabled)\n            m_GraphAcceptedResultGeneration')
accepted_end = adoption.index('        const auto submittedIt', accepted_start)
accepted = adoption[accepted_start:accepted_end]
inspection_start = queue.index('    case InspectionPhase::WaitForRender: {')
inspection_end = queue.index('    case InspectionPhase::Idle:', inspection_start)
inspection = queue[inspection_start:inspection_end]
export_start = queue.index('    case Phase::WaitForRender: {')
export_end = queue.index('    case Phase::WriteOutput:', export_start)
exported = queue[export_start:export_end]

pre = r'''
#include "Editor/RenderPresentationState.h"
#include "Editor/RawRenderPurpose.h"
#include "Renderer/ViewportPreviewPolicy.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Stack::Renderer;
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
class EditorRenderWorker {
public:
    struct Texture { int texture=0,width=64,height=48; };
    struct Workspace { std::string sourceKey;std::uint64_t sourceHash=0;std::size_t recipeRevision=0;Texture localRangeOverlayTexture; };
    struct Snapshot {
        std::uint64_t ownerId=0,generation=0,schedulingSerial=0;
        Stack::GraphRendering::RequestTag graphRequest;
        RawRenderPurpose rawRenderPurpose=RawRenderPurpose::InteractivePresentation;
        Workspace rawWorkspace;bool outputConnected=true;
        ViewportPreviewRequest viewportPreview;
    };
    struct Result : Snapshot {
        using Texture=EditorRenderWorker::Texture;
        struct Telemetry { bool superseded=false; } telemetry;
        bool success=true,denoisePending=false;std::string error;
        ViewportPreviewGeometry viewportPreviewGeometry;
        int viewportViewId=1,texture=0;std::size_t viewportContentIdentity=42;
        Texture outputTexture,viewportOverview,outputTiles;
    };
};
using Result=EditorRenderWorker::Result;
using Snapshot=EditorRenderWorker::Snapshot;
class EditorModule {
public:
    struct Graph {
        bool connected=true;
        bool IsOutputConnected() const { return connected; }
        int GetOutputNodeId() const { return 1; }
        const Graph* FindInputLink(int,const char*) const { return this; }
        std::vector<int> GetOutputNodeIds() const { return {1}; }
        void SetOutputNodeEnabled(int,bool) { connected=true; }
        void SetOutputNodeId(int) {}
        std::string GetOutputConnectionDiagnostic() const { return "output disconnected"; }
    };
    struct Project { Graph graph; } project;
    Project* m_Project=&project;
    Stack::EditorRendering::RenderPresentationState m_RenderPresentation;
    Stack::EditorRendering::FullQualityRenderRequestState m_FullQualityRenderRequest;
    Stack::GraphRendering::RequestTag currentTag;
    std::uint64_t m_RawRenderClientId=9,m_RenderGeneration=100,m_GraphAcceptedResultGeneration=0,m_ViewportOutputRenderGeneration=0,m_LatestRawPresentationGeneration=100;
    std::size_t m_LatestRawPresentationRecipeRevision=4;
    bool m_RenderDirty=false,m_RenderPending=false,busy=false,graphViewport=true,m_RawWorkspaceRootTabActive=true,rawActive=true;
    bool m_ViewportOutputNativeExtentVerified=true;
    int m_ViewportOutputPreviewMaxDimension=0;
    int m_ViewportOutputExpectedNativeWidth=64,m_ViewportOutputExpectedNativeHeight=48;
    ImageRegion m_RawViewportPresentedRegion;
    Result::Texture m_RawWorkspacePresentationTexture;
    struct Pipeline {
        int texture=0,width=64,height=48;bool async=true;
        int GetOutputTexture() const { return texture; }
        int GetCanvasWidth() const { return width; }
        int GetCanvasHeight() const { return height; }
        void SetRawRgbDenoiseAsyncEnabled(bool value) { async=value; }
    } m_Pipeline;
    bool m_RawWorkspaceExplicitFullQualityRenderRequested=false,m_RawWorkspaceFullResolutionPreviewPending=false,m_RawWorkspaceFullResolutionPreviewRequested=false,m_RawWorkspaceFullResolutionPreviewDeferredByBudget=false;
    std::uint64_t m_RawWorkspaceFullResolutionPreviewRequestGeneration=0,m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes=0;
    int m_RawWorkspaceFullResolutionPreviewRetryCount=0,displayedTexture=0;
    std::vector<int> m_HdrMergeRenderingNodeIds;
    double m_RawWorkspaceFastPreviewUntilTime=-1;
    std::string m_RawWorkspacePreviewSourceKey;
    std::string activeSourceKey="raw-1";std::uint64_t activeSourceHash=55;
    struct ViewState { ViewportPreviewRequest request;ViewportPreviewGeometry geometry;std::size_t contentIdentity=42,acceptedContentIdentity=0;std::uint64_t acceptedGeneration=0;int physicalWidth=64,physicalHeight=48; } view;
    std::vector<ViewState> m_ViewportPreviewStates;
    Stack::GraphRendering::RequestTag CurrentGraphRenderTag() const { return currentTag; }
    bool UsesGraphViewportPreview() const { return graphViewport; }
    const ViewState& ActiveViewportPreviewState() const { return view; }
    bool IsAnyRenderBackendBusy() const { return busy; }
    bool HasCompleteNativeViewportOutput() const;
    bool TryGetViewportPreviewGeometry(ViewportPreviewGeometry& geometry) const;
    bool IsRawWorkspaceProjectActive() const { return rawActive; }
    std::string GetActiveRawWorkspacePreviewIdentity() const { return activeSourceKey; }
    std::uint64_t RawViewportSourceHash() const { return activeSourceHash; }
    void MarkRenderRefreshDirty();
    bool RequestFullQualityRender(std::string* = nullptr);
    bool DidFullQualityRenderFail(std::string* = nullptr) const;
    bool IsRenderSettledForFullQualityExport() const;
    bool IsCurrentGraphResult(const Result&) const;
    bool IsViewportPreviewResultCurrent(const Result&) const;
    bool IsGraphContinuationViewCurrent(const Result&) const;
    bool IsCurrentGraphDenoiseContinuation(const Result&) const;
    bool IsPresentedRenderComplete() const;
    void AdoptRenderPresentationState(const Result&);
    void ObserveRenderPresentationFailure(const Result&);
    void TrackFullQualityRenderSnapshot(const Snapshot&);
    void CompleteFullQualityRenderRequest(const Result&,bool);
    void FailFullQualityRenderRequest(const std::string&,bool=false);
    void CancelFullQualityRenderRequest(const std::string&);
    std::string GetFullQualityRenderDiagnostic() const { return "still pending"; }
    float GetFullQualityRenderProgress(std::string* text) const { if(text)*text="pending";return 0; }
    bool Consume(Result result);
    void Discard(Result result);
    void ConfigureSynchronous(const Snapshot& snapshot);
    enum class ViewportMode { Single,CompositeCanvas };
    ViewportMode GetViewportMode() const { return ViewportMode::Single; }
    void ConfigureViewportPreview(Snapshot&);
    void PrepareSubmission(Snapshot&);
    void BlockHdrOutput();
    void FinishViewportPreviewRequest(const Result&,bool) {}
    void QueueViewportOutputTextureRelease(Result::Texture& value) { value.texture=0; }
    void QueueViewportOutputTileSetRelease(Result::Texture& value) { value.texture=0; }
};
namespace EditorNodeGraph { constexpr const char* kImageInputSocketId="imageIn"; }
'''
methods = state + '\n' + method(graph, 'bool EditorModule::IsCurrentGraphResult(')
methods += method(viewport, 'bool EditorModule::IsViewportPreviewResultCurrent(')
methods += method(viewport, 'bool EditorModule::TryGetViewportPreviewGeometry(')
methods += method(editor, 'bool EditorModule::RequestFullQualityRender(')
methods += method(rendering, 'bool EditorModule::HasCompleteNativeViewportOutput() const')
methods += method(header, 'bool IsRenderSettledForFullQualityExport() const').replace('bool IsRenderSettledForFullQualityExport()', 'bool EditorModule::IsRenderSettledForFullQualityExport()', 1)
consumer = r'''
bool EditorModule::Consume(Result result) {
    // The production branch decides staleness and terminates failed explicit
    // requests. Unrelated RAW acceptance inputs are held current in this graph fixture.
    const bool rawResult=!result.rawWorkspace.sourceKey.empty();
    const bool currentRawGeneration=true,currentRawRecipe=true,obsoleteViewport=false;
''' + decision + r'''
    if (staleResult) return false;
''' + accepted + r'''
    const bool acceptedMainFrame=result.success && result.error.empty() && result.texture!=0;
    if (acceptedMainFrame) {
        // Fake only GPU texture ownership transfer and resident geometry.
        displayedTexture=result.texture;view.geometry=result.viewportPreviewGeometry;
        view.acceptedContentIdentity=result.viewportContentIdentity;
        view.acceptedGeneration=result.generation;m_ViewportPreviewStates={view};
        m_RawWorkspacePresentationTexture.texture=result.texture;m_Pipeline.texture=result.texture;
        m_ViewportOutputRenderGeneration=result.generation;
        AdoptRenderPresentationState(result);
    }
    CompleteFullQualityRenderRequest(result,acceptedMainFrame);
    return acceptedMainFrame;
}
void EditorModule::Discard(Result result) {
''' + cleanup + r'''
    releaseDeferredResultResources(result);
}
void EditorModule::ConfigureSynchronous(const Snapshot& snapshot) {
    const bool rawWorkspaceActive=rawActive;
''' + sync_policy + r'''
}
void EditorModule::ConfigureViewportPreview(Snapshot& snapshot) {
''' + viewport_guard + r'''
    // Regional planning is not entered by either explicit request purpose.
    throw std::runtime_error("fixture unexpectedly entered regional planning");
}
void EditorModule::PrepareSubmission(Snapshot& snapshot) {
    ConfigureViewportPreview(snapshot);
''' + submission + r'''
}
void EditorModule::MarkRenderRefreshDirty() {
''' + refresh_cancel + r'''
    ++currentTag.revision;m_RenderDirty=true;
}
void EditorModule::BlockHdrOutput() {
''' + blocked_terminal + r'''
}
enum class ItemState { Rendering };
struct Model { void UpdateState(int,ItemState,float,std::string) {} };
class QueueRenderCoordinator {
public:
    explicit QueueRenderCoordinator(EditorModule* editor) : m_RenderEditor(editor) {}
    enum class InspectionPhase { WaitForRender,Idle };
    enum class Phase { WaitForRender,WriteOutput };
    EditorModule* m_RenderEditor;
    Model* m_Model=nullptr;
    float m_FullQualityProgressBase=.48f;
    struct Item { int id=1; } m_Current;
    int failures=0,inspections=0,writes=0;std::string error;
    void FailInspection(std::string value) { ++failures;error=std::move(value); }
    void CompleteInspection() { ++inspections; }
    void FailCurrent(std::string value) { ++failures;error=std::move(value); }
    void BeginOutputWrite() { ++writes; }
    bool PhaseTimedOut(std::chrono::minutes) const { return false; }
    void TickInspection() {
        const auto timedOut=[](std::chrono::minutes){return false;};
        switch(InspectionPhase::WaitForRender) {
''' + inspection + r'''
        default:return;
        }
    }
    void Tick() {
        switch(Phase::WaitForRender) {
''' + exported + r'''
        default:return;
        }
    }
};
EditorModule Editor() {
    EditorModule value;value.currentTag.enabled=true;value.currentTag.outputNodeId=1;
    value.currentTag.structureRevision=2;value.currentTag.sourceIdentity=3;value.currentTag.revision=7;
    value.view.request.enabled=true;value.view.request.generation=10;value.view.request.empty=false;
    value.view.request.visible=WholeImageRegion(64,48);
    return value;
}
Result Frame(const EditorModule& editor,std::uint64_t generation=100,std::uint64_t serial=10) {
    Result value;value.ownerId=9;value.generation=generation;value.schedulingSerial=serial;
    value.graphRequest=editor.currentTag;value.rawWorkspace.sourceKey="raw-1";
    value.rawWorkspace.sourceHash=55;value.rawWorkspace.recipeRevision=4;
    value.viewportPreview=editor.view.request;value.viewportPreviewGeometry={64,48,WholeImageRegion(64,48),64,48};
    value.texture=11;return value;
}
void SeedProvisional(EditorModule& editor) {
    auto result=Frame(editor);result.denoisePending=true;
    Require(editor.Consume(result),"provisional pixels adopted for responsiveness");
    Require(editor.displayedTexture==11 && !editor.IsRenderSettledForFullQualityExport(),"idle native provisional image cannot be exported");
}
Result Completion(const EditorModule& editor) {
    auto result=Frame(editor,100,11);result.rawRenderPurpose=RawRenderPurpose::DenoiseCompletion;result.texture=22;return result;
}
void SuccessfulCompletion() {
    auto editor=Editor();editor.m_Project=&editor.project;SeedProvisional(editor);
    auto result=Completion(editor);auto pending=result;pending.denoisePending=true;
    Require(!editor.Consume(pending) && editor.displayedTexture==11,"still-provisional continuation cannot use final upgrade exception");
    Require(editor.Consume(result) && editor.displayedTexture==22 && editor.IsRenderSettledForFullQualityExport(),"same-generation final completion replaces provisional and becomes exportable");
    result.texture=33;Require(!editor.Consume(result) && editor.displayedTexture==22,"duplicate final does not re-adopt");
    ++result.schedulingSerial;Require(!editor.Consume(result),"final state closes same-generation exception even with a later serial");
    QueueRenderCoordinator queue{&editor};queue.Tick();queue.TickInspection();Require(queue.writes==1 && queue.inspections==1 && !queue.failures,"real Queue consumers accept final only");
}
void FailureAndIdentity() {
    auto editor=Editor();editor.m_Project=&editor.project;SeedProvisional(editor);
    auto result=Completion(editor);result.success=false;result.texture=0;result.error="inference failed";
    Require(!editor.Consume(result) && editor.displayedTexture==11 && editor.m_RenderPresentation.failed,"failure preserves valid old pixels and records incomplete processing");
    Require(!editor.IsRenderSettledForFullQualityExport(),"failed native provisional never becomes ready when idle");
    for(int variant=0;variant<13;++variant) {
        auto changed=Editor();changed.m_Project=&changed.project;SeedProvisional(changed);auto late=Completion(changed);
        switch(variant) {
        case 0:late.ownerId=8;break;
        case 1:late.schedulingSerial=10;break;
        case 2:late.rawWorkspace.sourceHash=56;break;
        case 3:late.rawWorkspace.recipeRevision=5;break;
        case 4:++changed.currentTag.revision;break;
        case 5:++changed.currentTag.outputNodeId;break;
        case 6:changed.currentTag.enabled=false;break;
        case 7:late.telemetry.superseded=true;break;
        case 8:changed.m_RawRenderClientId=10;break;
        case 9:changed.m_RenderPresentation={};changed.m_ViewportOutputRenderGeneration=0;break;
        case 10:late.viewportViewId=2;break;
        case 11:late.viewportPreview.enabled=false;break;
        case 12:++late.viewportContentIdentity;break;
        }
        Require(!changed.Consume(late) && changed.displayedTexture==11,"changed owner/source/recipe/node/request or closed presentation cannot be revived");
    }
    auto oldView=Editor();oldView.m_Project=&oldView.project;SeedProvisional(oldView);auto late=Completion(oldView);
    ++oldView.view.request.generation;late.success=false;late.error="old camera failed";
    Require(!oldView.Consume(late) && !oldView.m_RenderPresentation.failed,"obsolete-camera failure does not mutate live presentation status");
    late.success=true;late.error.clear();Require(oldView.Consume(late),"successful native coverage remains reusable after camera-generation change");
    auto uncovered=Editor();uncovered.m_Project=&uncovered.project;SeedProvisional(uncovered);late=Completion(uncovered);
    ++uncovered.view.request.generation;late.viewportPreviewGeometry.resident={64,48,0,0,32,48};late.viewportPreviewGeometry.storageWidth=32;
    Require(!uncovered.Consume(late),"older camera completion without current coverage rejected");
    auto fatal=Editor();fatal.m_Project=&fatal.project;SeedProvisional(fatal);late=Completion(fatal);late.success=false;late.schedulingSerial=10;late.error="worker stopped";
    Require(!fatal.Consume(late) && !fatal.IsRenderSettledForFullQualityExport(),"parked fatal original serial still leaves provisional processing incomplete");
}
Snapshot SubmitExplicit(EditorModule& editor,std::uint64_t generation,bool raw=true) {
    editor.m_RenderGeneration=generation;editor.m_LatestRawPresentationGeneration=generation;
    Snapshot request=Frame(editor,generation,generation);request.rawRenderPurpose=RawRenderPurpose::ExplicitInspection;
    request.graphRequest={};request.viewportPreview={};
    if(!raw) { request.rawWorkspace={};editor.rawActive=false; }
    editor.PrepareSubmission(request);editor.m_RenderDirty=false;return request;
}
Result ExplicitResult(const EditorModule& editor,std::uint64_t generation) {
    auto result=Frame(editor,generation,generation);const auto& request=editor.m_FullQualityRenderRequest;
    result.rawRenderPurpose=RawRenderPurpose::ExplicitInspection;result.graphRequest=request.emittedGraphRequest;
    result.ownerId=request.ownerId;result.rawWorkspace.sourceKey=request.sourceKey;
    result.rawWorkspace.sourceHash=request.sourceHash;result.rawWorkspace.recipeRevision=request.recipeRevision;
    result.viewportPreview={};result.viewportViewId=0;return result;
}
void ExplicitConsumers() {
    auto editor=Editor();editor.m_Project=&editor.project;SeedProvisional(editor);
    Require(editor.RequestFullQualityRender() && editor.m_RawWorkspaceExplicitFullQualityRenderRequested,"provisional pixels request real explicit work");
    SubmitExplicit(editor,101);
    Require(editor.RequestFullQualityRender() && editor.m_RawWorkspaceExplicitFullQualityRenderRequested && editor.m_FullQualityRenderRequest.Pending(),"replacing pending explicit request retains fresh flags");
    auto request=SubmitExplicit(editor,102);
    auto thumbnail=request;thumbnail.outputConnected=false;thumbnail.rawRenderPurpose=RawRenderPurpose::Thumbnail;thumbnail.generation=999;
    editor.TrackFullQualityRenderSnapshot(thumbnail);Require(editor.m_FullQualityRenderRequest.generation==102,"thumbnail-only work cannot steal explicit generation");
    auto failure=ExplicitResult(editor,102);failure.success=false;failure.texture=0;failure.error="model retry failed";
    editor.Consume(failure);std::string reason;
    Require(editor.DidFullQualityRenderFail(&reason) && reason==failure.error && editor.displayedTexture==11,"explicit failure has exact terminal error and retains previous pixels");
    QueueRenderCoordinator queue{&editor};queue.Tick();queue.TickInspection();Require(queue.failures==2 && !queue.writes && !queue.inspections && queue.error==failure.error,"Queue/export inspection fail promptly without timeout or provisional output");
    Require(editor.RequestFullQualityRender() && !editor.DidFullQualityRenderFail(),"fresh explicit retry clears only old request outcome");
    SubmitExplicit(editor,103);auto final=ExplicitResult(editor,103);final.texture=44;
    Require(editor.Consume(final) && !editor.DidFullQualityRenderFail() && editor.IsRenderSettledForFullQualityExport(),"successful explicit retry supplies final pixels");
    queue.Tick();Require(queue.writes==1,"Queue proceeds after verified retry success");
    editor.RequestFullQualityRender(); // Already-ready fast path.
    Require(!editor.m_FullQualityRenderRequest.Pending(),"ready fast path creates no extra job");
    auto canceled=Editor();canceled.m_Project=&canceled.project;SeedProvisional(canceled);canceled.RequestFullQualityRender();SubmitExplicit(canceled,104);
    canceled.CancelFullQualityRenderRequest("document replaced");QueueRenderCoordinator canceledQueue{&canceled};canceledQueue.Tick();Require(canceledQueue.failures==1 && canceledQueue.error=="document replaced","cancellation is a prompt Queue terminal outcome");
    auto blocked=Editor();blocked.m_Project=&blocked.project;blocked.RequestFullQualityRender();blocked.FailFullQualityRenderRequest("snapshot allocation failed");QueueRenderCoordinator blockedQueue{&blocked};blockedQueue.TickInspection();Require(blockedQueue.failures==1 && !blockedQueue.inspections,"pre-submission failure is also terminal");
}
void OwnedTerminalRejection() {
    for (int variant=0;variant<8;++variant) {
        auto editor=Editor();editor.m_Project=&editor.project;SeedProvisional(editor);
        editor.RequestFullQualityRender();SubmitExplicit(editor,101,false);
        auto result=ExplicitResult(editor,101);
        switch(variant) {
        case 0:result.rawWorkspace.sourceKey="different source";break;
        case 1:++result.rawWorkspace.sourceHash;break;
        case 2:++result.rawWorkspace.recipeRevision;break;
        case 3:++result.graphRequest.outputNodeId;break;
        case 4:++result.graphRequest.revision;break;
        case 5:result.graphRequest.enabled=!result.graphRequest.enabled;break;
        case 6:--result.generation;break;
        case 7:--result.ownerId;break;
        }
        editor.CompleteFullQualityRenderRequest(result,false);
        Require(editor.DidFullQualityRenderFail()==(variant<6),"owned malformed terminal fails; old generation or other owner remains irrelevant");
        Require(editor.displayedTexture==11,"rejected explicit terminal retains valid pixels");
    }
    auto discarded=Editor();discarded.m_Project=&discarded.project;SeedProvisional(discarded);
    discarded.RequestFullQualityRender();SubmitExplicit(discarded,101);
    auto result=ExplicitResult(discarded,101);
    discarded.Discard(result);QueueRenderCoordinator queue{&discarded};queue.Tick();
    Require(queue.failures==1 && !queue.writes && discarded.displayedTexture==11,"actual deferred-result resource discard terminates owned attempt promptly");
    auto drift=Editor();drift.m_Project=&drift.project;drift.RequestFullQualityRender();
    drift.MarkRenderRefreshDirty();Require(drift.m_FullQualityRenderRequest.Pending() && !drift.DidFullQualityRenderFail(),"pre-submit internal refresh does not cancel fresh explicit request");
    SubmitExplicit(drift,101);drift.MarkRenderRefreshDirty();
    Require(drift.DidFullQualityRenderFail() && !drift.m_FullQualityRenderRequest.Pending(),"submitted refresh cancels immutable attempt without needing a result");
    drift.RequestFullQualityRender();auto request=SubmitExplicit(drift,102);++drift.currentTag.outputNodeId;
    QueueRenderCoordinator driftQueue{&drift};driftQueue.Tick();Require(driftQueue.failures==1,"status exposes graph context drift before completion");
    ++request.generation;drift.TrackFullQualityRenderSnapshot(request);
    Require(drift.DidFullQualityRenderFail() && drift.m_FullQualityRenderRequest.generation==102,"new work cannot silently rebind submitted ownership");
    auto nongraph=Editor();nongraph.m_Project=&nongraph.project;nongraph.currentTag.enabled=false;
    nongraph.RequestFullQualityRender();SubmitExplicit(nongraph,101);auto wrongMode=ExplicitResult(nongraph,101);
    wrongMode.rawRenderPurpose=RawRenderPurpose::ExplicitInspection;wrongMode.graphRequest.enabled=true;
    nongraph.CompleteFullQualityRenderRequest(wrongMode,false);Require(nongraph.DidFullQualityRenderFail(),"expected non-graph request rejects graph-mode terminal mismatch too");
    auto malformed=Editor();malformed.m_Project=&malformed.project;SeedProvisional(malformed);
    malformed.RequestFullQualityRender();SubmitExplicit(malformed,101);
    auto wrongPacket=ExplicitResult(malformed,101);wrongPacket.graphRequest=malformed.currentTag;
    malformed.Consume(wrongPacket);
    Require(malformed.DidFullQualityRenderFail() && !malformed.IsRenderSettledForFullQualityExport(),"rejected malformed success cannot claim ready after ordinary pixel adoption");
}
void EmittedModesAndSourceIdentity() {
    for(bool raw:{false,true}) {
        auto editor=Editor();editor.m_Project=&editor.project;editor.rawActive=raw;
        editor.RequestFullQualityRender();const auto snapshot=SubmitExplicit(editor,101,raw);
        Require(snapshot.graphRequest.enabled==!raw,"actual submission keeps RAW explicit nonregional and ordinary graph explicit tagged");
        Require(editor.m_FullQualityRenderRequest.graphRequest.enabled,"current UI context fence remains graph-enabled independently of emitted mode");
        auto result=ExplicitResult(editor,101);result.texture=44;
        Require(editor.Consume(result) && editor.IsRenderSettledForFullQualityExport() && !editor.DidFullQualityRenderFail(),"both actual emitted modes complete successfully against their own immutable request");
        if(raw) {
            const auto accepted=editor.m_RenderPresentation;
            ++editor.activeSourceHash;Require(!editor.IsRenderSettledForFullQualityExport(),"same-key rescan hash replacement cannot reuse non-graph accepted final pixels");
            editor.activeSourceHash=accepted.sourceHash;editor.activeSourceKey="raw-2";
            Require(!editor.IsRenderSettledForFullQualityExport(),"same-recipe source key replacement cannot revive accepted final pixels");
            editor.activeSourceKey=accepted.sourceKey;editor.activeSourceHash=0;
            Require(!editor.IsRenderSettledForFullQualityExport(),"unknown current source hash cannot attest readiness");
            editor.activeSourceHash=accepted.sourceHash;editor.m_RenderPresentation={};
            Require(!editor.IsRenderSettledForFullQualityExport(),"source reset cannot revive output even with reused generation and recipe");
            editor.m_RenderPresentation=accepted;editor.MarkRenderRefreshDirty();editor.BlockHdrOutput();
            Require(!editor.m_RenderDirty && !editor.m_RenderPending && !editor.IsRenderSettledForFullQualityExport(),"RAW explicit final retains document fence after authored edit and blocked HDR clears dirty without advancing generation");
            QueueRenderCoordinator queue{&editor};queue.Tick();Require(queue.failures==1 && !queue.writes,"Ready request invalidation during pump terminates Queue instead of using old pixels or timing out");
            Require(editor.RequestFullQualityRender() && !editor.DidFullQualityRenderFail(),"next explicit retry replaces only completed request fence");
            SubmitExplicit(editor,102);auto retry=ExplicitResult(editor,102);retry.texture=55;
            Require(editor.Consume(retry) && editor.IsRenderSettledForFullQualityExport(),"successful fresh request publishes a new accepted document fence");
            Require(editor.RequestFullQualityRender() && !editor.m_FullQualityRenderRequest.Pending(),"already-ready fast path does not render again");
            ++editor.currentTag.outputNodeId;editor.BlockHdrOutput();
            Require(editor.DidFullQualityRenderFail() && !editor.IsRenderSettledForFullQualityExport(),"ready fast path also retains exact output/context fence until consumed or replaced");
        }
    }
}
void NativeAndSynchronousContracts() {
    auto editor=Editor();editor.m_Project=&editor.project;auto frame=Frame(editor);
    Require(editor.Consume(frame) && editor.IsRenderSettledForFullQualityExport(),"ordinary non-denoise native output remains immediately ready");
    editor.m_ViewportPreviewStates[0].geometry.resident={64,48,0,0,32,48};editor.m_ViewportPreviewStates[0].geometry.storageWidth=32;
    Require(!editor.IsRenderSettledForFullQualityExport(),"partial native crop is not a complete export");
    editor.m_ViewportPreviewStates[0].geometry=frame.viewportPreviewGeometry;++editor.m_ViewportPreviewStates[0].contentIdentity;
    Require(!editor.IsRenderSettledForFullQualityExport(),"stale accepted content cannot claim native readiness");
    editor.m_ViewportPreviewStates[0].contentIdentity=frame.viewportContentIdentity;editor.m_RawWorkspacePresentationTexture.width=63;
    Require(!editor.IsRenderSettledForFullQualityExport(),"texture extent mismatch cannot claim native readiness");
    editor.m_RawWorkspacePresentationTexture.width=64;Require(editor.IsRenderSettledForFullQualityExport(),"matching complete native extent passes actual production predicate");
    for(const auto purpose:{RawRenderPurpose::ExplicitInspection,RawRenderPurpose::ExplicitExport,RawRenderPurpose::InteractivePresentation}) {
        Snapshot snapshot;snapshot.rawRenderPurpose=purpose;editor.ConfigureSynchronous(snapshot);
        Require(editor.m_Pipeline.async==(purpose==RawRenderPurpose::InteractivePresentation),"direct explicit execution disables asynchronous denoise for a real retry");
    }
}
int main() {
    try { SuccessfulCompletion();FailureAndIdentity();ExplicitConsumers();OwnedTerminalRejection();EmittedModesAndSourceIdentity();NativeAndSynchronousContracts();std::cout<<"render presentation consumers: PASS\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
'''
args.output.write_text(pre + methods + consumer)
