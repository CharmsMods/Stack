#pragma once

#include "ThirdParty/json.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Stack::RawWorkspace {

inline constexpr int kRawGallerySimilarityDescriptorVersion = 2;
inline constexpr int kRawGallerySimilarityGridSize = 32;
inline constexpr int kRawGallerySimilarityGradientCellCount = 4;
inline constexpr int kRawGallerySimilarityGradientBinCount = 8;
inline constexpr int kRawGalleryManualGroupingVersion = 2;
inline constexpr std::size_t kRawGallerySimilarityLuminanceCount =
    kRawGallerySimilarityGridSize * kRawGallerySimilarityGridSize;
inline constexpr std::size_t kRawGallerySimilarityGradientCount =
    kRawGallerySimilarityGradientCellCount *
    kRawGallerySimilarityGradientCellCount *
    kRawGallerySimilarityGradientBinCount;

struct RawGallerySimilarityDescriptor {
    int version = 0;
    std::uint64_t perceptualHash = 0;
    std::array<std::uint8_t, kRawGallerySimilarityLuminanceCount>
        normalizedLuminance {};
    std::array<std::uint8_t, kRawGallerySimilarityGradientCount>
        gradientOrientation {};
    float edgeEnergy = 0.0f;
    float aspectRatio = 0.0f;

    bool IsValid() const;
};

struct RawGallerySimilarityInput {
    std::string sourceKey;
    std::string folderKey;
    std::size_t catalogIndex = 0;
    RawGallerySimilarityDescriptor descriptor;
};

struct RawGallerySimilarityStack {
    std::string folderKey;
    std::size_t anchorCatalogIndex = 0;
    std::vector<std::string> sourceKeys;
    // Derived presentation only. Project membership, not manual grouping,
    // owns a processed stack and its cover.
    std::string resultProjectId, resultProjectName;
    std::filesystem::path resultProjectPath, resultCoverPath;
};

enum class RawGalleryFilmstripSortMode {
    Manual = 0,
    FileNameAscending,
    FileNameDescending,
    ModifiedNewestFirst,
    ModifiedOldestFirst,
    FolderThenNameAscending,
    FolderThenNameDescending,
    TimelineAll,
    TimelineByFolder
};

struct RawGalleryFilmstripSourceSortInfo {
    std::string sourceKey;
    std::string fileName;
    std::string folderKey;
    std::int64_t modifiedTimeTicks = 0;
    std::size_t catalogIndex = 0;
    std::int64_t modifiedUnixSeconds = 0;
    std::int64_t captureTimestamp = 0;
};

struct RawGalleryManualGrouping {
    int version = kRawGalleryManualGroupingVersion;
    std::vector<std::vector<std::string>> stacks;
    std::vector<std::string> detachedSourceKeys;
    RawGalleryFilmstripSortMode sortMode =
        RawGalleryFilmstripSortMode::TimelineAll;
    std::vector<std::string> manualSourceOrder;
};

struct RawGallerySimilarityRequest {
    std::uint64_t generation = 0;
    std::string workspaceKey;
    std::vector<RawGallerySimilarityInput> inputs;
};

struct RawGallerySimilarityResult {
    std::uint64_t generation = 0;
    std::string workspaceKey;
    std::vector<RawGallerySimilarityStack> stacks;
};

struct RawGalleryFilmstripStackLayout {
    float cardStep = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
};

struct RawGalleryFilmstripDrawerState {
    double hoverStartedAt = -1.0;
    double leaveStartedAt = -1.0;
    bool open = false;
    bool keyboardPinned = false;
    bool suppressHoverUntilPointerExit = false;
};

struct RawGalleryFilmstripDrawerInput {
    double now = 0.0;
    bool enabled = false;
    bool pointerInside = false;
    bool interactionRetained = false;
    bool keyboardToggle = false;
    bool galleryWorkspace = false;
    bool previewHovered = false;
};

RawGallerySimilarityDescriptor BuildRawGallerySimilarityDescriptor(
    const unsigned char* rgba,
    int width,
    int height);

nlohmann::json SerializeRawGallerySimilarityDescriptor(
    const RawGallerySimilarityDescriptor& descriptor);

bool DeserializeRawGallerySimilarityDescriptor(
    const nlohmann::json& value,
    RawGallerySimilarityDescriptor& descriptor);

nlohmann::json SerializeRawGalleryManualGrouping(
    const RawGalleryManualGrouping& grouping);

bool DeserializeRawGalleryManualGrouping(
    const nlohmann::json& value,
    RawGalleryManualGrouping& grouping);

bool MergeRawGalleryManualStack(
    RawGalleryManualGrouping& grouping,
    const std::string& sourceKey,
    const std::vector<std::string>& targetStackSourceKeys);

bool DetachRawGalleryManualStackMember(
    RawGalleryManualGrouping& grouping,
    const std::string& sourceKey);

std::vector<std::string> NormalizeRawGalleryManualSourceOrder(
    const std::vector<std::string>& manualOrder,
    const std::vector<std::string>& catalogSourceKeys);

std::vector<std::string> SeedRawGalleryManualSourceOrder(
    const std::vector<RawGallerySimilarityStack>& visibleStacks);

std::vector<RawGallerySimilarityStack> SortRawGalleryFilmstripStacks(
    std::vector<RawGallerySimilarityStack> stacks,
    const std::vector<RawGalleryFilmstripSourceSortInfo>& sources,
    const RawGalleryManualGrouping& organization);

std::vector<std::string> ReorderRawGalleryManualSourceOrder(
    const std::vector<RawGallerySimilarityStack>& visibleStacks,
    const std::vector<std::string>& movingSourceKeys,
    std::size_t insertionStackIndex,
    const std::vector<std::string>& catalogSourceKeys);

bool MergeRawGalleryManualStackMembers(
    RawGalleryManualGrouping& grouping,
    const std::vector<std::string>& sourceKeys,
    const std::vector<std::string>& targetStackSourceKeys);

const char* RawGalleryFilmstripSortModeLabel(
    RawGalleryFilmstripSortMode mode);

std::size_t ResolveRawGalleryFilmstripInsertionIndex(
    float pointerX,
    float firstSlotX,
    float slotWidth,
    std::size_t stackCount);

float ComputeRawGalleryFilmstripAutoScrollVelocity(
    float pointerX,
    float minimumX,
    float maximumX,
    float edgeWidth,
    float maximumPixelsPerSecond);

float EaseRawGalleryFilmstripDrag(float progress);

std::vector<RawGallerySimilarityStack> ApplyRawGalleryManualGrouping(
    const std::vector<RawGallerySimilarityStack>& automaticStacks,
    const std::vector<RawGallerySimilarityInput>& catalog,
    const RawGalleryManualGrouping& grouping);

std::vector<RawGallerySimilarityStack> BuildRawGallerySimilarityStacks(
    const std::vector<RawGallerySimilarityInput>& inputs);

RawGallerySimilarityResult BuildRawGallerySimilarityResult(
    const RawGallerySimilarityRequest& request);

bool IsRawGallerySimilarityResultCurrent(
    const RawGallerySimilarityResult& result,
    std::uint64_t currentGeneration,
    const std::string& currentWorkspaceKey);

std::string ResolveRawGalleryStackCover(
    const std::vector<std::string>& sourceKeys,
    const std::string& selectedSourceKey);

std::vector<std::string> BuildRawGalleryExpansionOrder(
    const std::vector<std::string>& sourceKeys,
    const std::string& coverSourceKey);

RawGalleryFilmstripDrawerState UpdateRawGalleryFilmstripDrawerState(
    const RawGalleryFilmstripDrawerState& current,
    const RawGalleryFilmstripDrawerInput& input);

float ComputeRawGalleryFilmstripDrawerTargetHeight(
    float collapsedHeight,
    float tileHeight,
    float naturalGap,
    std::size_t maximumStackSize,
    float availableWorkspaceHeight);

RawGalleryFilmstripStackLayout ComputeRawGalleryFilmstripStackLayout(
    std::size_t cardCount,
    float cardHeight,
    float naturalGap,
    float baseCardTop,
    float topBoundary,
    float expansionProgress = 1.0f);

// A drawer keeps its expansion order stable while it is open. If asynchronous
// similarity results change a stack's membership, that saved order must be
// discarded so every current member can join the expansion.
bool IsRawGalleryFilmstripExpansionOrderCurrent(
    const std::vector<std::string>& sourceKeys,
    const std::vector<std::string>& expansionOrder);

bool ShouldRestoreRawGalleryForEmptyWorkspace(
    bool workspaceAvailable,
    bool workspaceLocked,
    bool rawProjectActive,
    bool multiFrameProjectActive,
    bool explicitOpenPending,
    bool galleryClosed);

} // namespace Stack::RawWorkspace
