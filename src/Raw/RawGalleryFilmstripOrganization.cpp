#include "Raw/RawGallerySimilarity.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Stack::RawWorkspace {
namespace {

std::string LowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

template <typename T>
int CompareValues(const T& left, const T& right) {
    if (left < right) return -1;
    if (right < left) return 1;
    return 0;
}

std::int64_t TimelineTime(const RawGalleryFilmstripSourceSortInfo& source) {
    if (source.captureTimestamp > 0) return source.captureTimestamp;
    if (source.modifiedUnixSeconds > 0) return source.modifiedUnixSeconds;
    return source.modifiedTimeTicks;
}

int CompareKnownTimes(std::int64_t left, std::int64_t right) {
    if (left == 0) return right == 0 ? 0 : 1;
    if (right == 0) return -1;
    return CompareValues(left, right);
}

} // namespace

std::vector<std::string> NormalizeRawGalleryManualSourceOrder(
    const std::vector<std::string>& manualOrder,
    const std::vector<std::string>& catalogSourceKeys) {
    std::unordered_set<std::string> catalog;
    catalog.reserve(catalogSourceKeys.size());
    for (const std::string& key : catalogSourceKeys) {
        if (!key.empty()) catalog.insert(key);
    }

    std::vector<std::string> normalized;
    normalized.reserve(catalog.size());
    std::unordered_set<std::string> appended;
    appended.reserve(catalog.size());
    for (const std::string& key : manualOrder) {
        if (catalog.find(key) != catalog.end() && appended.insert(key).second) {
            normalized.push_back(key);
        }
    }
    for (const std::string& key : catalogSourceKeys) {
        if (!key.empty() && appended.insert(key).second) {
            normalized.push_back(key);
        }
    }
    return normalized;
}

std::vector<std::string> SeedRawGalleryManualSourceOrder(
    const std::vector<RawGallerySimilarityStack>& visibleStacks) {
    std::vector<std::string> order;
    std::unordered_set<std::string> appended;
    for (const RawGallerySimilarityStack& stack : visibleStacks) {
        for (const std::string& key : stack.sourceKeys) {
            if (!key.empty() && appended.insert(key).second) {
                order.push_back(key);
            }
        }
    }
    return order;
}

std::vector<RawGallerySimilarityStack> SortRawGalleryFilmstripStacks(
    std::vector<RawGallerySimilarityStack> stacks,
    const std::vector<RawGalleryFilmstripSourceSortInfo>& sources,
    const RawGalleryManualGrouping& organization) {
    std::unordered_map<std::string, RawGalleryFilmstripSourceSortInfo> byKey;
    byKey.reserve(sources.size());
    std::vector<std::string> catalogKeys;
    catalogKeys.reserve(sources.size());
    for (const RawGalleryFilmstripSourceSortInfo& source : sources) {
        if (source.sourceKey.empty()) continue;
        byKey.emplace(source.sourceKey, source);
        catalogKeys.push_back(source.sourceKey);
    }

    const std::vector<std::string> manualOrder =
        NormalizeRawGalleryManualSourceOrder(
            organization.manualSourceOrder,
            catalogKeys);
    std::unordered_map<std::string, std::size_t> manualRanks;
    manualRanks.reserve(manualOrder.size());
    for (std::size_t index = 0; index < manualOrder.size(); ++index) {
        manualRanks.emplace(manualOrder[index], index);
    }

    const auto sourceForStack = [&](const RawGallerySimilarityStack& stack)
        -> const RawGalleryFilmstripSourceSortInfo* {
        if (stack.sourceKeys.empty()) return nullptr;
        const auto found = byKey.find(stack.sourceKeys.front());
        return found == byKey.end() ? nullptr : &found->second;
    };
    const auto rankForStack = [&](const RawGallerySimilarityStack& stack) {
        if (stack.sourceKeys.empty()) {
            return std::numeric_limits<std::size_t>::max();
        }
        const auto found = manualRanks.find(stack.sourceKeys.front());
        return found == manualRanks.end()
            ? std::numeric_limits<std::size_t>::max()
            : found->second;
    };

    std::stable_sort(
        stacks.begin(),
        stacks.end(),
        [&](const RawGallerySimilarityStack& left,
            const RawGallerySimilarityStack& right) {
            const auto* leftSource = sourceForStack(left);
            const auto* rightSource = sourceForStack(right);
            if (leftSource == nullptr || rightSource == nullptr) {
                return left.anchorCatalogIndex < right.anchorCatalogIndex;
            }

            int comparison = 0;
            switch (organization.sortMode) {
                case RawGalleryFilmstripSortMode::Manual:
                    comparison = CompareValues(
                        rankForStack(left), rankForStack(right));
                    break;
                case RawGalleryFilmstripSortMode::FileNameAscending:
                case RawGalleryFilmstripSortMode::FileNameDescending:
                    comparison = CompareValues(
                        LowerAscii(leftSource->fileName),
                        LowerAscii(rightSource->fileName));
                    break;
                case RawGalleryFilmstripSortMode::ModifiedNewestFirst:
                case RawGalleryFilmstripSortMode::ModifiedOldestFirst:
                    comparison = CompareValues(
                        leftSource->modifiedTimeTicks,
                        rightSource->modifiedTimeTicks);
                    break;
                case RawGalleryFilmstripSortMode::TimelineAll:
                    comparison = CompareKnownTimes(
                        TimelineTime(*leftSource), TimelineTime(*rightSource));
                    break;
                case RawGalleryFilmstripSortMode::TimelineByFolder:
                    comparison = CompareValues(
                        LowerAscii(leftSource->folderKey),
                        LowerAscii(rightSource->folderKey));
                    if (comparison == 0) {
                        comparison = CompareKnownTimes(
                            TimelineTime(*leftSource), TimelineTime(*rightSource));
                    }
                    break;
                case RawGalleryFilmstripSortMode::FolderThenNameAscending:
                case RawGalleryFilmstripSortMode::FolderThenNameDescending:
                    comparison = CompareValues(
                        LowerAscii(leftSource->folderKey),
                        LowerAscii(rightSource->folderKey));
                    if (comparison == 0) {
                        comparison = CompareValues(
                            LowerAscii(leftSource->fileName),
                            LowerAscii(rightSource->fileName));
                    }
                    break;
            }

            const bool descending =
                organization.sortMode ==
                    RawGalleryFilmstripSortMode::FileNameDescending ||
                organization.sortMode ==
                    RawGalleryFilmstripSortMode::ModifiedNewestFirst ||
                organization.sortMode ==
                    RawGalleryFilmstripSortMode::FolderThenNameDescending;
            if (comparison != 0) {
                return descending ? comparison > 0 : comparison < 0;
            }
            return leftSource->catalogIndex < rightSource->catalogIndex;
        });
    return stacks;
}

std::vector<std::string> ReorderRawGalleryManualSourceOrder(
    const std::vector<RawGallerySimilarityStack>& visibleStacks,
    const std::vector<std::string>& movingSourceKeys,
    std::size_t insertionStackIndex,
    const std::vector<std::string>& catalogSourceKeys) {
    std::unordered_set<std::string> moving;
    for (const std::string& key : movingSourceKeys) {
        if (!key.empty()) moving.insert(key);
    }
    if (moving.empty()) {
        return NormalizeRawGalleryManualSourceOrder({}, catalogSourceKeys);
    }

    insertionStackIndex = std::min(insertionStackIndex, visibleStacks.size());
    std::size_t removedStacksBeforeInsertion = 0;
    std::vector<std::vector<std::string>> remainingStacks;
    remainingStacks.reserve(visibleStacks.size());
    for (std::size_t stackIndex = 0;
         stackIndex < visibleStacks.size();
         ++stackIndex) {
        std::vector<std::string> remaining;
        for (const std::string& key : visibleStacks[stackIndex].sourceKeys) {
            if (moving.find(key) == moving.end()) remaining.push_back(key);
        }
        if (remaining.empty()) {
            if (stackIndex < insertionStackIndex) {
                ++removedStacksBeforeInsertion;
            }
        } else {
            remainingStacks.push_back(std::move(remaining));
        }
    }

    const std::size_t adjustedInsertion = std::min(
        insertionStackIndex -
            std::min(insertionStackIndex, removedStacksBeforeInsertion),
        remainingStacks.size());
    std::vector<std::string> order;
    order.reserve(catalogSourceKeys.size());
    const auto appendMoving = [&]() {
        for (const std::string& key : movingSourceKeys) {
            if (!key.empty()) order.push_back(key);
        }
    };
    for (std::size_t stackIndex = 0;
         stackIndex <= remainingStacks.size();
         ++stackIndex) {
        if (stackIndex == adjustedInsertion) appendMoving();
        if (stackIndex < remainingStacks.size()) {
            order.insert(
                order.end(),
                remainingStacks[stackIndex].begin(),
                remainingStacks[stackIndex].end());
        }
    }
    return NormalizeRawGalleryManualSourceOrder(order, catalogSourceKeys);
}

bool MergeRawGalleryManualStackMembers(
    RawGalleryManualGrouping& grouping,
    const std::vector<std::string>& sourceKeys,
    const std::vector<std::string>& targetStackSourceKeys) {
    std::vector<std::string> mergedTarget = targetStackSourceKeys;
    bool changed = false;
    for (const std::string& sourceKey : sourceKeys) {
        if (sourceKey.empty() ||
            std::find(
                mergedTarget.begin(), mergedTarget.end(), sourceKey) !=
                mergedTarget.end()) {
            continue;
        }
        if (MergeRawGalleryManualStack(
                grouping, sourceKey, mergedTarget)) {
            mergedTarget.push_back(sourceKey);
            changed = true;
        }
    }
    return changed;
}

const char* RawGalleryFilmstripSortModeLabel(
    RawGalleryFilmstripSortMode mode) {
    switch (mode) {
        case RawGalleryFilmstripSortMode::Manual:
            return "Manual";
        case RawGalleryFilmstripSortMode::FileNameAscending:
            return "File name A-Z";
        case RawGalleryFilmstripSortMode::FileNameDescending:
            return "File name Z-A";
        case RawGalleryFilmstripSortMode::ModifiedNewestFirst:
            return "Modified newest first";
        case RawGalleryFilmstripSortMode::ModifiedOldestFirst:
            return "Modified oldest first";
        case RawGalleryFilmstripSortMode::FolderThenNameAscending:
            return "Folder then filename A-Z";
        case RawGalleryFilmstripSortMode::FolderThenNameDescending:
            return "Folder then filename Z-A";
        case RawGalleryFilmstripSortMode::TimelineAll:
            return "Timeline: all folders";
        case RawGalleryFilmstripSortMode::TimelineByFolder:
            return "Timeline: separate folders";
    }
    return "Folder then filename A-Z";
}

std::size_t ResolveRawGalleryFilmstripInsertionIndex(
    float pointerX,
    float firstSlotX,
    float slotWidth,
    std::size_t stackCount) {
    if (!std::isfinite(pointerX) || !std::isfinite(firstSlotX) ||
        !std::isfinite(slotWidth) || slotWidth <= 0.0f) {
        return 0u;
    }
    const double rawIndex = std::floor(
        static_cast<double>(pointerX - firstSlotX + slotWidth * 0.5f) /
        static_cast<double>(slotWidth));
    return static_cast<std::size_t>(std::clamp(
        rawIndex,
        0.0,
        static_cast<double>(stackCount)));
}

float ComputeRawGalleryFilmstripAutoScrollVelocity(
    float pointerX,
    float minimumX,
    float maximumX,
    float edgeWidth,
    float maximumPixelsPerSecond) {
    if (!std::isfinite(pointerX) || !std::isfinite(minimumX) ||
        !std::isfinite(maximumX) || !std::isfinite(edgeWidth) ||
        !std::isfinite(maximumPixelsPerSecond) || edgeWidth <= 0.0f ||
        maximumX <= minimumX || maximumPixelsPerSecond <= 0.0f) {
        return 0.0f;
    }
    if (pointerX < minimumX + edgeWidth) {
        const float strength = std::clamp(
            (minimumX + edgeWidth - pointerX) / edgeWidth,
            0.0f,
            1.0f);
        return -maximumPixelsPerSecond * strength;
    }
    if (pointerX > maximumX - edgeWidth) {
        const float strength = std::clamp(
            (pointerX - (maximumX - edgeWidth)) / edgeWidth,
            0.0f,
            1.0f);
        return maximumPixelsPerSecond * strength;
    }
    return 0.0f;
}

float EaseRawGalleryFilmstripDrag(float progress) {
    const float clamped = std::clamp(progress, 0.0f, 1.0f);
    return 1.0f - std::pow(1.0f - clamped, 3.0f);
}

} // namespace Stack::RawWorkspace
