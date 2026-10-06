#include "Editor/AutoBracket/AutoBracketEditingScope.h"
#include "Utils/UiBusyState.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/RawWorkspace/RawWorkspaceRecipeUiPolicy.h"

#include "App/AppPaths.h"
#include "App/settings/AppearanceTheme.h"
#include "Async/TaskSystem.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Library/LibraryManager.h"
#include "Raw/RawLoader.h"
#include "Restormer/RestormerClient.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/PixelBufferUtils.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>


namespace {

constexpr int kRawToneCurveMaxPoints = 12;
constexpr float kRawToneCurveHitRadius = 14.0f;
constexpr float kRawWorkspaceControlsDefaultWidth = 420.0f;
constexpr float kRawWorkspaceControlsMinWidth = 340.0f;
// The manual RAW surfaces (especially Color Warp) need substantially more
// horizontal working room on wide displays. The adaptive resolver below still
// protects the minimum viewport width on smaller windows.
constexpr float kRawWorkspaceControlsMaxWidth = 1040.0f;
constexpr float kRawWorkspaceSplitterWidth = 6.0f;
constexpr float kRawWorkspaceSplitterSideGap = 4.0f;
constexpr float kRawWorkspacePreviewMinWidth = 320.0f;
constexpr float kRawWorkspaceCropDefaultEpsilon = 0.0001f;

float NormalizeRawWorkspaceControlsPanelWidth(float width) {
    if (!std::isfinite(width) || width <= 0.0f) {
        width = kRawWorkspaceControlsDefaultWidth;
    }
    return std::clamp(width, kRawWorkspaceControlsMinWidth, kRawWorkspaceControlsMaxWidth);
}

float ResolveRawWorkspaceControlsPanelWidth(
    float preferredWidth,
    float availableWidth,
    float reservedWidth,
    float targetPreviewWidth) {
    const float normalized = NormalizeRawWorkspaceControlsPanelWidth(preferredWidth);
    const float adaptiveMax = std::min(
        kRawWorkspaceControlsMaxWidth,
        std::max(
            kRawWorkspaceControlsMinWidth,
            availableWidth - reservedWidth - targetPreviewWidth));
    return std::clamp(normalized, kRawWorkspaceControlsMinWidth, adaptiveMax);
}

bool RenderRawWorkspaceControlsSplitter(const char* id, float height, float* width) {
    ImGui::SameLine(0.0f, kRawWorkspaceSplitterSideGap);
    ImGui::InvisibleButton(
        id,
        ImVec2(kRawWorkspaceSplitterWidth, std::max(1.0f, height)));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    bool changed = false;
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (active && width != nullptr) {
        const float oldWidth = *width;
        *width = NormalizeRawWorkspaceControlsPanelWidth(oldWidth + ImGui::GetIO().MouseDelta.x);
        changed = std::abs(oldWidth - *width) > 0.01f;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float x = (min.x + max.x) * 0.5f;
    const ImU32 color = ImGui::GetColorU32(
        active ? ImGuiCol_ButtonActive : (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Border));
    drawList->AddLine(
        ImVec2(x, min.y + 6.0f),
        ImVec2(x, max.y - 6.0f),
        color,
        active ? 2.0f : 1.0f);
    return changed;
}

struct RawWorkspaceSuggestionBadgeSummary {
    bool known = false;
    int suggestions = 0;
    int warnings = 0;
};

enum class RawWorkspaceSuggestionPopoutKind {
    RawExposure,
    WhiteBalance,
    HighlightProtection,
    LocalRange,
    AppliedOnly
};

struct RawWorkspaceSuggestionPopoutItem {
    RawWorkspaceSuggestionPopoutKind kind = RawWorkspaceSuggestionPopoutKind::RawExposure;
    std::size_t localSuggestionIndex = 0;
    std::string key;
    std::string actionLabel;
    std::string section;
    std::string rationale;
    std::string detail;
    bool applied = false;
};

std::string FormatRawWorkspaceCountBadge(int count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

std::string BuildRawWorkspaceSuggestionButtonLabel(
    const RawWorkspaceSuggestionBadgeSummary& summary) {
    if (!summary.known) {
        return "Suggestions";
    }

    std::string label;
    if (summary.suggestions > 0) {
        label = FormatRawWorkspaceCountBadge(summary.suggestions, "suggestion", "suggestions");
    }
    if (summary.warnings > 0) {
        const std::string warningLabel =
            FormatRawWorkspaceCountBadge(summary.warnings, "warning", "warnings");
        label = label.empty() ? warningLabel : (label + " + " + warningLabel);
    }
    return label.empty() ? "No suggestions" : label;
}

std::string BuildRawWorkspaceOwnerSuggestionMarker(
    const std::vector<RawWorkspaceSuggestionPopoutItem>& items,
    const char* section) {
    const RawWorkspaceSuggestionPopoutItem* appliedItem = nullptr;
    const RawWorkspaceSuggestionPopoutItem* firstSuggestedItem = nullptr;
    int suggestedCount = 0;
    for (const RawWorkspaceSuggestionPopoutItem& item : items) {
        if (item.section != section) {
            continue;
        }
        if (item.applied || item.kind == RawWorkspaceSuggestionPopoutKind::AppliedOnly) {
            if (appliedItem == nullptr) {
                appliedItem = &item;
            }
            continue;
        }
        ++suggestedCount;
        if (firstSuggestedItem == nullptr) {
            firstSuggestedItem = &item;
        }
    }

    if (appliedItem != nullptr) {
        return "Applied: " + appliedItem->actionLabel;
    }
    if (suggestedCount <= 0 || firstSuggestedItem == nullptr) {
        return {};
    }
    if (suggestedCount == 1) {
        return "Manual suggestion: " + firstSuggestedItem->actionLabel;
    }
    return FormatRawWorkspaceCountBadge(suggestedCount, "manual suggestion", "manual suggestions");
}

std::string FormatRawWorkspaceSignedEv(float value) {
    std::ostringstream out;
    out << (value >= 0.0f ? "+" : "") << std::fixed << std::setprecision(2) << value << " EV";
    return out.str();
}

std::string BuildRawWorkspaceDisplayFitStateLabel(
    bool hasUsableStats,
    bool hasAppliedViewFit,
    Stack::EditorModuleTypes::RawAutoValueOwner owner,
    std::uint64_t appliedAnalysisHash,
    std::uint64_t currentAnalysisHash) {
    if (owner == Stack::EditorModuleTypes::RawAutoValueOwner::User) {
        return "Manual/locked";
    }

    if (owner == Stack::EditorModuleTypes::RawAutoValueOwner::AutoBase && hasAppliedViewFit) {
        if (!hasUsableStats) {
            return "Auto recorded";
        }
        if (appliedAnalysisHash != 0 && appliedAnalysisHash == currentAnalysisHash) {
            return "Auto-current";
        }
        return "Needs Refit";
    }

    if (owner == Stack::EditorModuleTypes::RawAutoValueOwner::AutoBase) {
        return "Auto-adjusted";
    }

    return hasUsableStats ? "Ready" : "Preview pending";
}

std::string BuildRawWorkspaceBaseLightSummary(
    float rawExposureEv,
    bool hasUsableStats,
    bool hasAppliedViewFit,
    Stack::EditorModuleTypes::RawAutoValueOwner displayFitOwner,
    std::uint64_t appliedAnalysisHash,
    std::uint64_t currentAnalysisHash) {
    return "RAW Exposure " + FormatRawWorkspaceSignedEv(rawExposureEv) +
        "    Display Fit: " +
        BuildRawWorkspaceDisplayFitStateLabel(
            hasUsableStats,
            hasAppliedViewFit,
            displayFitOwner,
            appliedAnalysisHash,
            currentAnalysisHash);
}

std::string BuildRawWorkspaceWhiteBalanceSummary(
    const Stack::RawRecipe::RawWhiteBalanceRecipe& whiteBalance) {
    std::ostringstream out;
    switch (whiteBalance.mode) {
        case Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers:
            out << "WB: Custom";
            if (whiteBalance.hasMultipliers) {
                out << " RGB "
                    << std::fixed << std::setprecision(2)
                    << whiteBalance.multipliers[0] << "/"
                    << whiteBalance.multipliers[1] << "/"
                    << whiteBalance.multipliers[2];
            }
            return out.str();
        case Stack::RawRecipe::WhiteBalanceMode::AsShot:
        default:
            return "WB: As Shot";
    }
}

const char* RawWorkspaceFinishToneModeLabel(int mode) {
    switch (mode) {
        case 0:
            return "Y";
        case 1:
            return "RGB";
        case 2:
            return "R";
        case 3:
            return "G";
        case 4:
            return "B";
        default:
            return "RGB";
    }
}

const char* RawWorkspaceFinishToneDomainLabel(int domain) {
    switch (domain) {
        case 0:
            return "Scene Linear";
        case 1:
        default:
            return "Log Scene";
    }
}

std::string BuildRawWorkspaceFinishToneSummary(
    int mode,
    int domain,
    std::size_t pointCount) {
    std::ostringstream out;
    out << "Mode: " << RawWorkspaceFinishToneModeLabel(mode)
        << "    Domain: " << RawWorkspaceFinishToneDomainLabel(domain)
        << "    " << pointCount << (pointCount == 1 ? " point" : " points");
    return out.str();
}

std::string BuildRawWorkspaceFinishTonePointValuesSummary(
    const std::vector<Stack::RawRecipe::RawToneCurvePoint>& points) {
    if (points.empty()) {
        return "Curve points: none";
    }

    std::ostringstream out;
    out << "Curve points: ";
    constexpr std::size_t kMaxInlinePoints = 6;
    const std::size_t visibleCount = std::min(points.size(), kMaxInlinePoints);
    for (std::size_t i = 0; i < visibleCount; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << std::fixed << std::setprecision(2)
            << std::clamp(points[i].input, 0.0f, 1.0f)
            << "->"
            << std::clamp(points[i].output, 0.0f, 1.0f);
    }
    if (points.size() > visibleCount) {
        out << ", +" << (points.size() - visibleCount) << " more";
    }
    return out.str();
}

int RawWorkspacePercent(float normalized) {
    return static_cast<int>(
        std::lround(std::clamp(normalized, 0.0f, 1.0f) * 100.0f));
}

int NormalizeRawWorkspaceRotationDegrees(int rotationDegrees) {
    int normalized = rotationDegrees % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return normalized;
}

bool HasRawWorkspaceCropWindowNonDefault(
    const Stack::RawRecipe::RawCropRotationRecipe& cropRotation) {
    return std::abs(cropRotation.cropX) > kRawWorkspaceCropDefaultEpsilon ||
        std::abs(cropRotation.cropY) > kRawWorkspaceCropDefaultEpsilon ||
        std::abs(cropRotation.cropWidth - 1.0f) > kRawWorkspaceCropDefaultEpsilon ||
        std::abs(cropRotation.cropHeight - 1.0f) > kRawWorkspaceCropDefaultEpsilon;
}

bool IsRawWorkspaceCropRotationActive(
    const Stack::RawRecipe::RawCropRotationRecipe& cropRotation) {
    return cropRotation.cropEnabled ||
        HasRawWorkspaceCropWindowNonDefault(cropRotation) ||
        NormalizeRawWorkspaceRotationDegrees(cropRotation.rotationDegrees) != 0 ||
        cropRotation.flipHorizontally ||
        cropRotation.flipVertically;
}

std::string BuildRawWorkspaceCropRotationSummary(
    const Stack::RawRecipe::RawCropRotationRecipe& cropRotation) {
    std::ostringstream out;
    const bool cropWindowNonDefault = HasRawWorkspaceCropWindowNonDefault(cropRotation);
    if (cropRotation.cropEnabled) {
        out << "Crop "
            << RawWorkspacePercent(cropRotation.cropWidth)
            << "% x "
            << RawWorkspacePercent(cropRotation.cropHeight)
            << "%";
    } else {
        out << "Crop off";
        if (cropWindowNonDefault) {
            out << " (saved "
                << RawWorkspacePercent(cropRotation.cropWidth)
                << "% x "
                << RawWorkspacePercent(cropRotation.cropHeight)
                << "%)";
        }
    }

    const int rotationDegrees =
        NormalizeRawWorkspaceRotationDegrees(cropRotation.rotationDegrees);
    if (rotationDegrees != 0) {
        out << ", rotate " << rotationDegrees;
    }
    if (cropRotation.flipHorizontally) {
        out << ", flip horizontal";
    }
    if (cropRotation.flipVertically) {
        out << ", flip vertical";
    }
    return out.str();
}

const char* RawWorkspaceWhiteBalanceMethodLabel(
    Stack::RawAutoBase::WhiteBalanceRecommendation::Method method) {
    switch (method) {
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::CameraAsShot:
            return "Camera/as-shot";
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld:
            return "Gray World";
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::ShadesOfGray:
            return "Shades of Gray";
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::GreyEdge:
            return "Grey Edge";
        default:
            return "Unknown";
    }
}

std::uint64_t MixRawWorkspaceSuggestionHash(std::uint64_t seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

std::uint64_t HashRawWorkspaceSuggestionFloat(float value) {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }
    return static_cast<std::uint64_t>(std::hash<float>{}(value));
}

std::uint64_t BuildRawWorkspaceSuggestionAnalysisHash(
    const Stack::RawAnalysis::RawImageAnalysis& analysis) {
    const Stack::RawAnalysis::PercentileStats& stats = analysis.currentFrameStats;
    std::uint64_t seed = static_cast<std::uint64_t>(std::hash<std::string>{}(analysis.sourceKey));
    seed = MixRawWorkspaceSuggestionHash(seed, HashRawWorkspaceSuggestionFloat(stats.p01Ev));
    seed = MixRawWorkspaceSuggestionHash(seed, HashRawWorkspaceSuggestionFloat(stats.p05Ev));
    seed = MixRawWorkspaceSuggestionHash(seed, HashRawWorkspaceSuggestionFloat(stats.p50Ev));
    seed = MixRawWorkspaceSuggestionHash(seed, HashRawWorkspaceSuggestionFloat(stats.p99Ev));
    seed = MixRawWorkspaceSuggestionHash(seed, HashRawWorkspaceSuggestionFloat(stats.p999Ev));
    seed = MixRawWorkspaceSuggestionHash(seed, HashRawWorkspaceSuggestionFloat(stats.dynamicRangeEv));
    seed = MixRawWorkspaceSuggestionHash(
        seed,
        HashRawWorkspaceSuggestionFloat(analysis.highlight.displayClipPercent));
    seed = MixRawWorkspaceSuggestionHash(
        seed,
        HashRawWorkspaceSuggestionFloat(analysis.highlight.hdrPixelPercent));
    return seed;
}

std::string ShortRawWorkspaceSuggestionRationale(const std::string& rationale) {
    if (rationale.empty()) {
        return {};
    }

    std::size_t end = rationale.find('.');
    if (end == std::string::npos || end > 150) {
        end = std::min<std::size_t>(rationale.size(), 150);
    } else {
        ++end;
    }

    std::string result = rationale.substr(0, end);
    while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) {
        result.pop_back();
    }
    if (end < rationale.size() && !result.empty() && result.back() != '.') {
        result += "...";
    }
    return result;
}

std::vector<RawWorkspaceSuggestionPopoutItem> BuildRawWorkspaceSuggestionPopoutItems(
    const Stack::EditorModuleTypes::RawWorkspaceAutoBaseUiState& autoBaseUi,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const std::string& sourceKey) {
    std::vector<RawWorkspaceSuggestionPopoutItem> items;
    if (sourceKey.empty()) {
        return items;
    }

    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations =
        autoBaseUi.recommendations;
    const Stack::RawAutoBase::RawExposureRecommendation& exposure =
        recommendations.exposure;
    if (exposure.valid &&
        exposure.action == Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue &&
        !exposure.blockedByHighlightRisk) {
        RawWorkspaceSuggestionPopoutItem item;
        item.kind = RawWorkspaceSuggestionPopoutKind::RawExposure;
        item.key = "raw-exposure:" + FormatRawWorkspaceSignedEv(exposure.deltaEv);
        item.actionLabel =
            (exposure.deltaEv >= 0.0f ? "Raise RAW Exposure " : "Lower RAW Exposure ") +
            FormatRawWorkspaceSignedEv(exposure.deltaEv);
        item.section = "Base Light";
        item.rationale = ShortRawWorkspaceSuggestionRationale(exposure.rationale);
        item.detail =
            "Confidence " +
            std::to_string(static_cast<int>(std::round(
                std::clamp(exposure.confidence, 0.0f, 1.0f) * 100.0f))) +
            "%";
        items.push_back(std::move(item));
    }

    const Stack::RawAutoBase::WhiteBalanceRecommendation& whiteBalance =
        recommendations.whiteBalance;
    if (whiteBalance.alternateCandidateAvailable &&
        whiteBalance.action == Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue &&
        !whiteBalance.manualWhiteBalanceProtected) {
        RawWorkspaceSuggestionPopoutItem item;
        item.kind = RawWorkspaceSuggestionPopoutKind::WhiteBalance;
        item.key = std::string("white-balance:") + RawWorkspaceWhiteBalanceMethodLabel(whiteBalance.method);
        item.actionLabel =
            std::string("Suggested WB: ") + RawWorkspaceWhiteBalanceMethodLabel(whiteBalance.method);
        item.section = "White Balance";
        item.rationale = ShortRawWorkspaceSuggestionRationale(whiteBalance.rationale);
        item.detail =
            "Residual " +
            std::to_string(static_cast<int>(std::round(whiteBalance.neutralResidualBefore * 100.0f))) +
            "% -> " +
            std::to_string(static_cast<int>(std::round(whiteBalance.neutralResidualAfter * 100.0f))) +
            "%";
        items.push_back(std::move(item));
    }

    const Stack::RawAutoBase::HighlightRecommendation& highlight =
        recommendations.highlight;
    if (highlight.recommendProtectiveViewShoulder &&
        highlight.protectionAction == Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue) {
        RawWorkspaceSuggestionPopoutItem item;
        item.kind = RawWorkspaceSuggestionPopoutKind::HighlightProtection;
        item.key = "highlight-protection";
        item.actionLabel = "Protect highlights";
        item.section = "Display Fit";
        item.rationale = ShortRawWorkspaceSuggestionRationale(highlight.rationale);
        item.detail = "Adjusts View Transform shoulder/white EV";
        items.push_back(std::move(item));
    }

    for (std::size_t i = 0; i < recommendations.localAdjustments.size(); ++i) {
        const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion =
            recommendations.localAdjustments[i];
        if (!suggestion.valid) {
            continue;
        }

        RawWorkspaceSuggestionPopoutItem item;
        item.kind = RawWorkspaceSuggestionPopoutKind::LocalRange;
        item.localSuggestionIndex = i;
        item.key = "local-range:" + std::to_string(i);
        const std::string label = suggestion.label.empty()
            ? std::string(Stack::RawAutoBase::SuggestedLocalAdjustmentKindLabel(suggestion.kind))
            : suggestion.label;
        item.actionLabel = label + " " + FormatRawWorkspaceSignedEv(suggestion.deltaEv);
        if (suggestion.colorQualifierEnabled) {
            item.actionLabel += " color";
        }
        item.section = "Local Range";
        item.rationale = ShortRawWorkspaceSuggestionRationale(suggestion.rationale);
        item.detail =
            "Area " +
            std::to_string(static_cast<int>(std::round(std::max(0.0f, suggestion.affectedAreaPercent)))) +
            "%";
        items.push_back(std::move(item));
    }

    const std::uint64_t currentAnalysisHash = BuildRawWorkspaceSuggestionAnalysisHash(analysis);
    const bool appliedStillCurrent =
        !autoBaseUi.appliedSuggestionKey.empty() &&
        autoBaseUi.appliedSuggestionSourceHash == autoBaseUi.sourceHash &&
        autoBaseUi.appliedSuggestionAnalysisHash == currentAnalysisHash;
    if (appliedStillCurrent) {
        bool matchedApplyableItem = false;
        for (RawWorkspaceSuggestionPopoutItem& item : items) {
            if (item.key == autoBaseUi.appliedSuggestionKey) {
                item.applied = true;
                matchedApplyableItem = true;
                break;
            }
        }
        if (!matchedApplyableItem && !autoBaseUi.appliedSuggestionLabel.empty()) {
            RawWorkspaceSuggestionPopoutItem item;
            item.kind = RawWorkspaceSuggestionPopoutKind::AppliedOnly;
            item.key = autoBaseUi.appliedSuggestionKey;
            item.actionLabel = autoBaseUi.appliedSuggestionLabel;
            item.section = autoBaseUi.appliedSuggestionSection.empty()
                ? "Applied"
                : autoBaseUi.appliedSuggestionSection;
            item.applied = true;
            items.insert(items.begin(), std::move(item));
        }
    }

    return items;
}

const char* RawWorkspaceProjectBadgeLabel(
    Stack::RawWorkspace::ProjectStatus status,
    bool editCreatesProject) {
    switch (status) {
        case Stack::RawWorkspace::ProjectStatus::NoProject:
            return editCreatesProject ? "New project" : "Not edited";
        case Stack::RawWorkspace::ProjectStatus::Existing:
            return "Existing project";
        case Stack::RawWorkspace::ProjectStatus::Embedded:
            return "Embedded";
        case Stack::RawWorkspace::ProjectStatus::MissingSource:
            return "Source missing";
        case Stack::RawWorkspace::ProjectStatus::Conflict:
            return "Conflict";
        case Stack::RawWorkspace::ProjectStatus::Invalid:
            return "Invalid";
        case Stack::RawWorkspace::ProjectStatus::Unknown:
        default:
            return "Unknown project";
    }
}

const char* RawWorkspaceModeBadgeLabel(Stack::RawWorkspace::RawProjectMode mode) {
    switch (mode) {
        case Stack::RawWorkspace::RawProjectMode::UnifiedLayers:
            return "Layer graphs";
        case Stack::RawWorkspace::RawProjectMode::ManagedDecomposed:
            return "Managed graph";
        case Stack::RawWorkspace::RawProjectMode::CustomGraph:
            return "Custom graph";
        case Stack::RawWorkspace::RawProjectMode::Unknown:
        default:
            return "Graph unknown";
    }
}

bool HasRawWorkspaceNoiseDetailAdvisory(
    const Stack::RawAutoBase::NoiseDetailRecommendation& noiseDetail) {
    return noiseDetail.valid &&
        (noiseDetail.suggestChromaDenoise ||
         noiseDetail.suggestLumaDenoise ||
         noiseDetail.suggestReduceSharpening ||
         noiseDetail.shadowLiftEv >= 0.5f);
}

std::string BuildRawWorkspaceNoiseDetailAdvisorySummary(
    const Stack::RawAutoBase::NoiseDetailRecommendation& noiseDetail) {
    std::ostringstream out;
    out << "Advisory: ";
    if (noiseDetail.suggestChromaDenoise && noiseDetail.suggestLumaDenoise) {
        out << "denoise may help";
    } else if (noiseDetail.suggestChromaDenoise) {
        out << "chroma noise may need attention";
    } else if (noiseDetail.suggestLumaDenoise) {
        out << "luma noise may need attention";
    } else if (noiseDetail.suggestReduceSharpening) {
        out << "sharpening may need restraint";
    } else {
        out << "watch shadow-lift noise";
    }

    if (noiseDetail.suggestReduceSharpening &&
        (noiseDetail.suggestChromaDenoise || noiseDetail.suggestLumaDenoise)) {
        out << "; reduce sharpening";
    }

    out << ". ISO " << std::fixed << std::setprecision(0) << noiseDetail.iso
        << ", effective noise "
        << std::setprecision(0)
        << std::clamp(noiseDetail.effectiveNoiseScore, 0.0f, 1.0f) * 100.0f
        << "%";
    if (noiseDetail.shadowLiftEv >= 0.05f) {
        out << ", shadow lift +" << std::setprecision(2) << noiseDetail.shadowLiftEv << " EV";
    }
    out << ".";
    return out.str();
}

RawWorkspaceSuggestionBadgeSummary BuildRawWorkspaceSuggestionBadgeSummary(
    const Stack::EditorModuleTypes::RawWorkspaceAutoBaseUiState& autoBaseUi,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const std::string& sourceKey) {
    RawWorkspaceSuggestionBadgeSummary summary;
    summary.known =
        autoBaseUi.sourceKey == sourceKey ||
        (analysis.sourceKey == sourceKey && analysis.valid);
    if (!summary.known) {
        return summary;
    }

    const std::vector<RawWorkspaceSuggestionPopoutItem> suggestionItems =
        BuildRawWorkspaceSuggestionPopoutItems(autoBaseUi, analysis, sourceKey);
    for (const RawWorkspaceSuggestionPopoutItem& item : suggestionItems) {
        if (item.kind != RawWorkspaceSuggestionPopoutKind::AppliedOnly) {
            ++summary.suggestions;
        }
    }

    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations =
        autoBaseUi.recommendations;
    const Stack::RawAutoBase::RawExposureRecommendation& exposure =
        recommendations.exposure;
    if (exposure.blockedByHighlightRisk) {
        ++summary.warnings;
    }

    const Stack::RawAutoBase::HighlightRecommendation& highlight =
        recommendations.highlight;
    if (highlight.recommendNoPositiveRawExposure ||
        highlight.recommendReconstruction ||
        highlight.recommendAchromaticClip) {
        ++summary.warnings;
    }

    if (HasRawWorkspaceNoiseDetailAdvisory(recommendations.noiseDetail)) {
        ++summary.warnings;
    }

    return summary;
}

bool RenderRawWorkspaceBadge(const char* label, bool warning = false) {
    const ImVec4 base = ImGui::GetStyleColorVec4(warning ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg);
    const ImVec4 hover = ImGui::GetStyleColorVec4(warning ? ImGuiCol_ButtonHovered : ImGuiCol_FrameBgHovered);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, base);
    const bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return pressed;
}

void RenderRawWorkspaceBadgeLine(
    const std::vector<std::string>& labels,
    const std::vector<bool>& warnings = {}) {
    const float lineStart = ImGui::GetCursorPosX();
    const float lineLimit = lineStart + ImGui::GetContentRegionAvail().x;
    const ImVec2 padding(7.0f, 2.0f);
    bool firstOnLine = true;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const std::string& label = labels[index];
        if (label.empty()) {
            continue;
        }
        const float width = ImGui::CalcTextSize(label.c_str()).x + padding.x * 2.0f;
        if (!firstOnLine) {
            if (ImGui::GetCursorPosX() + width > lineLimit) {
                firstOnLine = true;
            } else {
                ImGui::SameLine(0.0f, 5.0f);
            }
        }
        ImGui::PushID(static_cast<int>(index));
        RenderRawWorkspaceBadge(
            label.c_str(),
            index < warnings.size() ? warnings[index] : false);
        ImGui::PopID();
        firstOnLine = false;
    }
}

std::string FormatFileSize(std::uintmax_t bytes) {
    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;
    char buffer[64] = {};
    if (bytes >= static_cast<std::uintmax_t>(kGiB)) {
        snprintf(buffer, sizeof(buffer), "%.1f GB", static_cast<double>(bytes) / kGiB);
    } else if (bytes >= static_cast<std::uintmax_t>(kMiB)) {
        snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / kMiB);
    } else if (bytes >= static_cast<std::uintmax_t>(kKiB)) {
        snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / kKiB);
    } else {
        snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return std::string(buffer);
}

std::string GroupLabel(const std::string& parentFolderKey) {
    return parentFolderKey.empty() ? std::string("Workspace root") : parentFolderKey;
}

std::string EllipsizeTextToWidth(const std::string& text, float maxWidth) {
    if (text.empty() || ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }

    std::string result = text;
    const char* suffix = "...";
    while (result.size() > 4) {
        result.pop_back();
        std::string candidate = result + suffix;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            return candidate;
        }
    }
    return suffix;
}

ImVec2 FitImageSize(float sourceWidth, float sourceHeight, const ImVec2& bounds) {
    if (sourceWidth <= 0.0f || sourceHeight <= 0.0f || bounds.x <= 0.0f || bounds.y <= 0.0f) {
        return ImVec2(std::max(1.0f, bounds.x), std::max(1.0f, bounds.y));
    }

    const float scale = std::min(bounds.x / sourceWidth, bounds.y / sourceHeight);
    return ImVec2(std::max(1.0f, sourceWidth * scale), std::max(1.0f, sourceHeight * scale));
}

void DrawRawPlaceholder(ImDrawList* drawList, const ImRect& rect, bool selected, const char* label = "RAW") {
    const ImU32 fill = ImGui::GetColorU32(selected ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg);
    const ImU32 border = ImGui::GetColorU32(selected ? ImGuiCol_CheckMark : ImGuiCol_Border);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    drawList->AddRectFilled(rect.Min, rect.Max, fill, 4.0f);
    drawList->AddRect(rect.Min, rect.Max, border, 4.0f, 0, selected ? 2.0f : 1.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(
        ImVec2(
            rect.Min.x + (rect.GetWidth() - textSize.x) * 0.5f,
            rect.Min.y + (rect.GetHeight() - textSize.y) * 0.5f),
        text,
        label);
}

const Stack::RawWorkspace::SourceRecord* FindRawWorkspaceSourceByIndex(
    const Stack::RawWorkspace::WorkspaceState& state,
    std::size_t sourceIndex) {
    return sourceIndex < state.sources.size() ? &state.sources[sourceIndex] : nullptr;
}

void TooltipIfHovered(const char* text, ImGuiHoveredFlags flags = 0) {
    if (text != nullptr && text[0] != '\0' && ImGui::IsItemHovered(flags)) {
        ImGui::SetTooltip("%s", text);
    }
}

bool RenderRawWorkspaceIconButton(
    const char* id,
    unsigned int texture,
    const char* tooltip,
    const StackAppearance::AppearanceManager* appearance,
    bool selected = false) {
    constexpr float buttonSize = 22.0f;
    constexpr float iconSize = 18.0f;
    const ImVec4 transparent(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, transparent);
    ImGui::PushStyleColor(ImGuiCol_Border, transparent);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    const bool clicked = ImGui::Button(id, ImVec2(buttonSize, buttonSize));
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    if (texture != 0) {
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const ImVec2 iconMin(
            itemMin.x + (itemMax.x - itemMin.x - iconSize) * 0.5f,
            itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f);
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        const ImU32 tint = StackAppearance::ResolveThemedMonochromeIconTint(
            appearance,
            selected || ImGui::IsItemActive(),
            ImGui::IsItemHovered());
        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)(intptr_t)texture,
            iconMin,
            iconMax,
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            tint);
    }
    TooltipIfHovered(tooltip);
    return clicked;
}

bool RenderRawWorkspaceOwnerSuggestionMarker(
    const char* id,
    const std::string& marker,
    float controlWidth) {
    ImGui::PushID(id);
    bool openSuggestions = false;
    // Dummy() adds ItemSpacing itself, so use the frame height rather than
    // GetFrameHeightWithSpacing(). Both marker states must advance identically.
    const float rowHeight = ImGui::GetFrameHeight();
    const float viewWidth =
        ImGui::CalcTextSize("View").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float gap = 6.0f;
    if (marker.empty()) {
        ImGui::Dummy(ImVec2(0.0f, rowHeight));
        ImGui::PopID();
        return false;
    }

    const float textWidth = std::max(32.0f, controlWidth - viewWidth - gap);
    const std::string clippedMarker = EllipsizeTextToWidth(marker, textWidth);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", clippedMarker.c_str());
    ImGui::SameLine(0.0f, gap);
    if (ImGui::SmallButton("View")) {
        openSuggestions = true;
    }
    TooltipIfHovered("Open the suggestions panel. This marker does not change the recipe.");
    ImGui::PopID();
    return openSuggestions;
}

std::string TrimRawWorkspaceText(std::string text) {
    const auto isSpace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };
    text.erase(
        text.begin(),
        std::find_if_not(text.begin(), text.end(), isSpace));
    text.erase(
        std::find_if_not(text.rbegin(), text.rend(), isSpace).base(),
        text.end());
    return text;
}

std::string ExtractStartingPointControlStatus(
    const std::string& statusSummary,
    const char* statusLabel) {
    if (statusLabel == nullptr || statusLabel[0] == '\0') {
        return {};
    }

    const std::string prefix = std::string(statusLabel) + ":";
    std::size_t start = 0;
    while (start <= statusSummary.size()) {
        const std::size_t end = statusSummary.find(';', start);
        const std::string segment = TrimRawWorkspaceText(
            statusSummary.substr(
                start,
                end == std::string::npos ? std::string::npos : end - start));
        if (segment.rfind(prefix, 0) == 0) {
            return segment;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
}

std::string BuildStartingPointManualControlReadout(
    const Stack::EditorModuleTypes::RawWorkspaceAutoBaseUiState& ui,
    const std::string& sourceKey,
    const char* statusLabel,
    const char* searchLabel,
    const char* alternateSearchLabel = nullptr) {
    if (sourceKey.empty() ||
        ui.sourceKey != sourceKey ||
        Stack::EditorModuleTypes::IsRawStartingPointSummaryEmptyOrNone(
            ui.startingPointControlStatusSummary)) {
        return {};
    }

    const std::string status = ExtractStartingPointControlStatus(
        ui.startingPointControlStatusSummary,
        statusLabel);
    if (status.empty()) {
        return {};
    }

    std::string detail =
        Stack::EditorModuleTypes::RawStartingPointRelevantControlValueText(
            ui.startingPointAppliedValuesSummary,
            searchLabel,
            alternateSearchLabel);
    if (detail.empty()) {
        detail =
            Stack::EditorModuleTypes::RawStartingPointRelevantControlText(
                ui.startingPointResultSummary,
                ui.startingPointWithheldControlsSummary,
                searchLabel,
                alternateSearchLabel);
    }

    std::string readout = "Build Starting Point: " + status;
    if (!detail.empty() && detail != status) {
        readout += " - " + detail;
    }
    return readout;
}

void RenderStartingPointManualControlReadout(
    const Stack::EditorModuleTypes::RawWorkspaceAutoBaseUiState& ui,
    const std::string& sourceKey,
    const char* statusLabel,
    const char* searchLabel,
    float controlWidth,
    const char* alternateSearchLabel = nullptr) {
    const std::string readout = BuildStartingPointManualControlReadout(
        ui,
        sourceKey,
        statusLabel,
        searchLabel,
        alternateSearchLabel);
    if (readout.empty()) {
        // Keep downstream sliders and graphs fixed while render-driven status
        // evidence appears or disappears.
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
        return;
    }
    ImGui::TextDisabled(
        "%s",
        EllipsizeTextToWidth(readout, std::max(32.0f, controlWidth)).c_str());
    TooltipIfHovered(readout.c_str());
}

int LocalRangeOverlayModeToIndex(const std::string& mode) {
    if (mode == "affected-tones") {
        return 1;
    }
    if (mode == "delta-map") {
        return 2;
    }
    if (mode == "region-mask") {
        return 3;
    }
    return 0;
}

const char* LocalRangeOverlayModeFromIndex(int index) {
    switch (std::clamp(index, 0, 3)) {
        case 1: return "affected-tones";
        case 2: return "delta-map";
        case 3: return "region-mask";
        case 0:
        default:
            return "none";
    }
}

const char* LocalRangeOverlayModeLabel(const std::string& mode) {
    if (mode == "affected-tones") {
        return "Affected";
    }
    if (mode == "delta-map") {
        return "Delta";
    }
    if (mode == "region-mask") {
        return "Mask";
    }
    if (mode == "target-outline") {
        return "Target Outline";
    }
    return "Final";
}

int LocalRangeRegionMaskModeToIndex(const std::string& mode) {
    if (mode == "radial-gradient") {
        return 1;
    }
    if (mode == "luminance-range") {
        return 2;
    }
    return 0;
}

const char* LocalRangeRegionMaskModeFromIndex(int index) {
    switch (std::clamp(index, 0, 2)) {
        case 1: return "radial-gradient";
        case 2: return "luminance-range";
        case 0:
        default:
            return "linear-gradient";
    }
}

std::vector<Stack::RawRecipe::RawToneCurvePoint> BuildToneCurveUiPoints(
    const Stack::RawRecipe::RawToneCurveRecipe& toneCurve);

float JsonNumber(const nlohmann::json& value, const char* key, float fallback) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number()) {
        return fallback;
    }
    return it->get<float>();
}

int JsonInt(const nlohmann::json& value, const char* key, int fallback) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number_integer()) {
        return fallback;
    }
    return it->get<int>();
}

bool JsonBool(const nlohmann::json& value, const char* key, bool fallback) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

std::string FormatRawWorkspaceSignedNumber(float value, int precision) {
    std::ostringstream out;
    out << (value >= 0.0f ? "+" : "")
        << std::fixed << std::setprecision(precision)
        << value;
    return out.str();
}

std::string FormatRawWorkspaceNumber(float value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string BuildRawWorkspaceDisplayFitValuesSummary(
    const nlohmann::json& viewTransform,
    bool detailed) {
    const float viewExposure = JsonNumber(viewTransform, "exposure", 0.0f);
    const float blackEv = JsonNumber(viewTransform, "blackEv", -8.0f);
    const float whiteEv = JsonNumber(viewTransform, "whiteEv", 4.0f);
    const float middleGrey = JsonNumber(viewTransform, "middleGrey", 0.18f);
    const float shoulder = JsonNumber(viewTransform, "shoulder", 0.45f);
    const float toe = JsonNumber(viewTransform, "toe", 0.18f);
    const float contrast = JsonNumber(viewTransform, "contrast", 1.0f);
    const float contrastPivotEv = JsonNumber(viewTransform, "contrastPivotEv", 0.0f);
    const float saturation = JsonNumber(viewTransform, "saturation", 1.0f);

    std::ostringstream out;
    out << "Display Fit values: Exp "
        << FormatRawWorkspaceSignedNumber(viewExposure, 2)
        << ", Black " << FormatRawWorkspaceSignedNumber(blackEv, 2)
        << ", White " << FormatRawWorkspaceSignedNumber(whiteEv, 2)
        << ", Grey " << FormatRawWorkspaceNumber(middleGrey, 3);
    if (detailed) {
        out << ", Shoulder " << FormatRawWorkspaceNumber(shoulder, 2)
            << ", Toe " << FormatRawWorkspaceNumber(toe, 2)
            << ", Contrast " << FormatRawWorkspaceNumber(contrast, 2)
            << " @ " << FormatRawWorkspaceSignedNumber(contrastPivotEv, 2)
            << " EV"
            << ", Saturation " << FormatRawWorkspaceNumber(saturation, 2);
    }
    return out.str();
}

void EnsureFinishToneJson(nlohmann::json& finishTone) {
    if (!finishTone.is_object()) {
        finishTone = Stack::RawRecipe::DefaultFinishToneJson();
    }
    finishTone["type"] = "ToneCurve";
    if (!finishTone.contains("points") || !finishTone["points"].is_array()) {
        finishTone["points"] = Stack::RawRecipe::DefaultFinishToneJson()["points"];
    }
    if (!finishTone.contains("preparedPoints") || !finishTone["preparedPoints"].is_array()) {
        finishTone["preparedPoints"] = finishTone["points"];
    }
}

void EnsureViewTransformJson(nlohmann::json& viewTransform) {
    if (!viewTransform.is_object()) {
        viewTransform = Stack::RawRecipe::DefaultViewTransformJson();
    }
    viewTransform["type"] = "ViewTransform";
}

std::vector<Stack::RawRecipe::RawToneCurvePoint> BuildFinishToneUiPoints(const nlohmann::json& finishTone) {
    Stack::RawRecipe::RawToneCurveRecipe curve;
    curve.mode = Stack::RawRecipe::ToneCurveMode::Custom;
    const nlohmann::json pointsJson = finishTone.value("points", nlohmann::json::array());
    if (pointsJson.is_array()) {
        for (const nlohmann::json& item : pointsJson) {
            if (!item.is_object()) {
                continue;
            }
            curve.points.push_back({
                JsonNumber(item, "x", 0.0f),
                JsonNumber(item, "y", 0.0f)
            });
        }
    }
    return BuildToneCurveUiPoints(curve);
}

void StoreFinishToneUiPoints(nlohmann::json& finishTone, const std::vector<Stack::RawRecipe::RawToneCurvePoint>& points) {
    nlohmann::json serialized = nlohmann::json::array();
    for (const Stack::RawRecipe::RawToneCurvePoint& point : BuildToneCurveUiPoints({ Stack::RawRecipe::ToneCurveMode::Custom, points })) {
        serialized.push_back({
            { "x", point.input },
            { "y", point.output },
            { "shape", 1 }
        });
    }
    finishTone["points"] = serialized;
    finishTone["preparedPoints"] = std::move(serialized);
    finishTone["activeGraphView"] = 0;
}

std::vector<Stack::RawRecipe::RawToneCurvePoint> BuildToneCurveUiPoints(
    const Stack::RawRecipe::RawToneCurveRecipe& toneCurve) {
    std::vector<Stack::RawRecipe::RawToneCurvePoint> points = toneCurve.points;
    if (points.size() < 2) {
        points = {
            { 0.0f, 0.0f },
            { 1.0f, 1.0f }
        };
    }

    std::vector<Stack::RawRecipe::RawToneCurvePoint> finitePoints;
    finitePoints.reserve(points.size());
    for (Stack::RawRecipe::RawToneCurvePoint& point : points) {
        if (!std::isfinite(point.input) || !std::isfinite(point.output)) {
            continue;
        }
        finitePoints.push_back({
            std::clamp(point.input, 0.0f, 1.0f),
            std::clamp(point.output, 0.0f, 1.0f)
        });
    }
    points = std::move(finitePoints);
    if (points.size() < 2) {
        points = {
            { 0.0f, 0.0f },
            { 1.0f, 1.0f }
        };
    }
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.input < b.input;
    });

    std::vector<Stack::RawRecipe::RawToneCurvePoint> uniquePoints;
    uniquePoints.reserve(points.size());
    for (const Stack::RawRecipe::RawToneCurvePoint& point : points) {
        if (!uniquePoints.empty() &&
            std::abs(uniquePoints.back().input - point.input) < 0.0001f) {
            uniquePoints.back() = point;
            continue;
        }
        uniquePoints.push_back(point);
    }
    points = std::move(uniquePoints);
    if (points.size() < 2) {
        points = {
            { 0.0f, 0.0f },
            { 1.0f, 1.0f }
        };
    }
    points.front() = { 0.0f, 0.0f };
    points.back() = { 1.0f, 1.0f };
    while (points.size() > static_cast<std::size_t>(kRawToneCurveMaxPoints)) {
        points.erase(points.end() - 2);
    }
    return points;
}

bool DrawToneCurveWidget(std::vector<Stack::RawRecipe::RawToneCurvePoint>& points,
    const ImVec2& size, Stack::Editor::PointCurveInteractionState& interaction) {
    auto& selectedPoint = interaction.selectedPoint;
    auto& draggingPoint = interaction.draggingPoint;
    auto& contextPoint = interaction.contextPoint;

    bool changed = false;
    points = BuildToneCurveUiPoints({ Stack::RawRecipe::ToneCurveMode::Custom, points });
    if (selectedPoint >= static_cast<int>(points.size())) {
        selectedPoint = -1;
    }
    if (draggingPoint >= static_cast<int>(points.size())) {
        draggingPoint = -1;
    }

    ImGui::InvisibleButton("##RawToneCurveGraph", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 grid = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.28f);
    const ImU32 curve = ImGui::GetColorU32(ImGuiCol_CheckMark);
    drawList->AddRectFilled(min, max, bg, 5.0f);
    drawList->AddRect(min, max, border, 5.0f);

    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float x = min.x + (max.x - min.x) * t;
        const float y = min.y + (max.y - min.y) * t;
        drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), grid, 1.0f);
        drawList->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), grid, 1.0f);
    }

    auto toScreen = [&](const Stack::RawRecipe::RawToneCurvePoint& point) {
        return ImVec2(
            min.x + (max.x - min.x) * std::clamp(point.input, 0.0f, 1.0f),
            max.y - (max.y - min.y) * std::clamp(point.output, 0.0f, 1.0f));
    };
    auto fromScreen = [&](const ImVec2& screen) {
        return Stack::RawRecipe::RawToneCurvePoint{
            std::clamp((screen.x - min.x) / std::max(1.0f, max.x - min.x), 0.0f, 1.0f),
            std::clamp((max.y - screen.y) / std::max(1.0f, max.y - min.y), 0.0f, 1.0f)
        };
    };
    auto isEndpoint = [&](int index) {
        return index == 0 || index == static_cast<int>(points.size()) - 1;
    };
    auto findPointNearMouse = [&]() {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int bestIndex = -1;
        float bestDist2 = kRawToneCurveHitRadius * kRawToneCurveHitRadius;
        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            const ImVec2 point = toScreen(points[static_cast<std::size_t>(i)]);
            const float dx = point.x - mouse.x;
            const float dy = point.y - mouse.y;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 <= bestDist2) {
                bestDist2 = dist2;
                bestIndex = i;
            }
        }
        return bestIndex;
    };
    auto sortAndFind = [&](float input, float output) {
        points = BuildToneCurveUiPoints({ Stack::RawRecipe::ToneCurveMode::Custom, points });
        int bestIndex = -1;
        float bestDist = 10.0f;
        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            const float dist =
                std::abs(points[static_cast<std::size_t>(i)].input - input) +
                std::abs(points[static_cast<std::size_t>(i)].output - output);
            if (dist < bestDist) {
                bestDist = dist;
                bestIndex = i;
            }
        }
        return bestIndex;
    };

    const int hoveredPoint = findPointNearMouse();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredPoint >= 0) {
            selectedPoint = hoveredPoint;
            draggingPoint = isEndpoint(hoveredPoint) ? -1 : hoveredPoint;
        } else if (points.size() < static_cast<std::size_t>(kRawToneCurveMaxPoints)) {
            const Stack::RawRecipe::RawToneCurvePoint point = fromScreen(ImGui::GetIO().MousePos);
            points.push_back(point);
            selectedPoint = sortAndFind(point.input, point.output);
            draggingPoint = selectedPoint;
            changed = true;
        }
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        draggingPoint > 0 &&
        draggingPoint < static_cast<int>(points.size()) - 1) {
        Stack::RawRecipe::RawToneCurvePoint point = fromScreen(ImGui::GetIO().MousePos);
        const float left = points[static_cast<std::size_t>(draggingPoint - 1)].input + 0.001f;
        const float right = points[static_cast<std::size_t>(draggingPoint + 1)].input - 0.001f;
        point.input = std::clamp(point.input, left, right);
        points[static_cast<std::size_t>(draggingPoint)] = point;
        selectedPoint = draggingPoint;
        changed = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggingPoint = -1;
    }
    if ((hovered || active) &&
        selectedPoint > 0 &&
        selectedPoint < static_cast<int>(points.size()) - 1 &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        points.erase(points.begin() + selectedPoint);
        selectedPoint = -1;
        draggingPoint = -1;
        changed = true;
    }
    if (hovered && hoveredPoint > 0 &&
        hoveredPoint < static_cast<int>(points.size()) - 1 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        selectedPoint = hoveredPoint;
        contextPoint = hoveredPoint;
        ImGui::OpenPopup("RawToneCurvePointMenu");
    }
    if (ImGui::BeginPopup("RawToneCurvePointMenu")) {
        if (contextPoint > 0 && contextPoint < static_cast<int>(points.size()) - 1) {
            const Stack::RawRecipe::RawToneCurvePoint point = points[static_cast<std::size_t>(contextPoint)];
            ImGui::TextDisabled("Point %.3f, %.3f", point.input, point.output);
            if (ImGui::MenuItem("Delete Point")) {
                points.erase(points.begin() + contextPoint);
                selectedPoint = -1;
                draggingPoint = -1;
                contextPoint = -1;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }

    if (changed) {
        points = BuildToneCurveUiPoints({ Stack::RawRecipe::ToneCurveMode::Custom, points });
        if (draggingPoint >= static_cast<int>(points.size())) {
            draggingPoint = -1;
        }
    }

    for (std::size_t i = 1; i < points.size(); ++i) {
        drawList->AddLine(toScreen(points[i - 1]), toScreen(points[i]), curve, 2.0f);
    }
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const ImVec2 point = toScreen(points[static_cast<std::size_t>(i)]);
        const bool selected = i == selectedPoint;
        const ImU32 pointColor = selected
            ? ImGui::GetColorU32(ImGuiCol_Text)
            : curve;
        drawList->AddCircleFilled(point, selected ? 6.0f : 4.5f, pointColor, 18);
        drawList->AddCircle(point, selected ? 7.5f : 5.8f, IM_COL32(0, 0, 0, 145), 18, 1.0f);
    }
    return changed;
}

bool DrawViewportTileSet(
    const EditorRenderWorker::SharedTextureTileSet& tiles,
    const ImRect& rect) {
    if (!tiles.tiled || !tiles.complete || tiles.tiles.empty() ||
        tiles.fullWidth <= 0 || tiles.fullHeight <= 0) {
        return false;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float drawW = std::max(1.0f, rect.GetWidth());
    const float drawH = std::max(1.0f, rect.GetHeight());
    drawList->PushClipRect(rect.Min, rect.Max, true);
    for (const EditorRenderWorker::SharedTextureTile& tile : tiles.tiles) {
        if (tile.texture == 0 || tile.width <= 0 || tile.height <= 0 ||
            tile.haloWidth <= 0 || tile.haloHeight <= 0) {
            continue;
        }
        const float tileMinX = rect.Min.x + (static_cast<float>(tile.x) / static_cast<float>(tiles.fullWidth)) * drawW;
        const float tileMaxX = rect.Min.x + (static_cast<float>(tile.x + tile.width) / static_cast<float>(tiles.fullWidth)) * drawW;
        const float tileMinY = rect.Max.y - (static_cast<float>(tile.y + tile.height) / static_cast<float>(tiles.fullHeight)) * drawH;
        const float tileMaxY = rect.Max.y - (static_cast<float>(tile.y) / static_cast<float>(tiles.fullHeight)) * drawH;
        const float localX = static_cast<float>(tile.x - tile.haloX);
        const float localY = static_cast<float>(tile.y - tile.haloY);
        const float u0 = (localX + 0.5f) / static_cast<float>(tile.haloWidth);
        const float u1 = (localX + static_cast<float>(tile.width) - 0.5f) / static_cast<float>(tile.haloWidth);
        const float bottomV = (localY + 0.5f) / static_cast<float>(tile.haloHeight);
        const float topV = (localY + static_cast<float>(tile.height) - 0.5f) / static_cast<float>(tile.haloHeight);
        drawList->AddImage(
            (ImTextureID)(intptr_t)tile.texture,
            ImVec2(tileMinX, tileMinY),
            ImVec2(tileMaxX, tileMaxY),
            ImVec2(u0, 1.0f - topV),
            ImVec2(u1, 1.0f - bottomV));
    }
    drawList->PopClipRect();
    return true;
}

} // namespace

void EditorModule::RenderRawWorkspaceControlsPanel(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    const Stack::RawWorkspace::RawPanelState& panelState) {
    if (selectedSource == nullptr) {
        return;
    }

    const bool selectedProjectActive =
        IsRawWorkspaceProjectActive() &&
        m_Project->rawSourceKey == selectedSource->relativePathKey;
    const bool selectedPreviewStageQueued =
        m_RawWorkspacePreviewStageQueued &&
        m_RawWorkspacePreviewStageSourceKey == selectedSource->relativePathKey;
    const bool selectedProjectLoading =
        selectedPreviewStageQueued ||
        (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
         m_RawWorkspaceProjectLoadSourceKey == selectedSource->relativePathKey);
    const bool selectedProjectLoadFailed =
        m_RawWorkspaceProjectLoadTaskState == Async::TaskState::Failed &&
        m_RawWorkspaceProjectLoadSourceKey == selectedSource->relativePathKey;
    const bool selectedStoredProject =
        selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Embedded;

    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    Stack::RawWorkspace::RawProjectMode resolvedMode = panelState.mode;
    std::string recipeError;
    bool recipeResolved = false;
    if (selectedStoredProject && !selectedProjectActive) {
        recipe = BuildRawWorkspaceDefaultRecipe(*selectedSource);
        recipeError = (selectedProjectLoading || selectedProjectLoadFailed)
            ? m_RawWorkspaceProjectLoadStatusText
            : "Double-click this image in Gallery to open its RAW project.";
    } else {
        recipeResolved = ResolveRawWorkspaceRecipeForSource(
            *selectedSource,
            recipe,
            &resolvedMode,
            &recipeError);
        if (selectedPreviewStageQueued && recipeError.empty()) {
            recipeError = m_RawWorkspaceProjectLoadStatusText.empty()
                ? "Preparing RAW preview..."
                : m_RawWorkspaceProjectLoadStatusText;
        }
        if (!selectedStoredProject && !selectedProjectActive && recipeError.empty()) {
            recipeError = "Double-click this image in Gallery to open it for editing.";
        }
    }
    const bool canEdit =
        panelState.recipeControlsEditable &&
        recipeResolved &&
        selectedProjectActive &&
        !selectedProjectLoading;

    const bool selectedProjectSaving =
        selectedProjectActive && IsRawWorkspaceProjectSaveBusy();
    const bool showInlineSave =
        selectedProjectActive && (IsDirty() || selectedProjectSaving);
    Stack::RawRecipe::RawDevelopmentRecipe editedRecipe = recipe;
    bool changed = false;

    const float identityWidth = std::max(48.0f, ImGui::GetContentRegionAvail().x);
    const std::string sourceTitle =
        EllipsizeTextToWidth(
            selectedSource->fileName.empty() ? selectedSource->relativePathKey : selectedSource->fileName,
            identityWidth);
    ImGui::TextUnformatted(sourceTitle.c_str());
    const std::string sourceTooltip =
        selectedSource->absolutePath.empty()
            ? selectedSource->relativePathKey
            : selectedSource->absolutePath.string();
    TooltipIfHovered(sourceTooltip.c_str());

    std::vector<std::string> topBadges;
    std::vector<bool> topBadgeWarnings;
    auto addTopBadge = [&](std::string label, bool warning = false) {
        if (label.empty()) {
            return;
        }
        topBadges.push_back(std::move(label));
        topBadgeWarnings.push_back(warning);
    };
    auto projectStatusNeedsAttention = [](Stack::RawWorkspace::ProjectStatus status) {
        switch (status) {
            case Stack::RawWorkspace::ProjectStatus::MissingSource:
            case Stack::RawWorkspace::ProjectStatus::Conflict:
            case Stack::RawWorkspace::ProjectStatus::Invalid:
            case Stack::RawWorkspace::ProjectStatus::Unknown:
                return true;
            case Stack::RawWorkspace::ProjectStatus::NoProject:
            case Stack::RawWorkspace::ProjectStatus::Existing:
            case Stack::RawWorkspace::ProjectStatus::Embedded:
            default:
                return false;
        }
    };
    addTopBadge(
        RawWorkspaceProjectBadgeLabel(panelState.projectStatus, panelState.editCreatesProject),
        projectStatusNeedsAttention(panelState.projectStatus) || selectedProjectLoadFailed);
    addTopBadge(
        RawWorkspaceModeBadgeLabel(resolvedMode),
        resolvedMode == Stack::RawWorkspace::RawProjectMode::Unknown ||
        resolvedMode == Stack::RawWorkspace::RawProjectMode::CustomGraph);
    if (selectedProjectLoading) {
        addTopBadge(selectedPreviewStageQueued ? "Preparing preview" : "Loading");
    }
    if (selectedProjectLoadFailed) {
        addTopBadge("Load failed", true);
    }
    if (selectedProjectSaving) {
        addTopBadge("Saving");
    } else if (showInlineSave) {
        addTopBadge("Unsaved");
    }
    if (!panelState.recipeControlsEditable && !panelState.readOnlyMessage.empty()) {
        addTopBadge("Read-only", true);
    }
    if (!topBadges.empty()) {
        const float badgeStripHeight =
            ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild(
            "##RawWorkspaceTopBadgeStrip",
            ImVec2(0.0f, badgeStripHeight),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderRawWorkspaceBadgeLine(topBadges, topBadgeWarnings);
        ImGui::EndChild();
    }

    ImGui::Spacing();

    const float moreWidth = 38.0f;
    const float saveWidth = showInlineSave ? 54.0f : 0.0f;
    const float actionGap = ImGui::GetStyle().ItemSpacing.x;

    if (showInlineSave) {
        Stack::UiActivity::BeginDisabledForWork(selectedProjectSaving);
        if (ImGui::Button("Save", ImVec2(saveWidth, 0.0f))) {
            SaveActiveRawWorkspaceProject(true);
        }
        ImGui::EndDisabled();
        TooltipIfHovered(
            selectedProjectSaving ? "RAW project save is already running." : "Save this RAW project.",
            ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine(0.0f, actionGap);
    }

    if (ImGui::Button("...", ImVec2(moreWidth, 0.0f))) {
        ImGui::OpenPopup("RawWorkspaceProjectActions");
    }
    TooltipIfHovered("More project and source actions.");
    if (ImGui::BeginPopup("RawWorkspaceProjectActions")) {
        if (ImGui::MenuItem("Save", nullptr, false, selectedProjectActive)) {
            SaveActiveRawWorkspaceProject(true);
            ImGui::CloseCurrentPopup();
        }
        TooltipIfHovered(
            selectedProjectActive ? "Save this RAW project." : "Open or edit this RAW project before saving.",
            ImGuiHoveredFlags_AllowWhenDisabled);

        if (selectedProjectActive) {
            ImGui::Separator();
            if (ImGui::MenuItem("Close Project")) {
                m_ShowRawWorkspaceCloseProjectPopup = true;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    (void)recipeError;

    ImGui::Spacing();
    ImGui::Separator();

    const float controlWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);

    Stack::UiActivity::BeginDisabledForWork(
        !canEdit && IsRawWorkspaceProjectLoadBusy(),
        !canEdit && !IsRawWorkspaceProjectLoadBusy());

    ImGui::SeparatorText("Main Controls");

    if (ImGui::CollapsingHeader("RAW Pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Processing: Truthful RAW");
        ImGui::TextDisabled(
            "Working space: %s",
            Raw::RawWorkingSpaceName(editedRecipe.technical.workingSpace));
        const char* demosaicLabels[] = { "Fast / Bilinear", "Malvar-He-Cutler 5x5", "Nearest Neighbor", "Hamilton-Adams" };
        int demosaicMethod = static_cast<int>(editedRecipe.technical.demosaicMethod);
        if (ImGuiExtras::NodeCombo(
                "Demosaic",
                "##RawWorkspaceDemosaic",
                &demosaicMethod,
                demosaicLabels,
                IM_ARRAYSIZE(demosaicLabels),
                controlWidth)) {
            editedRecipe.technical.demosaicMethod = static_cast<Raw::DemosaicMethod>(
                std::clamp(demosaicMethod, 0, IM_ARRAYSIZE(demosaicLabels) - 1));
            changed = true;
        }
        bool applyBaselineExposure = editedRecipe.technical.applyBaselineExposure;
        if (ImGuiExtras::NodeCheckbox(
                "Apply DNG Baseline Exposure",
                "##RawWorkspaceApplyBaselineExposure",
                &applyBaselineExposure,
                controlWidth)) {
            editedRecipe.technical.applyBaselineExposure = applyBaselineExposure;
            changed = true;
        }
        TooltipIfHovered("Applies the camera maker's disclosed DNG baseline exposure before your RAW Exposure adjustment.");
        ImGui::TextWrapped("The RAW engine keeps signed scene RGB through local and finish stages, applies metadata correction before demosaic, and handles Linear DNG white balance in the working domain.");
    }

    if (ImGui::CollapsingHeader("Base Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnsureViewTransformJson(editedRecipe.viewTransform.layerJson);
        nlohmann::json& viewTransform = editedRecipe.viewTransform.layerJson;
        bool viewTransformChangedByUser = false;

        float exposureEv = editedRecipe.preToneExposureEv;
        if (ImGuiExtras::NodeSliderFloat("RAW Exposure", "##RawExposureEv", &exposureEv, -8.0f, 8.0f, "%+.2f EV", controlWidth)) {
            editedRecipe.preToneExposureEv = exposureEv;
            changed = true;
        }
        TooltipIfHovered("+1 EV multiplies scene-linear decoded values by 2 before tone shaping.");

        float viewExposure = JsonNumber(viewTransform, "exposure", 0.0f);
        float blackEv = JsonNumber(viewTransform, "blackEv", -8.0f);
        float whiteEv = JsonNumber(viewTransform, "whiteEv", 4.0f);
        float middleGrey = JsonNumber(viewTransform, "middleGrey", 0.18f);
        ImGui::TextDisabled("View Transform / Display Fit");
        TooltipIfHovered("Maps scene-linear values into the finite display range. It does not recover sensor data or change RAW Exposure.");
        bool builtInViewTransformEnabled = JsonBool(viewTransform, "enabled", true);
        if (ImGuiExtras::NodeCheckbox(
                "Apply Built-in View Transform",
                "##RawBuiltInViewTransformEnabled",
                &builtInViewTransformEnabled,
                controlWidth)) {
            viewTransform["enabled"] = builtInViewTransformEnabled;
            changed = true;
            viewTransformChangedByUser = true;
        }
        TooltipIfHovered("Disable this to keep the compact RAW Development node scene-linear and place a View Transform later in the Editor graph.");
        if (ImGuiExtras::NodeSliderFloat("Display Exposure", "##RawViewExposure", &viewExposure, -8.0f, 8.0f, "%.2f stops", controlWidth)) {
            viewTransform["exposure"] = viewExposure;
            changed = true;
            viewTransformChangedByUser = true;
        }
        TooltipIfHovered("Display-stage exposure offset inside the View Transform. Use RAW Exposure for scene-linear capture placement.");
        if (ImGuiExtras::NodeSliderFloat("Black EV", "##RawViewBlackEv", &blackEv, -16.0f, 0.0f, "%.2f", controlWidth)) {
            viewTransform["blackEv"] = blackEv;
            changed = true;
            viewTransformChangedByUser = true;
        }
        TooltipIfHovered("Scene EV below Middle Grey that maps near display black after black subtraction.");
        if (ImGuiExtras::NodeSliderFloat("White EV", "##RawViewWhiteEv", &whiteEv, 0.0f, 16.0f, "%.2f", controlWidth)) {
            viewTransform["whiteEv"] = whiteEv;
            changed = true;
            viewTransformChangedByUser = true;
        }
        TooltipIfHovered("Scene EV above Middle Grey that anchors display white before shoulder rolloff.");
        if (ImGuiExtras::NodeSliderFloat("Middle Grey", "##RawViewMiddleGrey", &middleGrey, 0.01f, 1.0f, "%.3f", controlWidth)) {
            viewTransform["middleGrey"] = middleGrey;
            changed = true;
            viewTransformChangedByUser = true;
        }
        TooltipIfHovered("Scene-linear luma anchor for the EV scale.");

        if (ImGui::TreeNodeEx("Advanced##RawBaseLightAdvanced")) {
            if (ImGui::SmallButton("Reset View Transform##RawBaseLightResetViewTransform")) {
                viewTransform = Stack::RawRecipe::DefaultViewTransformJson();
                changed = true;
                viewTransformChangedByUser = true;
            }
            TooltipIfHovered("Returns the display mapping to generic scene-linear defaults.");

            float shoulder = JsonNumber(viewTransform, "shoulder", 0.45f);
            float toe = JsonNumber(viewTransform, "toe", 0.18f);
            float contrast = JsonNumber(viewTransform, "contrast", 1.0f);
            float contrastPivotEv = JsonNumber(
                viewTransform,
                "contrastPivotEv",
                0.0f);
            float saturation = JsonNumber(viewTransform, "saturation", 1.0f);
            bool preserveHue = JsonBool(viewTransform, "preserveHue", true);
            bool falseColor = JsonBool(viewTransform, "debugFalseColor", false);
            bool encodeSrgbOutput = JsonBool(
                viewTransform,
                "encodeSrgbOutput",
                editedRecipe.technical.encodeSrgbOutput);

            if (ImGuiExtras::NodeSliderFloat("Shoulder", "##RawViewShoulder", &shoulder, 0.05f, 4.0f, "%.2f", controlWidth)) {
                viewTransform["shoulder"] = shoulder;
                changed = true;
                viewTransformChangedByUser = true;
            }
            if (ImGuiExtras::NodeSliderFloat("Toe", "##RawViewToe", &toe, 0.0f, 1.0f, "%.2f", controlWidth)) {
                viewTransform["toe"] = toe;
                changed = true;
                viewTransformChangedByUser = true;
            }
            if (ImGuiExtras::NodeSliderFloat("Contrast", "##RawViewContrast", &contrast, 0.25f, 2.5f, "%.2f", controlWidth)) {
                viewTransform["contrast"] = contrast;
                changed = true;
                viewTransformChangedByUser = true;
            }
            if (ImGuiExtras::NodeSliderFloat(
                    "Contrast Pivot",
                    "##RawViewContrastPivot",
                    &contrastPivotEv,
                    std::max(-8.0f, blackEv + 0.1f),
                    std::min(8.0f, whiteEv - 0.1f),
                    "%+.2f EV",
                    controlWidth)) {
                viewTransform["contrastPivotEv"] = contrastPivotEv;
                changed = true;
                viewTransformChangedByUser = true;
            }
            if (ImGuiExtras::NodeSliderFloat("Saturation", "##RawViewSaturation", &saturation, 0.0f, 2.0f, "%.2f", controlWidth)) {
                viewTransform["saturation"] = saturation;
                changed = true;
                viewTransformChangedByUser = true;
            }
            if (ImGuiExtras::NodeCheckbox("Preserve Hue", "##RawViewPreserveHue", &preserveHue, controlWidth)) {
                viewTransform["preserveHue"] = preserveHue;
                changed = true;
                viewTransformChangedByUser = true;
            }
            if (ImGuiExtras::NodeCheckbox("EV False Color", "##RawViewFalseColor", &falseColor, controlWidth)) {
                viewTransform["debugFalseColor"] = falseColor;
                changed = true;
                viewTransformChangedByUser = true;
            }
            if (ImGuiExtras::NodeCheckbox(
                    "Encode sRGB Output",
                    "##RawViewEncodeSrgb",
                    &encodeSrgbOutput,
                    controlWidth)) {
                viewTransform["encodeSrgbOutput"] = encodeSrgbOutput;
                editedRecipe.technical.encodeSrgbOutput = encodeSrgbOutput;
                changed = true;
                viewTransformChangedByUser = true;
            }
            ImGui::TextDisabled(
                "Input: %s",
                Raw::RawWorkingSpaceName(editedRecipe.technical.workingSpace));
            ImGui::TreePop();
        }

        if (viewTransformChangedByUser) {
            MarkRawWorkspaceViewTransformUserEdited();
        }
    }

    if (ImGui::CollapsingHeader("White Balance", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::string whiteBalanceSummary =
            BuildRawWorkspaceWhiteBalanceSummary(editedRecipe.whiteBalance);
        ImGui::TextDisabled("%s", whiteBalanceSummary.c_str());
        TooltipIfHovered("White Balance summary from the current visible recipe controls.");
        int wbIndex =
            editedRecipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers
                ? 1
                : 0;
        bool wbModeChanged = false;
        const char* manualLabels[] = { "As Shot", "Custom" };
        if (ImGuiExtras::NodeCombo(
                "Mode",
                "##RawWhiteBalanceMode",
                &wbIndex,
                manualLabels,
                IM_ARRAYSIZE(manualLabels),
                controlWidth)) {
            editedRecipe.whiteBalance.mode = wbIndex == 1
                ? Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers
                : Stack::RawRecipe::WhiteBalanceMode::AsShot;
            wbModeChanged = true;
        }
        if (wbModeChanged) {
            if (editedRecipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers) {
                editedRecipe.whiteBalance.hasMultipliers = true;
            }
            changed = true;
        }

        if (editedRecipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers) {
            ImGui::TextDisabled("Camera-neutral multipliers are the canonical white-balance control.");
            TooltipIfHovered("Temperature and tint are hidden until Stack has a calibrated two-way conversion for this camera profile.");
            for (int channel = 0; channel < 3; ++channel) {
                const char* channelLabel = channel == 0 ? "Red Multiplier" : (channel == 1 ? "Green Multiplier" : "Blue Multiplier");
                float multiplier = editedRecipe.whiteBalance.multipliers[channel];
                const char* channelId = channel == 0
                    ? "##RawWbRedMultiplier"
                    : (channel == 1 ? "##RawWbGreenMultiplier" : "##RawWbBlueMultiplier");
                if (ImGuiExtras::NodeSliderFloat(channelLabel, channelId, &multiplier, 0.05f, 16.0f, "%.3f", controlWidth)) {
                    editedRecipe.whiteBalance.multipliers[channel] = multiplier;
                    editedRecipe.whiteBalance.hasMultipliers = true;
                    changed = true;
                }
            }
        }
    }

    RenderRawWorkspaceAnalysisPanel(controlWidth);

    const Stack::RawRecipe::RawCropRotationRecipe& cropRotation = editedRecipe.cropRotation;
    const std::string cropRotationSummary =
        BuildRawWorkspaceCropRotationSummary(cropRotation);
    const bool cropRotationActive = IsRawWorkspaceCropRotationActive(cropRotation);
    const ImGuiTreeNodeFlags cropRotationHeaderFlags =
        cropRotationActive ? ImGuiTreeNodeFlags_DefaultOpen : 0;

    const bool cropRotationOpen =
        ImGui::CollapsingHeader("Crop & Rotate", cropRotationHeaderFlags);
    ImGui::TextDisabled("%s", cropRotationSummary.c_str());
    if (cropRotationOpen) {
        bool cropEnabled = editedRecipe.cropRotation.cropEnabled;
        if (ImGui::Checkbox("Crop Enabled", &cropEnabled)) {
            editedRecipe.cropRotation.cropEnabled = cropEnabled;
            changed = true;
        }

        ImGui::BeginDisabled(!editedRecipe.cropRotation.cropEnabled);
        float cropX = editedRecipe.cropRotation.cropX;
        float cropY = editedRecipe.cropRotation.cropY;
        float cropW = editedRecipe.cropRotation.cropWidth;
        float cropH = editedRecipe.cropRotation.cropHeight;
        if (ImGuiExtras::NodeSliderFloat("Left", "##RawCropLeft", &cropX, 0.0f, 0.95f, "%.3f", controlWidth)) {
            editedRecipe.cropRotation.cropX = std::clamp(cropX, 0.0f, 0.95f);
            editedRecipe.cropRotation.cropWidth = std::min(editedRecipe.cropRotation.cropWidth, 1.0f - editedRecipe.cropRotation.cropX);
            changed = true;
        }
        if (ImGuiExtras::NodeSliderFloat("Top", "##RawCropTop", &cropY, 0.0f, 0.95f, "%.3f", controlWidth)) {
            editedRecipe.cropRotation.cropY = std::clamp(cropY, 0.0f, 0.95f);
            editedRecipe.cropRotation.cropHeight = std::min(editedRecipe.cropRotation.cropHeight, 1.0f - editedRecipe.cropRotation.cropY);
            changed = true;
        }
        if (ImGuiExtras::NodeSliderFloat("Width", "##RawCropWidth", &cropW, 0.05f, 1.0f, "%.3f", controlWidth)) {
            editedRecipe.cropRotation.cropWidth = std::clamp(cropW, 0.05f, 1.0f - editedRecipe.cropRotation.cropX);
            changed = true;
        }
        if (ImGuiExtras::NodeSliderFloat("Height", "##RawCropHeight", &cropH, 0.05f, 1.0f, "%.3f", controlWidth)) {
            editedRecipe.cropRotation.cropHeight = std::clamp(cropH, 0.05f, 1.0f - editedRecipe.cropRotation.cropY);
            changed = true;
        }
        ImGui::EndDisabled();

        const char* rotationLabels[] = { "0 Degrees", "90 Degrees CW", "180 Degrees", "270 Degrees CW" };
        int rotationIndex = std::clamp(editedRecipe.cropRotation.rotationDegrees / 90, 0, 3);
        if (ImGuiExtras::NodeCombo("Rotation", "##RawCropRotation", &rotationIndex, rotationLabels, IM_ARRAYSIZE(rotationLabels), controlWidth)) {
            editedRecipe.cropRotation.rotationDegrees = std::clamp(rotationIndex, 0, 3) * 90;
            changed = true;
        }
        if (ImGui::Checkbox(
                "Flip Horizontally",
                &editedRecipe.cropRotation.flipHorizontally)) {
            changed = true;
        }
        if (ImGui::Checkbox(
                "Flip Vertically",
                &editedRecipe.cropRotation.flipVertically)) {
            changed = true;
        }
    }

    ImGui::EndDisabled();

    if (changed && canEdit) {
        ApplyRawWorkspaceRecipeEditForSelectedSource(editedRecipe, ImGui::IsAnyItemActive());
    }
}

void EditorModule::RenderRawWorkspaceToneGraphsPanel(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    const Stack::RawWorkspace::RawPanelState& panelState) {
    if (selectedSource == nullptr) {
        ImGui::TextDisabled("Select a RAW image to edit its tone graphs.");
        return;
    }

    const bool selectedProjectActive =
        IsRawWorkspaceProjectActive() &&
        m_Project->rawSourceKey == selectedSource->relativePathKey;
    const bool canEdit =
        panelState.recipeControlsEditable &&
        selectedProjectActive &&
        !IsRawWorkspaceProjectLoadBusy();
    Stack::RawRecipe::RawDevelopmentRecipe editedRecipe = selectedProjectActive
        ? m_Project->rawRecipe
        : BuildRawWorkspaceDefaultRecipe(*selectedSource);
    bool changed = false;
    const float controlWidth = std::max(180.0f, ImGui::GetContentRegionAvail().x);

    ImGui::TextDisabled("Scene-local placement and final tone shaping stay visible as graphs.");
    Stack::UiActivity::BeginDisabledForWork(
        !canEdit && IsRawWorkspaceProjectLoadBusy(),
        !canEdit && !IsRawWorkspaceProjectLoadBusy());

    const bool localRangeDefaultOpen =
        Stack::RawRecipe::IsLocalRangeEnabled(editedRecipe.localRange);
    if (RenderRawWorkspaceLocalRangeControls(
            selectedSource,
            editedRecipe,
            controlWidth,
            localRangeDefaultOpen)) {
        changed = true;
    }

    if (ImGui::CollapsingHeader("Finish Tone", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnsureFinishToneJson(editedRecipe.finishTone.layerJson);
        nlohmann::json& finishTone = editedRecipe.finishTone.layerJson;

        const struct ModeButton {
            int value;
            const char* label;
        } modeButtons[] = {
            { 0, "Y" },
            { 1, "RGB" },
            { 2, "R" },
            { 3, "G" },
            { 4, "B" }
        };
        int mode = std::clamp(JsonInt(finishTone, "mode", 1), 0, 4);
        const char* domainLabels[] = { "Scene Linear", "Log Scene" };
        int domain = std::clamp(JsonInt(finishTone, "domain", 1), 0, 1);
        std::vector<Stack::RawRecipe::RawToneCurvePoint> points =
            BuildFinishToneUiPoints(finishTone);
        const std::string finishToneSummary =
            BuildRawWorkspaceFinishToneSummary(mode, domain, points.size());
        ImGui::TextDisabled("%s", finishToneSummary.c_str());
        TooltipIfHovered("Finish Tone summary from the current visible graph controls.");

        const float modeGap = 6.0f;
        const float modeWidth = std::max(34.0f, (controlWidth - modeGap * 4.0f) / 5.0f);
        for (int i = 0; i < IM_ARRAYSIZE(modeButtons); ++i) {
            const bool selected = mode == modeButtons[i].value;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 128, 176, 215));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(72, 146, 198, 235));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 156, 210, 255));
            }
            if (ImGuiExtras::RichFullWidthButton(modeButtons[i].label, modeWidth, 0.0f)) {
                mode = modeButtons[i].value;
                finishTone["mode"] = mode;
                changed = true;
            }
            if (selected) {
                ImGui::PopStyleColor(3);
            }
            if (i + 1 < IM_ARRAYSIZE(modeButtons)) {
                ImGui::SameLine(0.0f, modeGap);
            }
        }

        if (ImGuiExtras::NodeCombo(
                "Curve Domain",
                "##RawFinishToneDomain",
                &domain,
                domainLabels,
                IM_ARRAYSIZE(domainLabels),
                controlWidth)) {
            finishTone["domain"] = domain;
            changed = true;
        }

        const std::string pointValuesSummary =
            BuildRawWorkspaceFinishTonePointValuesSummary(points);
        ImGui::TextDisabled("%s", EllipsizeTextToWidth(pointValuesSummary, controlWidth).c_str());
        TooltipIfHovered(pointValuesSummary.c_str());
        if (DrawToneCurveWidget(
                points,
                ImVec2(controlWidth, std::clamp(controlWidth * 0.54f, 210.0f, 360.0f)),
                m_ProjectInteractionUi.finishTone)) {
            StoreFinishToneUiPoints(finishTone, points);
            changed = true;
        }
        TooltipIfHovered("Curve points edit the graph-compatible finish tone layer.");

        if (ImGui::TreeNodeEx("Advanced##RawFinishToneAdvanced")) {
            if (domain == 1) {
                float logMinEv = JsonNumber(finishTone, "logMinEv", -10.0f);
                float logMaxEv = JsonNumber(finishTone, "logMaxEv", 6.0f);
                if (ImGuiExtras::NodeSliderFloat(
                        "Graph Black EV",
                        "##RawFinishToneLogMinEv",
                        &logMinEv,
                        -20.0f,
                        0.0f,
                        "%.2f",
                        controlWidth)) {
                    finishTone["logMinEv"] = logMinEv;
                    changed = true;
                }
                if (ImGuiExtras::NodeSliderFloat(
                        "Graph White EV",
                        "##RawFinishToneLogMaxEv",
                        &logMaxEv,
                        0.0f,
                        20.0f,
                        "%.2f",
                        controlWidth)) {
                    finishTone["logMaxEv"] = logMaxEv;
                    changed = true;
                }
                if (logMaxEv <= logMinEv + 0.1f) {
                    finishTone["logMaxEv"] = logMinEv + 0.1f;
                    changed = true;
                }
            }
            if (ImGui::SmallButton("Reset Curve##RawFinishToneResetCurve")) {
                nlohmann::json resetTone = Stack::RawRecipe::DefaultFinishToneJson();
                resetTone["mode"] = mode;
                resetTone["domain"] = domain;
                finishTone = std::move(resetTone);
                changed = true;
            }
            TooltipIfHovered("Reset Finish Tone points while keeping the current mode and domain.");
            ImGui::TreePop();
        }
    }

    ImGui::EndDisabled();
    if (changed && canEdit) {
        ApplyRawWorkspaceRecipeEditForSelectedSource(editedRecipe, ImGui::IsAnyItemActive());
    }
}

void EditorModule::RenderRawWorkspacePreviewPanel(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    const Stack::RawWorkspace::RawPanelState& panelState) {
    (void)panelState;
    if (selectedSource == nullptr) {
        return;
    }

    const bool selectedProjectActive =
        IsRawWorkspaceProjectActive() &&
        m_Project->rawSourceKey == selectedSource->relativePathKey;
    const bool selectedPreviewStageQueued =
        m_RawWorkspacePreviewStageQueued &&
        m_RawWorkspacePreviewStageSourceKey == selectedSource->relativePathKey;
    const bool localRangeActive =
        selectedProjectActive &&
        Stack::RawRecipe::IsLocalRangeEnabled(m_Project->rawRecipe.localRange);
    const bool localRangeMaskAvailable =
        selectedProjectActive &&
        (m_Project->rawRecipe.localRange.regionMaskEnabled ||
         m_Project->rawRecipe.localRange.colorMaskEnabled);

    auto renderViewModeButton = [&](const char* label,
                                    const char* mode,
                                    float width,
                                    bool enabled,
                                    const char* tooltip,
                                    const char* disabledTooltip) {
        const bool selected = m_RawWorkspaceLocalRangeOverlayMode == mode;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button(label, ImVec2(width, 0.0f)) &&
            m_RawWorkspaceLocalRangeOverlayMode != mode) {
            m_RawWorkspaceLocalRangeOverlayMode = mode;
            ClearRawWorkspaceLocalRangeOverlayState();
            MarkRenderRefreshDirty();
        }
        ImGui::EndDisabled();
        TooltipIfHovered(enabled ? tooltip : disabledTooltip, ImGuiHoveredFlags_AllowWhenDisabled);
        if (selected) {
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine(0.0f, 5.0f);
    };
    renderViewModeButton(
        "Final",
        "none",
        52.0f,
        true,
        "Show the final RAW preview.",
        "");
    ImGui::TextDisabled("Compare");
    TooltipIfHovered(
        "Compare needs a stable before/after RAW preview state; it is deferred until that state can be isolated safely.");
    ImGui::SameLine(0.0f, 5.0f);
    renderViewModeButton(
        "Affected",
        "affected-tones",
        68.0f,
        localRangeActive,
        "Show tones affected by Local Range.",
        "Enable Local Range before viewing affected tones.");
    renderViewModeButton(
        "Delta",
        "delta-map",
        56.0f,
        localRangeActive,
        "Show the Local Range EV delta map.",
        "Enable Local Range before viewing the delta map.");
    renderViewModeButton(
        "Mask",
        "region-mask",
        52.0f,
        localRangeMaskAvailable,
        "Show the Local Range region/color mask.",
        "Enable a Region Mask or Color Target before viewing the mask.");
    if (ImGui::Button("Risk", ImVec2(54.0f, 0.0f))) {
        m_RawWorkspaceLayoutUi.diagnosticsOpenRequested = true;
    }
    TooltipIfHovered("Open Diagnostics for Highlight Risk until a viewport overlay exists.");

    if (!selectedSource->thumbnail.errorMessage.empty()) {
        ImGui::TextWrapped("%s", selectedSource->thumbnail.errorMessage.c_str());
    }
    if (!selectedSource->project.errorMessage.empty()) {
        ImGui::TextWrapped("%s", selectedSource->project.errorMessage.c_str());
    }

    ImGui::Spacing();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 imageBounds(
        std::max(120.0f, avail.x - 12.0f),
        std::max(140.0f, avail.y - 12.0f));
    UpdateRawWorkspaceInteractivePreviewDimension(imageBounds);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImRect bounds(start, ImVec2(start.x + imageBounds.x, start.y + imageBounds.y));

    const bool currentRawPreview =
        selectedProjectActive &&
        m_ViewportOutputRawWorkspaceSourceKey == selectedSource->relativePathKey;
    bool drewRecipePreview = false;
    auto drawLocalRangeOverlay = [&](const ImRect& imageRect) {
        if (!HasRawWorkspaceLocalRangeOverlayForSource(selectedSource->relativePathKey)) {
            return;
        }
        const float uInset = m_RawWorkspaceLocalRangeOverlayWidth > 1
            ? (0.5f / static_cast<float>(m_RawWorkspaceLocalRangeOverlayWidth))
            : 0.0f;
        const float vInset = m_RawWorkspaceLocalRangeOverlayHeight > 1
            ? (0.5f / static_cast<float>(m_RawWorkspaceLocalRangeOverlayHeight))
            : 0.0f;
        drawList->AddImage(
            (ImTextureID)(intptr_t)m_RawWorkspaceLocalRangeOverlayTexture,
            imageRect.Min,
            imageRect.Max,
            ImVec2(uInset, 1.0f - vInset),
            ImVec2(1.0f - uInset, vInset));
    };
    if (currentRawPreview &&
        m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::Tiled &&
        HasViewportOutputTiles()) {
        const EditorRenderWorker::SharedTextureTileSet& tiles = GetViewportOutputTiles();
        const ImVec2 imageSize = FitImageSize(
            static_cast<float>(tiles.fullWidth),
            static_cast<float>(tiles.fullHeight),
            imageBounds);
        const ImVec2 imageMin(
            bounds.Min.x + (imageBounds.x - imageSize.x) * 0.5f,
            bounds.Min.y + std::max(0.0f, (imageBounds.y - imageSize.y) * 0.48f));
        const ImRect imageRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
        drewRecipePreview = DrawViewportTileSet(tiles, imageRect);
        if (drewRecipePreview) {
            drawLocalRangeOverlay(imageRect);
            drawList->AddRect(imageRect.Min, imageRect.Max, ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
            HandleRawWorkspaceLocalRangeTargetInteraction(
                *selectedSource,
                imageRect.Min,
                imageRect.Max,
                selectedProjectActive,
                currentRawPreview);
        }
    }
    if (!drewRecipePreview &&
        currentRawPreview &&
        m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::SingleTexture &&
        IsViewportTextureSafeForDrawing(m_RawWorkspacePresentationTexture.texture) &&
        m_RawWorkspacePresentationTexture.width > 0 &&
        m_RawWorkspacePresentationTexture.height > 0) {
        const ImVec2 imageSize = FitImageSize(
            static_cast<float>(m_RawWorkspacePresentationTexture.width),
            static_cast<float>(m_RawWorkspacePresentationTexture.height),
            imageBounds);
        const ImVec2 imageMin(
            bounds.Min.x + (imageBounds.x - imageSize.x) * 0.5f,
            bounds.Min.y + std::max(0.0f, (imageBounds.y - imageSize.y) * 0.48f));
        const ImRect imageRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
        const float uInset = m_RawWorkspacePresentationTexture.width > 1
            ? (0.5f / static_cast<float>(m_RawWorkspacePresentationTexture.width))
            : 0.0f;
        const float vInset = m_RawWorkspacePresentationTexture.height > 1
            ? (0.5f / static_cast<float>(m_RawWorkspacePresentationTexture.height))
            : 0.0f;
        drawList->AddImage(
            (ImTextureID)(intptr_t)m_RawWorkspacePresentationTexture.texture,
            imageRect.Min,
            imageRect.Max,
            ImVec2(uInset, 1.0f - vInset),
            ImVec2(1.0f - uInset, vInset));
        drawLocalRangeOverlay(imageRect);
        drawList->AddRect(imageRect.Min, imageRect.Max, ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
        HandleRawWorkspaceLocalRangeTargetInteraction(
            *selectedSource,
            imageRect.Min,
            imageRect.Max,
            selectedProjectActive,
            currentRawPreview);
        drewRecipePreview = true;
    }

    if (!drewRecipePreview && selectedProjectActive) {
        const ImVec2 imageSize = FitImageSize(4.0f, 3.0f, imageBounds);
        const ImVec2 imageMin(
            bounds.Min.x + (imageBounds.x - imageSize.x) * 0.5f,
            bounds.Min.y + std::max(0.0f, (imageBounds.y - imageSize.y) * 0.48f));
        const ImRect imageRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
        DrawRawPlaceholder(drawList, imageRect, true, "RAW");
        drewRecipePreview = true;
    }

    if (!drewRecipePreview && selectedPreviewStageQueued) {
        const ImVec2 imageSize = FitImageSize(4.0f, 3.0f, imageBounds);
        const ImVec2 imageMin(
            bounds.Min.x + (imageBounds.x - imageSize.x) * 0.5f,
            bounds.Min.y + std::max(0.0f, (imageBounds.y - imageSize.y) * 0.48f));
        const ImRect imageRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
        DrawRawPlaceholder(drawList, imageRect, true, "RAW");
        drewRecipePreview = true;
    }

    if (!drewRecipePreview) {
        const char* label =
            "Double-click this image in Gallery to open it for editing.";
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        drawList->AddText(
            ImVec2(
                bounds.GetCenter().x - labelSize.x * 0.5f,
                bounds.GetCenter().y - labelSize.y * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            label);
    }

    ImGui::Dummy(ImVec2(imageBounds.x, imageBounds.y));
}

void EditorModule::RenderRawWorkspaceEmptyState(const RawWorkspaceScanSnapshot& scanSnapshot) {
    ImGui::TextUnformatted("RAW Workspace");
    ImGui::Spacing();
    if (ImGui::Button("Open RAW Folder", ImVec2(180.0f, 0.0f))) {
        OpenRawWorkspaceFolderDialog();
        return;
    }

    if (!scanSnapshot.statusText.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", scanSnapshot.statusText.c_str());
    }

    if (!m_RawWorkspace.recentWorkspaceRoots.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Recent Workspaces");
        for (std::size_t index = 0; index < m_RawWorkspace.recentWorkspaceRoots.size(); ++index) {
            const std::filesystem::path recent = m_RawWorkspace.recentWorkspaceRoots[index];
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(recent.string().c_str(), false)) {
                ImGui::PopID();
                RequestOpenRawWorkspace(recent);
                return;
            }
            ImGui::PopID();
        }
    }
}

void EditorModule::RenderRawWorkspaceBrowser(
    const RawWorkspaceScanSnapshot& scanSnapshot,
    const RawWorkspaceThumbnailSnapshot& thumbnailSnapshot) {
    const bool scanBusy = Async::IsBusy(scanSnapshot.state);
    const bool thumbnailBusy = Async::IsBusy(thumbnailSnapshot.state);
    const Stack::RawWorkspace::GalleryPresentation& presentation =
        GetRawWorkspaceGalleryPresentation();
    const auto selectedIt = std::find_if(
        m_RawWorkspace.sources.begin(),
        m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == m_RawWorkspace.selectedSourceKey;
        });
    const Stack::RawWorkspace::SourceRecord* selectedSource =
        selectedIt == m_RawWorkspace.sources.end() ? nullptr : &(*selectedIt);

    if (RenderRawWorkspaceIconButton(
            "##RawWorkspaceOpenFolder",
            m_RawFolderIconTexture,
            "Open RAW Folder",
            m_Appearance)) {
        OpenRawWorkspaceFolderDialog();
        return;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (RenderRawWorkspaceIconButton(
            "##RawWorkspaceRescan",
            m_RawRefreshIconTexture,
            "Rescan RAW Folder",
            m_Appearance)) {
        RescanRawWorkspace();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (RenderRawWorkspaceIconButton(
            "##RawWorkspaceClear",
            m_RawClearIconTexture,
            "Clear RAW Workspace",
            m_Appearance)) {
        m_RawWorkspaceGalleryWindowOpen = false;
        ClearRawWorkspaceForUser();
        return;
    }
    ImGui::SameLine(0.0f, 18.0f);
    if (RenderRawWorkspaceIconButton(
            "##RawWorkspaceGallery",
            m_RawGalleryIconTexture,
            m_RawWorkspaceGalleryWindowOpen ? "Close Gallery" : "Open Gallery",
            m_Appearance,
            m_RawWorkspaceGalleryWindowOpen)) {
        m_RawWorkspaceGalleryWindowOpen = !m_RawWorkspaceGalleryWindowOpen;
    }

    ImGui::SameLine(0.0f, 12.0f);
    if (ImGui::Button(("Projects (" +
            std::to_string(m_RawWorkspace.sourceSetProjects.size()) + ")").c_str())) {
        m_OpenRawSourceSetProjectBrowser = true;
    }
    if (m_OpenRawSourceSetProjectBrowser) {
        ImGui::OpenPopup("RAW Project Browser");
        m_OpenRawSourceSetProjectBrowser = false;
    }
    if (ImGui::BeginPopup("RAW Project Browser")) {
        ImGui::TextUnformatted("Saved RAW Projects");
        ImGui::Separator();
        if (m_RawWorkspace.sourceSetProjects.empty()) {
            ImGui::TextDisabled("No .stackbundle or portable v3 projects found.");
        }
        for (const Stack::RawWorkspace::SourceSetProjectCatalogEntry& project :
             m_RawWorkspace.sourceSetProjects) {
            ImGui::PushID(project.absolutePath.string().c_str());
            const bool activeProject = m_Project->snapshot &&
                m_Project->snapshot->projectId == project.projectId;
            const Stack::Project::ProjectLifecyclePhase lifecyclePhase =
                m_Project->lifecycle.Phase();
            const bool activeDirty = activeProject &&
                m_Project->lifecycle.IsDirty();
            const bool conflicted = project.readOnlyRecovery ||
                (activeProject &&
                 lifecyclePhase == Stack::Project::ProjectLifecyclePhase::Conflict);
            ImGui::TextUnformatted(project.projectName.empty()
                ? project.absolutePath.filename().string().c_str()
                : project.projectName.c_str());
            if (project.multiFrameProject) {
                ImGui::TextDisabled(
                    "%s | %llu sets | %llu frames | %llu RAW / %llu raster",
                    Stack::Project::ProjectStorageKindName(project.storageKind),
                    static_cast<unsigned long long>(project.sourceSetCount),
                    static_cast<unsigned long long>(project.totalFrameCount),
                    static_cast<unsigned long long>(project.rawSetCount),
                    static_cast<unsigned long long>(project.rasterSetCount));
            } else {
                ImGui::TextDisabled(
                    "%s | Single RAW image",
                    Stack::Project::ProjectStorageKindName(project.storageKind));
            }
            ImGui::TextDisabled(
                "State: %s%s%s",
                conflicted ? "Conflict" : (activeDirty ? "Dirty" : "Clean"),
                activeProject ? " | Active" : "",
                project.readOnlyRecovery ? " | Read-only recovery" : "");
            if (project.readOnlyRecovery) {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.68f, 0.25f, 1.0f),
                    "Recovered previous manifest - Save Repaired Copy required");
            } else if (!project.errorMessage.empty()) {
                ImGui::TextWrapped("%s", project.errorMessage.c_str());
            }
            ImGui::BeginDisabled(
                project.status == Stack::RawWorkspace::ProjectStatus::Invalid);
            if (ImGui::SmallButton("Open Project")) {
                if (RequestOpenRawWorkspaceProjectFromGallery(
                        project.absolutePath)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    const auto selectedSourcePaths = [&]() {
        std::vector<std::filesystem::path> paths;
        paths.reserve(m_RawWorkspace.selectedSourceKeys.size());
        for (const std::string& key : m_RawWorkspace.selectedSourceKeys) {
            const auto source = std::find_if(
                m_RawWorkspace.sources.begin(), m_RawWorkspace.sources.end(),
                [&](const Stack::RawWorkspace::SourceRecord& candidate) {
                    return candidate.relativePathKey == key;
                });
            if (source != m_RawWorkspace.sources.end()) paths.push_back(source->absolutePath);
        }
        return paths;
    };
    const bool hasMultiSelection = !m_RawWorkspace.selectedSourceKeys.empty();
    ImGui::Spacing();
    ImGui::TextDisabled(
        "%llu selected - Ctrl/Shift-click gallery items to change selection",
        static_cast<unsigned long long>(m_RawWorkspace.selectedSourceKeys.size()));
    if (hasMultiSelection) {
        const bool replacementBusy =
            IsDeferredLoadedProjectApplyActive() ||
            IsRawWorkspaceProjectLoadBusy() ||
            IsMfdExperimentalProcessingBusy();
        Stack::UiActivity::BeginDisabledForWork(replacementBusy);
        if (ImGui::SmallButton("Create Capture Set")) {
            m_PendingMultiFrameCreationIntent =
                Stack::Project::MultiFrameOperationIntent::RawCaptureSet;
            RequestCreateMfdProjectFromGallerySelection();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                replacementBusy
                    ? "Finish the current load or processing run first."
                    : "Create a separate project from the selected RAW files.");
        }
        if (IsMultiFrameRawProjectActive()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Add to Current Burst") &&
                m_Project->snapshot) {
                std::string error;
                if (!AddFramesToMultiFrameSourceSet(
                        m_Project->snapshot->activeSourceSetId,
                        selectedSourcePaths(),
                        &error)) {
                    m_RawWorkspaceLabUi.multiFrameStatusText = error;
                }
            }
        }
    }

    ImGui::Spacing();
    if (scanBusy) {
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1.0f, 0.0f), "");
    } else if (scanSnapshot.state == Async::TaskState::Failed) {
        ImGui::TextWrapped("%s", scanSnapshot.errorMessage.empty()
            ? "Workspace scan failed."
            : scanSnapshot.errorMessage.c_str());
    }

    if (thumbnailBusy) {
        const Stack::RawWorkspace::ThumbnailProgress& progress = thumbnailSnapshot.progress;
        const int denominator = std::max(1, progress.total);
        const float fraction = static_cast<float>(std::clamp(progress.completed + progress.failed, 0, denominator)) /
            static_cast<float>(denominator);
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), "");
    }

    if (m_RawWorkspace.sources.empty() && !scanBusy) {
        return;
    }

    auto renderSourceTile = [&](const Stack::RawWorkspace::SourceRecord& source,
                                 const Stack::RawWorkspace::GallerySourceView& view,
                                 const ImVec2& tileSize,
                                 const ImVec2& imageBounds,
                                bool showMeta) {
        ImGui::PushID(source.relativePathKey.c_str());
        const ImVec2 tileMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##RawSourceTile", tileSize);
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::BeginPopupContextItem("Source Project Memberships")) {
            ImGui::TextUnformatted("Saved Project Versions");
            ImGui::Separator();
            if (source.sourceSetProjectMemberships.empty()) {
                ImGui::TextDisabled("This source has no saved project version.");
            }
            for (const Stack::RawWorkspace::SourceSetProjectMembership& membership :
                 source.sourceSetProjectMemberships) {
                const std::string projectLabel =
                    (membership.projectName.empty()
                        ? membership.projectPath.filename().string()
                        : membership.projectName) +
                    " / " +
                    (membership.sourceSetName.empty()
                        ? std::string("Unnamed Set")
                        : membership.sourceSetName);
                if (ImGui::MenuItem(projectLabel.c_str())) {
                    RequestOpenRawWorkspaceProjectFromGallery(
                        membership.projectPath);
                }
            }
            ImGui::EndPopup();
        }
        if (clicked) {
            const bool doubleClicked = hovered &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (doubleClicked && view.representsProject &&
                !view.projectPath.empty()) {
                RequestOpenRawWorkspaceProjectFromGallery(view.projectPath);
            } else {
                SelectRawWorkspaceSourceForGallery(
                    source.relativePathKey,
                    ImGui::GetIO().KeyCtrl,
                    ImGui::GetIO().KeyShift,
                    doubleClicked);
            }
        }
        if (hovered) {
            ImGui::SetTooltip("%s", source.relativePathKey.c_str());
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect tileRect(
            tileMin,
            ImVec2(tileMin.x + tileSize.x, tileMin.y + tileSize.y));
        const bool selected = view.selected || view.multiSelected ||
            source.relativePathKey == m_RawWorkspace.selectedSourceKey;
        if (hovered || selected) {
            ImU32 tileFill = ImGui::GetColorU32(
                selected ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBgHovered,
                selected ? 0.58f : 0.34f);
            drawList->AddRectFilled(tileRect.Min, tileRect.Max, tileFill, 6.0f);
        }

        int textureWidth = 0;
        int textureHeight = 0;
        Stack::RawWorkspace::SourceRecord thumbnailSource = source;
        if (view.representsProject &&
            !view.projectCoverThumbnailCachePath.empty()) {
            thumbnailSource.relativePathKey =
                "project-overlay:" + view.projectId;
            thumbnailSource.thumbnail.absolutePath =
                view.projectCoverThumbnailCachePath;
            thumbnailSource.thumbnail.status =
                Stack::RawWorkspace::ThumbnailStatus::Ready;
        }
        const unsigned int texture = GetRawWorkspaceThumbnailTexture(
            thumbnailSource, &textureWidth, &textureHeight);
        const ImVec2 imageSize = texture != 0
            ? FitImageSize(static_cast<float>(textureWidth), static_cast<float>(textureHeight), imageBounds)
            : imageBounds;
        const ImVec2 imageMin(
            tileMin.x + (tileSize.x - imageSize.x) * 0.5f,
            tileMin.y + 2.0f);
        const ImRect imageRect(
            imageMin,
            ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
        if (texture != 0) {
            drawList->AddImage(
                (ImTextureID)(intptr_t)texture,
                imageRect.Min,
                imageRect.Max,
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f));
            if (selected) {
                drawList->AddRect(imageRect.Min, imageRect.Max, ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f, 0, 2.0f);
            }
        } else {
            DrawRawPlaceholder(drawList, imageRect, selected);
        }

        const float textWidth = tileSize.x - 10.0f;
        const std::string fileText = EllipsizeTextToWidth(source.fileName, textWidth);
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 disabledColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        drawList->AddText(ImVec2(tileMin.x + 5.0f, imageRect.Max.y + 6.0f), textColor, fileText.c_str());

        if (showMeta) {
            const std::string statusText = EllipsizeTextToWidth(
                std::string(Stack::RawWorkspace::ThumbnailStatusLabel(source.thumbnail.status)) +
                    " / Project " + Stack::RawWorkspace::ProjectStatusLabel(view.projectStatus),
                textWidth);
            drawList->AddText(ImVec2(tileMin.x + 5.0f, imageRect.Max.y + 25.0f), disabledColor, statusText.c_str());
        }
        if (view.savedProjectCount > 0u) {
            const std::string badge = std::to_string(view.savedProjectCount) + " saved edit" +
                (view.savedProjectCount == 1u ? "" : "s");
            drawList->AddText(
                ImVec2(tileMin.x + 5.0f, tileMin.y + 5.0f),
                ImGui::GetColorU32(ImGuiCol_CheckMark),
                badge.c_str());
        }
        ImGui::PopID();
    };

    auto isTileVisible = [](const ImVec2& tileMin, const ImVec2& tileSize) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window == nullptr) {
            return true;
        }
        constexpr float kCullMargin = 240.0f;
        ImRect clip = window->ClipRect;
        clip.Expand(kCullMargin);
        const ImRect tileRect(tileMin, ImVec2(tileMin.x + tileSize.x, tileMin.y + tileSize.y));
        return clip.Overlaps(tileRect);
    };

    auto reserveCulledTile = [](const ImVec2& tileSize) {
        ImGui::Dummy(tileSize);
    };

    auto renderSourceTileIfVisible = [&](const Stack::RawWorkspace::SourceRecord& source,
                                         const Stack::RawWorkspace::GallerySourceView& view,
                                         const ImVec2& tileSize,
                                         const ImVec2& imageBounds,
                                         bool showMeta) {
        if (isTileVisible(ImGui::GetCursorScreenPos(), tileSize)) {
            renderSourceTile(source, view, tileSize, imageBounds, showMeta);
        } else {
            reserveCulledTile(tileSize);
        }
    };

    auto renderGridGallery = [&](bool compactTiles, bool showMetaText) {
        const float tileWidth = compactTiles ? 124.0f : 154.0f;
        const float imageHeight = compactTiles ? 76.0f : 106.0f;
        const float tileHeight = compactTiles ? 124.0f : 136.0f;
        const float gap = compactTiles ? 10.0f : 16.0f;
        const ImVec2 tileSize(tileWidth, tileHeight);
        const ImVec2 imageBounds(tileWidth - 12.0f, imageHeight);

        if (compactTiles) {
            bool firstGroup = true;
            for (const Stack::RawWorkspace::GalleryFolderGroup& group : presentation.groups) {
                if (!firstGroup) {
                    ImGui::SameLine(0.0f, 24.0f);
                }
                firstGroup = false;
                ImGui::BeginGroup();
                ImGui::TextDisabled("%s", group.label.c_str());
                bool firstTile = true;
                for (const Stack::RawWorkspace::GallerySourceView& view : group.sources) {
                    const Stack::RawWorkspace::SourceRecord* source =
                        FindRawWorkspaceSourceByIndex(m_RawWorkspace, view.sourceIndex);
                    if (source == nullptr) {
                        continue;
                    }
                    if (!firstTile) {
                        ImGui::SameLine(0.0f, gap);
                    }
                    firstTile = false;
                    renderSourceTileIfVisible(*source, view, tileSize, imageBounds, false);
                }
                ImGui::EndGroup();
            }
            return;
        }

        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const int columns = std::max(1, static_cast<int>((availableWidth + gap) / (tileWidth + gap)));
        for (const Stack::RawWorkspace::GalleryFolderGroup& group : presentation.groups) {
            ImGui::TextDisabled("%s", group.label.c_str());
            const int sourceCount = static_cast<int>(group.sources.size());
            const int rowCount = (sourceCount + columns - 1) / columns;
            for (int row = 0; row < rowCount; ++row) {
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                const bool rowVisible = isTileVisible(
                    rowMin,
                    ImVec2(availableWidth, tileHeight));
                for (int column = 0; column < columns; ++column) {
                    const int sourceOffset = row * columns + column;
                    if (sourceOffset >= sourceCount) {
                        break;
                    }
                    const Stack::RawWorkspace::GallerySourceView& view =
                        group.sources[static_cast<std::size_t>(sourceOffset)];
                    const Stack::RawWorkspace::SourceRecord* source =
                        FindRawWorkspaceSourceByIndex(m_RawWorkspace, view.sourceIndex);
                    if (source == nullptr) {
                        continue;
                    }
                    if (column > 0) {
                        ImGui::SameLine(0.0f, gap);
                    }
                    if (rowVisible) {
                        renderSourceTile(*source, view, tileSize, imageBounds, showMetaText);
                    } else {
                        reserveCulledTile(tileSize);
                    }
                }
            }
            ImGui::Spacing();
        }
    };

    auto renderListGallery = [&]() {
        for (const Stack::RawWorkspace::GalleryFolderGroup& group : presentation.groups) {
            ImGui::TextDisabled("%s", group.label.c_str());
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(group.sources.size()));
            while (clipper.Step()) {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                    const Stack::RawWorkspace::GallerySourceView& view =
                        group.sources[static_cast<std::size_t>(index)];
                    const Stack::RawWorkspace::SourceRecord* source =
                        FindRawWorkspaceSourceByIndex(m_RawWorkspace, view.sourceIndex);
                    if (source == nullptr) {
                        continue;
                    }
                    ImGui::PushID(source->relativePathKey.c_str());
                    const bool selected = view.selected || view.multiSelected ||
                        source->relativePathKey == m_RawWorkspace.selectedSourceKey;
                    const std::string row =
                        source->fileName + "    " +
                        FormatFileSize(source->fileSizeBytes) + "    " +
                        Stack::RawWorkspace::ThumbnailStatusLabel(source->thumbnail.status) + "    Project " +
                        Stack::RawWorkspace::ProjectStatusLabel(view.projectStatus);
                    if (ImGui::Selectable(row.c_str(), selected)) {
                        SelectRawWorkspaceSourceForGallery(
                            source->relativePathKey,
                            ImGui::GetIO().KeyCtrl,
                            ImGui::GetIO().KeyShift,
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", source->relativePathKey.c_str());
                    }
                    ImGui::PopID();
                }
            }
            ImGui::Spacing();
        }
    };

    auto renderGallery = [&](bool compactTiles, bool showMetaText) {
        if (m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::List && !compactTiles) {
            renderListGallery();
        } else {
            renderGridGallery(compactTiles, showMetaText);
        }
    };

    const Stack::RawWorkspace::RawPanelState panelState =
        Stack::RawWorkspace::BuildRawPanelState(selectedSource);

    ImGui::Spacing();
    if (ImGui::SmallButton("Reset Panels##RawWorkspaceResetDockLayout")) {
        m_RawWorkspaceLayoutUi.dockLayoutInitialized = false;
    }
    TooltipIfHovered("Restore the manual RAW workspace to Controls, Image, and Tone Graphs.");
    const ImVec2 bodyAvail = ImGui::GetContentRegionAvail();
    const ImGuiWindowFlags workspaceFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild(
        "RawWorkspaceDockRegion",
        bodyAvail,
        false,
        workspaceFlags);
    ImGui::PopStyleVar();

    const ImGuiID dockspaceId = ImGui::GetID("RawWorkspaceManualDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    if (!m_RawWorkspaceLayoutUi.dockLayoutInitialized) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, bodyAvail);
        ImGuiID imageDockId = dockspaceId;
        ImGuiID controlsDockId = 0;
        ImGuiID toneDockId = 0;
        ImGui::DockBuilderSplitNode(
            imageDockId,
            ImGuiDir_Left,
            0.27f,
            &controlsDockId,
            &imageDockId);
        ImGui::DockBuilderSplitNode(
            imageDockId,
            ImGuiDir_Right,
            0.30f,
            &toneDockId,
            &imageDockId);
        ImGui::DockBuilderDockWindow("Controls###RawWorkspaceControlsWindow", controlsDockId);
        ImGui::DockBuilderDockWindow("Image###RawWorkspaceImageWindow", imageDockId);
        ImGui::DockBuilderDockWindow("Tone Graphs###RawWorkspaceToneGraphsWindow", toneDockId);
        ImGui::DockBuilderFinish(dockspaceId);
        m_RawWorkspaceLayoutUi.dockLayoutInitialized = true;
    }

    if (ImGui::Begin("Controls###RawWorkspaceControlsWindow")) {
        Stack::AutoBracket::EditingScope autoBracketInput(*this,ImGui::GetCursorScreenPos(),ImGui::GetContentRegionAvail());
        RenderRawWorkspaceControlsPanel(selectedSource, panelState);
    }
    ImGui::End();

    if (ImGui::Begin(
            "Image###RawWorkspaceImageWindow",
            nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        Stack::AutoBracket::EditingScope autoBracketInput(*this,ImGui::GetCursorScreenPos(),ImGui::GetContentRegionAvail());
        RenderRawWorkspacePreviewPanel(selectedSource, panelState);
    }
    ImGui::End();

    if (ImGui::Begin("Tone Graphs###RawWorkspaceToneGraphsWindow")) {
        Stack::AutoBracket::EditingScope autoBracketInput(*this,ImGui::GetCursorScreenPos(),ImGui::GetContentRegionAvail());
        RenderRawWorkspaceToneGraphsPanel(selectedSource, panelState);
    }
    ImGui::End();
    ImGui::EndChild();

    if (m_RawWorkspaceGalleryWindowOpen) {
        const bool wallpaperSurfaces = m_Appearance && m_Appearance->GetSeamlessSurfaceStylingEnabled();
        const StackAppearance::RuntimeSurfacePalette surfacePalette =
            m_Appearance ? m_Appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};
        const ImVec4 popupBg = m_Appearance
            ? m_Appearance->GetEffectivePopupBackgroundColor()
            : ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
        const ImVec4 borderColor = m_Appearance
            ? surfacePalette.border
            : ImGui::GetStyleColorVec4(ImGuiCol_Border);
        const ImVec4 separatorColor = m_Appearance
            ? surfacePalette.separator
            : ImGui::GetStyleColorVec4(ImGuiCol_Separator);
        const ImVec4 closeButton = m_Appearance
            ? surfacePalette.controlSurface
            : ImGui::GetStyleColorVec4(ImGuiCol_Button);
        const ImVec4 closeButtonHovered = m_Appearance
            ? surfacePalette.controlSurfaceHovered
            : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        const ImVec4 closeButtonActive = m_Appearance
            ? surfacePalette.controlSurfaceActive
            : ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 workPos = viewport != nullptr ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
        const ImVec2 workSize = viewport != nullptr ? viewport->WorkSize : ImGui::GetIO().DisplaySize;
        const ImVec2 maxWindowSize(
            std::max(280.0f, workSize.x - 48.0f),
            std::max(220.0f, workSize.y - 72.0f));
        const ImVec2 defaultSize(
            std::min(430.0f, maxWindowSize.x),
            std::min(620.0f, maxWindowSize.y));
        const ImVec2 defaultPos(
            workPos.x + workSize.x - defaultSize.x - 44.0f,
            workPos.y + 86.0f);

        ImGui::SetNextWindowPos(defaultPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(defaultSize, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 220.0f), maxWindowSize);
        if (viewport != nullptr) {
            ImGui::SetNextWindowViewport(viewport->ID);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, wallpaperSurfaces ? 10.0f : ImGui::GetStyle().FrameRounding);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, popupBg);
        ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
        ImGui::PushStyleColor(ImGuiCol_Separator, separatorColor);
        ImGui::PushStyleColor(ImGuiCol_Button, closeButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, closeButtonHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, closeButtonActive);

        const ImGuiWindowFlags galleryFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking;
        bool galleryOpen = m_RawWorkspaceGalleryWindowOpen;
        if (ImGui::Begin("##RawWorkspaceGalleryWindow", &galleryOpen, galleryFlags)) {
            {
                const ImVec2 windowPos = ImGui::GetWindowPos();
                const ImVec2 windowSize = ImGui::GetWindowSize();
                const ImVec2 savedCursorPos = ImGui::GetCursorPos();
                constexpr float headerControlsWidth = 78.0f;

                ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
                ImGui::InvisibleButton(
                    "##RawWorkspaceGalleryHeaderDragZone",
                    ImVec2(std::max(10.0f, windowSize.x - headerControlsWidth - 24.0f), 42.0f));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    ImGui::SetWindowPos(ImVec2(windowPos.x + delta.x, windowPos.y + delta.y));
                }

                ImGui::SetCursorPos(savedCursorPos);
            }

            ImGui::TextUnformatted("Gallery");
            ImGui::SameLine();
                ImGui::SetCursorPosX(std::max(
                    ImGui::GetCursorPosX(),
                    ImGui::GetWindowContentRegionMax().x - 78.0f));
            if (RenderRawWorkspaceIconButton(
                    "##RawWorkspaceGalleryGrid",
                    m_RawGridIconTexture,
                    "Grid View",
                    m_Appearance,
                    m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::Grid)) {
                m_RawWorkspaceGalleryDisplayMode = Stack::RawWorkspace::GalleryDisplayMode::Grid;
            }
            ImGui::SameLine(0.0f, 6.0f);
            if (RenderRawWorkspaceIconButton(
                    "##RawWorkspaceGalleryList",
                    m_RawListIconTexture,
                    "List View",
                    m_Appearance,
                    m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::List)) {
                m_RawWorkspaceGalleryDisplayMode = Stack::RawWorkspace::GalleryDisplayMode::List;
            }
            ImGui::SameLine(0.0f, 6.0f);
            if (RenderRawWorkspaceIconButton(
                    "##RawWorkspaceGalleryClose",
                    m_RawClearIconTexture,
                    "Close Gallery",
                    m_Appearance)) {
                galleryOpen = false;
            }
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 10.0f));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 4.0f));
            if (wallpaperSurfaces) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            }
            ImGui::BeginChild("RawWorkspaceGalleryScroll", ImVec2(0.0f, 0.0f), false);
            if (presentation.totalSources <= 0) {
                ImGui::TextDisabled("Empty");
            } else {
                renderGallery(false, false);
            }
            ImGui::EndChild();
            if (wallpaperSurfaces) {
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleVar();

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                galleryOpen = false;
            }
        }
        ImGui::End();
        m_RawWorkspaceGalleryWindowOpen = galleryOpen;
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(4);
    }
}

void EditorModule::RenderRawWorkspaceUI() {
    LoadResourceTextures();
    EnsureRawWorkspaceLoaded();
    PumpNonRenderingWork(2.5);
    PumpRawWorkspaceThumbnailTextureUploads();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
    ImGui::BeginChild(
        "RawWorkspaceRoot",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Leave the application-level floating navigation and the native caption
    // controls a clean reveal lane. RAW owns the space immediately below it.
    constexpr float kRawWorkspaceFloatingChromeClearance = 42.0f;
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + kRawWorkspaceFloatingChromeClearance);

    const RawWorkspaceScanSnapshot scanSnapshot = GetRawWorkspaceScanSnapshot();
    const RawWorkspaceThumbnailSnapshot thumbnailSnapshot = GetRawWorkspaceThumbnailSnapshot();
    if (m_RawWorkspace.workspaceRoot.empty()) {
        RenderRawWorkspaceEmptyState(scanSnapshot);
    } else {
        RenderRawWorkspaceBrowser(scanSnapshot, thumbnailSnapshot);
    }
    RenderRawWorkspaceLifecyclePopups();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
