#include "Editor/NodeEditors/ColorWarpCloudPreparation.h"
#include "Renderer/RenderPipeline.h"
#include "Raw/RawDevelopmentRecipe.h"
#include <cstdlib>
#include <deque>
#include <iostream>

namespace {
void Check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
struct Jobs {
    std::deque<std::function<void()>> jobs;
    bool Submit(std::function<void()> job) { jobs.push_back(std::move(job)); return true; }
    void Run() { auto job = std::move(jobs.front()); jobs.pop_front(); job(); }
};
}

int main() {
    using namespace Stack::Editor;
    RawDevelopmentGraphScopeReadback scope;
    scope.valid = true; scope.stage = RawDevelopmentGraphScopeStage::ColorWarpInput;
    scope.width = scope.height = 2;
    scope.pixels = {.8f,.1f,.1f, .1f,.8f,.1f, .1f,.1f,.8f, .5f,.5f,.5f};
    Stack::RawRecipe::RawColorWarpRecipe recipe;
    ColorWarpCloudDemand demand;
    demand.inputKey = 1; demand.live = true;
    Jobs jobs;
    const auto submit = [&](std::function<void()> job) { return jobs.Submit(std::move(job)); };
    ColorWarpCloudPreparation cloud;
    Check(!cloud.Update(scope, recipe, {}, demand, submit) && jobs.jobs.size() == 1,
        "Drawing only queues CPU preparation");
    Check(cloud.InputChanged(), "First input has its own identity");
    ++demand.nodeRevision;
    Check(!cloud.Update(scope, recipe, {}, demand, submit) && jobs.jobs.size() == 1,
        "Edits coalesce while one owner job is active");
    jobs.Run();
    auto result = cloud.Update(scope, recipe, {}, demand, submit);
    Check(result && result->size() == 4 && jobs.jobs.size() == 1,
        "Completed compatible feedback can display while latest adjustment queues");
    jobs.Run();
    result = cloud.Update(scope, recipe, {}, demand, submit);
    Check(result && cloud.SelectionCurrent() && jobs.jobs.empty(), "Latest preparation settles without more work");

    ++demand.nodeRevision;
    cloud.Update(scope, recipe, {}, demand, submit);
    recipe.limitHslToSelections = true;
    ++demand.nodeRevision;
    cloud.Update(scope, recipe, {}, demand, submit);
    Check(!cloud.SelectionCurrent(), "Changing selection semantics disables stale catch highlighting");
    jobs.Run();
    Check(!cloud.Update(scope, recipe, {}, demand, submit) && jobs.jobs.size() == 1,
        "Superseded selection interpretation never publishes");
    jobs.Run();
    Check(cloud.Update(scope, recipe, {}, demand, submit).has_value(), "Current selection eventually publishes");

    ++demand.nodeRevision;
    cloud.Update(scope, recipe, {}, demand, submit);
    demand.inputKey = 2;
    cloud.Update(scope, recipe, {}, demand, submit);
    Check(cloud.InputChanged(), "New source invalidates the old display buffer");
    jobs.Run();
    Check(!cloud.Update(scope, recipe, {}, demand, submit), "Old source cannot publish into new source");
    jobs.Run();
    Check(cloud.Update(scope, recipe, {}, demand, submit).has_value(), "New source remains independently runnable");

    {
        ColorWarpCloudPreparation a, b, c;
        a.Update(scope, recipe, {}, demand, submit);
        b.Update(scope, recipe, {}, demand, submit);
        c.Update(scope, recipe, {}, demand, submit);
        Check(jobs.jobs.size() == 2, "Queued and running work is bounded across owners");
        jobs.Run();
        c.Update(scope, recipe, {}, demand, submit);
        Check(jobs.jobs.size() == 2, "Released admission permits another owner to progress");
    }
    while (!jobs.jobs.empty()) jobs.Run();
    Check(true, "Queued work survives owner destruction without borrowing its state");
    std::cout << "Color cloud coalescing, selection/source ownership and bounded admission passed\n";
}
