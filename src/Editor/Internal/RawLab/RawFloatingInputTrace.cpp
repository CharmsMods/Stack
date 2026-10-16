#include "Editor/Internal/RawLab/RawFloatingInputTrace.h"
#include "App/AppPaths.h"
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <cstdlib>

namespace Stack::Editor::RawLabInternal {
namespace {
// Keep recent events even after a long session. Two bounded files replace the
// old lifetime event limit, which silently stopped recording after 2048 lines.
class TraceFile {
public:
    void Write(const std::string& line) {
        if (!initialized) {
            path = AppPaths::GetLogsDirectory() / "raw_tool_input_trace.log";
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            Rotate();
            initialized = true;
        }
        if (bytes + line.size() > 2 * 1024 * 1024) Rotate();
        if (!output) return;
        output << line << '\n';
        output.flush();
        bytes += line.size() + 1;
    }
private:
    void Rotate() {
        output.close();
        std::error_code error;
        const auto previous = path.parent_path() / "raw_tool_input_trace.previous.log";
        if (std::filesystem::exists(path, error)) {
            std::filesystem::remove(previous, error);
            error.clear();
            std::filesystem::rename(path, previous, error);
        }
        output.clear();
        output.open(path, std::ios::trunc);
        bytes = 0;
    }
    bool initialized = false;
    std::size_t bytes = 0;
    std::filesystem::path path;
    std::ofstream output;
};

void Write(const std::string& line) {
    static TraceFile output;
    output.Write(line);
}

std::string State(const RawFloatingTraceContext& c) {
    std::ostringstream out;
    out << "owner=" << c.owner << " document=" << c.document << " source=" << c.source
        << " layer=" << c.layer << " operation=" << c.operation << " tool=" << c.tool
        << " enabled=" << c.enabled << " editable=" << c.editable << " generic=" << c.genericOperation
        << " navigating=" << c.navigating << " catalogStatus=" << c.projectStatus << " resolvedMode=" << c.projectMode
        << " sourcePresent=" << c.gates.sourcePresent << " projectActive=" << c.gates.projectActive
        << " sourceMatches=" << c.gates.sourceMatches << " resolved=" << c.gates.recipeResolved
        << " catalogEditable=" << c.gates.catalogEditable << " previewQueued=" << c.gates.previewQueued
        << " loading=" << c.gates.loading << " loadFailed=" << c.gates.loadFailed
        << " lifecycle=" << c.gates.lifecycle << " loadState=" << c.gates.loadState
        << " error=" << std::quoted(c.error.substr(0, 512))
        << " catalogReason=" << std::quoted(c.catalogReason.substr(0, 512));
    return out.str();
}

float ReceivedWheel() {
    // NewFrame's event trail survives temporary input suppression. Compare
    // it with routed IO without exposing project state or changing routing.
    float wheel = 0.f;
    for (const auto& event : ImGui::GetCurrentContext()->InputEventsTrail)
        if (event.Type == ImGuiInputEventType_MouseWheel) wheel += event.MouseWheel.WheelY;
    return wheel;
}
} // namespace

bool FloatingInputTraceEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("STACK_RAW_TOOL_INPUT_TRACE");
        return value && value[0] && value[0] != '0';
    }();
    return enabled;
}

bool BeginFloatingInputTrace(RawFloatingTraceState& state,
    const RawFloatingTraceContext& context, int phase, bool ownsPointer) noexcept {
    if (!FloatingInputTraceEnabled()) return false;
    try {
        const auto key = std::hash<std::string>{}(State(context));
        const auto& io = ImGui::GetIO();
        const double now = ImGui::GetTime();
        const bool changed = !state.observed || key != state.contextKey || phase != state.phase;
        const bool edge = io.MouseClicked[0] || io.MouseClicked[1] || io.MouseReleased[0] || io.MouseReleased[1];
        const bool sample = ownsPointer && now >= state.nextPointerSample;
        const bool heartbeat = now >= state.nextHeartbeat;
        const bool wheel = std::abs(ReceivedWheel()) > .0001f;
        state.observed = true;
        state.contextKey = key;
        state.phase = phase;
        if (!changed && !edge && !sample && !heartbeat && !wheel) return false;
        state.nextPointerSample = now + .1;
        state.nextHeartbeat = now + 2;
        return true;
    } catch (...) { return false; }
}

void TraceFloatingInput(const RawFloatingTraceContext& context, int phase,
    const char* stage) noexcept {
    if (!FloatingInputTraceEnabled()) return;
    try {
        const auto& gui = *ImGui::GetCurrentContext();
        const auto& io = gui.IO;
        const auto* window = ImGui::GetCurrentWindow();
        std::ostringstream out;
        out << "v=2 frame=" << gui.FrameCount << " time=" << gui.Time << ' ' << State(context)
            << " stage=" << stage << " phase=" << phase << " canStart=" << context.canStart
            << " disabled=" << bool(gui.CurrentItemFlags & ImGuiItemFlags_Disabled)
            << " disabledDepth=" << gui.DisabledStackSize << " focusLost=" << io.AppFocusLost
            << " popupDepth=" << gui.OpenPopupStack.Size << " wantText=" << io.WantTextInput
            << " wantMouse=" << io.WantCaptureMouse << " mouse=" << io.MousePos.x << ',' << io.MousePos.y
            << " receivedWheel=" << ReceivedWheel() << " routedWheel=" << io.MouseWheel
            << " buttons=" << io.MouseDown[0] << io.MouseDown[1]
            << " clicks=" << io.MouseClicked[0] << io.MouseClicked[1]
            << " releases=" << io.MouseReleased[0] << io.MouseReleased[1]
            << " active=" << gui.ActiveId << " activeTime=" << gui.ActiveIdTimer << " move=" << window->MoveId
            << " activeWindow=" << (gui.ActiveIdWindow ? gui.ActiveIdWindow->Name : "none")
            << " hoveredWindow=" << (gui.HoveredWindow ? gui.HoveredWindow->Name : "none")
            << " window=" << window->Pos.x << ',' << window->Pos.y << ',' << window->Size.x << ',' << window->Size.y
            << " item=" << gui.LastItemData.ID << " itemFlags=" << gui.LastItemData.ItemFlags;
        Write(out.str());
    } catch (...) { /* Diagnostics must not change editing behavior. */ }
}

void TraceRawImageNavigation(const void* owner, int tool, const char* stage,
    bool toolOwnsPointer, bool hovered, bool wheelAllowed,
    float zoom, float targetBefore, float targetAfter) noexcept {
    if (!FloatingInputTraceEnabled()) return;
    try {
        const float received = ReceivedWheel();
        const auto& gui = *ImGui::GetCurrentContext();
        const auto& io = gui.IO;
        if (std::abs(received) < .0001f && std::abs(io.MouseWheel) < .0001f) return;
        std::ostringstream out;
        out << "v=3 frame=" << gui.FrameCount << " time=" << gui.Time << " owner=" << owner
            << " tool=" << tool << " stage=" << stage
            << " receivedWheel=" << received << " routedWheel=" << io.MouseWheel
            << " mouse=" << io.MousePos.x << ',' << io.MousePos.y
            << " toolOwnsPointer=" << toolOwnsPointer << " hovered=" << hovered
            << " wheelAllowed=" << wheelAllowed << " zoom=" << zoom
            << " target=" << targetBefore << '/' << targetAfter
            << " alt=" << io.KeyAlt << " middleDown=" << io.MouseDown[2]
            << " focusLost=" << io.AppFocusLost << " popupDepth=" << gui.OpenPopupStack.Size
            << " disabledDepth=" << gui.DisabledStackSize << " active=" << gui.ActiveId
            << " activeWindow=" << (gui.ActiveIdWindow ? gui.ActiveIdWindow->Name : "none")
            << " hoveredWindow=" << (gui.HoveredWindow ? gui.HoveredWindow->Name : "none");
        Write(out.str());
    } catch (...) { /* Diagnostics must not change navigation behavior. */ }
}

void TraceFloatingControlEdit(RawFloatingTraceState& state, const void* owner,
    bool changed, bool interactionActive, bool editable,
    std::uint64_t revisionBefore, std::uint64_t revisionAfter) noexcept {
    if (!FloatingInputTraceEnabled()) return;
    try {
        const bool transition = interactionActive != state.editing;
        state.editing = interactionActive;
        const auto now = ImGui::GetTime();
        if (!transition && (!changed || now < state.nextEditSample)) return;
        state.nextEditSample = now + .1;
        std::ostringstream out;
        out << "v=2 frame=" << ImGui::GetFrameCount() << " time=" << now << " owner=" << owner
            << " stage=edit-commit changed=" << changed << " interaction=" << interactionActive
            << " editable=" << editable << " revision=" << revisionBefore << '/' << revisionAfter;
        Write(out.str());
    } catch (...) { /* Diagnostics must not change editing behavior. */ }
}
} // namespace Stack::Editor::RawLabInternal
