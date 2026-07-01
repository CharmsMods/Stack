#pragma once

#include "Library/LibraryModule.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

namespace Stack::Library::ModuleUI {

inline constexpr float kCardMotionSpeed = 16.0f;
inline constexpr float kPreviewMotionSpeed = 18.0f;
inline constexpr float kStatusMotionSpeed = 12.0f;
inline constexpr float kDialogAppearDuration = 0.12f;
inline constexpr double kCardEntranceDurationSeconds = 0.95;
inline constexpr double kCardEntranceStaggerSeconds = 0.04;
inline constexpr int kCardEntranceStaggerMaxRank = 22;
inline constexpr float kCardEntranceOffsetY = 24.0f;

struct LibraryPackedCard {
    std::size_t index = 0;
    ImVec2 pos = ImVec2(0.0f, 0.0f);
    ImVec2 size = ImVec2(0.0f, 0.0f);
};

template <typename TMap>
LibraryCardMotionState& GetCardMotionState(TMap& states, const std::string& key) {
    return states[key];
}

inline float ResolveCardEntranceProgress(LibraryCardMotionState& motion, double now) {
    if (!motion.entranceInitialized) {
        motion.entranceInitialized = true;
        motion.entrance = 1.0f;
        motion.entranceStartTime = now - kCardEntranceDurationSeconds;
        return 1.0f;
    }

    const double raw = kCardEntranceDurationSeconds > 0.0
        ? (now - motion.entranceStartTime) / kCardEntranceDurationSeconds
        : 1.0;
    const float t = std::clamp(static_cast<float>(raw), 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    motion.entrance = std::max(motion.entrance, 1.0f - inverse * inverse * inverse);
    return std::clamp(motion.entrance, 0.0f, 1.0f);
}

template <typename TMap>
void PruneCardMotionStates(TMap& states, int frameCount) {
    for (auto it = states.begin(); it != states.end(); ) {
        if (frameCount - it->second.lastSeenFrame > 240) {
            it = states.erase(it);
        } else {
            ++it;
        }
    }
}

unsigned int LoadIconTexture(const std::string& filename);

void ComputeCoverUv(float sourceWidth, float sourceHeight, const ImRect& targetRect, ImVec2& outUv0, ImVec2& outUv1);
void DrawCardMotionFrame(ImDrawList* drawList, const ImRect& rect, float hover, float selected, bool hovered);
void DrawCardOverlayText(
    ImDrawList* drawList,
    const ImRect& rect,
    float alpha,
    const std::string& title,
    const std::string& secondary,
    const std::string& tertiary);
void DrawSplitHandle(ImDrawList* drawList, const ImRect& rect, float split, ImGuiID handleId, bool hovered, bool active, float alpha = 1.0f);

bool ProjectMatchesFilter(const ProjectEntry& project, const char* filter, const std::unordered_set<std::string>& activeTags, bool noTagOnly);
bool AssetMatchesFilter(const AssetEntry& asset, const char* filter, const std::unordered_set<std::string>& activeTags, bool noTagOnly);
ImVec2 ComputeLibraryCardSize(float sourceWidth, float sourceHeight);
std::vector<LibraryPackedCard> PackLibraryCards(const std::vector<LibraryPackedCard>& inputCards, float contentWidth, float gap);
std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value);
std::uint64_t HashString(const std::string& value);
std::string BuildTagFilterKey(const std::unordered_set<std::string>& activeTags);
bool IsRenderProject(const ProjectEntry& project);
bool IsCompositeProject(const ProjectEntry& project);
ImVec2 FitImageToBounds(float imageWidth, float imageHeight, const ImVec2& bounds);
void DrawComparePreview(
    const ProjectEntry& project,
    const ImVec2& requestedSize,
    float& split,
    float rounding = 14.0f,
    bool showLabels = false,
    float alpha = 1.0f);

} // namespace Stack::Library::ModuleUI
