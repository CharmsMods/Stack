#!/usr/bin/env python3
"""Generate a CPU-only component proof of the production overlay lifecycle.

The request preparation, callback, poll, retry gate, draw gate, request reset and
texture teardown statements are extracted from production. Real TaskGroup and
TaskSystem run the callback; small endpoint stubs replace overlay computation,
OpenGL upload and ImGui queries. This does not process images or automate UI.

python3 tools/tests/color_warp_diagnostic_callback_tests.py --output /tmp/color_warp_diagnostic_tests.cpp
g++ -std=c++17 -pthread -Isrc /tmp/color_warp_diagnostic_tests.cpp src/Async/TaskSystem.cpp -o /tmp/color_warp_diagnostic_tests
/tmp/color_warp_diagnostic_tests
"""
import argparse
from pathlib import Path
import textwrap


def between(text, start, end):
    return textwrap.dedent(text.split(start, 1)[1].split(end, 1)[0])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    preview = (root / 'src/Editor/Internal/RawLab/EditorModuleRawLabPreview.cpp').read_text()
    types = (root / 'src/Editor/EditorModuleTypes.h').read_text()
    surface = (root / 'src/Editor/Internal/RawLab/RawLabColorSurface.cpp').read_text()
    teardown = (root / 'src/Editor/Internal/RawWorkspace/EditorModuleRawWorkspacePreview.cpp').read_text()
    state = between(types, 'struct RawWorkspaceColorWarpDiagnosticState {', '\n};')
    request_fields = 'int colorWarpDiagnosticTargetLongEdge' + between(
        types, 'int colorWarpDiagnosticTargetLongEdge', '\n    // Captured on pointer-down')
    texture_fields = 'unsigned int colorWarpAffectedOverlayTexture' + between(
        types, 'unsigned int colorWarpAffectedOverlayTexture', '\n    Stack::Editor::RawLabInternal::RawLabColorWheelRenderer')
    pin_selection = 'const Stack::RawRecipe::RawColorWarpPin& selectedPin =' + between(
        preview, 'const Stack::RawRecipe::RawColorWarpPin& selectedPin =',
        '                Stack::Editor::ColorWarpFeedback* feedbackCache')
    lifecycle = 'if (overlayFingerprint == 0u)' + between(
        preview, 'if (overlayFingerprint == 0u)',
        '\n            }\n        }\n        if (drawImageFrame && m_PermanentGalleryWorkspace)')
    reset_call = 'm_RawWorkspaceLabUi.ResetColorWarpDiagnosticRequest();'
    teardown_entry = between(teardown, 'void EditorModule::ClearRawWorkspaceLivePreviewState() {',
                             '    ClearRawWorkspaceLabGradingScope();')
    assert reset_call in teardown_entry
    assert "m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();" in teardown_entry
    texture_teardown = 'if (m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture != 0) {' + between(
        teardown, 'if (m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture != 0) {',
        '\n    int outputWidth = 0;')
    # Bind the tested reset helper to every production invalidation caller.
    source_reset = between(surface, 'if (args.ui.colorWarpViewSourceKey != previewIdentity) {',
                           '\n    args.ui.colorWarpViewExtent = std::clamp')
    escape_reset = between(surface, 'if (ImGui::IsKeyPressed(ImGuiKey_Escape)',
                           '\n    if (!colorWarp.pins.empty()')
    resolution_reset = between(preview, 'if (requestedLongEdge >',
                               '\n        if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color &&')
    assert 'args.ui.ResetColorWarpDiagnosticRequest();' in source_reset
    assert 'args.ui.ResetColorWarpDiagnosticRequest();' in escape_reset
    assert reset_call in resolution_reset
    assert 'colorUi.ResetColorWarpDiagnosticRequest();' in preview.split(
        'const bool colorWarpDiagnosticHeld', 1)[0]
    preview_escape = 'if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color &&' + between(
        preview, 'if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color &&',
        '\n        if(currentRawPreview && !ImGui::GetIO().WantTextInput')
    text = r'''
#include "Async/TaskGroup.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
thread_local int failAllocationAfter = -1;
// Keep the intentional malloc/free replacement boundary out of optimized
// callers. Inlining delete into a new-expression makes GCC misdiagnose this
// matched fixture allocator as mismatched-new-delete; no warning is disabled.
#if defined(_MSC_VER)
#define DIAGNOSTIC_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define DIAGNOSTIC_NOINLINE __attribute__((noinline))
#else
#define DIAGNOSTIC_NOINLINE
#endif
DIAGNOSTIC_NOINLINE void* operator new(std::size_t size) {
    if (failAllocationAfter >= 0 && failAllocationAfter-- == 0) {
        failAllocationAfter = -1;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
DIAGNOSTIC_NOINLINE void operator delete(void* memory) noexcept { std::free(memory); }
DIAGNOSTIC_NOINLINE void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
#undef DIAGNOSTIC_NOINLINE
struct ImVec2 { float x=0, y=0; ImVec2()=default; ImVec2(float a,float b):x(a),y(b){} };
struct Rect {
    ImVec2 Min{0,0}, Max{100,100};
    bool Contains(ImVec2 p) const { return p.x>=Min.x && p.x<=Max.x && p.y>=Min.y && p.y<=Max.y; }
};
using ImTextureID = void*;
constexpr int ImGuiKey_H=1, ImGuiKey_Escape=2, ImGuiPopupFlags_AnyPopupId=1;
#define IM_COL32(r,g,b,a) 0u
namespace ImGui {
struct Io { bool AppFocusLost=false,WantTextInput=false,KeyCtrl=false,KeyAlt=false,KeySuper=false; ImVec2 MousePos{50,50}; } io;
int frame=1;
bool freshH=false, repeatedH=false, escape=false, hovered=true, active=false, popup=false;
int GetFrameCount() { return frame; }
bool IsKeyPressed(int key, bool repeat=true) { return key==ImGuiKey_Escape ? escape : freshH || (repeat && repeatedH); }
Io& GetIO() { return io; }
bool IsWindowHovered() { return hovered; }
bool IsAnyItemActive() { return active; }
bool IsPopupOpen(const char*,int) { return popup; }
ImVec2 CalcTextSize(const char*) { return {300,16}; }
void Reset() { io={}; freshH=false; repeatedH=false; escape=false; hovered=true; active=false; popup=false; ++frame; }
}
struct DrawList {
    int images=0, failures=0;
    void AddRectFilled(ImVec2,ImVec2,unsigned,float) {}
    void AddText(ImVec2,unsigned,const char* text) { if (text[0]=='C') ++failures; }
    void AddImage(ImTextureID,ImVec2,ImVec2,ImVec2,ImVec2) { ++images; }
};
namespace GLHelpers {
int uploads=0, mode=0; // 0 success, 1 rejection, 2 throw.
unsigned next=10;
unsigned int CreateTextureFromPixels(const unsigned char* pixels,int width,int height,int channels) {
    ++uploads;
    Require(pixels && width>0 && height>0 && channels==4,"Invalid pixels reached upload endpoint");
    if(mode==2) throw std::runtime_error("controlled upload failure");
    return mode==1 ? 0u : next++;
}
}
std::vector<unsigned> deletedTextures;
void glDeleteTextures(int count,const unsigned* textures) {
    for(int i=0;i<count;++i) deletedTextures.push_back(textures[i]);
}
namespace Stack::RawRecipe {
thread_local int copiesBeforeThrow=-1;
struct RawColorWarpPin { std::string id="pin"; float strength=1; };
struct RawColorWarpRecipe {
    int value=1;
    std::vector<RawColorWarpPin> pins{{}};
    struct Group { std::vector<std::string> pinIds; };
    std::vector<Group> linkGroups;
    RawColorWarpRecipe()=default;
    RawColorWarpRecipe(const RawColorWarpRecipe& other) {
        if(copiesBeforeThrow>=0 && copiesBeforeThrow--==0) {
            copiesBeforeThrow=-1;
            throw std::runtime_error("controlled recipe copy failure");
        }
        value=other.value; pins=other.pins; linkGroups=other.linkGroups;
    }
};
}
namespace Stack::Editor {
struct Feedback { struct Field { bool spatialAvailable=false; }; std::vector<Field> fields{{}}; };
struct Control {
    std::mutex mutex;
    std::condition_variable cv;
    bool block=false, entered=false, release=false;
    int throwValue=-1, emptyValue=-1;
    std::atomic<int> builds{0};
} control;
std::vector<unsigned char> BuildColorWarpFeedbackOverlay(const Feedback&,
    const RawRecipe::RawColorWarpRecipe& recipe,const std::vector<RawRecipe::RawColorWarpPin>&) {
    ++control.builds;
    std::unique_lock lock(control.mutex);
    if(recipe.value==control.throwValue) throw std::runtime_error("controlled worker failure");
    if(control.block && recipe.value==1) {
        control.entered=true; control.cv.notify_all();
        control.cv.wait(lock,[]{return control.release;});
    }
    return recipe.value==control.emptyValue ? std::vector<unsigned char>{} : std::vector<unsigned char>{1,2,3,4};
}
}
enum class RawLabTool { Color };
namespace Stack::EditorModuleTypes {
struct RawWorkspaceColorWarpDiagnosticState {''' + state + r'''
};
struct Ui {
''' + request_fields + texture_fields + r'''
    bool colorWarpDiagnosticIsolateSelected=false, colorWarpPendingCircleDrawing=false;
    int colorWarpGestureOwner=0;
    bool colorWarpInspectionActive=false,colorWarpPendingCircleActive=false,colorWarpDiagnosticLocked=false;
    std::string colorWarpInspectedRegionId;
    RawLabTool activeTool=RawLabTool::Color;
    bool colorWarpPendingCircleRefining=false,colorWarpPendingCircleCommitRequested=false,colorWarpPendingCircleAppend=false;
    std::string colorWarpPendingReplaceRegionId,colorWarpPendingReplaceCircleId;
    std::uint64_t colorWarpAreaAnalysisGeneration=0;
    std::shared_ptr<int> colorWarpAreaAnalysisState;
    void ResetColorWarpAreaRequest() {
        ++colorWarpAreaAnalysisGeneration;colorWarpAreaAnalysisState.reset();
        colorWarpPendingCircleActive=colorWarpPendingCircleDrawing=colorWarpPendingCircleRefining=false;
        colorWarpPendingCircleCommitRequested=colorWarpPendingCircleAppend=false;
        colorWarpPendingReplaceRegionId.clear();colorWarpPendingReplaceCircleId.clear();
    }
};
}
struct Tasks {
    Async::TaskGroup group;
    std::shared_ptr<std::atomic<bool>> frameFinished=std::make_shared<std::atomic<bool>>(true);
    int submissions=0;
    bool throwSubmission=false;
    bool SubmitHighPriority(std::string label,Async::TaskSystem::Task work) {
        ++submissions;
        if(throwSubmission) throw std::runtime_error("controlled submission failure");
        return group.SubmitHighPriority(std::move(label),
            [gate=frameFinished,work=std::move(work)] {
                while(!gate->load()) std::this_thread::yield();
                work();
            });
    }
};
struct FeedbackCache {
    bool failed=false;
    int retries=0;
    bool Failed() const { return failed; }
    void RetryFailed() { if(failed) { failed=false; ++retries; } }
};
namespace Stack::Editor { using ColorWarpFeedback=FeedbackCache; }
struct Fixture {
    FeedbackCache cache;
    struct NodeState {
        FeedbackCache& cache;
        template<class T> T& Get(int) { return cache; }
    } m_NodeEditorState{cache};
    Stack::EditorModuleTypes::Ui m_RawWorkspaceLabUi;
    Tasks tasks;
    DrawList draws;
    Rect imageRect;
    bool floatingOwnsPointer=false,navigation=false,m_EditingRawLayerMask=false;
    std::string previewIdentity="source A";
    std::size_t fingerprint=7;
    int colorWarpDisplayedPin=0,colorWarpSelectionViewMode=0;
    struct Scope { int width=1,height=1; } colorScope;
    struct Recipe { Stack::RawRecipe::RawColorWarpRecipe colorWarp; } previewRecipe;
    std::shared_ptr<const Stack::Editor::Feedback> feedbackData=std::make_shared<Stack::Editor::Feedback>();
    Tasks& ProjectTasks() { return tasks; }
    bool RawFloatingSurfaceOwnsPointer() { return floatingOwnsPointer; }
    bool IsRawImageNavigationActive() { return navigation; }
    void Step(bool advance=true) {
        tasks.frameFinished->store(false);
        if(advance) ++ImGui::frame;
        draws.images=draws.failures=0;
        auto* drawList=&draws;
        auto* feedbackCache=&cache;
        const auto feedback=cache.failed ? nullptr : feedbackData;
        std::size_t overlayFingerprint=fingerprint;
''' + pin_selection + lifecycle + r'''
        tasks.frameFinished->store(true);
    }
    void Teardown() {
''' + teardown_entry + texture_teardown + r'''
    }
    void PreviewEscape() {
        struct { struct { std::optional<int> nodeTarget=1; } edit; } preview;
''' + preview_escape + r'''
    }
    void SurfaceEscape() {
        struct Args { Stack::EditorModuleTypes::Ui& ui;FeedbackCache* feedback; } args{m_RawWorkspaceLabUi,&cache};
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)''' + escape_reset + r'''
    }
    void Reset() { m_RawWorkspaceLabUi.ResetColorWarpDiagnosticRequest(); }
    void Drain(bool poll=true) {
        const auto deadline=std::chrono::steady_clock::now()+5s;
        while(!tasks.group.IsIdle() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
        Require(tasks.group.IsIdle(),"Diagnostic callback did not drain");
        if(poll) Step();
    }
    void Success() {
        Step(); Drain();
        Require(m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture!=0 &&
            !m_RawWorkspaceLabUi.colorWarpDiagnosticFailed && !m_RawWorkspaceLabUi.colorWarpDiagnosticState &&
            draws.images==1,"Successful overlay was not published/drawn");
    }
    void Failed() {
        Require(m_RawWorkspaceLabUi.colorWarpDiagnosticFailed && !m_RawWorkspaceLabUi.colorWarpDiagnosticState &&
            m_RawWorkspaceLabUi.colorWarpDiagnosticRequestedFingerprint==(fingerprint?fingerprint:1) &&
            draws.failures==1 && draws.images==0,"Failure was not terminal, current and visible");
    }
    void NoRetries() {
        const int submissions=tasks.submissions,uploads=GLHelpers::uploads;
        const auto generation=m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration;
        for(int frame=0;frame<6;++frame) Step();
        Require(tasks.submissions==submissions && GLHelpers::uploads==uploads &&
            m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration==generation,"Unchanged frame retried failed work");
    }
};
void ResetEndpoints() {
    ImGui::Reset(); GLHelpers::mode=0; GLHelpers::uploads=0; deletedTextures.clear();
    auto& c=Stack::Editor::control;
    std::lock_guard lock(c.mutex);
    c.throwValue=-1; c.emptyValue=-1; c.block=false; c.entered=false; c.release=false; c.builds=0;
    Stack::RawRecipe::copiesBeforeThrow=-1; failAllocationAfter=-1;
}
void TestSuccessAndKeyRepeat() {
    Fixture f; f.Success(); const int submits=f.tasks.submissions,uploads=GLHelpers::uploads;
    ImGui::freshH=true; f.Step(); ImGui::freshH=false; ImGui::repeatedH=true; f.Step();
    Require(f.tasks.submissions==submits && GLHelpers::uploads==uploads && f.draws.images==1,
        "H/repeat rebuilt a successful matching overlay");
}
void TestUpstreamFailureUsesRetryGate() {
    Fixture f; f.cache.failed=true; f.Step(); f.Failed();
    Require(f.tasks.submissions==0,"Failed prerequisite submitted an overlay");
    for(int frame=0;frame<100;++frame) f.Step();
    Require(f.cache.retries==0 && f.tasks.submissions==0,"Upstream failure retried on idle frames");
    ImGui::repeatedH=true; f.Step(); ImGui::repeatedH=false;
    ImGui::freshH=true; ImGui::io.WantTextInput=true; f.Step(); ImGui::io.WantTextInput=false;
    Require(f.cache.retries==0,"Ineligible H retried upstream feedback");
    f.Step(); ImGui::freshH=false;
    Require(f.cache.retries==1 && !f.m_RawWorkspaceLabUi.colorWarpDiagnosticFailed,
        "Eligible H did not clear the same upstream/overlay failure");
    f.Step(); f.Drain(); Require(f.draws.images==1,"Feedback recovery did not allow overlay publication");
}
void TestUpstreamEscapeReset() {
    Fixture f; f.cache.failed=true; f.Step(); f.Failed();
    const auto generation=f.m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration;
    ImGui::escape=true;f.m_RawWorkspaceLabUi.colorWarpPendingCircleActive=true;f.SurfaceEscape();
    Require(f.cache.retries==0 && f.m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration==generation,
        "Pending circle Escape improperly reset feedback");
    f.m_RawWorkspaceLabUi.colorWarpPendingCircleActive=false;f.m_RawWorkspaceLabUi.colorWarpDiagnosticLocked=true;f.SurfaceEscape();
    Require(f.cache.retries==0,"Locked diagnostic Escape improperly reset feedback");
    f.m_RawWorkspaceLabUi.colorWarpDiagnosticLocked=false;f.SurfaceEscape();ImGui::escape=false;
    Require(f.cache.retries==1 && !f.m_RawWorkspaceLabUi.colorWarpDiagnosticFailed &&
        f.m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration==generation+1,
        "Explicit Escape reset did not rearm upstream and overlay together");
}
void TestUpstreamPreviewEscape() {
    Fixture f; f.cache.failed=true; f.Step(); f.Failed();
    const auto generation=f.m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration;
    f.PreviewEscape();Require(f.cache.retries==0,"Preview reset without Escape");
    ImGui::escape=true;f.m_RawWorkspaceLabUi.colorWarpPendingCircleActive=true;
    f.m_RawWorkspaceLabUi.colorWarpDiagnosticLocked=true;f.PreviewEscape();ImGui::escape=false;
    Require(f.cache.retries==1 && !f.m_RawWorkspaceLabUi.colorWarpDiagnosticFailed &&
        !f.m_RawWorkspaceLabUi.colorWarpPendingCircleActive && !f.m_RawWorkspaceLabUi.colorWarpDiagnosticLocked &&
        f.m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration==generation+1,
        "Image Escape did not reset failed upstream when floating surface is absent");
}
void TestZeroFingerprint() { Fixture f; f.fingerprint=0; f.Success(); Require(f.m_RawWorkspaceLabUi.colorWarpDiagnosticRequestedFingerprint==1,"Zero key collided with no attempt sentinel"); }
void TestAllocationFailure() {
    Fixture f; failAllocationAfter=0; f.Step(); f.Failed();
    Require(f.tasks.submissions==0,"Allocation failure reached task submission"); f.NoRetries();
}
void TestPreparationFailure() {
    Fixture f; Stack::RawRecipe::copiesBeforeThrow=0; f.Step(); f.Failed();
    Require(f.tasks.submissions==0,"Recipe preparation failure reached submission"); f.NoRetries();
}
void TestCaptureFailure() {
    Fixture f; Stack::RawRecipe::copiesBeforeThrow=1; f.Step(); f.Failed();
    Require(f.tasks.submissions==0,"Recipe capture failure reached submission"); f.NoRetries();
}
void TestSubmissionFailures() {
    { Fixture f; f.tasks.throwSubmission=true; f.Step(); f.Failed(); f.NoRetries(); }
    { Fixture f; f.tasks.group.Stop(); f.Step(); f.Failed(); f.NoRetries(); }
}
void TestWorkerFailureAndEmptyOutput() {
    { Fixture f; Stack::Editor::control.throwValue=1; f.Step(); f.Drain(); f.Failed(); f.NoRetries();
      Require(GLHelpers::uploads==0,"Worker failure uploaded empty pixels"); }
    Stack::Editor::control.throwValue=-1;
    { Fixture f; Stack::Editor::control.emptyValue=1; f.Step(); f.Drain(); f.Failed(); f.NoRetries();
      Require(GLHelpers::uploads==0,"Empty output was published as a valid overlay"); }
}
void TestUploadFailuresAndOldTexture() {
    Fixture f; f.Success(); const unsigned old=f.m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture;
    f.fingerprint=9; GLHelpers::mode=1; f.Step(); f.Drain(); f.Failed(); f.NoRetries();
    Require(f.m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture==old && deletedTextures.empty(),
        "Failed replacement deleted the still-owned old texture");
    f.fingerprint=10; GLHelpers::mode=2; f.Step(); f.Drain(); f.Failed(); f.NoRetries();
    Require(f.m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture==old && deletedTextures.empty(),
        "Throwing upload partially replaced old texture");
}
void TestFreshRetryOwnershipAndBound() {
    Fixture f; GLHelpers::mode=1; f.Step(); f.Drain(); f.Failed();
    const int submits=f.tasks.submissions;
    ImGui::repeatedH=true; f.NoRetries(); ImGui::repeatedH=false;
    ImGui::freshH=true;
    const auto blocked=[&](bool& flag) { flag=true; f.Step(); flag=false; Require(f.tasks.submissions==submits,"Input owner did not block retry"); };
    blocked(ImGui::io.AppFocusLost); blocked(ImGui::io.WantTextInput); blocked(ImGui::io.KeyCtrl);
    blocked(ImGui::io.KeyAlt); blocked(ImGui::io.KeySuper); blocked(ImGui::active); blocked(ImGui::popup);
    blocked(f.floatingOwnsPointer); blocked(f.navigation); blocked(f.m_EditingRawLayerMask);
    blocked(f.m_RawWorkspaceLabUi.colorWarpPendingCircleDrawing);
    ImGui::hovered=false; f.Step(); ImGui::hovered=true;
    ImGui::io.MousePos={200,200}; f.Step(); ImGui::io.MousePos={50,50};
    f.m_RawWorkspaceLabUi.colorWarpGestureOwner=1; f.Step(); f.m_RawWorkspaceLabUi.colorWarpGestureOwner=0;
    Require(f.tasks.submissions==submits,"Wrong image or gesture owner allowed retry");
    f.tasks.throwSubmission=true; f.Step(); f.Failed();
    Require(f.tasks.submissions==submits+1,"Fresh eligible H did not retry once");
    f.Step(false); Require(f.tasks.submissions==submits+1,"Same-frame press retried twice");
    ImGui::freshH=false; f.NoRetries();
    GLHelpers::mode=0; f.tasks.throwSubmission=false; ImGui::freshH=true; f.Step();
    ImGui::freshH=false; f.Drain(); Require(f.draws.images==1,"Explicit fresh retry did not recover");
}
void TestStalePublicationGuards() {
    for(int mode=0;mode<5;++mode) {
        Fixture f; f.Step(); f.Drain(false); const int uploads=GLHelpers::uploads;
        auto state=f.m_RawWorkspaceLabUi.colorWarpDiagnosticState;
        Require(bool(state),"Fixture did not retain completed request for stale probe");
        if(mode==0) f.previewIdentity="source B";
        if(mode==1) ++f.m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration;
        if(mode==2) ++state->fingerprint;
        if(mode==3) ++f.colorScope.width;
        if(mode==4) ++f.colorScope.height;
        f.Step();
        Require(GLHelpers::uploads==uploads && f.draws.images==0 &&
            !f.m_RawWorkspaceLabUi.colorWarpDiagnosticState &&
            f.m_RawWorkspaceLabUi.colorWarpDiagnosticRequestedFingerprint==0,
            "Stale source/generation/fingerprint/dimensions published or remained latched");
    }
}
void TestTeardownRearmsUnchangedSource() {
    Fixture f; f.Success(); const int submits=f.tasks.submissions;
    const unsigned texture=f.m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture;
    f.Teardown();
    Require(!f.m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture &&
        f.m_RawWorkspaceLabUi.colorWarpDiagnosticRequestedFingerprint==0 &&
        !f.m_RawWorkspaceLabUi.colorWarpDiagnosticFailed && deletedTextures==std::vector<unsigned>{texture},
        "Production teardown did not clear texture and request latch");
    f.Success(); Require(f.tasks.submissions==submits+1,"Unchanged source stayed suppressed after teardown");
}
void TestResetRearmsFailure() {
    Fixture f; GLHelpers::mode=1; f.Step(); f.Drain(); f.Failed();
    const int submits=f.tasks.submissions; f.Reset(); GLHelpers::mode=0; f.Success();
    Require(f.tasks.submissions==submits+1,"Source/resolution/Escape reset did not rearm failure");
}
void ReleaseBlocked() {
    auto& c=Stack::Editor::control;
    {std::lock_guard lock(c.mutex); c.release=true; c.block=false;} c.cv.notify_all();
}
void WaitBlocked() {
    auto& c=Stack::Editor::control; std::unique_lock lock(c.mutex);
    if(!c.cv.wait_for(lock,5s,[&]{return c.entered;})) {
        c.release=true; c.block=false; lock.unlock(); c.cv.notify_all(); Require(false,"Delayed callback did not enter");
    }
}
void TestLateResetAndReplacement() {
    Fixture f;
    {std::lock_guard lock(Stack::Editor::control.mutex); Stack::Editor::control.block=true;}
    f.Step(); WaitBlocked();
    std::weak_ptr<Stack::EditorModuleTypes::RawWorkspaceColorWarpDiagnosticState> old=f.m_RawWorkspaceLabUi.colorWarpDiagnosticState;
    f.Teardown(); f.previewIdentity="source B"; f.fingerprint=19; f.previewRecipe.colorWarp.value=2;
    f.Step(); ReleaseBlocked(); f.Drain();
    Require(old.expired() && f.m_RawWorkspaceLabUi.colorWarpAffectedOverlaySourceKey=="source B" &&
        f.m_RawWorkspaceLabUi.colorWarpAffectedOverlayFingerprint==19 && GLHelpers::uploads==1,
        "Late old-source completion replaced reset/new source");
}
void TestDetachedOwnerLifetime() {
    auto f=std::make_unique<Fixture>();
    {std::lock_guard lock(Stack::Editor::control.mutex); Stack::Editor::control.block=true;}
    f->Step(); WaitBlocked();
    std::weak_ptr<Stack::EditorModuleTypes::RawWorkspaceColorWarpDiagnosticState> old=f->m_RawWorkspaceLabUi.colorWarpDiagnosticState;
    f->tasks.group.Stop(); f->Teardown(); f.reset(); ReleaseBlocked();
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(Async::TaskSystem::Get().HasPendingWork() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    Require(!Async::TaskSystem::Get().HasPendingWork() && old.expired() && GLHelpers::uploads==0,
        "Detached/reset owner received upload or retained callback state");
}
int main() {
    int failures=0;
    const auto run=[&](const char* name,auto test) {
        ResetEndpoints();
        try { test(); std::cout<<"PASS "<<name<<'\n'; }
        catch(const std::exception& error) { ReleaseBlocked(); ++failures; std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
    };
    run("success and H repeat",TestSuccessAndKeyRepeat);
    run("upstream failure shares retry gate",TestUpstreamFailureUsesRetryGate);
    run("upstream Escape reset",TestUpstreamEscapeReset);
    run("upstream image Escape reset",TestUpstreamPreviewEscape);
    run("zero fingerprint",TestZeroFingerprint);
    run("request allocation failure",TestAllocationFailure);
    run("preparation copy failure",TestPreparationFailure);
    run("callback capture failure",TestCaptureFailure);
    run("submission throw and shutdown rejection",TestSubmissionFailures);
    run("worker exception and empty output",TestWorkerFailureAndEmptyOutput);
    run("upload failures preserve old texture",TestUploadFailuresAndOldTexture);
    run("fresh retry ownership and bound",TestFreshRetryOwnershipAndBound);
    run("stale publication guards",TestStalePublicationGuards);
    run("production texture teardown",TestTeardownRearmsUnchangedSource);
    run("source size Escape reset",TestResetRearmsFailure);
    run("late reset and replacement",TestLateResetAndReplacement);
    run("detached owner lifetime",TestDetachedOwnerLifetime);
    Async::TaskSystem::Get().Shutdown();
    return failures?1:0;
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == '__main__':
    main()
