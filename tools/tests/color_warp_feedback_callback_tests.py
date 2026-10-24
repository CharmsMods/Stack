#!/usr/bin/env python3
"""Generate no-GPU checks from the production feedback cache and method bodies.

Real TaskGroup/TaskSystem execute jobs. Only scope/recipe/computation endpoints
are reduced, with explicit allocation/copy/submission/worker failure injection.
No user files, image codecs, GL or native UI are exercised. The companion overlay
fixture checks the production H ownership/retry and error-presentation gate.
"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'src/Editor/NodeEditors/ColorWarpFeedback.cpp').read_text()
    header = (root / 'src/Editor/NodeEditors/ColorWarpFeedback.h').read_text()
    methods = source[source.index('void ColorWarpFeedback::Update('):
                     source.index('std::vector<unsigned char> BuildColorWarpFeedbackOverlay(')]
    cache = header[header.index('class ColorWarpFeedback {'):
                   header.index('\n} // namespace Stack::Editor')]
    text = r'''
#include "Async/TaskGroup.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
thread_local int failAllocationAfter=-1;
#if defined(_MSC_VER)
#define FEEDBACK_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define FEEDBACK_NOINLINE __attribute__((noinline))
#else
#define FEEDBACK_NOINLINE
#endif
FEEDBACK_NOINLINE void* operator new(std::size_t size) {
    if(failAllocationAfter>=0 && failAllocationAfter--==0) {
        failAllocationAfter=-1; throw std::bad_alloc();
    }
    if(void* value=std::malloc(size?size:1)) return value;
    throw std::bad_alloc();
}
FEEDBACK_NOINLINE void operator delete(void* value) noexcept { std::free(value); }
FEEDBACK_NOINLINE void operator delete(void* value,std::size_t) noexcept { std::free(value); }
#undef FEEDBACK_NOINLINE
namespace Raw { enum class RawWorkingSpace { Default, Other }; }
enum class RawDevelopmentGraphScopeStage { ColorWarpInput };
int scopeCopiesBeforeThrow=-1,recipeCopiesBeforeThrow=-1;
struct RawDevelopmentGraphScopeReadback {
    bool valid=true; RawDevelopmentGraphScopeStage stage=RawDevelopmentGraphScopeStage::ColorWarpInput;
    int width=1,height=1,sourceWidth=1,sourceHeight=1; std::size_t graphInputFingerprint=42;
    std::vector<float> pixels{0.1f,0.2f,0.3f};
    RawDevelopmentGraphScopeReadback()=default;
    RawDevelopmentGraphScopeReadback(const RawDevelopmentGraphScopeReadback& other) {
        if(scopeCopiesBeforeThrow>=0 && scopeCopiesBeforeThrow--==0) {
            scopeCopiesBeforeThrow=-1; throw std::runtime_error("scope copy failure");
        }
        valid=other.valid;stage=other.stage;width=other.width;height=other.height;
        sourceWidth=other.sourceWidth;sourceHeight=other.sourceHeight;
        graphInputFingerprint=other.graphInputFingerprint;pixels=other.pixels;
    }
};
namespace Stack::RawRecipe {
struct RawColorWarpCoordinate { float a=0,b=0,sceneEv=0; };
struct RawColorWarpPin { bool enabled=true; std::string regionId; };
struct Region { std::string id; };
struct RawColorWarpRecipe {
    int identity=1;
    std::vector<RawColorWarpPin> pins{{}}; std::vector<Region> regions;
    RawColorWarpRecipe()=default;
    RawColorWarpRecipe(const RawColorWarpRecipe& other) {
        if(recipeCopiesBeforeThrow>=0 && recipeCopiesBeforeThrow--==0) {
            recipeCopiesBeforeThrow=-1; throw std::runtime_error("recipe copy failure");
        }
        identity=other.identity;pins=other.pins;regions=other.regions;
    }
};
struct RawColorWarpMaskGuide { int width=0,height=0,sourceWidth=0,sourceHeight=0; std::vector<RawColorWarpCoordinate> pixels; };
struct RawColorWarpMaskFields {};
struct Control {
    std::mutex mutex; std::condition_variable cv;
    bool block=false,entered=false,release=false,fail=false;
    std::atomic<int> builds{0};
} control;
std::size_t ColorWarpSelectionFingerprint(const RawColorWarpRecipe& recipe) { return recipe.identity; }
RawColorWarpCoordinate WorkingRgbToColorWarpCoordinate(std::array<float,3> pixel,Raw::RawWorkingSpace) { return {pixel[0],pixel[1],pixel[2]}; }
template<class Cancel> RawColorWarpMaskFields BuildRawColorWarpMaskFields(
    const RawColorWarpMaskGuide&,const RawColorWarpPin&,const Region*,Cancel) {
    ++control.builds;
    std::unique_lock lock(control.mutex);
    if(control.block) { control.entered=true;control.cv.notify_all();control.cv.wait(lock,[]{return control.release;}); }
    if(control.fail) throw std::runtime_error("worker computation failure");
    return {};
}
}
namespace Stack::Editor {
struct ColorWarpFeedbackData { std::size_t selectionFingerprint=0; RawRecipe::RawColorWarpMaskGuide guide; std::vector<RawRecipe::RawColorWarpMaskFields> fields; };
template<class T> void Hash(std::size_t& seed,const T& value) { seed ^= std::hash<T>{}(value)+0x9e3779b97f4a7c15ull+(seed<<6u)+(seed>>2u); }
''' + cache + methods + r'''
}
struct Fixture {
    Stack::Editor::ColorWarpFeedback cache;
    Async::TaskGroup group;
    RawDevelopmentGraphScopeReadback scope;
    Stack::RawRecipe::RawColorWarpRecipe recipe;
    Raw::RawWorkingSpace working=Raw::RawWorkingSpace::Default;
    std::string identity="document/source/node";
    int submissions=0;
    bool reject=false,throwSubmit=false;
    std::function<bool(std::function<void()>)> submit=[this](std::function<void()> task) {
        ++submissions;
        if(throwSubmit) throw std::runtime_error("submission failure");
        return !reject && group.SubmitHighPriority("Feedback test",std::move(task));
    };
    void Step() { cache.Update(scope,identity,42,recipe,working,submit); }
    void Drain() {
        const auto deadline=std::chrono::steady_clock::now()+5s;
        while(!group.IsIdle() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
        Require(group.IsIdle(),"Feedback task did not drain");
    }
    void NoRetries() {
        const int submitted=submissions,built=Stack::RawRecipe::control.builds;
        const auto key=cache.Key();
        for(int frame=0;frame<100;++frame) Step();
        Require(cache.Failed() && !cache.Current() && key==cache.Key() &&
            submissions==submitted && Stack::RawRecipe::control.builds==built,
            "Unchanged failed feedback retried or exposed pixels");
    }
    void Recover() {
        reject=throwSubmit=false;
        {std::lock_guard lock(Stack::RawRecipe::control.mutex);Stack::RawRecipe::control.fail=false;}
        cache.RetryFailed();cache.RetryFailed();Step();Drain();
        Require(cache.Current() && !cache.Failed(),"Explicit failure retry did not recover");
    }
};
void ReleaseBlocked() {
    auto& c=Stack::RawRecipe::control;
    {std::lock_guard lock(c.mutex);c.release=true;c.block=false;}c.cv.notify_all();
}
void WaitBlocked() {
    auto& c=Stack::RawRecipe::control;std::unique_lock lock(c.mutex);
    if(!c.cv.wait_for(lock,5s,[&]{return c.entered;})) {
        c.release=true;c.block=false;lock.unlock();c.cv.notify_all();Require(false,"Blocked task did not enter");
    }
}
void Reset() {
    failAllocationAfter=scopeCopiesBeforeThrow=recipeCopiesBeforeThrow=-1;
    auto& c=Stack::RawRecipe::control;std::lock_guard lock(c.mutex);
    c.block=c.entered=c.release=c.fail=false;c.builds=0;
}
void SuccessRetained() {
    Fixture f;f.Step();f.Drain();auto data=f.cache.Current();
    Require(data && !f.cache.Failed(),"Success not published");
    f.cache.RetryFailed();
    for(int frame=0;frame<100;++frame)f.Step();
    Require(f.submissions==1 && f.cache.Current()==data,"Successful unchanged feedback rebuilt or retry discarded it");
}
void PreparationFailures() {
    for(int mode=0;mode<5;++mode) {
        Fixture f;
        if(mode==0)failAllocationAfter=0;
        if(mode==1)scopeCopiesBeforeThrow=0;
        if(mode==2)recipeCopiesBeforeThrow=0;
        if(mode==3)scopeCopiesBeforeThrow=1;
        if(mode==4)recipeCopiesBeforeThrow=1;
        f.Step();f.Drain();
        Require(f.submissions==0,"Preparation failure reached submit");
        f.NoRetries();f.Recover();
        Require(f.submissions==1,"Preparation retry submitted more than once");
    }
}
void SubmissionFailures() {
    for(int mode=0;mode<3;++mode) {
        Fixture f;f.reject=mode==0;f.throwSubmit=mode==1;if(mode==2)f.group.Stop();
        f.Step();f.Drain();f.NoRetries();
        Require(f.submissions==1,"Rejected submission did not latch");
        if(mode!=2)f.Recover();
    }
}
void WorkerFailure() {
    Fixture f;{std::lock_guard lock(Stack::RawRecipe::control.mutex);Stack::RawRecipe::control.fail=true;}
    f.Step();f.Drain();f.NoRetries();f.Recover();
    Require(f.submissions==2,"Worker retry did not submit exactly once");
}
void ChangedIdentityAndInvalidReset() {
    for(int change=0;change<7;++change) {
        Fixture f;f.reject=true;f.Step();f.NoRetries();f.reject=false;
        if(change==0)f.identity="other document/source/node";
        if(change==1)++f.recipe.identity;
        if(change==2)++f.scope.graphInputFingerprint;
        if(change==3)++f.scope.sourceWidth;
        if(change==4)f.working=Raw::RawWorkingSpace::Other;
        if(change==5) {f.scope.width=2;f.scope.pixels.resize(6);}
        if(change==6) {f.scope.valid=false;f.Step();Require(!f.cache.Failed()&&!f.cache.Current(),"Invalid scope kept old failure");f.scope.valid=true;}
        f.Step();f.Drain();Require(f.submissions==2 && f.cache.Current()&&!f.cache.Failed(),"Changed input/reset did not rearm");
    }
}
void SupersededCompletion() {
    Fixture f;{std::lock_guard lock(Stack::RawRecipe::control.mutex);Stack::RawRecipe::control.block=true;}
    f.Step();WaitBlocked();f.identity="new owner/input";f.scope.pixels[0]=0.7f;++f.recipe.identity;
    f.Step();Require(f.submissions==1&&!f.cache.Current()&&!f.cache.Failed(),"Supersession replaced active job early or called it failure");
    ReleaseBlocked();f.Drain();Require(!f.cache.Current()&&!f.cache.Failed(),"Canceled old result became current");
    f.Step();f.Drain();auto data=f.cache.Current();
    Require(f.submissions==2 && data && data->selectionFingerprint==2 && data->guide.pixels[0].a==0.7f,
        "Latest owner/input did not replace canceled callback");
}
void RetiredOwner() {
    auto f=std::make_unique<Fixture>();
    {std::lock_guard lock(Stack::RawRecipe::control.mutex);Stack::RawRecipe::control.block=true;}
    f->Step();WaitBlocked();f->group.Stop();f.reset();ReleaseBlocked();
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(Async::TaskSystem::Get().HasPendingWork()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    Require(!Async::TaskSystem::Get().HasPendingWork(),"Retired owner's callback did not drain");
}
int main() {
    int failures=0;
    const auto run=[&](const char* name,auto test) {
        Reset();try{test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){ReleaseBlocked();++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    };
    run("successful unchanged feedback",SuccessRetained);
    run("allocation and capture failures",PreparationFailures);
    run("submission false throw stopped",SubmissionFailures);
    run("worker failure 100 frames and retry",WorkerFailure);
    run("changed identity and reset",ChangedIdentityAndInvalidReset);
    run("delayed superseded completion",SupersededCompletion);
    run("retired owner",RetiredOwner);
    Async::TaskSystem::Get().Shutdown();return failures?1:0;
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == '__main__':
    main()
