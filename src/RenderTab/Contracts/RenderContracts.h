#pragma once

#include "RenderTab/Foundation/RenderFoundationTypes.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>
#include <limits>
#include <string>

namespace RenderContracts {

using SceneSnapshot = RenderFoundation::Snapshot;
using Id = RenderFoundation::Id;

inline constexpr Id kCameraObjectId = std::numeric_limits<Id>::max();

enum class DirtyFlags : std::uint32_t {
    None = 0,
    SceneStructure = 1u << 0,
    SceneContent = 1u << 1,
    Camera = 1u << 2,
    Display = 1u << 3,
    Settings = 1u << 4,
    Viewport = 1u << 5
};

inline DirtyFlags operator|(DirtyFlags left, DirtyFlags right) {
    return static_cast<DirtyFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

inline DirtyFlags& operator|=(DirtyFlags& left, DirtyFlags right) {
    left = left | right;
    return left;
}

inline bool HasAny(DirtyFlags value, DirtyFlags mask) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(mask)) != 0u;
}

enum class ResetClass : std::uint8_t {
    None = 0,
    DisplayOnly = 1,
    PartialAccumulation = 2,
    FullAccumulation = 3
};

struct SceneChangeSet {
    DirtyFlags dirtyFlags = DirtyFlags::None;
    ResetClass resetClass = ResetClass::None;
    std::string reason;
};

struct ViewportInputFrame {
    ImRect viewportRect {};
    ImVec2 mousePosition {};
    ImVec2 mouseDelta {};
    float deltaTime = 0.0f;
    bool viewportHovered = false;
    bool viewportActive = false;
    bool mouseAvailable = false;
    bool keyboardAvailable = false;
    bool leftClicked = false;
    bool leftReleased = false;
    bool leftDown = false;
    bool rightClicked = false;
    bool rightReleased = false;
    bool rightDown = false;
};

} // namespace RenderContracts
