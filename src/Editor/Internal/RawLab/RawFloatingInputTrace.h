#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Stack::Editor::RawLabInternal {

// Temporary diagnostics, owned by each editor. These describe the actual
// editability checks; they never grant permission or change input routing.
struct RawEditabilityTrace {
    bool sourcePresent = false;
    bool projectActive = false;
    bool sourceMatches = false;
    bool recipeResolved = false;
    bool catalogEditable = false;
    bool previewQueued = false;
    bool loading = false;
    bool loadFailed = false;
    int lifecycle = -1;
    int loadState = -1;
};

struct RawFloatingTraceState {
    std::size_t contextKey = 0;
    int phase = -1;
    bool observed = false;
    bool editing = false;
    double nextPointerSample = 0;
    double nextHeartbeat = 0;
    double nextEditSample = 0;
};

struct RawFloatingTraceContext {
    const void* owner = nullptr;
    std::size_t document = 0, source = 0, layer = 0, operation = 0;
    int tool = -1, projectStatus = -1, projectMode = -1;
    bool enabled = false, editable = false, navigating = false, canStart = false;
    bool genericOperation = false;
    RawEditabilityTrace gates;
    std::string error, catalogReason;
};

bool BeginFloatingInputTrace(RawFloatingTraceState& state,
    const RawFloatingTraceContext& context, int phase, bool ownsPointer) noexcept;
bool FloatingInputTraceEnabled() noexcept;
void TraceFloatingInput(const RawFloatingTraceContext& context, int phase,
    const char* stage) noexcept;
void TraceFloatingControlEdit(RawFloatingTraceState& state, const void* owner,
    bool changed, bool interactionActive, bool editable,
    std::uint64_t revisionBefore, std::uint64_t revisionAfter) noexcept;
void TraceRawImageNavigation(const void* owner, int tool, const char* stage,
    bool toolOwnsPointer, bool hovered, bool wheelAllowed,
    float zoom, float targetBefore, float targetAfter) noexcept;

} // namespace Stack::Editor::RawLabInternal
