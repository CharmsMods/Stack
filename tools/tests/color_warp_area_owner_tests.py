#!/usr/bin/env python3
"""Generate exact AREA ownership/lifecycle checks with real recipe payloads.

Production owner/state/reset, submission callback, polling, click gate, append
and replacement commit bodies are copied unchanged. Real TaskGroup/TaskSystem
execute jobs; only analysis math and UI input are synthetic endpoints. Selection
and reset call sites are source-checked. This is not native UI or GPU testing.
"""
import argparse
from pathlib import Path


def section(text, start, end):
    first = text.index(start)
    return text[first:text.index(end, first)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    types = (root / 'src/Editor/EditorModuleTypes.h').read_text()
    preview = (root / 'src/Editor/Internal/RawLab/EditorModuleRawLabPreview.cpp').read_text()
    surface = (root / 'src/Editor/Internal/RawLab/RawLabColorSurface.cpp').read_text()
    owner = section(types, 'struct RawWorkspaceColorWarpAreaOwner {', 'struct RawWorkspaceColorWarpDiagnosticState {')
    fields = section(types, '    bool colorWarpPendingCircleActive', '    bool colorWarpDiagnosticLocked')
    submit = section(preview, '                const auto submitAreaAnalysis = [&]() {', '\n                if (colorUi.colorWarpAreaAnalysisState)')
    poll = section(preview, '                if (colorUi.colorWarpAreaAnalysisState)', '\n                const ImVec2 mouse')
    click = section(preview, '                        if (!wasInitial && colorUi.colorWarpPendingCircleEditKind == 1', '} else if (!colorUi.colorWarpPendingCircleAppend)') + '}'
    context_guard = section(preview, '    if (m_RawWorkspaceLabUi.colorWarpAreaOwner.Valid()', '    if (preview.identity.empty())')
    commit_guard = section(surface, '    const bool ownsPendingArea =', '    if ((!std::isfinite')
    commit = section(surface, '    if (ownsPendingArea && args.ui.colorWarpPendingCircleCommitRequested', '    constexpr float fullWheelExtent')
    surface_source_reset = section(surface, '    if (args.ui.colorWarpViewSourceKey != previewIdentity)', '    args.ui.colorWarpViewExtent = std::clamp')
    # A delayed callback must be detached on every production owner lifetime boundary.
    selection = (root / 'src/Editor/NodeEditors/EditorModuleNodeSelection.cpp').read_text()
    assert 'if(changed) {\n        m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();' in selection
    assert 'if (!SelectedEditingNode()) {\n            m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();' in selection
    assert 'void EditorModule::BindRequestedRawNodeEditor() {\n    m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();' in selection
    assert section(selection, 'void EditorModule::ReconcileRawNodeEditorSelection()', '\n}') .count('ResetColorWarpAreaRequest();') == 2
    project_reset = (root / 'src/Editor/Internal/Project/EditorModuleProjectInteraction.cpp').read_text()
    assert 'ui.ResetColorWarpAreaRequest();' in project_reset
    teardown = (root / 'src/Editor/Internal/RawWorkspace/EditorModuleRawWorkspacePreview.cpp').read_text()
    assert 'void EditorModule::ClearRawWorkspaceLivePreviewState() {\n    m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();' in teardown
    graph_select = (root / 'src/Editor/Internal/RawLab/EditorModuleRawProjectGraph.cpp').read_text()
    assert graph_select.count('m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();') == 3
    assert preview.count('colorUi.colorWarpAreaOwner = areaOwner;') == 2
    assert 'args.ui.colorWarpAreaOwner = args.areaOwner;' in surface
    color = (root / 'src/Editor/Internal/RawLab/EditorModuleRawLabColor.cpp').read_text()
    assert 'args.areaOwner = {context.nodeTarget, previewIdentity,\n        RawViewportSourceHash()};' in color
    assert 'preview.edit.nodeTarget, preview.identity, RawViewportSourceHash()};' in preview
    text = r'''
#include "Async/TaskGroup.h"
#include "Project/NodeEditTarget.h"
#include "Raw/RawColorWarpAnalysis.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
using namespace std::chrono_literals;
void Require(bool condition,const char* text) { if(!condition)throw std::runtime_error(text); }
struct ImVec2 { float x=0,y=0; ImVec2(float a,float b):x(a),y(b){} };
namespace Stack::RawRecipe {
struct Control { std::mutex mutex;std::condition_variable cv;bool block=false,entered=false,release=false; } control;
RawColorWarpCoordinate WorkingRgbToColorWarpCoordinate(const std::array<float,3>& pixel,Raw::RawWorkingSpace) {
    RawColorWarpCoordinate coordinate;coordinate.a=pixel[0];coordinate.b=pixel[1];coordinate.sceneEv=pixel[2];return coordinate;
}
RawColorWarpAreaAnalysisResult AnalyzeRawColorWarpArea(const std::vector<RawColorWarpAnalysisSample>& samples,
    const RawColorWarpSampleCircle&,std::size_t,const std::function<bool()>&) {
    std::unique_lock lock(control.mutex);
    if(control.block) {control.entered=true;control.cv.notify_all();control.cv.wait(lock,[]{return control.release;});}
    RawColorWarpAreaProposal proposal;proposal.sourceA=samples.front().color.a;proposal.sourceB=samples.front().color.b;
    RawColorWarpAreaAnalysisResult result;result.proposals.push_back(proposal);return result;
}
}
namespace Stack::EditorModuleTypes {
''' + owner + r'''
struct Ui {
''' + fields + r'''
    int selectedColorWarpPin=-1;
    bool colorWarpInspectionActive=false;
    std::string colorWarpInspectedRegionId;
    std::string colorWarpViewSourceKey="source",colorWarpEvGesturePinId;
    bool colorWarpViewNeedsFit=false,colorWarpProvisionalPinValid=false,colorWarpProvisionalSceneEvValid=false;
    bool colorWarpDraggingProvisional=false,colorWarpPhotoHoverValid=false;
    float colorWarpViewCenterA=0,colorWarpViewCenterB=0,colorWarpViewExtent=0.45f;
    float colorWarpViewTargetCenterA=0,colorWarpViewTargetCenterB=0,colorWarpViewTargetExtent=0.45f;
    float colorWarpRegionReachWheelDelta=0,colorWarpRegionFeatherWheelDelta=0;
    int colorWarpEvGesture=0,colorWarpSmartSelection=0,colorWarpGestureOwner=0;
    void ResetColorWarpDiagnosticRequest() {}
};
}
using Owner=Stack::EditorModuleTypes::RawWorkspaceColorWarpAreaOwner;
using Recipe=Stack::RawRecipe::RawColorWarpRecipe;
Owner MakeOwner(std::string node="A",std::string source="source",std::uint64_t hash=19) {
    return {Stack::Project::NodeEditTarget{"document","layer",std::move(node)},std::move(source),hash};
}
struct Fixture {
    Stack::EditorModuleTypes::Ui m_RawWorkspaceLabUi;
    Owner areaOwner=MakeOwner();
    bool canEdit=true;
    Recipe a,b;
    Async::TaskGroup tasks;
    struct Scope {int width=1,height=1;std::vector<float> pixels{0.27f,-0.13f,0};}colorScope;
    std::size_t colorScopePixelCount=1;
    struct PreviewRecipe {struct {Raw::RawWorkingSpace workingSpace=Raw::RawWorkingSpace::LinearSrgbD65;}technical;Recipe colorWarp;}previewRecipe;
    Async::TaskGroup& ProjectTasks(){return tasks;}
    void Begin() {
        auto& ui=m_RawWorkspaceLabUi;ui.ResetColorWarpAreaRequest();ui.colorWarpAreaOwner=areaOwner;
        ui.colorWarpPendingCircleActive=true;ui.colorWarpPendingCircleEditKind=1;
    }
    void Context() {
        struct {struct{bool canEdit=true;}edit;}preview;preview.edit.canEdit=canEdit;
''' + context_guard + r'''
    }
    void Select(const Owner& selected) {
        if(areaOwner!=selected)m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();
        areaOwner=selected;Context();
    }
    void Submit() {auto& colorUi=m_RawWorkspaceLabUi;
''' + submit + r'''
        submitAreaAnalysis();
    }
    void Poll() {auto& colorUi=m_RawWorkspaceLabUi;
''' + poll + r'''
    }
    void Click() {auto& colorUi=m_RawWorkspaceLabUi;const bool wasInitial=false;const float dragDistance=0;
''' + click + r'''
    }
    bool Surface(const Owner& drawingOwner,Recipe& colorWarp,bool editable=true) {
        struct {Stack::EditorModuleTypes::Ui& ui;const Owner& areaOwner;bool areaEditingAllowed;}args{m_RawWorkspaceLabUi,drawingOwner,editable};
        using Stack::RawRecipe::RawColorWarpPin;
        bool changed=false;
''' + commit_guard + commit + r'''
        const std::string& previewIdentity=drawingOwner.sourceKey;
        constexpr float fullWheelExtent=0.45f;constexpr int kColorWarpGestureNone=0;
''' + surface_source_reset + r'''
        return changed;
    }
    void Drain(bool poll=true) {
        const auto deadline=std::chrono::steady_clock::now()+5s;
        while(!tasks.IsIdle()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        Require(tasks.IsIdle(),"AREA callback did not drain");if(poll)Poll();
    }
    void Ready(){Begin();Submit();Drain();Require(!m_RawWorkspaceLabUi.colorWarpAreaAnalysisResult.proposals.empty(),"Owned result did not publish");}
};
void ReleaseBlocked() {
    auto& c=Stack::RawRecipe::control;{std::lock_guard lock(c.mutex);c.release=true;c.block=false;}c.cv.notify_all();
}
void WaitBlocked() {
    auto& c=Stack::RawRecipe::control;std::unique_lock lock(c.mutex);
    if(!c.cv.wait_for(lock,5s,[&]{return c.entered;})) {
        c.release=true;c.block=false;lock.unlock();c.cv.notify_all();Require(false,"Delayed AREA job did not start");
    }
}
void SuccessAndNonselectedCard() {
    for(int variant=0;variant<3;++variant) {
        Fixture f;f.Ready();f.Click();const auto generation=f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisGeneration;
        Owner other=MakeOwner("B");if(variant==1)other.sourceKey="different source";if(variant==2)++other.sourceHash;
        Require(!f.Surface(other,f.b) && f.b.pins.empty() &&
            f.m_RawWorkspaceLabUi.colorWarpPendingCircleCommitRequested &&
            f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisGeneration==generation,
            "Nonselected same/different-source card consumed or canceled A's pending commit");
        Require(f.Surface(MakeOwner(),f.a) && f.a.pins.size()==1 && f.a.pins[0].sourceA==0.27f &&
            !f.m_RawWorkspaceLabUi.colorWarpAreaOwner.Valid(),"Owned commit did not publish once and retire request");
        Require(!f.Surface(MakeOwner(),f.a)&&f.a.pins.size()==1,"Completed request committed twice");
    }
}
void CompletedBeforeSwitch() {
    Fixture f;f.Ready();f.Click();f.Select(MakeOwner("B"));f.Click();
    Require(!f.Surface(MakeOwner("B"),f.b)&&f.b.pins.empty()&&f.a.pins.empty()&&
        !f.m_RawWorkspaceLabUi.colorWarpPendingCircleActive,"Completed A proposals crossed selection into B");
}
void CompletesAfterSwitch() {
    Fixture f;f.Begin();{std::lock_guard lock(Stack::RawRecipe::control.mutex);Stack::RawRecipe::control.block=true;}
    f.Submit();WaitBlocked();std::weak_ptr<Stack::EditorModuleTypes::RawWorkspaceColorWarpAreaAnalysisState> old=f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisState;
    f.Select(MakeOwner("B"));ReleaseBlocked();f.Drain();f.Click();
    Require(old.expired()&&!f.Surface(MakeOwner("B"),f.b)&&f.b.pins.empty()&&
        f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisResult.proposals.empty(),"Late A completion crossed into B");
}
void SourceDocumentAndNodeGuards() {
    for(int variant=0;variant<6;++variant) {
        Fixture f;f.Ready();f.Click();Owner other=MakeOwner();
        if(variant==0)other.sourceKey="new source";
        if(variant==1)++other.sourceHash;
        if(variant==2)other.nodeTarget->documentId="new document";
        if(variant==3)other.nodeTarget->layerId="other layer";
        if(variant==4)other.nodeTarget->nodeUuid="reused numeric id but new uuid";
        if(variant==5)other.nodeTarget.reset();
        Require(!f.Surface(other,f.b)&&f.b.pins.empty(),"Mismatched owner mutated recipe before source reset");
        f.areaOwner=other;f.Context();
        Require(!f.m_RawWorkspaceLabUi.colorWarpPendingCircleActive&&
            !f.m_RawWorkspaceLabUi.colorWarpAreaOwner.Valid(),"Preview context did not cancel stale owner");
    }
}
void SameDocumentResetAndCancel() {
    Fixture f;f.Ready();f.Click();
    const auto generation=f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisGeneration;
    f.m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();f.Poll();f.Click();
    Require(f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisGeneration>generation &&
        !f.Surface(MakeOwner(),f.a)&&f.a.pins.empty()&&
        f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisResult.proposals.empty(),
        "Reset/cancel with reused saved identities accepted old result");
    f.Ready();f.Click();Require(f.Surface(MakeOwner(),f.a),"Reset prevented later valid owned request");
}
void DelayedSameIdentityReload() {
    Fixture f;f.Begin();{std::lock_guard lock(Stack::RawRecipe::control.mutex);Stack::RawRecipe::control.block=true;}
    f.Submit();WaitBlocked();
    const auto oldGeneration=f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisGeneration;
    std::weak_ptr<Stack::EditorModuleTypes::RawWorkspaceColorWarpAreaAnalysisState> old=f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisState;
    // Reload can preserve the document, node UUID, source key and file hash.
    // Its reset boundary must still detach the older in-flight sample capture.
    f.m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();f.Begin();
    f.colorScope.pixels[0]=0.41f;f.Submit();
    Require(f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisGeneration>oldGeneration,"Same-identity reload reused request generation");
    ReleaseBlocked();f.Drain();f.Click();
    Require(old.expired()&&f.Surface(MakeOwner(),f.a)&&f.a.pins.size()==1&&
        f.a.pins.front().sourceA==0.41f,"Same-identity reload adopted the detached old source sample");
}
void ForgedStaleCallbackGuard() {
    for(int variant=0;variant<4;++variant) {
        Fixture f;f.Begin();f.Submit();f.Drain(false);auto state=f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisState;
        Require(state&&state->ready,"Fixture did not retain completion");
        if(variant==0)state->owner=MakeOwner("B");
        if(variant==1)++state->generation;
        if(variant==2)f.m_RawWorkspaceLabUi.colorWarpAreaOwner=MakeOwner("B");
        if(variant==3)++f.areaOwner.sourceHash;
        f.Poll();f.Click();Require(!f.Surface(MakeOwner(),f.a)&&f.a.pins.empty()&&
            !f.m_RawWorkspaceLabUi.colorWarpAreaAnalysisState,"Mismatched request packet was adopted");
    }
}
void AppendAndReplaceOwnership() {
    Fixture f;Stack::RawRecipe::RawColorWarpRegion region;region.id="same-region";region.circles.emplace_back();region.circles.back().id="sample-1";
    f.a.regions.push_back(region);f.b.regions.push_back(region);
    f.Begin();auto& ui=f.m_RawWorkspaceLabUi;ui.colorWarpPendingCircleAppend=true;ui.colorWarpPendingReplaceRegionId=region.id;f.Click();
    Require(!f.Surface(MakeOwner("B"),f.b)&&f.b.regions.front().circles.size()==1,"Append crossed equal region IDs");
    Require(f.Surface(MakeOwner(),f.a)&&f.a.regions.front().circles.size()==2,"Owned append changed semantics");
    f.Ready();ui.colorWarpPendingReplaceRegionId=region.id;ui.colorWarpPendingReplaceCircleId="sample-1";f.Click();
    Require(!f.Surface(MakeOwner("B"),f.b)&&f.b.pins.empty(),"Replacement crossed equal region IDs");
    Require(f.Surface(MakeOwner(),f.a)&&f.a.pins.size()==1,"Owned replacement did not commit");
}
void ReadOnlyLifecycleAndMissingTarget() {
    Fixture f;f.Ready();f.Click();Require(!f.Surface(MakeOwner(),f.a,false)&&f.a.pins.empty(),"Read-only drawing committed pending area");
    f.canEdit=false;f.Context();Require(!f.m_RawWorkspaceLabUi.colorWarpPendingCircleActive,"Read-only lifecycle kept active pending area");
    Owner empty;Require(!empty.Valid(),"Missing source/node was a valid owner");
}
int main() {
    int failures=0;
    const auto run=[&](const char* name,auto test){
        {auto& c=Stack::RawRecipe::control;std::lock_guard lock(c.mutex);c.block=c.entered=c.release=false;}
        try{test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){ReleaseBlocked();++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    };
    run("owned success and nonselected card",SuccessAndNonselectedCard);
    run("completed A before B selection",CompletedBeforeSwitch);
    run("delayed A after B selection",CompletesAfterSwitch);
    run("source hash document layer UUID deletion",SourceDocumentAndNodeGuards);
    run("same-document reset cancel and valid retry",SameDocumentResetAndCancel);
    run("delayed reload preserving all saved identities",DelayedSameIdentityReload);
    run("stale callback owner and generation",ForgedStaleCallbackGuard);
    run("append and replace same region IDs",AppendAndReplaceOwnership);
    run("read-only and absent owner",ReadOnlyLifecycleAndMissingTarget);
    Async::TaskSystem::Get().Shutdown();return failures?1:0;
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == '__main__':
    main()
