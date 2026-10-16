#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <array>

namespace Stack::UiPerformance {

enum class Stage : std::size_t {
    Frame, MainCompletions, Interface, DrawSubmission, Swap,
    ProjectWorkspaces, GraphCanvas, LogicalLayout, Projection, NodeOrder,
    Links, LinkHitTest, Catalog, PreviewRequests, ViewWorkspace, ViewPanel,
    FloatingControls, NavigationBars, Compositor, NativeChrome,
    ViewServices, ViewContext, ViewImage, ViewSubmission,
    Backdrop, BackdropBlur, BackdropNoise, CompositorBody,
    FrameTarget, CompositorState, CompositorPresent, PlatformPresent, Count
};
enum class Counter : std::size_t {
    Nodes, VisibleNodes, Links, VisibleLinks, CatalogMatches, CatalogCards,
    LogicalReuse, LogicalRebuild, ProjectionReuse, ProjectionRebuild,
    OrderReuse, OrderRebuild, PreviewCandidates, PreviewCandidateRebuild, Count
};
enum class Workspace { Other, Graph, View, Library, Queue };
struct FrameSample {
    std::array<double, static_cast<std::size_t>(Stage::Count)> milliseconds {};
    std::array<std::uint64_t, static_cast<std::size_t>(Counter::Count)> counters {};
    int vertices = 0, indices = 0;
};
const FrameSample& LastFrame();
void EnableFrameCapture(bool enabled);
struct Context {
    Workspace workspace = Workspace::Other;
    bool panelOpen = false;
    bool transitioning = false;
    std::size_t projects = 0;
    int refreshRate = 60;
};

// UI-thread diagnostics only. Disabled unless STACK_UI_PERF_TRACE is enabled.
// Keep a bounded batch in memory; disk output occurs at batch/context boundaries.
void BeginFrame();
void FinishFrame(const Context& context, double frameMs, double completionMs,
    double interfaceMs, double drawMs, double swapMs, int vertices, int indices);
void Add(Counter counter, std::uint64_t value = 1);
void Flush();

class Span {
public:
    explicit Span(Stage stage);
    ~Span();
    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;
private:
    Stage m_Stage;
    bool m_Active;
    bool m_Entered;
    std::chrono::steady_clock::time_point m_Started;
};

} // namespace Stack::UiPerformance
