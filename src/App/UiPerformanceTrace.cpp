#include "App/UiPerformanceTrace.h"
#include "App/AppPaths.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace Stack::UiPerformance {
namespace {
constexpr std::size_t kBatchSize = 240;
constexpr std::size_t kStages = static_cast<std::size_t>(Stage::Count);
constexpr std::size_t kCounters = static_cast<std::size_t>(Counter::Count);
bool captureEnabled = false;
using Sample = FrameSample;
struct State {
    Sample current;
    std::array<Sample, kBatchSize> samples;
    Context context;
    std::size_t size = 0;
    bool frameOpen = false;
    bool failed = false;
    std::array<unsigned,kStages> depth {};
    std::ofstream stream;
};
State& Trace() { static State state; return state; }
bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("STACK_UI_PERF_TRACE");
        return value && value[0] && value[0] != '0';
    }();
    return enabled || captureEnabled;
}
bool SameContext(const Context& a, const Context& b) {
    return a.workspace == b.workspace && a.panelOpen == b.panelOpen &&
        a.transitioning == b.transitioning && a.projects == b.projects &&
        a.refreshRate == b.refreshRate;
}
const char* Name(Workspace workspace) {
    switch (workspace) {
        case Workspace::Graph: return "graph";
        case Workspace::View: return "view";
        case Workspace::Library: return "library";
        case Workspace::Queue: return "queue";
        default: return "other";
    }
}
constexpr std::array<const char*, kStages> kStageNames {
    "frame", "mainCompletions", "interface", "drawSubmission", "swap",
    "projectWorkspaces", "graphCanvas", "logicalLayout", "projection", "nodeOrder",
    "links", "linkHitTest", "catalog", "previewRequests", "viewWorkspace", "viewPanel",
    "floatingControls", "navigationBars", "compositor", "nativeChrome",
    "viewServices", "viewContext", "viewImage", "viewSubmission",
    "backdrop", "backdropBlur", "backdropNoise", "compositorBody",
    "frameTarget", "compositorState", "compositorPresent", "platformPresent"
};
constexpr std::array<const char*, kCounters> kCounterNames {
    "nodes", "visibleNodes", "links", "visibleLinks", "catalogMatches", "catalogCards",
    "logicalReuse", "logicalRebuild", "projectionReuse", "projectionRebuild",
    "orderReuse", "orderRebuild", "previewCandidates", "previewCandidateRebuild", "layoutPrepare", "layoutSolve",
    "layoutRevealingNodes", "layoutTracingLinks", "layoutOrigin",
    "wireRouteRequest", "wireRoutePublish", "wireProjection"
};
} // namespace

void Flush() {
    if (!Enabled()) return;
    auto& state = Trace();
    if (captureEnabled) { state.size = 0; return; }
    if (!state.size || state.failed) return;
    try {
        if (!state.stream.is_open()) {
            const auto directory = AppPaths::GetLogsDirectory();
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            state.stream.open(directory / "ui_perf_trace.log", std::ios::app);
        }
        if (!state.stream.is_open()) { state.failed = true; state.size = 0; return; }
        auto& out = state.stream;
        out << std::fixed << std::setprecision(3)
            << "workspace=" << Name(state.context.workspace)
            << " panel=" << state.context.panelOpen
            << " transition=" << state.context.transitioning
            << " projects=" << state.context.projects
            << " refreshHz=" << state.context.refreshRate
            << " samples=" << state.size;
        const double budget = 1000.0 / std::max(1, state.context.refreshRate);
        std::size_t overBudget = 0;
        std::uint64_t vertices = 0, indices = 0;
        for (std::size_t i = 0; i < state.size; ++i) {
            overBudget += state.samples[i].milliseconds[static_cast<std::size_t>(Stage::Frame)] > budget;
            vertices += static_cast<std::uint64_t>(std::max(0, state.samples[i].vertices));
            indices += static_cast<std::uint64_t>(std::max(0, state.samples[i].indices));
        }
        out << " overBudget=" << overBudget;
        std::array<double, kBatchSize> sorted {};
        for (std::size_t stage = 0; stage < kStages; ++stage) {
            for (std::size_t i = 0; i < state.size; ++i) sorted[i] = state.samples[i].milliseconds[stage];
            std::sort(sorted.begin(), sorted.begin() + state.size);
            out << ' ' << kStageNames[stage] << "Ms(p50/p95/max)="
                << sorted[(state.size - 1) / 2] << '/'
                << sorted[((state.size - 1) * 95 + 99) / 100] << '/' << sorted[state.size - 1];
        }
        for (std::size_t counter = 0; counter < kCounters; ++counter) {
            std::uint64_t total = 0;
            for (std::size_t i = 0; i < state.size; ++i) total += state.samples[i].counters[counter];
            out << ' ' << kCounterNames[counter] << "Avg=" << static_cast<double>(total) / state.size;
        }
        out << " verticesAvg=" << static_cast<double>(vertices) / state.size
            << " indicesAvg=" << static_cast<double>(indices) / state.size << '\n';
        out.flush();
        if (!out) state.failed = true;
    } catch (...) {
        // A diagnostic cannot prevent drawing or make a project operation fail.
        state.failed = true;
    }
    state.size = 0;
}

void BeginFrame() {
    if (!Enabled()) return;
    auto& state = Trace();
    state.current = {};
    state.frameOpen = !state.failed;
}
const FrameSample& LastFrame() { return Trace().current; }
void EnableFrameCapture(bool enabled) { captureEnabled = enabled; }
void FinishFrame(const Context& context, double frameMs, double completionMs,
    double interfaceMs, double drawMs, double swapMs, int vertices, int indices) {
    if (!Enabled()) return;
    auto& state = Trace();
    if (!state.frameOpen) return;
    state.frameOpen = false;
    if (state.size && !SameContext(context, state.context)) Flush();
    if (state.failed) return;
    state.context = context;
    auto& sample = state.current;
    sample.milliseconds[static_cast<std::size_t>(Stage::Frame)] = frameMs;
    sample.milliseconds[static_cast<std::size_t>(Stage::MainCompletions)] = completionMs;
    sample.milliseconds[static_cast<std::size_t>(Stage::Interface)] = interfaceMs;
    // drawMs includes glfwSwapBuffers. This reports CPU submission/waits outside swap;
    // it is not a GPU execution timer. Stage spans overlap their parent scopes.
    sample.milliseconds[static_cast<std::size_t>(Stage::DrawSubmission)] = std::max(0.0, drawMs - swapMs);
    sample.milliseconds[static_cast<std::size_t>(Stage::Swap)] = swapMs;
    sample.vertices = vertices; sample.indices = indices;
    state.samples[state.size++] = sample;
    if (state.size == kBatchSize) Flush();
}
void Add(Counter counter, std::uint64_t value) {
    if (Enabled() && Trace().frameOpen) Trace().current.counters[static_cast<std::size_t>(counter)] += value;
}
Span::Span(Stage stage) : m_Stage(stage), m_Active(false), m_Entered(Enabled() && Trace().frameOpen) {
    if (m_Entered) m_Active = Trace().depth[static_cast<std::size_t>(stage)]++ == 0;
    if (m_Active) m_Started = std::chrono::steady_clock::now();
}
Span::~Span() {
    if (m_Entered) --Trace().depth[static_cast<std::size_t>(m_Stage)];
    if (m_Active && Trace().frameOpen) {
        Trace().current.milliseconds[static_cast<std::size_t>(m_Stage)] +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Started).count();
    }
}
} // namespace Stack::UiPerformance
