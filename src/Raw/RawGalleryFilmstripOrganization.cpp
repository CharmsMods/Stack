#include "Raw/RawGallerySimilarity.h"
#include "Raw/GalleryDateLabels.h"

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
    return 0;
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
        auto& prepared = byKey.at(source.sourceKey);
        prepared.fileName = LowerAscii(prepared.fileName);
        prepared.folderKey = LowerAscii(prepared.folderKey);
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
                if ((leftSource == nullptr) != (rightSource == nullptr))
                    return leftSource != nullptr;
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
                        leftSource->fileName, rightSource->fileName);
                    break;
                case RawGalleryFilmstripSortMode::ModifiedNewestFirst:
                case RawGalleryFilmstripSortMode::ModifiedOldestFirst:
                    if ((leftSource->modifiedTimeTicks == 0) !=
                        (rightSource->modifiedTimeTicks == 0))
                        return leftSource->modifiedTimeTicks != 0;
                    comparison = CompareValues(
                        leftSource->modifiedTimeTicks,
                        rightSource->modifiedTimeTicks);
                    break;
                case RawGalleryFilmstripSortMode::TimelineAll:
                case RawGalleryFilmstripSortMode::TimelineOldestFirst: {
                    const auto left=TimelineTime(*leftSource),right=TimelineTime(*rightSource);
                    if((left==0)!=(right==0))return left!=0;
                    comparison=CompareValues(left,right);
                    if(organization.sortMode==RawGalleryFilmstripSortMode::TimelineAll)comparison=-comparison;
                    break;
                }
                case RawGalleryFilmstripSortMode::TimelineByFolder:
                case RawGalleryFilmstripSortMode::TimelineByFolderOldestFirst:
                    comparison = CompareValues(
                        leftSource->folderKey, rightSource->folderKey);
                    if (comparison == 0) {
                        const auto left=TimelineTime(*leftSource),right=TimelineTime(*rightSource);
                        if((left==0)!=(right==0))return left!=0;
                        comparison=CompareValues(left,right);
                        if(organization.sortMode==RawGalleryFilmstripSortMode::TimelineByFolder)comparison=-comparison;
                    }
                    break;
                case RawGalleryFilmstripSortMode::FolderThenNameAscending:
                case RawGalleryFilmstripSortMode::FolderThenNameDescending:
                    comparison = CompareValues(
                        leftSource->folderKey, rightSource->folderKey);
                    if (comparison == 0) {
                        comparison = CompareValues(
                            leftSource->fileName, rightSource->fileName);
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

std::vector<RawGalleryStackSection> BuildRawGalleryStackSections(
    const std::vector<RawGallerySimilarityStack>& stacks,
    RawGalleryFilmstripSortMode mode,
    const std::vector<RawGalleryFilmstripSourceSortInfo>& sources) {
    if (stacks.empty()) return {};
    GalleryCalendarRevision();
    if(mode==RawGalleryFilmstripSortMode::TimelineAll || mode==RawGalleryFilmstripSortMode::TimelineOldestFirst ||
        mode==RawGalleryFilmstripSortMode::ModifiedNewestFirst || mode==RawGalleryFilmstripSortMode::ModifiedOldestFirst) {
        std::unordered_map<std::string,std::int64_t> times;
        const bool modified=mode==RawGalleryFilmstripSortMode::ModifiedNewestFirst || mode==RawGalleryFilmstripSortMode::ModifiedOldestFirst;
        for(const auto& source:sources)times.emplace(source.sourceKey,modified?source.modifiedUnixSeconds:source.captureTimestamp);
        std::vector<RawGalleryStackSection> dates;
        for(std::size_t i=0;i<stacks.size();++i) {
            const auto found=stacks[i].sourceKeys.empty()?times.end():times.find(stacks[i].sourceKeys.front());
            const auto date=FriendlyGalleryDate(found==times.end()?0:found->second);
            if(dates.empty() || dates.back().dateKey!=date.key) {
                RawGalleryStackSection section;section.firstStack=i;section.dateKey=date.key;section.dateLabel=date.label;section.dateDetail=date.exact;
                dates.push_back(std::move(section));
            }
            auto& section=dates.back();
            if(!date.exact.empty()) {
                const auto first=times.find(stacks[section.firstStack].sourceKeys.front());
                const auto firstDate=FriendlyGalleryDate(first==times.end()?0:first->second).exact;
                section.dateDetail=firstDate==date.exact?firstDate:
                    (first!=times.end() && found!=times.end() && first->second>found->second
                        ? date.exact+" - "+firstDate:firstDate+" - "+date.exact);
            }
            ++section.stackCount;
        }
        return dates;
    }
    const bool byFolder = mode == RawGalleryFilmstripSortMode::TimelineByFolder ||
        mode == RawGalleryFilmstripSortMode::TimelineByFolderOldestFirst ||
        mode == RawGalleryFilmstripSortMode::FolderThenNameAscending ||
        mode == RawGalleryFilmstripSortMode::FolderThenNameDescending;
    if (!byFolder) return {{ {}, 0u, stacks.size() }};
    std::vector<RawGalleryStackSection> sections;
    for (std::size_t index = 0; index < stacks.size(); ++index) {
        if (sections.empty() || sections.back().folderKey != stacks[index].folderKey)
            sections.push_back({stacks[index].folderKey, index, 0u, true});
        ++sections.back().stackCount;
    }
    return sections;
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
            return "Date taken";
        case RawGalleryFilmstripSortMode::TimelineByFolder:
            return "Folder";
        case RawGalleryFilmstripSortMode::TimelineOldestFirst:
            return "Date taken (oldest first)";
        case RawGalleryFilmstripSortMode::TimelineByFolderOldestFirst:
            return "Folder (oldest first)";
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
