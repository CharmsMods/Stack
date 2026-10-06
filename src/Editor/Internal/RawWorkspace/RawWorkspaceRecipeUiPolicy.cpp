#include "Editor/Internal/RawWorkspace/RawWorkspaceRecipeUiPolicy.h"

#include <algorithm>
#include <cstddef>

namespace Stack::Editor::RawWorkspaceInternal {
namespace {

constexpr std::size_t kMaximumLocalRangePoints = 12;

} // namespace

RawRecipe::RawLocalRangeRecipe BuildLocalRangeUiRecipe(
    const RawRecipe::RawLocalRangeRecipe& localRange) {
    RawRecipe::RawLocalRangeRecipe recipe =
        RawRecipe::SanitizeLocalRangeRecipe(localRange);
    const float minEv = recipe.minEv;
    const float maxEv = recipe.maxEv;
    if (recipe.points.empty()) {
        recipe.points = RawRecipe::DefaultLocalRangePoints(minEv, maxEv);
    }

    std::sort(
        recipe.points.begin(),
        recipe.points.end(),
        [](const auto& a, const auto& b) { return a.ev < b.ev; });
    if (recipe.points.empty() || recipe.points.front().ev > minEv + 0.001f) {
        recipe.points.insert(recipe.points.begin(), { minEv, 0.0f });
    } else {
        recipe.points.front().ev = minEv;
    }
    if (recipe.points.size() == 1 ||
        recipe.points.back().ev < maxEv - 0.001f) {
        recipe.points.push_back({ maxEv, 0.0f });
    } else {
        recipe.points.back().ev = maxEv;
    }
    while (recipe.points.size() > kMaximumLocalRangePoints) {
        recipe.points.erase(recipe.points.end() - 2);
    }
    return RawRecipe::SanitizeLocalRangeRecipe(recipe);
}

} // namespace Stack::Editor::RawWorkspaceInternal
