#!/usr/bin/env python3
"""Generate a CPU-only test of the production Color Warp area job boundary.

The submit lambda, result poll, commit gate, and request state are copied from
production. The real TaskGroup/TaskSystem execute the callbacks. Only the
surrounding UI/recipe types and analysis computation are reduced. No image
decoding, encoding, GPU access, or UI automation is involved.

python3 tools/tests/color_warp_analysis_callback_tests.py --output /tmp/color_warp_analysis_tests.cpp
g++ -std=c++17 -pthread -Isrc /tmp/color_warp_analysis_tests.cpp src/Async/TaskSystem.cpp -o /tmp/color_warp_analysis_tests
/tmp/color_warp_analysis_tests
"""
import argparse
from pathlib import Path
import textwrap


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'src/Editor/Internal/RawLab/EditorModuleRawLabPreview.cpp').read_text(encoding="utf-8")
    start = source.index('                const auto submitAreaAnalysis = [&]() {')
    end = source.index('\n                if (colorUi.colorWarpAreaAnalysisState)', start)
    submit = textwrap.dedent(source[start:end])
    start = end + 1
    end = source.index('\n                const ImVec2 mouse', start)
    poll = textwrap.dedent(source[start:end])
    start = source.index('                        if (!wasInitial && colorUi.colorWarpPendingCircleEditKind == 1')
    end = source.index('} else if (!colorUi.colorWarpPendingCircleAppend)', start)
    commit = textwrap.dedent(source[start:end] + '}')
    types = (root / 'src/Editor/EditorModuleTypes.h').read_text(encoding="utf-8")
    state = types.split('struct RawWorkspaceColorWarpAreaAnalysisState {', 1)[1].split('\n};', 1)[0]
    owner = 'struct RawWorkspaceColorWarpAreaOwner {' + types.split('struct RawWorkspaceColorWarpAreaOwner {', 1)[1].split('\n};', 1)[0] + '\n};\n'
    area_fields = 'bool colorWarpPendingCircleActive' + types.split('bool colorWarpPendingCircleActive', 1)[1].split('    bool colorWarpDiagnosticLocked', 1)[0]
    # Primary gesture fields and host-owned async state now live in separate
    # base/derived structs; copy both current production declaration ranges.
    area_fields += '    std::uint64_t colorWarpAreaAnalysisGeneration' + types.split(
        '    std::uint64_t colorWarpAreaAnalysisGeneration', 1)[1].split(
        '    int colorWarpDiagnosticTargetLongEdge', 1)[0]
    text = r'''
#include "Async/TaskGroup.h"
#include "Project/NodeEditTarget.h"
#include <optional>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
struct ImVec2 { float x=0,y=0; ImVec2(float xValue,float yValue):x(xValue),y(yValue){} };
using namespace std::chrono_literals;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
namespace Stack::RawRecipe {
struct Coordinate { float a=0, b=0, sceneEv=0; };
struct RawColorWarpAnalysisSample { float sourceU=0, sourceV=0; Coordinate color; bool valid=true; };
struct Circle { int value=1; };
using RawColorWarpSampleCircle=Circle;
struct RawColorWarpAreaAnalysisResult { std::vector<int> proposals; bool capacityLimited=false; };
constexpr std::size_t kMaxRawColorWarpPins = 32;
struct Control {
    std::mutex mutex;
    std::condition_variable cv;
    bool blockFirst=false, entered=false, release=false;
    int throwValue=0;
} control;
Coordinate WorkingRgbToColorWarpCoordinate(const std::array<float,3>&, int) { return {}; }
RawColorWarpAreaAnalysisResult AnalyzeRawColorWarpArea(
    const std::vector<RawColorWarpAnalysisSample>&, const Circle& circle, std::size_t) {
    std::unique_lock lock(control.mutex);
    if (circle.value==control.throwValue) throw std::runtime_error("controlled analysis failure");
    if (circle.value==1 && control.blockFirst) {
        control.entered=true;
        control.cv.notify_all();
        control.cv.wait(lock, [] { return control.release; });
    }
    return circle.value<=0 ? RawColorWarpAreaAnalysisResult{{},circle.value<0}
        : RawColorWarpAreaAnalysisResult{{circle.value},false};
}
}
namespace Stack::EditorModuleTypes {
''' + owner + r'''
struct RawWorkspaceColorWarpAreaAnalysisState {''' + state + r'''
};
}
using Stack::EditorModuleTypes::RawWorkspaceColorWarpAreaOwner;
using Stack::EditorModuleTypes::RawWorkspaceColorWarpAreaAnalysisState;
struct Ui {
''' + area_fields + r'''
};
struct Tasks {
    Async::TaskGroup group;
    bool throwSubmission=false;
    bool SubmitHighPriority(std::string label, Async::TaskSystem::Task work) {
        if (throwSubmission) throw std::runtime_error("controlled submission failure");
        return group.SubmitHighPriority(std::move(label), std::move(work));
    }
};
struct Fixture {
    Ui colorUi;
    Stack::EditorModuleTypes::RawWorkspaceColorWarpAreaOwner areaOwner{Stack::Project::NodeEditTarget{"document","layer","node"},"source",42};
    Fixture() {colorUi.colorWarpAreaOwner=areaOwner;colorUi.colorWarpPendingCircleActive=true;colorUi.colorWarpPendingCircleEditKind=1;}
    Tasks tasks;
    struct Scope { int width=1, height=1; std::vector<float> pixels{0,0,0}; } colorScope;
    std::size_t colorScopePixelCount=1;
    struct Recipe {
        struct { int workingSpace=0; } technical;
        struct { struct Pin { std::string regionId; }; std::vector<Pin> pins; } colorWarp;
    } previewRecipe;
    Tasks& ProjectTasks() { return tasks; }
    void Submit() {
''' + submit + r'''
        submitAreaAnalysis();
    }
    void Poll() {
''' + poll + r'''
    }
    bool ClickToCommit() {
        const bool wasInitial=false;
        const float dragDistance=0;
''' + commit + r'''
        return colorUi.colorWarpPendingCircleCommitRequested;
    }
    void Drain() {
        const auto deadline=std::chrono::steady_clock::now()+5s;
        while (!tasks.group.IsIdle() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
        Require(tasks.group.IsIdle(), "Analysis job did not drain");
        Poll();
    }
    void ReadyFirst() {
        Submit(); Drain();
        Require(colorUi.colorWarpAreaAnalysisResult.proposals==std::vector<int>{1} &&
            !colorUi.colorWarpPendingCircleRefining, "Successful production callback did not publish");
    }
};
void TestSuccess() { Fixture f; f.ReadyFirst(); Require(f.ClickToCommit(), "Successful analysis cannot commit"); }
void TestWorkerFailure() {
    Fixture f;
    { std::lock_guard lock(Stack::RawRecipe::control.mutex); Stack::RawRecipe::control.throwValue=2; }
    f.colorUi.colorWarpPendingCircle.value=2;
    f.Submit(); f.Drain();
    Require(!f.colorUi.colorWarpPendingCircleRefining && !f.colorUi.colorWarpAreaAnalysisState,
        "Worker exception left analysis refining after TaskGroup became idle");
    Require(f.colorUi.colorWarpAreaStatus=="Analysis unavailable" && !f.ClickToCommit(),
        "Failed analysis must report failure without publishing proposals");
}
void TestRejectedReplacement() {
    Fixture f; f.ReadyFirst();
    f.colorUi.colorWarpPendingCircle.value=3;
    f.tasks.group.Stop();
    f.Submit();
    Require(!f.ClickToCommit() && f.colorUi.colorWarpAreaAnalysisResult.proposals.empty(),
        "Rejected replacement still permits committing the previous circle's proposals");
    Require(!f.colorUi.colorWarpPendingCircleRefining && !f.colorUi.colorWarpAreaAnalysisState &&
        f.colorUi.colorWarpAreaStatus=="Analysis unavailable", "Rejected submission has no terminal failure");
}
void TestThrowingSubmission() {
    Fixture f; f.ReadyFirst();
    f.colorUi.colorWarpPendingCircle.value=4;
    f.tasks.throwSubmission=true;
    f.Submit();
    Require(!f.colorUi.colorWarpPendingCircleRefining && !f.colorUi.colorWarpAreaAnalysisState &&
        !f.ClickToCommit() && f.colorUi.colorWarpAreaStatus=="Analysis unavailable",
        "Throwing submission left an unfinished or committable request");
}
void TestPreparationFailure() {
    Fixture f; f.ReadyFirst();
    f.colorUi.colorWarpPendingCircleCommitRequested=true;
    f.colorUi.colorWarpPendingCircle.value=4;
    // vector::reserve rejects this length before allocating any storage.
    f.colorScopePixelCount=std::vector<Stack::RawRecipe::RawColorWarpAnalysisSample>{}.max_size()+1;
    f.Submit();
    Require(!f.colorUi.colorWarpPendingCircleRefining && !f.colorUi.colorWarpAreaAnalysisState &&
        f.colorUi.colorWarpAreaAnalysisResult.proposals.empty() && !f.ClickToCommit() &&
        f.colorUi.colorWarpAreaStatus=="Analysis unavailable", "Preparation failure retained stale or unfinished work");
}
void TestReplacementClearsCommitIntent() {
    Fixture f; f.ReadyFirst();
    f.colorUi.colorWarpPendingCircleCommitRequested=true;
    f.colorUi.colorWarpPendingCircle.value=3;
    f.Submit();
    Require(!f.colorUi.colorWarpPendingCircleCommitRequested &&
        f.colorUi.colorWarpAreaAnalysisResult.proposals.empty(), "Replacement retained proposals or commit intent");
    f.Drain();
    Require(!f.colorUi.colorWarpPendingCircleCommitRequested &&
        f.colorUi.colorWarpAreaAnalysisResult.proposals==std::vector<int>{3},
        "New result must wait for its own commit gesture");
}
void TestEmptyAndAppendSemantics() {
    Fixture f;
    f.colorUi.colorWarpPendingCircle.value=0; f.Submit(); f.Drain();
    Require(f.colorUi.colorWarpAreaStatus=="No usable pixels" && !f.ClickToCommit(),
        "Empty successful analysis was treated as failure or allowed a commit");
    f.colorUi.colorWarpPendingCircle.value=-1; f.Submit(); f.Drain();
    Require(f.colorUi.colorWarpAreaStatus=="No pin slots available" && !f.ClickToCommit(),
        "Capacity-limited analysis lost its distinct status");
    f.colorUi.colorWarpPendingCircleAppend=true;
    Require(f.ClickToCommit(), "Appending an include/exclude circle must not require color proposals");
}
void TestLateReplacedAndResetResults() {
    Fixture f;
    auto& control=Stack::RawRecipe::control;
    { std::lock_guard lock(control.mutex); control.blockFirst=true; control.release=false; control.entered=false; }
    f.Submit();
    bool entered;
    { std::unique_lock lock(control.mutex); entered=control.cv.wait_for(lock,5s,[&]{return control.entered;}); }
    if (!entered) {
        { std::lock_guard lock(control.mutex); control.release=true; } control.cv.notify_all();
        f.Drain(); Require(false,"Delayed analysis did not start");
    }
    f.colorUi.colorWarpPendingCircle.value=5;
    f.Submit();
    { std::lock_guard lock(control.mutex); control.release=true; control.blockFirst=false; }
    control.cv.notify_all();
    f.Drain();
    Require(f.colorUi.colorWarpAreaAnalysisResult.proposals==std::vector<int>{5},
        "Delayed older analysis replaced the current request");
    f.colorUi.colorWarpPendingCircle.value=6;
    f.Submit();
    ++f.colorUi.colorWarpAreaAnalysisGeneration;
    f.colorUi.colorWarpAreaAnalysisState.reset();
    f.colorUi.colorWarpAreaAnalysisResult={};
    f.colorUi.colorWarpPendingCircleRefining=false;
    f.Drain();
    Require(f.colorUi.colorWarpAreaAnalysisResult.proposals.empty(), "Reset request republished obsolete proposals");
}
void TestDetachedRequestLifetime() {
    auto f=std::make_unique<Fixture>();
    auto& control=Stack::RawRecipe::control;
    { std::lock_guard lock(control.mutex); control.blockFirst=true; control.release=false; control.entered=false; }
    f->Submit();
    bool entered;
    { std::unique_lock lock(control.mutex); entered=control.cv.wait_for(lock,5s,[&]{return control.entered;}); }
    std::weak_ptr<Stack::EditorModuleTypes::RawWorkspaceColorWarpAreaAnalysisState> request=
        f->colorUi.colorWarpAreaAnalysisState;
    f.reset();
    { std::lock_guard lock(control.mutex); control.release=true; control.blockFirst=false; }
    control.cv.notify_all();
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while (Async::TaskSystem::Get().HasPendingWork() && std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    Require(entered && !Async::TaskSystem::Get().HasPendingWork() && request.expired(),
        "Detached analysis retained its UI/request owner after worker completion");
}
int main() {
    int failures=0;
    const auto run=[&](const char* name,auto test) {
        try { test(); std::cout<<"PASS "<<name<<'\n'; }
        catch(const std::exception& error) { ++failures; std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
    };
    run("success",TestSuccess);
    run("worker exception",TestWorkerFailure);
    run("rejected replacement",TestRejectedReplacement);
    run("throwing submission",TestThrowingSubmission);
    run("preparation failure",TestPreparationFailure);
    run("replacement commit intent",TestReplacementClearsCommitIntent);
    run("empty and append semantics",TestEmptyAndAppendSemantics);
    run("late and reset results",TestLateReplacedAndResetResults);
    run("detached request lifetime",TestDetachedRequestLifetime);
    Async::TaskSystem::Get().Shutdown();
    return failures ? 1 : 0;
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")


if __name__ == '__main__':
    main()
