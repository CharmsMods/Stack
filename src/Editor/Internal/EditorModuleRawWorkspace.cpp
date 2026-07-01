#include "Editor/EditorModule.h"

#include "App/AppPaths.h"
#include "App/settings/AppearanceTheme.h"
#include "Async/TaskSystem.h"
#include "Raw/RawLoader.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <imgui_internal.h>

#include <algorithm>
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
#include <unordered_set>
#include <vector>

namespace {

constexpr double kRawWorkspaceFastPreviewQuietSeconds = 0.35;
constexpr double kRawWorkspacePersistDebounceSeconds = 0.35;
constexpr int kRawToneCurveMaxPoints = 12;
constexpr float kRawToneCurveHitRadius = 14.0f;
constexpr int kRawLocalRangeMaxPoints = 12;
constexpr float kRawLocalRangeHitRadius = 14.0f;
constexpr float kRawLocalRangeMinDeltaEv = -4.0f;
constexpr float kRawLocalRangeMaxDeltaEv = 4.0f;
constexpr float kRawLocalRangeTargetDragPixelsPerEv = 80.0f;
constexpr float kRawLocalRangeTargetPointToleranceEv = 0.35f;
constexpr std::size_t kRawWorkspaceThumbnailApplyBatchSize = 8;
constexpr std::size_t kRawWorkspaceThumbnailTextureDecodeRequestsPerFrame = 12;
constexpr std::size_t kRawWorkspaceThumbnailTextureUploadsPerFrame = 3;
constexpr std::size_t kRawWorkspaceThumbnailTextureDeletesPerFrame = 8;
constexpr float kRawWorkspaceControlsDefaultWidth = 420.0f;
constexpr float kRawWorkspaceControlsMinWidth = 340.0f;
constexpr float kRawWorkspaceControlsMaxWidth = 520.0f;
constexpr float kRawWorkspaceSplitterWidth = 6.0f;
constexpr float kRawWorkspaceSplitterSideGap = 4.0f;
constexpr float kRawWorkspacePreviewMinWidth = 320.0f;

struct RawWorkspaceThumbnailUpdate {
    std::size_t sourceIndex = 0;
    std::string sourceKey;
    Stack::RawWorkspace::ThumbnailInfo thumbnail;
};

struct RawWorkspaceThumbnailWorkItem {
    std::size_t sourceIndex = 0;
    Stack::RawWorkspace::SourceRecord source;
};

double RawWorkspaceClockSeconds() {
    return ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
}

bool RawWorkspaceDebounceElapsed(double dirtyTime) {
    if (!ImGui::GetCurrentContext() || dirtyTime < 0.0) {
        return true;
    }
    return (ImGui::GetTime() - dirtyTime) >= kRawWorkspacePersistDebounceSeconds;
}

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

std::string FormatRawWorkspaceSignedEv(float value) {
    std::ostringstream out;
    out << (value >= 0.0f ? "+" : "") << std::fixed << std::setprecision(2) << value << " EV";
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
        case Stack::RawWorkspace::RawProjectMode::RecipeBacked:
            return "Recipe";
        case Stack::RawWorkspace::RawProjectMode::ManagedDecomposed:
            return "Managed graph";
        case Stack::RawWorkspace::RawProjectMode::CustomGraph:
            return "Custom graph";
        case Stack::RawWorkspace::RawProjectMode::Unknown:
        default:
            return "Graph unknown";
    }
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

int FindDirectDownstreamRawDecode(const EditorNodeGraph::Graph& graph, int rawNodeId) {
    if (rawNodeId <= 0) {
        return -1;
    }

    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        if (link.fromNodeId != rawNodeId ||
            link.fromSocketId != EditorNodeGraph::kRawOutputSocketId ||
            link.toSocketId != EditorNodeGraph::kRawInputSocketId) {
            continue;
        }

        const EditorNodeGraph::Node* downstream = graph.FindNode(link.toNodeId);
        if (downstream && downstream->kind == EditorNodeGraph::NodeKind::RawDecode) {
            return downstream->id;
        }
    }

    return -1;
}

int FindUpstreamRawDecode(const EditorNodeGraph::Graph& graph, int nodeId) {
    std::unordered_set<int> visited;
    int currentNodeId = nodeId;

    for (int depth = 0; depth < 64 && currentNodeId > 0; ++depth) {
        if (!visited.insert(currentNodeId).second) {
            break;
        }

        const EditorNodeGraph::Node* node = graph.FindNode(currentNodeId);
        if (!node) {
            break;
        }

        if (node->kind == EditorNodeGraph::NodeKind::RawDecode) {
            return node->id;
        }

        if (node->kind == EditorNodeGraph::NodeKind::RawSource ||
            node->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            const int downstreamRawDecode = FindDirectDownstreamRawDecode(graph, node->id);
            return downstreamRawDecode > 0 ? downstreamRawDecode : -1;
        }

        const EditorNodeGraph::Link* input = graph.FindInputLink(
            node->id,
            node->kind == EditorNodeGraph::NodeKind::RawDevelop
                ? EditorNodeGraph::kRawInputSocketId
                : EditorNodeGraph::kImageInputSocketId);
        if (!input) {
            break;
        }
        currentNodeId = input->fromNodeId;
    }

    return -1;
}

int FindFirstNodeOfKind(const EditorNodeGraph::Graph& graph, EditorNodeGraph::NodeKind kind) {
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == kind) {
            return node.id;
        }
    }
    return -1;
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

bool NearlyEqual(float a, float b, float epsilon = 0.001f) {
    return std::abs(a - b) <= epsilon;
}

bool RawLocalExposureLooksUntouched(const Stack::RawRecipe::RawLocalExposureRecipe& localExposure) {
    return !localExposure.enabled &&
        (NearlyEqual(localExposure.amount, 0.85f) ||
            NearlyEqual(localExposure.amount, 0.35f) ||
            NearlyEqual(localExposure.amount, 1.0f)) &&
        NearlyEqual(localExposure.shadowLiftEv, 0.0f) &&
        NearlyEqual(localExposure.highlightCompressionEv, 0.0f) &&
        NearlyEqual(localExposure.localBaselineEv, 0.0f) &&
        NearlyEqual(localExposure.noiseGuardBias, 0.0f) &&
        NearlyEqual(localExposure.highlightGuardBias, 0.0f) &&
        NearlyEqual(localExposure.shadowGuardBias, 0.0f) &&
        NearlyEqual(localExposure.smoothGradientProtection, 0.85f) &&
        NearlyEqual(localExposure.haloGuard, 0.90f);
}

float ProtectDetailFromLocalExposure(const Stack::RawRecipe::RawLocalExposureRecipe& localExposure) {
    const float noise = std::clamp(localExposure.noiseGuardBias * 0.5f + 0.5f, 0.0f, 1.0f);
    const float highlight = std::clamp(localExposure.highlightGuardBias * 0.5f + 0.5f, 0.0f, 1.0f);
    const float shadow = std::clamp(localExposure.shadowGuardBias * 0.5f + 0.5f, 0.0f, 1.0f);
    const float gradient = std::clamp(localExposure.smoothGradientProtection, 0.0f, 1.0f);
    const float edge = std::clamp(localExposure.haloGuard, 0.0f, 1.0f);
    return std::clamp((noise + highlight + shadow + gradient + edge) * 0.20f, 0.0f, 1.0f);
}

void ApplyProtectDetailToLocalExposure(
    Stack::RawRecipe::RawLocalExposureRecipe& localExposure,
    float protectDetail) {
    protectDetail = std::clamp(protectDetail, 0.0f, 1.0f);
    const float guardBias = protectDetail * 2.0f - 1.0f;
    localExposure.noiseGuardBias = guardBias;
    localExposure.highlightGuardBias = guardBias;
    localExposure.shadowGuardBias = guardBias;
    localExposure.smoothGradientProtection = std::clamp(0.25f + protectDetail * 0.75f, 0.0f, 1.0f);
    localExposure.haloGuard = std::clamp(0.35f + protectDetail * 0.65f, 0.0f, 1.0f);
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

int WhiteBalanceModeToIndex(Stack::RawRecipe::WhiteBalanceMode mode) {
    switch (mode) {
        case Stack::RawRecipe::WhiteBalanceMode::Auto: return 1;
        case Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers: return 2;
        case Stack::RawRecipe::WhiteBalanceMode::SampledGrayPoint: return 3;
        case Stack::RawRecipe::WhiteBalanceMode::AsShot:
        default:
            return 0;
    }
}

Stack::RawRecipe::WhiteBalanceMode WhiteBalanceModeFromIndex(int index) {
    switch (std::clamp(index, 0, 3)) {
        case 1: return Stack::RawRecipe::WhiteBalanceMode::Auto;
        case 2: return Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers;
        case 3: return Stack::RawRecipe::WhiteBalanceMode::SampledGrayPoint;
        case 0:
        default:
            return Stack::RawRecipe::WhiteBalanceMode::AsShot;
    }
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

Stack::RawRecipe::RawLocalRangeRecipe BuildLocalRangeUiRecipe(
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange) {
    Stack::RawRecipe::RawLocalRangeRecipe recipe =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    const float minEv = recipe.minEv;
    const float maxEv = recipe.maxEv;
    if (recipe.points.empty()) {
        recipe.points = Stack::RawRecipe::DefaultLocalRangePoints(minEv, maxEv);
    }

    std::sort(recipe.points.begin(), recipe.points.end(), [](const auto& a, const auto& b) {
        return a.ev < b.ev;
    });
    if (recipe.points.empty() || recipe.points.front().ev > minEv + 0.001f) {
        recipe.points.insert(recipe.points.begin(), { minEv, 0.0f });
    } else {
        recipe.points.front().ev = minEv;
    }
    if (recipe.points.size() == 1 || recipe.points.back().ev < maxEv - 0.001f) {
        recipe.points.push_back({ maxEv, 0.0f });
    } else {
        recipe.points.back().ev = maxEv;
    }
    while (recipe.points.size() > static_cast<std::size_t>(kRawLocalRangeMaxPoints)) {
        recipe.points.erase(recipe.points.end() - 2);
    }
    return Stack::RawRecipe::SanitizeLocalRangeRecipe(recipe);
}

bool DrawLocalRangeWidget(
    Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    const ImVec2& size,
    int* outSelectedPoint,
    const float* sampledSceneEv = nullptr) {
    static int selectedPoint = -1;
    static int draggingPoint = -1;
    static int contextPoint = -1;

    bool changed = false;
    localRange = BuildLocalRangeUiRecipe(localRange);
    std::vector<Stack::RawRecipe::RawLocalRangePoint>& points = localRange.points;
    if (selectedPoint >= static_cast<int>(points.size())) {
        selectedPoint = -1;
    }
    if (draggingPoint >= static_cast<int>(points.size())) {
        draggingPoint = -1;
    }

    ImGui::InvisibleButton("##RawLocalRangeGraph", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 grid = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.28f);
    const ImU32 axis = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.55f);
    const ImU32 curve = ImGui::GetColorU32(ImGuiCol_CheckMark);
    drawList->AddRectFilled(min, max, bg, 5.0f);
    drawList->AddRect(min, max, border, 5.0f);

    const float labelPad = std::max(22.0f, ImGui::GetFontSize() + 6.0f);
    const ImRect plot(
        ImVec2(min.x + 34.0f, min.y + 14.0f),
        ImVec2(max.x - 10.0f, max.y - labelPad));
    drawList->AddRect(plot.Min, plot.Max, ImGui::GetColorU32(ImGuiCol_Border, 0.55f), 3.0f);

    const float evRange = std::max(0.1f, localRange.maxEv - localRange.minEv);
    const float deltaRange = kRawLocalRangeMaxDeltaEv - kRawLocalRangeMinDeltaEv;
    auto toScreen = [&](const Stack::RawRecipe::RawLocalRangePoint& point) {
        const float xNorm = std::clamp((point.ev - localRange.minEv) / evRange, 0.0f, 1.0f);
        const float yNorm = std::clamp((point.deltaEv - kRawLocalRangeMinDeltaEv) / deltaRange, 0.0f, 1.0f);
        return ImVec2(
            plot.Min.x + plot.GetWidth() * xNorm,
            plot.Max.y - plot.GetHeight() * yNorm);
    };
    auto fromScreen = [&](const ImVec2& screen) {
        const float xNorm = std::clamp((screen.x - plot.Min.x) / std::max(1.0f, plot.GetWidth()), 0.0f, 1.0f);
        const float yNorm = std::clamp((plot.Max.y - screen.y) / std::max(1.0f, plot.GetHeight()), 0.0f, 1.0f);
        return Stack::RawRecipe::RawLocalRangePoint{
            localRange.minEv + evRange * xNorm,
            kRawLocalRangeMinDeltaEv + deltaRange * yNorm
        };
    };
    auto isEndpoint = [&](int index) {
        return index == 0 || index == static_cast<int>(points.size()) - 1;
    };

    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float x = plot.Min.x + plot.GetWidth() * t;
        const float y = plot.Min.y + plot.GetHeight() * t;
        drawList->AddLine(ImVec2(x, plot.Min.y), ImVec2(x, plot.Max.y), grid, 1.0f);
        drawList->AddLine(ImVec2(plot.Min.x, y), ImVec2(plot.Max.x, y), grid, 1.0f);
    }
    const float zeroY = toScreen({ localRange.minEv, 0.0f }).y;
    drawList->AddLine(ImVec2(plot.Min.x, zeroY), ImVec2(plot.Max.x, zeroY), axis, 1.25f);
    if (sampledSceneEv && std::isfinite(*sampledSceneEv)) {
        const float sampleXNorm = std::clamp((*sampledSceneEv - localRange.minEv) / evRange, 0.0f, 1.0f);
        const float sampleX = plot.Min.x + plot.GetWidth() * sampleXNorm;
        const ImU32 markerColor = ImGui::GetColorU32(ImGuiCol_Text, 0.85f);
        drawList->AddLine(
            ImVec2(sampleX, plot.Min.y),
            ImVec2(sampleX, plot.Max.y),
            markerColor,
            1.5f);
        drawList->AddCircleFilled(ImVec2(sampleX, zeroY), 4.0f, markerColor, 16);
    }

    auto addSmallText = [&](const ImVec2& pos, const char* text) {
        drawList->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    };
    char labelBuffer[64] = {};
    snprintf(labelBuffer, sizeof(labelBuffer), "%+.0f", kRawLocalRangeMaxDeltaEv);
    addSmallText(ImVec2(min.x + 6.0f, plot.Min.y - 4.0f), labelBuffer);
    snprintf(labelBuffer, sizeof(labelBuffer), "%+.0f", 0.0f);
    addSmallText(ImVec2(min.x + 6.0f, zeroY - ImGui::GetFontSize() * 0.5f), labelBuffer);
    snprintf(labelBuffer, sizeof(labelBuffer), "%+.0f", kRawLocalRangeMinDeltaEv);
    addSmallText(ImVec2(min.x + 6.0f, plot.Max.y - ImGui::GetFontSize() + 2.0f), labelBuffer);
    snprintf(labelBuffer, sizeof(labelBuffer), "%.0f EV", localRange.minEv);
    addSmallText(ImVec2(plot.Min.x, plot.Max.y + 4.0f), labelBuffer);
    addSmallText(ImVec2(plot.Min.x + plot.GetWidth() * 0.5f - 18.0f, plot.Max.y + 4.0f), "Scene EV");
    snprintf(labelBuffer, sizeof(labelBuffer), "%+.0f EV", localRange.maxEv);
    const ImVec2 rightLabelSize = ImGui::CalcTextSize(labelBuffer);
    addSmallText(ImVec2(plot.Max.x - rightLabelSize.x, plot.Max.y + 4.0f), labelBuffer);

    auto findPointNearMouse = [&]() {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int bestIndex = -1;
        float bestDist2 = kRawLocalRangeHitRadius * kRawLocalRangeHitRadius;
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
    auto sortAndFind = [&](float ev, float deltaEv) {
        localRange = BuildLocalRangeUiRecipe(localRange);
        int bestIndex = -1;
        float bestDist = 100.0f;
        for (int i = 0; i < static_cast<int>(localRange.points.size()); ++i) {
            const float dist =
                std::abs(localRange.points[static_cast<std::size_t>(i)].ev - ev) +
                std::abs(localRange.points[static_cast<std::size_t>(i)].deltaEv - deltaEv);
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
            draggingPoint = hoveredPoint;
        } else if (points.size() < static_cast<std::size_t>(kRawLocalRangeMaxPoints)) {
            const Stack::RawRecipe::RawLocalRangePoint point = fromScreen(ImGui::GetIO().MousePos);
            points.push_back(point);
            localRange.enabled = true;
            selectedPoint = sortAndFind(point.ev, point.deltaEv);
            draggingPoint = selectedPoint;
            changed = true;
        }
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        draggingPoint >= 0 &&
        draggingPoint < static_cast<int>(points.size())) {
        Stack::RawRecipe::RawLocalRangePoint point = fromScreen(ImGui::GetIO().MousePos);
        if (isEndpoint(draggingPoint)) {
            point.ev = draggingPoint == 0 ? localRange.minEv : localRange.maxEv;
        } else {
            const float left = points[static_cast<std::size_t>(draggingPoint - 1)].ev + 0.001f;
            const float right = points[static_cast<std::size_t>(draggingPoint + 1)].ev - 0.001f;
            point.ev = std::clamp(point.ev, left, right);
        }
        point.deltaEv = std::clamp(point.deltaEv, kRawLocalRangeMinDeltaEv, kRawLocalRangeMaxDeltaEv);
        points[static_cast<std::size_t>(draggingPoint)] = point;
        selectedPoint = draggingPoint;
        localRange.enabled = true;
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
        localRange.enabled = true;
        changed = true;
    }
    if (hovered && hoveredPoint > 0 &&
        hoveredPoint < static_cast<int>(points.size()) - 1 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        selectedPoint = hoveredPoint;
        contextPoint = hoveredPoint;
        ImGui::OpenPopup("RawLocalRangePointMenu");
    }
    if (ImGui::BeginPopup("RawLocalRangePointMenu")) {
        if (contextPoint > 0 && contextPoint < static_cast<int>(points.size()) - 1) {
            const Stack::RawRecipe::RawLocalRangePoint point = points[static_cast<std::size_t>(contextPoint)];
            ImGui::TextDisabled("Scene %.2f EV, %+.2f EV", point.ev, point.deltaEv);
            if (ImGui::MenuItem("Delete Point")) {
                points.erase(points.begin() + contextPoint);
                selectedPoint = -1;
                draggingPoint = -1;
                contextPoint = -1;
                localRange.enabled = true;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }

    if (changed) {
        localRange = BuildLocalRangeUiRecipe(localRange);
        if (draggingPoint >= static_cast<int>(localRange.points.size())) {
            draggingPoint = -1;
        }
    }

    for (std::size_t i = 1; i < points.size(); ++i) {
        drawList->AddLine(toScreen(points[i - 1]), toScreen(points[i]), curve, 2.0f);
    }
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const ImVec2 point = toScreen(points[static_cast<std::size_t>(i)]);
        const bool selected = i == selectedPoint;
        const bool endpoint = isEndpoint(i);
        const ImU32 pointColor = selected
            ? ImGui::GetColorU32(ImGuiCol_Text)
            : (endpoint ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : curve);
        drawList->AddCircleFilled(point, selected ? 6.0f : 4.5f, pointColor, 18);
        drawList->AddCircle(point, selected ? 7.5f : 5.8f, IM_COL32(0, 0, 0, 145), 18, 1.0f);
    }
    if (outSelectedPoint) {
        *outSelectedPoint =
            selectedPoint >= 0 && selectedPoint < static_cast<int>(points.size())
                ? selectedPoint
                : -1;
    }
    return changed;
}

bool DrawToneCurveWidget(std::vector<Stack::RawRecipe::RawToneCurvePoint>& points, const ImVec2& size) {
    static int selectedPoint = -1;
    static int draggingPoint = -1;
    static int contextPoint = -1;

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

bool EditorModule::FocusRawWorkspace() {
    SwitchToSubWindow(EditorSubWindow::NodeGraph);

    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    const int selectedRawDecodeId = FindUpstreamRawDecode(m_NodeGraph, selectedNodeId);
    if (selectedRawDecodeId > 0) {
        SelectGraphNode(selectedRawDecodeId);
        return true;
    }

    const int firstRawDecodeId = FindFirstNodeOfKind(m_NodeGraph, EditorNodeGraph::NodeKind::RawDecode);
    if (firstRawDecodeId > 0) {
        SelectGraphNode(firstRawDecodeId);
        return true;
    }

    const int firstRawSourceId = FindFirstNodeOfKind(m_NodeGraph, EditorNodeGraph::NodeKind::RawSource);
    if (firstRawSourceId > 0) {
        SelectGraphNode(firstRawSourceId);
        return true;
    }

    return false;
}

std::filesystem::path EditorModule::GetRawWorkspaceAppStatePath() const {
    return AppPaths::GetSettingsDirectory() / "RawWorkspaceState.json";
}

EditorModule::RawWorkspaceScanSnapshot EditorModule::GetRawWorkspaceScanSnapshot() const {
    std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
    return m_RawWorkspaceScanSnapshot;
}

EditorModule::RawWorkspaceThumbnailSnapshot EditorModule::GetRawWorkspaceThumbnailSnapshot() const {
    std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
    return m_RawWorkspaceThumbnailSnapshot;
}

void EditorModule::EnsureRawWorkspaceLoaded() {
    if (!m_RawWorkspaceAppStateLoaded) {
        LoadRawWorkspaceAppState();
    }
}

void EditorModule::OpenRawWorkspaceFolderDialog() {
    EnsureRawWorkspaceLoaded();
    const std::string path = FileDialogs::OpenFolderDialog("Open RAW Folder");
    if (!path.empty()) {
        RequestOpenRawWorkspace(path);
    }
}

void EditorModule::RescanRawWorkspace() {
    EnsureRawWorkspaceLoaded();
    RequestRawWorkspaceScan();
}

void EditorModule::ClearRawWorkspaceForUser() {
    EnsureRawWorkspaceLoaded();
    ClearRawWorkspace();
}

void EditorModule::SelectRawWorkspaceSourceForPreview(const std::string& sourceKey) {
    EnsureRawWorkspaceLoaded();
    SelectRawWorkspaceSource(sourceKey);
}

bool EditorModule::IsRawWorkspaceScanBusy() const {
    return Async::IsBusy(GetRawWorkspaceScanSnapshot().state);
}

bool EditorModule::IsRawWorkspaceThumbnailBusy() const {
    return Async::IsBusy(GetRawWorkspaceThumbnailSnapshot().state);
}

void EditorModule::InvalidateRawWorkspaceGalleryPresentation() {
    ++m_RawWorkspaceGalleryRevision;
}

const Stack::RawWorkspace::GalleryPresentation& EditorModule::GetRawWorkspaceGalleryPresentation() {
    if (m_RawWorkspaceGalleryPresentationRevision != m_RawWorkspaceGalleryRevision) {
        m_RawWorkspaceGalleryPresentationCache =
            Stack::RawWorkspace::BuildGalleryPresentation(m_RawWorkspace);
        m_RawWorkspaceGalleryPresentationRevision = m_RawWorkspaceGalleryRevision;
    }
    return m_RawWorkspaceGalleryPresentationCache;
}

std::string EditorModule::GetRawWorkspaceScanStatusText() const {
    return GetRawWorkspaceScanSnapshot().statusText;
}

std::string EditorModule::GetRawWorkspaceThumbnailStatusText() const {
    return GetRawWorkspaceThumbnailSnapshot().statusText;
}

std::string EditorModule::GetRawWorkspaceProgramBarStatus() const {
    if (m_RawWorkspace.workspaceRoot.empty()) {
        return "RAW Workspace";
    }

    const RawWorkspaceScanSnapshot scanSnapshot = GetRawWorkspaceScanSnapshot();
    const RawWorkspaceThumbnailSnapshot thumbnailSnapshot = GetRawWorkspaceThumbnailSnapshot();
    std::vector<std::string> parts;

    std::filesystem::path workspaceName = m_RawWorkspace.workspaceRoot.filename();
    if (workspaceName.empty()) {
        workspaceName = m_RawWorkspace.workspaceRoot;
    }
    parts.emplace_back("RAW: " + workspaceName.string());
    parts.emplace_back(std::to_string(m_RawWorkspace.sources.size()) + " files");

    const Stack::RawWorkspace::SourceRecord* selectedSource =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (selectedSource != nullptr) {
        parts.emplace_back(selectedSource->fileName);
        const char* projectLabel =
            Stack::RawWorkspace::ProjectStatusLabel(selectedSource->project.status);
        if (projectLabel != nullptr && projectLabel[0] != '\0') {
            parts.emplace_back(projectLabel);
        }
    }

    std::string busyText;
    if (Async::IsBusy(scanSnapshot.state)) {
        busyText = scanSnapshot.statusText.empty() ? "Scanning" : scanSnapshot.statusText;
    } else if (Async::IsBusy(thumbnailSnapshot.state)) {
        const Stack::RawWorkspace::ThumbnailProgress& progress = thumbnailSnapshot.progress;
        busyText = "Thumbnails " +
            std::to_string(std::clamp(progress.completed + progress.failed, 0, std::max(1, progress.total))) +
            "/" +
            std::to_string(std::max(1, progress.total));
    } else if (IsRawWorkspaceProjectLoadBusy()) {
        busyText = GetRawWorkspaceProjectLoadStatusText();
        if (busyText.empty()) {
            busyText = "Loading project";
        }
    } else if (IsRawWorkspaceProjectSaveBusy()) {
        busyText = m_RawWorkspaceProjectSaveStatusText.empty()
            ? "Saving project"
            : m_RawWorkspaceProjectSaveStatusText;
    } else if (selectedSource != nullptr &&
        IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == selectedSource->relativePathKey &&
        (IsEditorRenderBusy() || m_RenderDirty || m_RawWorkspaceFullResolutionPreviewPending)) {
        busyText = "Rendering";
    }
    if (!busyText.empty()) {
        parts.emplace_back(busyText);
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out << " | ";
        }
        out << parts[i];
    }
    return out.str();
}

Stack::RawWorkspace::ScanProgress EditorModule::GetRawWorkspaceScanProgress() const {
    return GetRawWorkspaceScanSnapshot().progress;
}

Stack::RawWorkspace::ThumbnailProgress EditorModule::GetRawWorkspaceThumbnailProgress() const {
    return GetRawWorkspaceThumbnailSnapshot().progress;
}

unsigned int EditorModule::GetRawWorkspaceThumbnailTexture(
    const Stack::RawWorkspace::SourceRecord& source,
    int* outWidth,
    int* outHeight) {
    if (outWidth) {
        *outWidth = 0;
    }
    if (outHeight) {
        *outHeight = 0;
    }

    const auto clearCached = [&]() {
        auto it = m_RawWorkspaceThumbnailTextures.find(source.relativePathKey);
        if (it != m_RawWorkspaceThumbnailTextures.end()) {
            if (it->second.texture != 0) {
                QueueRawWorkspaceThumbnailTextureDelete(it->second.texture);
                it->second.texture = 0;
            }
            m_RawWorkspaceThumbnailTextures.erase(it);
        }
    };

    if (source.thumbnail.status != Stack::RawWorkspace::ThumbnailStatus::Valid &&
        source.thumbnail.status != Stack::RawWorkspace::ThumbnailStatus::Ready) {
        clearCached();
        return 0;
    }

    if (source.thumbnail.absolutePath.empty()) {
        clearCached();
        return 0;
    }

    auto existing = m_RawWorkspaceThumbnailTextures.find(source.relativePathKey);
    if (existing != m_RawWorkspaceThumbnailTextures.end()) {
        const bool cacheMatches =
            existing->second.absolutePath == source.thumbnail.absolutePath &&
            existing->second.status == source.thumbnail.status;
        if (!cacheMatches) {
            clearCached();
            existing = m_RawWorkspaceThumbnailTextures.end();
        } else if (existing->second.texture != 0) {
            if (outWidth) {
                *outWidth = existing->second.width;
            }
            if (outHeight) {
                *outHeight = existing->second.height;
            }
            return existing->second.texture;
        }
    }

    if (existing == m_RawWorkspaceThumbnailTextures.end()) {
        RawWorkspaceThumbnailTexture entry;
        entry.absolutePath = source.thumbnail.absolutePath;
        entry.status = source.thumbnail.status;
        existing = m_RawWorkspaceThumbnailTextures.emplace(source.relativePathKey, std::move(entry)).first;
    }

    RawWorkspaceThumbnailTexture& entry = existing->second;
    if (entry.decodeState == Async::TaskState::Queued ||
        entry.decodeState == Async::TaskState::Running ||
        entry.uploadPending) {
        return 0;
    }
    if (entry.decodeState == Async::TaskState::Failed) {
        return 0;
    }

    if (ImGui::GetCurrentContext()) {
        const int frame = ImGui::GetFrameCount();
        if (frame != m_RawWorkspaceThumbnailTextureRequestFrame) {
            m_RawWorkspaceThumbnailTextureRequestFrame = frame;
            m_RawWorkspaceThumbnailTextureRequestsThisFrame = 0;
        }
    }
    if (m_RawWorkspaceThumbnailTextureRequestsThisFrame >=
        kRawWorkspaceThumbnailTextureDecodeRequestsPerFrame) {
        return 0;
    }
    ++m_RawWorkspaceThumbnailTextureRequestsThisFrame;

    const std::uint64_t requestGeneration = ++m_RawWorkspaceThumbnailTextureRequestGeneration;
    const std::uint64_t resetGeneration =
        m_RawWorkspaceThumbnailTextureResetGeneration.load(std::memory_order_relaxed);
    entry.requestGeneration = requestGeneration;
    entry.decodeState = Async::TaskState::Queued;
    entry.uploadQueued = false;
    entry.decodedPixels.clear();
    entry.decodedWidth = 0;
    entry.decodedHeight = 0;

    const std::string sourceKey = source.relativePathKey;
    const std::filesystem::path thumbnailPath = source.thumbnail.absolutePath;
    const Stack::RawWorkspace::ThumbnailStatus thumbnailStatus = source.thumbnail.status;
    Async::TaskSystem::Get().Submit([
        this,
        requestGeneration,
        resetGeneration,
        sourceKey,
        thumbnailPath,
        thumbnailStatus
    ]() mutable {
        auto resetRequested = [this, resetGeneration]() {
            return resetGeneration !=
                m_RawWorkspaceThumbnailTextureResetGeneration.load(std::memory_order_relaxed);
        };
        if (resetRequested()) {
            return;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> decodedPixels;
        stbi_set_flip_vertically_on_load_thread(1);
        unsigned char* pixels = stbi_load(thumbnailPath.string().c_str(), &width, &height, &channels, 4);
        if (resetRequested()) {
            if (pixels != nullptr) {
                stbi_image_free(pixels);
            }
            return;
        }
        if (pixels != nullptr && width > 0 && height > 0) {
            const std::size_t byteCount =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
            decodedPixels.assign(pixels, pixels + byteCount);
        }
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        if (resetRequested()) {
            return;
        }

        Async::TaskSystem::Get().PostToMain([
            this,
            requestGeneration,
            sourceKey,
            thumbnailPath,
            thumbnailStatus,
            width,
            height,
            decodedPixels = std::move(decodedPixels)
        ]() mutable {
            auto it = m_RawWorkspaceThumbnailTextures.find(sourceKey);
            if (it == m_RawWorkspaceThumbnailTextures.end()) {
                return;
            }
            RawWorkspaceThumbnailTexture& target = it->second;
            if (target.requestGeneration != requestGeneration ||
                target.absolutePath != thumbnailPath ||
                target.status != thumbnailStatus) {
                return;
            }
            if (decodedPixels.empty() || width <= 0 || height <= 0) {
                target.decodeState = Async::TaskState::Failed;
                target.uploadPending = false;
                target.uploadQueued = false;
                target.decodedPixels.clear();
                target.decodedWidth = 0;
                target.decodedHeight = 0;
                return;
            }

            target.decodeState = Async::TaskState::Applying;
            target.uploadPending = true;
            if (!target.uploadQueued) {
                m_RawWorkspaceThumbnailTextureUploadQueue.push_back(sourceKey);
                target.uploadQueued = true;
            }
            target.decodedPixels = std::move(decodedPixels);
            target.decodedWidth = width;
            target.decodedHeight = height;
        });
    });

    return 0;
}

void EditorModule::PumpRawWorkspaceThumbnailTextureUploads() {
    TickRawWorkspacePersistence();
    TickRawWorkspacePreviewStaging();
    PumpRawWorkspaceThumbnailTextureDeletes();

    if (m_RawWorkspaceThumbnailTextureUploadQueue.empty()) {
        return;
    }

    if (ImGui::GetCurrentContext()) {
        const int frame = ImGui::GetFrameCount();
        if (frame != m_RawWorkspaceThumbnailTextureUploadFrame) {
            m_RawWorkspaceThumbnailTextureUploadFrame = frame;
            m_RawWorkspaceThumbnailTextureUploadsThisFrame = 0;
        }
    }

    while (m_RawWorkspaceThumbnailTextureUploadsThisFrame <
        kRawWorkspaceThumbnailTextureUploadsPerFrame &&
        !m_RawWorkspaceThumbnailTextureUploadQueue.empty()) {
        const std::string sourceKey = std::move(m_RawWorkspaceThumbnailTextureUploadQueue.front());
        m_RawWorkspaceThumbnailTextureUploadQueue.pop_front();
        auto it = m_RawWorkspaceThumbnailTextures.find(sourceKey);
        if (it == m_RawWorkspaceThumbnailTextures.end()) {
            continue;
        }
        RawWorkspaceThumbnailTexture& pendingEntry = it->second;
        pendingEntry.uploadQueued = false;
        if (!pendingEntry.uploadPending ||
            pendingEntry.decodedPixels.empty() ||
            pendingEntry.decodedWidth <= 0 ||
            pendingEntry.decodedHeight <= 0) {
            pendingEntry.uploadPending = false;
            continue;
        }

        if (pendingEntry.texture != 0) {
            QueueRawWorkspaceThumbnailTextureDelete(pendingEntry.texture);
            pendingEntry.texture = 0;
        }
        const unsigned int texture = GLHelpers::CreateTextureFromPixels(
            pendingEntry.decodedPixels.data(),
            pendingEntry.decodedWidth,
            pendingEntry.decodedHeight,
            4);
        pendingEntry.decodedPixels.clear();
        pendingEntry.uploadPending = false;
        if (texture == 0) {
            pendingEntry.decodeState = Async::TaskState::Failed;
            pendingEntry.decodedWidth = 0;
            pendingEntry.decodedHeight = 0;
            return;
        }

        pendingEntry.texture = texture;
        pendingEntry.width = pendingEntry.decodedWidth;
        pendingEntry.height = pendingEntry.decodedHeight;
        pendingEntry.decodedWidth = 0;
        pendingEntry.decodedHeight = 0;
        pendingEntry.decodeState = Async::TaskState::Idle;
        ++m_RawWorkspaceThumbnailTextureUploadsThisFrame;
    }
}

void EditorModule::QueueRawWorkspaceThumbnailTextureDelete(unsigned int texture) {
    if (texture != 0) {
        m_RawWorkspaceThumbnailTextureDeleteQueue.push_back(texture);
    }
}

void EditorModule::PumpRawWorkspaceThumbnailTextureDeletes(bool drainAll) {
    if (m_RawWorkspaceThumbnailTextureDeleteQueue.empty()) {
        return;
    }

    if (drainAll) {
        while (!m_RawWorkspaceThumbnailTextureDeleteQueue.empty()) {
            unsigned int texture = m_RawWorkspaceThumbnailTextureDeleteQueue.front();
            m_RawWorkspaceThumbnailTextureDeleteQueue.pop_front();
            if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
        }
        m_RawWorkspaceThumbnailTextureDeleteFrame = -1;
        m_RawWorkspaceThumbnailTextureDeletesThisFrame = 0;
        return;
    }

    if (ImGui::GetCurrentContext()) {
        const int frame = ImGui::GetFrameCount();
        if (frame != m_RawWorkspaceThumbnailTextureDeleteFrame) {
            m_RawWorkspaceThumbnailTextureDeleteFrame = frame;
            m_RawWorkspaceThumbnailTextureDeletesThisFrame = 0;
        }
    }

    while (!m_RawWorkspaceThumbnailTextureDeleteQueue.empty() &&
        m_RawWorkspaceThumbnailTextureDeletesThisFrame <
            kRawWorkspaceThumbnailTextureDeletesPerFrame) {
        unsigned int texture = m_RawWorkspaceThumbnailTextureDeleteQueue.front();
        m_RawWorkspaceThumbnailTextureDeleteQueue.pop_front();
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
        ++m_RawWorkspaceThumbnailTextureDeletesThisFrame;
    }
}

void EditorModule::ClearRawWorkspaceThumbnailTextures(bool immediate) {
    m_RawWorkspaceThumbnailTextureResetGeneration.fetch_add(1, std::memory_order_relaxed);
    ++m_RawWorkspaceThumbnailTextureRequestGeneration;
    m_RawWorkspaceThumbnailTextureRequestFrame = -1;
    m_RawWorkspaceThumbnailTextureRequestsThisFrame = 0;
    m_RawWorkspaceThumbnailTextureUploadQueue.clear();
    m_RawWorkspaceThumbnailTextureUploadFrame = -1;
    m_RawWorkspaceThumbnailTextureUploadsThisFrame = 0;
    m_RawWorkspaceThumbnailTextureDeleteFrame = -1;
    m_RawWorkspaceThumbnailTextureDeletesThisFrame = 0;
    for (auto& [key, entry] : m_RawWorkspaceThumbnailTextures) {
        (void)key;
        if (entry.texture != 0) {
            QueueRawWorkspaceThumbnailTextureDelete(entry.texture);
            entry.texture = 0;
        }
        entry.decodedPixels.clear();
        entry.uploadPending = false;
    }
    m_RawWorkspaceThumbnailTextures.clear();
    if (immediate) {
        PumpRawWorkspaceThumbnailTextureDeletes(true);
    }
}

void EditorModule::LoadRawWorkspaceAppState() {
    if (m_RawWorkspaceAppStateLoaded) {
        return;
    }
    m_RawWorkspaceAppStateLoaded = true;

    Stack::RawWorkspace::AppState appState;
    std::string error;
    if (!Stack::RawWorkspace::LoadAppState(GetRawWorkspaceAppStatePath(), appState, &error)) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            error.empty() ? "RAW Workspace state could not be loaded." : error,
            "raw-workspace-state-load");
    }

    m_RawWorkspace.recentWorkspaceRoots = appState.recentWorkspaceRoots;
    m_RawWorkspace.selectedSourceKey = appState.lastSelectedSourceKey;
    m_RawWorkspaceLayoutUi.controlsPanelWidth =
        NormalizeRawWorkspaceControlsPanelWidth(appState.controlsPanelWidth);

    if (!appState.lastWorkspaceRoot.empty()) {
        m_RawWorkspace.workspaceRoot = appState.lastWorkspaceRoot;
        Stack::RawWorkspace::AddRecentWorkspace(m_RawWorkspace, appState.lastWorkspaceRoot);
        RequestRawWorkspaceScan();
    }
}

void EditorModule::SaveRawWorkspaceAppState() {
    m_RawWorkspaceAppStatePersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceAppStatePersistDirty = true;
    m_RawWorkspaceAppStatePersistDirtyTime = RawWorkspaceClockSeconds();
    StartRawWorkspaceAppStatePersistIfNeeded();
}

void EditorModule::StartRawWorkspaceAppStatePersistIfNeeded() {
    if (!m_RawWorkspaceAppStatePersistDirty ||
        m_RawWorkspaceAppStatePersistInFlight) {
        return;
    }
    if (!RawWorkspaceDebounceElapsed(m_RawWorkspaceAppStatePersistDirtyTime)) {
        m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Queued;
        m_RawWorkspaceAppStatePersistStatusText = "Saving RAW Workspace state...";
        return;
    }

    Stack::RawWorkspace::AppState appState;
    appState.lastWorkspaceRoot = m_RawWorkspace.workspaceRoot;
    appState.lastSelectedSourceKey = m_RawWorkspace.selectedSourceKey;
    appState.recentWorkspaceRoots = m_RawWorkspace.recentWorkspaceRoots;
    appState.controlsPanelWidth =
        NormalizeRawWorkspaceControlsPanelWidth(m_RawWorkspaceLayoutUi.controlsPanelWidth);

    const std::filesystem::path appStatePath = GetRawWorkspaceAppStatePath();
    const std::uint64_t generation =
        m_RawWorkspaceAppStatePersistGeneration.load(std::memory_order_relaxed);
    m_RawWorkspaceAppStatePersistDirty = false;
    m_RawWorkspaceAppStatePersistDirtyTime = -1.0;
    m_RawWorkspaceAppStatePersistInFlight = true;
    m_RawWorkspaceAppStatePersistInFlightGeneration = generation;
    m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Queued;
    m_RawWorkspaceAppStatePersistStatusText = "Saving RAW Workspace state...";

    Async::TaskSystem::Get().Submit([
        this,
        generation,
        appStatePath,
        appState = std::move(appState)
    ]() mutable {
        std::string error;
        const bool success = Stack::RawWorkspace::SaveAppStateIfCurrent(
            appStatePath,
            appState,
            [this, generation]() {
                return generation ==
                    m_RawWorkspaceAppStatePersistGeneration.load(std::memory_order_relaxed);
            },
            &error);

        Async::TaskSystem::Get().PostToMain([
            this,
            generation,
            success,
            error = std::move(error)
        ]() mutable {
            if (generation != m_RawWorkspaceAppStatePersistInFlightGeneration) {
                return;
            }

            m_RawWorkspaceAppStatePersistInFlight = false;
            m_RawWorkspaceAppStatePersistInFlightGeneration = 0;
            if (generation != m_RawWorkspaceAppStatePersistGeneration.load(std::memory_order_relaxed)) {
                m_RawWorkspaceAppStatePersistTaskState = m_RawWorkspaceAppStatePersistDirty
                    ? Async::TaskState::Queued
                    : Async::TaskState::Idle;
                m_RawWorkspaceAppStatePersistStatusText = m_RawWorkspaceAppStatePersistDirty
                    ? "Saving RAW Workspace state..."
                    : std::string();
                StartRawWorkspaceAppStatePersistIfNeeded();
                return;
            }
            if (success) {
                m_RawWorkspaceAppStatePersistTaskState = m_RawWorkspaceAppStatePersistDirty
                    ? Async::TaskState::Queued
                    : Async::TaskState::Idle;
                m_RawWorkspaceAppStatePersistStatusText = m_RawWorkspaceAppStatePersistDirty
                    ? "Saving RAW Workspace state..."
                    : std::string();
            } else {
                m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Failed;
                m_RawWorkspaceAppStatePersistStatusText = error.empty()
                    ? "RAW Workspace state could not be saved."
                    : error;
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    m_RawWorkspaceAppStatePersistStatusText,
                    "raw-workspace-state-save");
            }
            StartRawWorkspaceAppStatePersistIfNeeded();
        });
    });
}

void EditorModule::ResetRawWorkspaceAppStatePersistState() {
    m_RawWorkspaceAppStatePersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Idle;
    m_RawWorkspaceAppStatePersistDirty = false;
    m_RawWorkspaceAppStatePersistInFlight = false;
    m_RawWorkspaceAppStatePersistInFlightGeneration = 0;
    m_RawWorkspaceAppStatePersistDirtyTime = -1.0;
    m_RawWorkspaceAppStatePersistStatusText.clear();
}

void EditorModule::FlushRawWorkspacePersistenceForShutdown() {
    m_RawWorkspaceAppStatePersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceCatalogPersistGeneration.fetch_add(1, std::memory_order_relaxed);

    if (m_RawWorkspaceAppStateLoaded) {
        Stack::RawWorkspace::AppState appState;
        appState.lastWorkspaceRoot = m_RawWorkspace.workspaceRoot;
        appState.lastSelectedSourceKey = m_RawWorkspace.selectedSourceKey;
        appState.recentWorkspaceRoots = m_RawWorkspace.recentWorkspaceRoots;
        appState.controlsPanelWidth =
            NormalizeRawWorkspaceControlsPanelWidth(m_RawWorkspaceLayoutUi.controlsPanelWidth);

        std::string appStateError;
        const bool appStateSaved =
            Stack::RawWorkspace::SaveAppState(GetRawWorkspaceAppStatePath(), appState, &appStateError);
        m_RawWorkspaceAppStatePersistDirty = false;
        m_RawWorkspaceAppStatePersistInFlight = false;
        m_RawWorkspaceAppStatePersistInFlightGeneration = 0;
        m_RawWorkspaceAppStatePersistDirtyTime = -1.0;
        m_RawWorkspaceAppStatePersistTaskState = appStateSaved
            ? Async::TaskState::Idle
            : Async::TaskState::Failed;
        m_RawWorkspaceAppStatePersistStatusText = appStateSaved
            ? std::string()
            : (appStateError.empty() ? "RAW Workspace state could not be saved." : appStateError);
    }

    if (!m_RawWorkspace.workspaceRoot.empty()) {
        const Stack::RawWorkspace::ManagedLayout layout =
            Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
        std::string catalogError;
        const bool catalogSaved = Stack::RawWorkspace::WriteCatalogSkeleton(
            layout,
            m_RawWorkspace.sources,
            m_RawWorkspace.selectedSourceKey,
            &catalogError);
        m_RawWorkspaceCatalogPersistDirty = false;
        m_RawWorkspaceCatalogPersistInFlight = false;
        m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
        m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
        m_RawWorkspaceCatalogPersistTaskState = catalogSaved
            ? Async::TaskState::Idle
            : Async::TaskState::Failed;
        m_RawWorkspaceCatalogPersistStatusText = catalogSaved
            ? std::string()
            : (catalogError.empty() ? "RAW Workspace catalog could not be saved." : catalogError);
    } else {
        m_RawWorkspaceCatalogPersistDirty = false;
        m_RawWorkspaceCatalogPersistInFlight = false;
        m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
        m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
        m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Idle;
        m_RawWorkspaceCatalogPersistStatusText.clear();
    }
}

void EditorModule::RequestOpenRawWorkspace(const std::filesystem::path& workspaceRoot) {
    if (workspaceRoot.empty()) {
        return;
    }
    if (!SaveActiveRawWorkspaceProjectIfDirty()) {
        return;
    }

    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::absolute(workspaceRoot, ec).lexically_normal();
    m_RawWorkspace.workspaceRoot = ec ? workspaceRoot.lexically_normal() : normalized;
    ClearRawWorkspaceLivePreviewState();
    m_RawWorkspace.sources.clear();
    m_RawWorkspace.selectedSourceKey.clear();
    InvalidateRawWorkspaceGalleryPresentation();
    m_ActiveRawWorkspaceSourceKey.clear();
    m_ActiveRawWorkspaceProjectPath.clear();
    m_RawWorkspacePreviewStageFailureSourceKey.clear();
    m_RawWorkspaceRecipePreviewCache.clear();
    m_RawWorkspaceProjectLoadGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
    m_RawWorkspaceProjectLoadSourceKey.clear();
    m_RawWorkspaceProjectLoadStatusText.clear();
    ResetDeferredLoadedProjectApplyState();
    m_PendingRawWorkspaceDeferredProjectFinalize = false;
    m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
    m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
    m_PendingRawWorkspaceOpenGraphSourceKey.clear();
    ClearRawWorkspaceThumbnailTextures();
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
    }
    ResetRawWorkspaceCatalogPersistState();
    Stack::RawWorkspace::AddRecentWorkspace(m_RawWorkspace, m_RawWorkspace.workspaceRoot);
    SaveRawWorkspaceAppState();
    RequestRawWorkspaceScan();
}

void EditorModule::RequestRawWorkspaceScan() {
    if (m_RawWorkspace.workspaceRoot.empty()) {
        return;
    }

    const std::filesystem::path workspaceRoot = m_RawWorkspace.workspaceRoot;
    const std::string selectedBeforeScan = m_RawWorkspace.selectedSourceKey;

    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
        generation = ++m_RawWorkspaceScanGeneration;
        m_RawWorkspaceScanSnapshot = {};
        m_RawWorkspaceScanSnapshot.generation = generation;
        m_RawWorkspaceScanSnapshot.state = Async::TaskState::Queued;
        m_RawWorkspaceScanSnapshot.statusText = "Scanning Workspace...";
    }

    Async::TaskSystem::Get().Submit([this, generation, workspaceRoot, selectedBeforeScan]() mutable {
        auto isScanCancelled = [this, generation]() {
            std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
            return generation != m_RawWorkspaceScanGeneration;
        };

        auto updateProgress = [this, generation](const Stack::RawWorkspace::ScanProgress& progress) {
            std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
            if (generation != m_RawWorkspaceScanGeneration) {
                return;
            }
            m_RawWorkspaceScanSnapshot.state = Async::TaskState::Running;
            m_RawWorkspaceScanSnapshot.progress = progress;
            m_RawWorkspaceScanSnapshot.statusText = progress.statusText.empty()
                ? std::string("Scanning Workspace...")
                : progress.statusText;
        };

        auto isRawPath = [](const std::filesystem::path& path) {
            return Raw::RawLoader::IsRawPath(path.string()) ||
                Stack::RawWorkspace::DefaultRawPathPredicate(path);
        };

        Stack::RawWorkspace::ScanResult result =
            Stack::RawWorkspace::ScanWorkspace(
                workspaceRoot,
                isRawPath,
                updateProgress,
                isScanCancelled);

        if (result.success) {
            {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                if (generation == m_RawWorkspaceScanGeneration) {
                    m_RawWorkspaceScanSnapshot.state = Async::TaskState::Applying;
                    m_RawWorkspaceScanSnapshot.progress = result.progress;
                    m_RawWorkspaceScanSnapshot.statusText = "Classifying RAW thumbnails...";
                }
            }
            if (!Stack::RawWorkspace::ClassifyThumbnails(
                    result.layout,
                    result.sources,
                    Stack::RawWorkspace::kNeutralThumbnailMaxDimension,
                    isScanCancelled)) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                if (generation == m_RawWorkspaceScanGeneration) {
                    m_RawWorkspaceScanSnapshot.state = Async::TaskState::Applying;
                    m_RawWorkspaceScanSnapshot.progress = result.progress;
                    m_RawWorkspaceScanSnapshot.statusText = "Discovering RAW projects...";
                }
            }
            if (!Stack::RawWorkspace::DiscoverProjects(
                    result.layout,
                    result.sources,
                    isScanCancelled)) {
                return;
            }
        }

        if (isScanCancelled()) {
            return;
        }

        Async::TaskSystem::Get().PostToMain([this, generation, selectedBeforeScan, result = std::move(result)]() mutable {
            {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                if (generation != m_RawWorkspaceScanGeneration) {
                    return;
                }
                m_RawWorkspaceScanSnapshot.state = Async::TaskState::Applying;
                m_RawWorkspaceScanSnapshot.statusText = "Applying Workspace scan...";
            }

            if (!result.success) {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                m_RawWorkspaceScanSnapshot.state = Async::TaskState::Failed;
                m_RawWorkspaceScanSnapshot.errorMessage = result.errorMessage.empty()
                    ? "Failed to scan Workspace."
                    : result.errorMessage;
                m_RawWorkspaceScanSnapshot.statusText = m_RawWorkspaceScanSnapshot.errorMessage;
                return;
            }

            m_RawWorkspace.workspaceRoot = result.layout.workspaceRoot;
            m_RawWorkspace.sources = std::move(result.sources);
            Stack::RawWorkspace::AddRecentWorkspace(m_RawWorkspace, m_RawWorkspace.workspaceRoot);

            const std::string latestSelection = m_RawWorkspace.selectedSourceKey;
            bool restoredSelection = !latestSelection.empty() &&
                Stack::RawWorkspace::SelectSourceByKey(m_RawWorkspace, latestSelection);
            if (!restoredSelection &&
                selectedBeforeScan != latestSelection &&
                !selectedBeforeScan.empty()) {
                restoredSelection = Stack::RawWorkspace::SelectSourceByKey(
                    m_RawWorkspace,
                    selectedBeforeScan);
            }
            if (!restoredSelection) {
                m_RawWorkspace.selectedSourceKey.clear();
            }
            m_RawWorkspacePreviewStageFailureSourceKey.clear();
            InvalidateRawWorkspaceGalleryPresentation();

            PersistRawWorkspaceCatalog();
            SaveRawWorkspaceAppState();
            RequestRawWorkspaceThumbnailGeneration();

            std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
            m_RawWorkspaceScanSnapshot.state = Async::TaskState::Idle;
            m_RawWorkspaceScanSnapshot.progress = result.progress;
            m_RawWorkspaceScanSnapshot.statusText = result.progress.statusText.empty()
                ? "Workspace ready."
                : result.progress.statusText;
        });
    });
}

void EditorModule::RequestRawWorkspaceThumbnailGeneration() {
    if (m_RawWorkspace.workspaceRoot.empty() || m_RawWorkspace.sources.empty()) {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
        m_RawWorkspaceThumbnailSnapshot.progress = Stack::RawWorkspace::BuildThumbnailProgress(m_RawWorkspace.sources);
        m_RawWorkspaceThumbnailSnapshot.statusText = m_RawWorkspaceThumbnailSnapshot.progress.statusText;
        InvalidateRawWorkspaceGalleryPresentation();
        return;
    }

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    std::vector<RawWorkspaceThumbnailWorkItem> pending;
    for (std::size_t sourceIndex = 0; sourceIndex < m_RawWorkspace.sources.size(); ++sourceIndex) {
        Stack::RawWorkspace::SourceRecord& source = m_RawWorkspace.sources[sourceIndex];
        if (source.thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Missing ||
            source.thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Stale ||
            source.thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Failed) {
            source.thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Queued;
            pending.push_back(RawWorkspaceThumbnailWorkItem{
                sourceIndex,
                source,
            });
        }
    }
    if (!pending.empty()) {
        InvalidateRawWorkspaceGalleryPresentation();
    }

    Stack::RawWorkspace::ThumbnailProgress initialProgress =
        Stack::RawWorkspace::BuildThumbnailProgress(m_RawWorkspace.sources);

    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        generation = ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
        m_RawWorkspaceThumbnailSnapshot.generation = generation;
        m_RawWorkspaceThumbnailSnapshot.progress = initialProgress;
        m_RawWorkspaceThumbnailSnapshot.statusText = initialProgress.statusText;
        m_RawWorkspaceThumbnailSnapshot.state = pending.empty()
            ? Async::TaskState::Idle
            : Async::TaskState::Queued;
    }

    if (pending.empty()) {
        PersistRawWorkspaceCatalog();
        return;
    }

    PersistRawWorkspaceCatalog();

    Async::TaskSystem::Get().Submit([this, generation, layout, initialProgress, pending = std::move(pending)]() mutable {
        const int pendingTotal = static_cast<int>(pending.size());
        Stack::RawWorkspace::ThumbnailProgress progress = initialProgress;
        progress.completed = 0;
        progress.failed = 0;
        progress.queued = pendingTotal;
        std::vector<RawWorkspaceThumbnailUpdate> thumbnailUpdates;
        thumbnailUpdates.reserve(kRawWorkspaceThumbnailApplyBatchSize);

        auto updateProgress = [&](const std::string& item) {
            progress.currentItem = item;
            progress.statusText = item.empty()
                ? "Generating RAW thumbnails..."
                : "Generating thumbnail for " + item;
            std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
            if (generation != m_RawWorkspaceThumbnailGeneration) {
                return;
            }
            m_RawWorkspaceThumbnailSnapshot.state = Async::TaskState::Running;
            m_RawWorkspaceThumbnailSnapshot.progress = progress;
            m_RawWorkspaceThumbnailSnapshot.statusText = progress.statusText;
        };
        auto flushThumbnailUpdates = [&]() {
            if (thumbnailUpdates.empty()) {
                return;
            }
            std::vector<RawWorkspaceThumbnailUpdate> updates = std::move(thumbnailUpdates);
            thumbnailUpdates.clear();
            thumbnailUpdates.reserve(kRawWorkspaceThumbnailApplyBatchSize);

            Async::TaskSystem::Get().PostToMain([
                this,
                generation,
                updates = std::move(updates)
            ]() mutable {
                {
                    std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
                    if (generation != m_RawWorkspaceThumbnailGeneration) {
                        return;
                    }
                }

                bool anyThumbnailUpdated = false;
                for (RawWorkspaceThumbnailUpdate& update : updates) {
                    if (update.sourceIndex < m_RawWorkspace.sources.size() &&
                        m_RawWorkspace.sources[update.sourceIndex].relativePathKey == update.sourceKey) {
                        m_RawWorkspace.sources[update.sourceIndex].thumbnail = std::move(update.thumbnail);
                        anyThumbnailUpdated = true;
                    }
                }
                if (anyThumbnailUpdated) {
                    InvalidateRawWorkspaceGalleryPresentation();
                    m_RawWorkspaceCatalogPersistGeneration.fetch_add(1, std::memory_order_relaxed);
                    m_RawWorkspaceCatalogPersistDirty = true;
                    m_RawWorkspaceCatalogPersistDirtyTime = RawWorkspaceClockSeconds();
                }
            });
        };

        for (const RawWorkspaceThumbnailWorkItem& item : pending) {
            {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
                if (generation != m_RawWorkspaceThumbnailGeneration) {
                    return;
                }
            }

            const Stack::RawWorkspace::SourceRecord& source = item.source;
            updateProgress(source.fileName);
            Stack::RawWorkspace::ThumbnailGenerationResult result =
                Stack::RawWorkspace::GenerateNeutralThumbnail(
                    layout,
                    source,
                    Stack::RawWorkspace::kNeutralThumbnailMaxDimension,
                    [this, generation]() {
                        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
                        return generation != m_RawWorkspaceThumbnailGeneration;
                    });
            {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
                if (generation != m_RawWorkspaceThumbnailGeneration) {
                    return;
                }
            }
            if (result.success) {
                ++progress.completed;
            } else {
                ++progress.failed;
            }
            progress.queued = std::max(0, pendingTotal - progress.completed - progress.failed);

            thumbnailUpdates.push_back(RawWorkspaceThumbnailUpdate{
                item.sourceIndex,
                source.relativePathKey,
                std::move(result.thumbnail),
            });
            if (thumbnailUpdates.size() >= kRawWorkspaceThumbnailApplyBatchSize) {
                flushThumbnailUpdates();
            }
        }
        flushThumbnailUpdates();

        progress.currentItem.clear();
        progress.statusText = "RAW thumbnail generation complete.";
        Async::TaskSystem::Get().PostToMain([this, generation, progress]() mutable {
            {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
                if (generation != m_RawWorkspaceThumbnailGeneration) {
                    return;
                }
                m_RawWorkspaceThumbnailSnapshot.state = Async::TaskState::Idle;
                m_RawWorkspaceThumbnailSnapshot.progress = progress;
                m_RawWorkspaceThumbnailSnapshot.statusText = progress.statusText;
            }
            PersistRawWorkspaceCatalog();
        });
    });
}

void EditorModule::ClearRawWorkspace() {
    if (!SaveActiveRawWorkspaceProjectIfDirty()) {
        return;
    }
    ClearRawWorkspaceLivePreviewState();
    m_RawWorkspace.workspaceRoot.clear();
    m_RawWorkspace.sources.clear();
    m_RawWorkspace.selectedSourceKey.clear();
    InvalidateRawWorkspaceGalleryPresentation();
    m_ActiveRawWorkspaceSourceKey.clear();
    m_ActiveRawWorkspaceProjectPath.clear();
    m_RawWorkspacePreviewStageFailureSourceKey.clear();
    m_RawWorkspaceRecipePreviewCache.clear();
    m_RawWorkspaceProjectLoadGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
    m_RawWorkspaceProjectLoadSourceKey.clear();
    m_RawWorkspaceProjectLoadStatusText.clear();
    ResetDeferredLoadedProjectApplyState();
    m_PendingRawWorkspaceDeferredProjectFinalize = false;
    m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
    m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
    m_PendingRawWorkspaceOpenGraphSourceKey.clear();
    m_RawWorkspacePreviewStageQueued = false;
    m_RawWorkspacePreviewStageSourceKey.clear();
    m_RawWorkspacePreviewStageQueuedFrame = -1;
    ResetRawWorkspaceAutoBaseState();
    ClearRawWorkspaceThumbnailTextures();
    ResetRawWorkspaceCatalogPersistState();
    SaveRawWorkspaceAppState();

    std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
    ++m_RawWorkspaceScanGeneration;
    m_RawWorkspaceScanSnapshot = {};
    {
        std::lock_guard<std::mutex> thumbnailLock(m_RawWorkspaceThumbnailMutex);
        ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
    }
}

void EditorModule::SelectRawWorkspaceSource(const std::string& sourceKey) {
    const bool selectionChanged = sourceKey != m_RawWorkspace.selectedSourceKey;
    if (selectionChanged && !SaveActiveRawWorkspaceProjectIfDirty()) {
        return;
    }
    if (selectionChanged) {
        ClearRawWorkspaceLivePreviewState();
        ResetRawWorkspaceAutoBaseState();
        m_RawWorkspacePreviewStageFailureSourceKey.clear();
        m_RawWorkspaceProjectLoadGeneration.fetch_add(1, std::memory_order_relaxed);
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
        m_RawWorkspaceProjectLoadSourceKey.clear();
        m_RawWorkspaceProjectLoadStatusText.clear();
        ResetDeferredLoadedProjectApplyState();
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
        m_PendingRawWorkspaceOpenGraphSourceKey.clear();
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
    } else if (m_RawWorkspacePreviewStageFailureSourceKey == sourceKey) {
        m_RawWorkspacePreviewStageFailureSourceKey.clear();
        if (m_RawWorkspaceProjectLoadTaskState == Async::TaskState::Failed &&
            m_RawWorkspaceProjectLoadSourceKey == sourceKey) {
            m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
            m_RawWorkspaceProjectLoadSourceKey.clear();
            m_RawWorkspaceProjectLoadStatusText.clear();
        }
    } else if (m_RawWorkspaceProjectLoadTaskState == Async::TaskState::Failed &&
        m_RawWorkspaceProjectLoadSourceKey == sourceKey) {
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
        m_RawWorkspaceProjectLoadSourceKey.clear();
        m_RawWorkspaceProjectLoadStatusText.clear();
    }
    if (!Stack::RawWorkspace::SelectSourceByKey(m_RawWorkspace, sourceKey)) {
        return;
    }
    if (selectionChanged) {
        InvalidateRawWorkspaceGalleryPresentation();
    }
    QueueSelectedRawWorkspaceSourcePreviewStaging();
    PersistRawWorkspaceCatalog();
    SaveRawWorkspaceAppState();
}

void EditorModule::QueueSelectedRawWorkspaceSourcePreviewStaging() {
    if (m_RawWorkspace.workspaceRoot.empty() || m_RawWorkspace.selectedSourceKey.empty()) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }

    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!source) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }

    if (IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == source->relativePathKey) {
        if (m_RawWorkspacePreviewStageSourceKey == source->relativePathKey) {
            m_RawWorkspacePreviewStageQueued = false;
            m_RawWorkspacePreviewStageSourceKey.clear();
            m_RawWorkspacePreviewStageQueuedFrame = -1;
        }
        m_RawWorkspacePreviewStageFailureSourceKey.clear();
        return;
    }

    if (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
        m_RawWorkspaceProjectLoadSourceKey == source->relativePathKey) {
        return;
    }
    if (m_RawWorkspaceProjectLoadTaskState == Async::TaskState::Failed &&
        m_RawWorkspaceProjectLoadSourceKey == source->relativePathKey) {
        return;
    }
    if (m_RawWorkspacePreviewStageFailureSourceKey == source->relativePathKey) {
        return;
    }

    const bool storedProject =
        source->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        source->project.status == Stack::RawWorkspace::ProjectStatus::Embedded;
    m_RawWorkspacePreviewStageQueued = true;
    m_RawWorkspacePreviewStageSourceKey = source->relativePathKey;
    m_RawWorkspacePreviewStageQueuedFrame =
        ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    m_RawWorkspaceProjectLoadStatusText =
        storedProject ? "Loading RAW project..." : "Preparing RAW preview...";
}

void EditorModule::TickRawWorkspacePreviewStaging() {
    if (!m_RawWorkspacePreviewStageQueued) {
        return;
    }
    if (ImGui::GetCurrentContext() &&
        m_RawWorkspacePreviewStageQueuedFrame >= 0 &&
        ImGui::GetFrameCount() <= m_RawWorkspacePreviewStageQueuedFrame) {
        return;
    }

    const std::string queuedSourceKey = m_RawWorkspacePreviewStageSourceKey;
    if (queuedSourceKey.empty() ||
        queuedSourceKey != m_RawWorkspace.selectedSourceKey) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }

    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(queuedSourceKey);
    if (!source) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }

    const bool storedProject =
        source->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        source->project.status == Stack::RawWorkspace::ProjectStatus::Embedded;
    m_RawWorkspacePreviewStageQueued = false;
    m_RawWorkspacePreviewStageSourceKey.clear();
    m_RawWorkspacePreviewStageQueuedFrame = -1;

    const bool staged = EnsureSelectedRawWorkspaceSourcePreviewStaged();
    if (!storedProject && !staged) {
        m_RawWorkspaceProjectLoadSourceKey = queuedSourceKey;
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
        m_RawWorkspaceProjectLoadStatusText = "Failed to prepare the RAW preview.";
    }
}

bool EditorModule::EnsureSelectedRawWorkspaceSourcePreviewStaged() {
    if (m_RawWorkspace.workspaceRoot.empty() || m_RawWorkspace.selectedSourceKey.empty()) {
        return false;
    }

    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!source) {
        return false;
    }

    if (IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == source->relativePathKey) {
        m_RawWorkspacePreviewStageFailureSourceKey.clear();
        return true;
    }
    if (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
        m_RawWorkspaceProjectLoadSourceKey == source->relativePathKey) {
        return true;
    }

    if (m_RawWorkspacePreviewStageFailureSourceKey == source->relativePathKey) {
        return false;
    }

    if (!SaveActiveRawWorkspaceProjectIfDirty()) {
        m_RawWorkspacePreviewStageFailureSourceKey = source->relativePathKey;
        return false;
    }

    ClearRawWorkspaceLivePreviewState();
    if (!StageRawWorkspaceProjectForSourcePreview(*source)) {
        m_RawWorkspacePreviewStageFailureSourceKey = source->relativePathKey;
        return false;
    }

    m_RawWorkspacePreviewStageFailureSourceKey.clear();
    return true;
}

void EditorModule::PersistRawWorkspaceCatalog() {
    if (m_RawWorkspace.workspaceRoot.empty()) {
        return;
    }

    m_RawWorkspaceCatalogPersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceCatalogPersistDirty = true;
    m_RawWorkspaceCatalogPersistDirtyTime = RawWorkspaceClockSeconds();
    StartRawWorkspaceCatalogPersistIfNeeded();
}

void EditorModule::StartRawWorkspaceCatalogPersistIfNeeded() {
    if (m_RawWorkspace.workspaceRoot.empty()) {
        m_RawWorkspaceCatalogPersistDirty = false;
        m_RawWorkspaceCatalogPersistInFlight = false;
        m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
        m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
        m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Idle;
        m_RawWorkspaceCatalogPersistStatusText.clear();
        return;
    }
    if (!m_RawWorkspaceCatalogPersistDirty || m_RawWorkspaceCatalogPersistInFlight) {
        return;
    }
    if (!RawWorkspaceDebounceElapsed(m_RawWorkspaceCatalogPersistDirtyTime)) {
        m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Queued;
        m_RawWorkspaceCatalogPersistStatusText = "Saving RAW Workspace catalog...";
        return;
    }

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    std::vector<Stack::RawWorkspace::CatalogSourceRecord> sources =
        Stack::RawWorkspace::BuildCatalogSourceRecords(m_RawWorkspace.sources);
    const std::string selectedSourceKey = m_RawWorkspace.selectedSourceKey;
    const std::uint64_t generation =
        m_RawWorkspaceCatalogPersistGeneration.load(std::memory_order_relaxed);
    m_RawWorkspaceCatalogPersistDirty = false;
    m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
    m_RawWorkspaceCatalogPersistInFlight = true;
    m_RawWorkspaceCatalogPersistInFlightGeneration = generation;
    m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Queued;
    m_RawWorkspaceCatalogPersistStatusText = "Saving RAW Workspace catalog...";

    Async::TaskSystem::Get().Submit([
        this,
        generation,
        layout,
        sources = std::move(sources),
        selectedSourceKey
    ]() mutable {
        std::string error;
        const bool success = Stack::RawWorkspace::WriteCatalogSkeletonIfCurrent(
            layout,
            sources,
            selectedSourceKey,
            [this, generation]() {
                return generation ==
                    m_RawWorkspaceCatalogPersistGeneration.load(std::memory_order_relaxed);
            },
            &error);

        Async::TaskSystem::Get().PostToMain([
            this,
            generation,
            success,
            error = std::move(error)
        ]() mutable {
            if (generation != m_RawWorkspaceCatalogPersistInFlightGeneration) {
                return;
            }

            m_RawWorkspaceCatalogPersistInFlight = false;
            m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
            if (generation != m_RawWorkspaceCatalogPersistGeneration.load(std::memory_order_relaxed)) {
                m_RawWorkspaceCatalogPersistTaskState = m_RawWorkspaceCatalogPersistDirty
                    ? Async::TaskState::Queued
                    : Async::TaskState::Idle;
                m_RawWorkspaceCatalogPersistStatusText = m_RawWorkspaceCatalogPersistDirty
                    ? "Saving RAW Workspace catalog..."
                    : std::string();
                StartRawWorkspaceCatalogPersistIfNeeded();
                return;
            }
            if (success) {
                m_RawWorkspaceCatalogPersistTaskState = m_RawWorkspaceCatalogPersistDirty
                    ? Async::TaskState::Queued
                    : Async::TaskState::Idle;
                m_RawWorkspaceCatalogPersistStatusText = m_RawWorkspaceCatalogPersistDirty
                    ? "Saving RAW Workspace catalog..."
                    : std::string();
            } else {
                m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Failed;
                m_RawWorkspaceCatalogPersistStatusText = error.empty()
                    ? "RAW Workspace catalog could not be saved."
                    : error;
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    m_RawWorkspaceCatalogPersistStatusText,
                    "raw-workspace-catalog-save");
            }
            StartRawWorkspaceCatalogPersistIfNeeded();
        });
    });
}

void EditorModule::ResetRawWorkspaceCatalogPersistState() {
    m_RawWorkspaceCatalogPersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Idle;
    m_RawWorkspaceCatalogPersistDirty = false;
    m_RawWorkspaceCatalogPersistInFlight = false;
    m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
    m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
    m_RawWorkspaceCatalogPersistStatusText.clear();
}

void EditorModule::TickRawWorkspacePersistence() {
    StartRawWorkspaceAppStatePersistIfNeeded();
    StartRawWorkspaceCatalogPersistIfNeeded();
}

void EditorModule::NoteRawWorkspaceRecipePreviewEdit(bool interactionActive) {
    m_RawWorkspacePreviewSourceKey = m_ActiveRawWorkspaceSourceKey;
    if (interactionActive && ImGui::GetCurrentContext()) {
        m_RawWorkspaceFastPreviewUntilTime =
            ImGui::GetTime() + kRawWorkspaceFastPreviewQuietSeconds;
        m_RawWorkspaceFullResolutionPreviewPending = true;
        m_RawWorkspaceFullResolutionPreviewRequested = false;
    } else {
        m_RawWorkspaceFastPreviewUntilTime = -1.0;
        m_RawWorkspaceFullResolutionPreviewPending = false;
        m_RawWorkspaceFullResolutionPreviewRequested = false;
    }
}

bool EditorModule::IsRawWorkspaceFastPreviewRenderActive(double now) const {
    return m_RawWorkspaceFullResolutionPreviewPending &&
        !m_RawWorkspacePreviewSourceKey.empty() &&
        m_RawWorkspacePreviewSourceKey == m_ActiveRawWorkspaceSourceKey &&
        m_RawWorkspaceFastPreviewUntilTime > 0.0 &&
        now < m_RawWorkspaceFastPreviewUntilTime;
}

void EditorModule::UpdateRawWorkspaceSettledPreviewRender(double now) {
    if (!m_RawWorkspaceFullResolutionPreviewPending) {
        return;
    }
    if (m_RawWorkspacePreviewSourceKey.empty() ||
        m_RawWorkspacePreviewSourceKey != m_ActiveRawWorkspaceSourceKey) {
        m_RawWorkspaceFullResolutionPreviewPending = false;
        m_RawWorkspaceFullResolutionPreviewRequested = false;
        m_RawWorkspaceFastPreviewUntilTime = -1.0;
        return;
    }
    if (m_RawWorkspaceFastPreviewUntilTime > 0.0 &&
        now < m_RawWorkspaceFastPreviewUntilTime) {
        return;
    }
    if (HasRawWorkspaceFullResolutionPreviewForSource(m_ActiveRawWorkspaceSourceKey)) {
        m_RawWorkspaceFullResolutionPreviewPending = false;
        m_RawWorkspaceFullResolutionPreviewRequested = false;
        m_RawWorkspaceFastPreviewUntilTime = -1.0;
        return;
    }
    if (m_RawWorkspaceFullResolutionPreviewRequested) {
        return;
    }

    m_RawWorkspaceFullResolutionPreviewRequested = true;
    m_RawWorkspaceFastPreviewUntilTime = -1.0;
    MarkRenderRefreshDirty();
}

bool EditorModule::HasRawWorkspaceLivePreviewForSource(const std::string& sourceKey) const {
    if (sourceKey.empty() || m_ViewportOutputRawWorkspaceSourceKey != sourceKey) {
        return false;
    }
    if (m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::Tiled) {
        return HasViewportOutputTiles();
    }
    if (m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::SingleTexture) {
        return m_Pipeline.GetOutputTexture() != 0 &&
            m_Pipeline.GetCanvasWidth() > 0 &&
            m_Pipeline.GetCanvasHeight() > 0;
    }
    return false;
}

bool EditorModule::HasRawWorkspaceFullResolutionPreviewForSource(const std::string& sourceKey) const {
    return HasRawWorkspaceLivePreviewForSource(sourceKey) &&
        m_ViewportOutputPreviewMaxDimension == 0 &&
        m_ViewportOutputRenderGeneration == m_RenderGeneration &&
        !m_RenderDirty &&
        !m_RenderPending;
}

void EditorModule::ClearRawWorkspaceLocalRangeTargetState(bool keepMode) {
    const bool targetMode = m_RawWorkspaceLocalRangeTargetMode;
    m_RawWorkspaceLocalRangeTargetMode = keepMode ? targetMode : false;
    m_RawWorkspaceLocalRangeTargetDragging = false;
    m_RawWorkspaceLocalRangeTargetSamplePending = false;
    m_RawWorkspaceLocalRangeTargetSampleValid = false;
    m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
    m_RawWorkspaceLocalRangeTargetSourceKey.clear();
    m_RawWorkspaceLocalRangeTargetU = 0.0f;
    m_RawWorkspaceLocalRangeTargetV = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneEv = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneLuma = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneR = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneG = 0.0f;
    m_RawWorkspaceLocalRangeTargetSceneB = 0.0f;
    m_RawWorkspaceLocalRangeTargetStartMouseY = 0.0f;
    m_RawWorkspaceLocalRangeTargetDeltaEv = 0.0f;
    m_RawWorkspaceLocalRangeTargetPointIndex = -1;
}

bool EditorModule::HasRawWorkspaceLocalRangeTargetSampleForSource(const std::string& sourceKey) const {
    return !sourceKey.empty() &&
        m_RawWorkspaceLocalRangeTargetSampleValid &&
        m_RawWorkspaceLocalRangeTargetSourceKey == sourceKey &&
        std::isfinite(m_RawWorkspaceLocalRangeTargetSceneEv);
}

void EditorModule::AdoptRawWorkspaceLocalRangeTargetSampleFromResult(
    const EditorRenderWorker::Result& result) {
    if (!m_RawWorkspaceLocalRangeTargetSamplePending) {
        return;
    }
    if (result.rawWorkspace.sourceKey.empty() ||
        result.rawWorkspace.sourceKey != m_RawWorkspaceLocalRangeTargetSourceKey ||
        result.rawWorkspace.sourceKey != m_ActiveRawWorkspaceSourceKey) {
        return;
    }

    const bool sameSamplePoint =
        std::abs(result.rawWorkspace.localRangeTargetSample.u - m_RawWorkspaceLocalRangeTargetU) < 0.001f &&
        std::abs(result.rawWorkspace.localRangeTargetSample.v - m_RawWorkspaceLocalRangeTargetV) < 0.001f;
    m_RawWorkspaceLocalRangeTargetSamplePending = false;
    if (!result.rawWorkspace.localRangeTargetSample.valid || !sameSamplePoint) {
        m_RawWorkspaceLocalRangeTargetSampleValid = false;
        m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
        return;
    }

    m_RawWorkspaceLocalRangeTargetSampleValid = true;
    m_RawWorkspaceLocalRangeTargetSceneEv = result.rawWorkspace.localRangeTargetSample.sceneEv;
    m_RawWorkspaceLocalRangeTargetSceneLuma = result.rawWorkspace.localRangeTargetSample.sceneLuma;
    m_RawWorkspaceLocalRangeTargetSceneR = result.rawWorkspace.localRangeTargetSample.sceneR;
    m_RawWorkspaceLocalRangeTargetSceneG = result.rawWorkspace.localRangeTargetSample.sceneG;
    m_RawWorkspaceLocalRangeTargetSceneB = result.rawWorkspace.localRangeTargetSample.sceneB;
    m_RawWorkspaceLocalRangeTargetU = result.rawWorkspace.localRangeTargetSample.u;
    m_RawWorkspaceLocalRangeTargetV = result.rawWorkspace.localRangeTargetSample.v;

    if (m_RawWorkspaceLocalRangeTargetApplyWhenSampled &&
        std::abs(m_RawWorkspaceLocalRangeTargetDeltaEv) > 0.005f) {
        ApplyRawWorkspaceLocalRangeTargetDelta(m_RawWorkspaceLocalRangeTargetDragging);
    }
    m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
}

bool EditorModule::ApplyRawWorkspaceLocalRangeTargetDelta(bool interactionActive) {
    if (!HasRawWorkspaceLocalRangeTargetSampleForSource(m_ActiveRawWorkspaceSourceKey)) {
        return false;
    }
    if (m_ActiveRawWorkspaceSourceKey != m_RawWorkspace.selectedSourceKey) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe editedRecipe = m_ActiveRawWorkspaceRecipe;
    Stack::RawRecipe::RawLocalRangeRecipe localRange =
        BuildLocalRangeUiRecipe(editedRecipe.localRange);
    const float targetEv = std::clamp(
        m_RawWorkspaceLocalRangeTargetSceneEv,
        localRange.minEv + 0.001f,
        localRange.maxEv - 0.001f);
    const float targetDeltaEv = std::clamp(
        m_RawWorkspaceLocalRangeTargetDeltaEv,
        kRawLocalRangeMinDeltaEv,
        kRawLocalRangeMaxDeltaEv);

    int editIndex = -1;
    if (m_RawWorkspaceLocalRangeTargetPointIndex > 0 &&
        m_RawWorkspaceLocalRangeTargetPointIndex < static_cast<int>(localRange.points.size()) - 1) {
        editIndex = m_RawWorkspaceLocalRangeTargetPointIndex;
    }

    float nearestDist = std::numeric_limits<float>::max();
    int nearestIndex = -1;
    for (int i = 1; i < static_cast<int>(localRange.points.size()) - 1; ++i) {
        const float dist = std::abs(localRange.points[static_cast<std::size_t>(i)].ev - targetEv);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearestIndex = i;
        }
    }
    if (editIndex < 0 &&
        nearestIndex >= 0 &&
        nearestDist <= kRawLocalRangeTargetPointToleranceEv) {
        editIndex = nearestIndex;
    }
    if (editIndex < 0) {
        if (localRange.points.size() < static_cast<std::size_t>(kRawLocalRangeMaxPoints)) {
            localRange.points.push_back({ targetEv, targetDeltaEv });
            localRange = BuildLocalRangeUiRecipe(localRange);
            nearestDist = std::numeric_limits<float>::max();
            for (int i = 1; i < static_cast<int>(localRange.points.size()) - 1; ++i) {
                const float dist = std::abs(localRange.points[static_cast<std::size_t>(i)].ev - targetEv);
                if (dist < nearestDist) {
                    nearestDist = dist;
                    editIndex = i;
                }
            }
        } else {
            editIndex = nearestIndex;
        }
    }
    if (editIndex <= 0 || editIndex >= static_cast<int>(localRange.points.size()) - 1) {
        return false;
    }

    localRange.points[static_cast<std::size_t>(editIndex)] = { targetEv, targetDeltaEv };
    localRange.enabled = true;
    if (localRange.colorMaskEnabled) {
        localRange.colorMaskTargetR = std::clamp(m_RawWorkspaceLocalRangeTargetSceneR, 0.0f, 32.0f);
        localRange.colorMaskTargetG = std::clamp(m_RawWorkspaceLocalRangeTargetSceneG, 0.0f, 32.0f);
        localRange.colorMaskTargetB = std::clamp(m_RawWorkspaceLocalRangeTargetSceneB, 0.0f, 32.0f);
    }
    editedRecipe.localRange = BuildLocalRangeUiRecipe(localRange);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(editedRecipe, interactionActive)) {
        return false;
    }

    m_RawWorkspaceLocalRangeTargetPointIndex = -1;
    float bestDist = std::numeric_limits<float>::max();
    const Stack::RawRecipe::RawLocalRangeRecipe acceptedRange =
        BuildLocalRangeUiRecipe(m_ActiveRawWorkspaceRecipe.localRange);
    for (int i = 1; i < static_cast<int>(acceptedRange.points.size()) - 1; ++i) {
        const Stack::RawRecipe::RawLocalRangePoint& point =
            acceptedRange.points[static_cast<std::size_t>(i)];
        const float dist = std::abs(point.ev - targetEv) + std::abs(point.deltaEv - targetDeltaEv);
        if (dist < bestDist) {
            bestDist = dist;
            m_RawWorkspaceLocalRangeTargetPointIndex = i;
        }
    }
    return true;
}

void EditorModule::HandleRawWorkspaceLocalRangeTargetInteraction(
    const Stack::RawWorkspace::SourceRecord& selectedSource,
    const ImVec2& imageMin,
    const ImVec2& imageMax,
    bool selectedProjectActive,
    bool currentRawPreview) {
    if (!m_RawWorkspaceLocalRangeTargetMode ||
        !selectedProjectActive ||
        !currentRawPreview ||
        imageMax.x <= imageMin.x ||
        imageMax.y <= imageMin.y) {
        if (m_RawWorkspaceLocalRangeTargetDragging &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_RawWorkspaceLocalRangeTargetDragging = false;
        }
        return;
    }

    const ImRect imageRect(imageMin, imageMax);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered =
        imageRect.Contains(mouse) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (hovered || m_RawWorkspaceLocalRangeTargetDragging) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_RawWorkspaceLocalRangeTargetDragging = true;
        m_RawWorkspaceLocalRangeTargetSamplePending = true;
        m_RawWorkspaceLocalRangeTargetSampleValid = false;
        m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
        m_RawWorkspaceLocalRangeTargetSourceKey = selectedSource.relativePathKey;
        m_RawWorkspaceLocalRangeTargetU =
            std::clamp((mouse.x - imageRect.Min.x) / std::max(1.0f, imageRect.GetWidth()), 0.0f, 1.0f);
        m_RawWorkspaceLocalRangeTargetV =
            std::clamp((mouse.y - imageRect.Min.y) / std::max(1.0f, imageRect.GetHeight()), 0.0f, 1.0f);
        m_RawWorkspaceLocalRangeTargetSceneEv = 0.0f;
        m_RawWorkspaceLocalRangeTargetSceneLuma = 0.0f;
        m_RawWorkspaceLocalRangeTargetSceneR = 0.0f;
        m_RawWorkspaceLocalRangeTargetSceneG = 0.0f;
        m_RawWorkspaceLocalRangeTargetSceneB = 0.0f;
        m_RawWorkspaceLocalRangeTargetStartMouseY = mouse.y;
        m_RawWorkspaceLocalRangeTargetDeltaEv = 0.0f;
        m_RawWorkspaceLocalRangeTargetPointIndex = -1;
        NoteRawWorkspaceRecipePreviewEdit(true);
        MarkRenderRefreshDirty();
    }

    if (!m_RawWorkspaceLocalRangeTargetDragging ||
        m_RawWorkspaceLocalRangeTargetSourceKey != selectedSource.relativePathKey) {
        return;
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float deltaEv = std::clamp(
            (m_RawWorkspaceLocalRangeTargetStartMouseY - mouse.y) /
                kRawLocalRangeTargetDragPixelsPerEv,
            kRawLocalRangeMinDeltaEv,
            kRawLocalRangeMaxDeltaEv);
        if (std::abs(deltaEv - m_RawWorkspaceLocalRangeTargetDeltaEv) >= 0.01f) {
            m_RawWorkspaceLocalRangeTargetDeltaEv = deltaEv;
            if (m_RawWorkspaceLocalRangeTargetSampleValid) {
                ApplyRawWorkspaceLocalRangeTargetDelta(true);
            } else if (m_RawWorkspaceLocalRangeTargetSamplePending) {
                m_RawWorkspaceLocalRangeTargetApplyWhenSampled = true;
            }
        }
        return;
    }

    const bool shouldApply = std::abs(m_RawWorkspaceLocalRangeTargetDeltaEv) > 0.005f;
    if (shouldApply && m_RawWorkspaceLocalRangeTargetSampleValid) {
        ApplyRawWorkspaceLocalRangeTargetDelta(false);
    } else if (shouldApply && m_RawWorkspaceLocalRangeTargetSamplePending) {
        m_RawWorkspaceLocalRangeTargetApplyWhenSampled = true;
    }
    m_RawWorkspaceLocalRangeTargetDragging = false;
}

void EditorModule::ClearRawWorkspaceLivePreviewState() {
    ClearViewportOutputTiles();
    int outputWidth = 0;
    int outputHeight = 0;
    EditorRenderWorker::SharedTextureResult outputTexture;
    outputTexture.texture = m_Pipeline.TakeExternalOutputTexture(outputWidth, outputHeight);
    outputTexture.width = outputWidth;
    outputTexture.height = outputHeight;
    QueueViewportOutputTextureRelease(outputTexture);
    m_RawWorkspacePreviewSourceKey.clear();
    m_RawWorkspaceFastPreviewUntilTime = -1.0;
    m_RawWorkspaceFullResolutionPreviewPending = false;
    m_RawWorkspaceFullResolutionPreviewRequested = false;
    m_RawWorkspaceViewTransformInputStats = {};
    m_RawWorkspaceAnalysis = Stack::RawAnalysis::RawImageAnalysis();
    ClearRawWorkspaceLocalRangeTargetState(true);
}

void EditorModule::ReleaseRawWorkspacePreviewForTabChange() {
    const bool hasRawPreviewState =
        !m_ViewportOutputRawWorkspaceSourceKey.empty() ||
        !m_RawWorkspacePreviewSourceKey.empty() ||
        m_RawWorkspaceFullResolutionPreviewPending ||
        m_RawWorkspacePreviewOutputKind != RawWorkspacePreviewOutputKind::None;
    if (!hasRawPreviewState) {
        return;
    }

    ClearRawWorkspaceLivePreviewState();
    if (IsRawWorkspaceProjectActive()) {
        MarkRenderRefreshDirty();
    }
}

void EditorModule::RenderRawWorkspaceControlsPanel(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    const Stack::RawWorkspace::RawPanelState& panelState) {
    if (selectedSource == nullptr) {
        return;
    }

    const bool selectedProjectActive =
        IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == selectedSource->relativePathKey;
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
            : "Loading this RAW project before editing.";
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
            recipeError = "Preparing RAW preview...";
        }
    }
    const bool canEdit =
        panelState.recipeControlsEditable &&
        recipeResolved &&
        selectedProjectActive &&
        !selectedProjectLoading &&
        !selectedProjectLoadFailed;

    const bool selectedProjectSaving =
        selectedProjectActive && IsRawWorkspaceProjectSaveBusy();
    const bool showInlineSave =
        selectedProjectActive && (m_Dirty || selectedProjectSaving);
    const RawWorkspaceSuggestionBadgeSummary suggestionSummary =
        BuildRawWorkspaceSuggestionBadgeSummary(
            m_RawWorkspaceAutoBaseUi,
            m_RawWorkspaceAnalysis,
            selectedSource->relativePathKey);
    const std::vector<RawWorkspaceSuggestionPopoutItem> suggestionItems =
        BuildRawWorkspaceSuggestionPopoutItems(
            m_RawWorkspaceAutoBaseUi,
            m_RawWorkspaceAnalysis,
            selectedSource->relativePathKey);
    const std::uint64_t selectedSourceHash = BuildRawWorkspaceAutoBaseSourceHash(*selectedSource);

    Stack::RawRecipe::RawDevelopmentRecipe editedRecipe = recipe;
    bool changed = false;

    const float moreWidth = 38.0f;
    const float saveWidth = showInlineSave ? 54.0f : 0.0f;
    const float actionGap = ImGui::GetStyle().ItemSpacing.x;

    if (showInlineSave) {
        ImGui::BeginDisabled(selectedProjectSaving);
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
        if (ImGui::MenuItem("Open In Graph", nullptr, false, panelState.openGraphEnabled)) {
            OpenRawWorkspaceProjectInGraph(*selectedSource);
            ImGui::CloseCurrentPopup();
        }
        TooltipIfHovered(panelState.graphTooltip.c_str(), ImGuiHoveredFlags_AllowWhenDisabled);

        if (ImGui::MenuItem("Save", nullptr, false, selectedProjectActive)) {
            SaveActiveRawWorkspaceProject(true);
            ImGui::CloseCurrentPopup();
        }
        TooltipIfHovered(
            selectedProjectActive ? "Save this RAW project." : "Open or edit this RAW project before saving.",
            ImGuiHoveredFlags_AllowWhenDisabled);

        ImGui::Separator();
        if (resolvedMode == Stack::RawWorkspace::RawProjectMode::RecipeBacked) {
            const bool enabled = selectedProjectActive && recipeResolved;
            if (ImGui::MenuItem("Convert to Nodes", nullptr, false, enabled)) {
                DecomposeActiveRawWorkspaceProjectToManagedGraph();
                ImGui::CloseCurrentPopup();
            }
            TooltipIfHovered(
                enabled
                    ? "Create a managed RAW graph section from this recipe."
                    : "Open or edit this RAW project before converting it.",
                ImGuiHoveredFlags_AllowWhenDisabled);
        } else if (resolvedMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
            if (ImGui::MenuItem("Validate RAW Chain", nullptr, false, selectedProjectActive)) {
                ValidateActiveRawWorkspaceManagedGraph(true);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Use Graph as Recipe", nullptr, false, selectedProjectActive)) {
                ReadoptActiveRawWorkspaceGraphAsRecipe();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Detach From RAW Tab", nullptr, false, selectedProjectActive)) {
                DetachActiveRawWorkspaceGraphFromRawTab();
                ImGui::CloseCurrentPopup();
            }
        } else if (resolvedMode == Stack::RawWorkspace::RawProjectMode::CustomGraph) {
            if (ImGui::MenuItem("Repair RAW Chain", nullptr, false, selectedProjectActive)) {
                RepairActiveRawWorkspaceManagedGraph();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Use Graph as Recipe", nullptr, false, selectedProjectActive)) {
                ReadoptActiveRawWorkspaceGraphAsRecipe();
                ImGui::CloseCurrentPopup();
            }
        }

        if (panelState.hasProject) {
            ImGui::Separator();
            if (ImGui::MenuItem("Relink")) {
                m_ShowRawWorkspaceRelinkPopup = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Embed")) {
                m_ShowRawWorkspaceEmbedPopup = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    const float suggestionButtonWidth =
        std::min(120.0f, std::max(1.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::Button("Suggestions", ImVec2(suggestionButtonWidth, 0.0f))) {
        m_RawWorkspaceAutoBaseUi.suggestionsOpen = !m_RawWorkspaceAutoBaseUi.suggestionsOpen;
    }
    TooltipIfHovered("Open active RAW suggestions. Closing this panel does not change the recipe.");

    if (m_RawWorkspaceAutoBaseUi.suggestionsOpen) {
        ImGui::Spacing();
        const float itemHeight = 74.0f;
        const float expanderHeight =
            std::clamp(76.0f + itemHeight * static_cast<float>(std::max<std::size_t>(1, suggestionItems.size())),
                       150.0f,
                       320.0f);
        ImGui::BeginChild(
            "##RawWorkspaceSuggestionsExpander",
            ImVec2(0.0f, expanderHeight),
            true,
            ImGuiWindowFlags_None);
        ImGui::TextUnformatted("Suggestions");
        const float closeWidth = ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - closeWidth));
        if (ImGui::SmallButton("X##RawWorkspaceSuggestionsClose")) {
            m_RawWorkspaceAutoBaseUi.suggestionsOpen = false;
        }
        TooltipIfHovered("Close suggestions. No recipe changes are made.");

        auto markSuggestionApplied = [&](const RawWorkspaceSuggestionPopoutItem& item) {
            m_RawWorkspaceAutoBaseUi.sourceKey = selectedSource->relativePathKey;
            m_RawWorkspaceAutoBaseUi.sourceHash = selectedSourceHash;
            m_RawWorkspaceAutoBaseUi.appliedSuggestionKey = item.key;
            m_RawWorkspaceAutoBaseUi.appliedSuggestionLabel = item.actionLabel;
            m_RawWorkspaceAutoBaseUi.appliedSuggestionSection = item.section;
            m_RawWorkspaceAutoBaseUi.appliedSuggestionSourceHash = selectedSourceHash;
            m_RawWorkspaceAutoBaseUi.appliedSuggestionAnalysisHash =
                BuildRawWorkspaceSuggestionAnalysisHash(m_RawWorkspaceAnalysis);
        };

        auto applySuggestion = [&](const RawWorkspaceSuggestionPopoutItem& item) {
            bool applied = false;
            switch (item.kind) {
                case RawWorkspaceSuggestionPopoutKind::RawExposure:
                    applied = ApplyRawWorkspaceAutoBaseExposureSuggestion(editedRecipe);
                    break;
                case RawWorkspaceSuggestionPopoutKind::WhiteBalance:
                    applied = ApplyRawWorkspaceAutoBaseWhiteBalanceSuggestion(editedRecipe);
                    break;
                case RawWorkspaceSuggestionPopoutKind::HighlightProtection:
                    applied = ApplyRawWorkspaceAutoBaseHighlightProtection(editedRecipe);
                    break;
                case RawWorkspaceSuggestionPopoutKind::LocalRange:
                    applied = ApplyRawWorkspaceAutoBaseLocalSuggestion(
                        item.localSuggestionIndex,
                        editedRecipe);
                    break;
                case RawWorkspaceSuggestionPopoutKind::AppliedOnly:
                default:
                    applied = false;
                    break;
            }
            if (applied) {
                editedRecipe = m_ActiveRawWorkspaceRecipe;
                recipe = editedRecipe;
                changed = false;
                markSuggestionApplied(item);
            }
            return applied;
        };

        if (suggestionSummary.known && !suggestionItems.empty()) {
            for (std::size_t i = 0; i < suggestionItems.size(); ++i) {
                const RawWorkspaceSuggestionPopoutItem& item = suggestionItems[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Separator();
                ImGui::TextUnformatted(item.actionLabel.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s", item.section.c_str());
                if (!item.detail.empty()) {
                    ImGui::TextDisabled("%s", item.detail.c_str());
                }
                if (!item.rationale.empty()) {
                    ImGui::TextWrapped("%s", item.rationale.c_str());
                }

                const bool applyDisabled =
                    !canEdit ||
                    item.applied ||
                    item.kind == RawWorkspaceSuggestionPopoutKind::AppliedOnly;
                ImGui::BeginDisabled(applyDisabled);
                if (ImGui::Button(item.applied ? "Applied" : "Apply", ImVec2(84.0f, 0.0f))) {
                    applySuggestion(item);
                }
                ImGui::EndDisabled();
                TooltipIfHovered(
                    canEdit
                        ? "Apply this as a normal visible recipe edit."
                        : "Open or create this RAW project before applying suggestions.",
                    ImGuiHoveredFlags_AllowWhenDisabled);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    (void)recipeError;

    ImGui::Spacing();
    ImGui::Separator();

    const float controlWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);

    ImGui::BeginDisabled(!canEdit);

    if (RenderRawWorkspaceAutoBasePanel(selectedSource, editedRecipe, controlWidth)) {
        editedRecipe = m_ActiveRawWorkspaceRecipe;
        changed = false;
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Base Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnsureViewTransformJson(editedRecipe.viewTransform.layerJson);
        nlohmann::json& viewTransform = editedRecipe.viewTransform.layerJson;
        const bool viewTransformAutoOwned =
            RawWorkspaceViewTransformAutoOwnedForSource(selectedSource->relativePathKey);
        const bool viewTransformEditedOwned =
            m_RawWorkspaceAutoBaseUi.sourceKey == selectedSource->relativePathKey &&
            m_RawWorkspaceAutoBaseUi.viewTransformOwner == RawAutoValueOwner::User;
        auto viewLabel = [&](const char* baseLabel) {
            static std::string label;
            label = baseLabel;
            if (viewTransformAutoOwned) {
                label += " [Auto]";
            } else if (viewTransformEditedOwned) {
                label += " [Edited]";
            }
            return label.c_str();
        };
        bool viewTransformChangedByUser = false;

        float exposureEv = editedRecipe.preToneExposureEv;
        if (ImGuiExtras::NodeSliderFloat("RAW Exposure", "##RawExposureEv", &exposureEv, -8.0f, 8.0f, "%+.2f EV", controlWidth)) {
            editedRecipe.preToneExposureEv = exposureEv;
            changed = true;
        }
        TooltipIfHovered("+1 EV multiplies scene-linear decoded values by 2 before tone shaping.");

        if (ImGuiExtras::RichFullWidthButton("Refit", controlWidth, 0.0f)) {
            if (m_RawWorkspaceAnalysis.currentFrameStats.valid &&
                m_RawWorkspaceAnalysis.sourceKey == selectedSource->relativePathKey) {
                if (ApplyRawWorkspaceAutoBaseViewFitForSource(*selectedSource, editedRecipe, true)) {
                    editedRecipe = m_ActiveRawWorkspaceRecipe;
                    changed = false;
                }
            } else {
                QueueUiNotification(
                    UiNotificationSeverity::Info,
                    "Render a RAW preview before refitting the display.",
                    "raw-workspace-view-auto-no-stats");
            }
        }
        TooltipIfHovered("Fits Display Fit / View Transform from the current frame. RAW Exposure remains unchanged.");

        if (ImGui::TreeNodeEx("Advanced##RawBaseLightAdvanced")) {
            if (ImGuiExtras::RichFullWidthButton("Reset", controlWidth, 0.0f)) {
                viewTransform = Stack::RawRecipe::DefaultViewTransformJson();
                changed = true;
                viewTransformChangedByUser = true;
            }
            TooltipIfHovered("Returns the display mapping to generic scene-linear defaults.");

            float viewExposure = JsonNumber(viewTransform, "exposure", 0.0f);
            float blackEv = JsonNumber(viewTransform, "blackEv", -8.0f);
            float whiteEv = JsonNumber(viewTransform, "whiteEv", 4.0f);
            float middleGrey = JsonNumber(viewTransform, "middleGrey", 0.18f);
            float shoulder = JsonNumber(viewTransform, "shoulder", 0.45f);
            float toe = JsonNumber(viewTransform, "toe", 0.18f);
            float contrast = JsonNumber(viewTransform, "contrast", 1.0f);
            float saturation = JsonNumber(viewTransform, "saturation", 1.0f);
            bool preserveHue = JsonBool(viewTransform, "preserveHue", true);
            bool falseColor = JsonBool(viewTransform, "debugFalseColor", false);

            if (ImGuiExtras::NodeSliderFloat("Display Exposure", "##RawViewExposure", &viewExposure, -8.0f, 8.0f, "%.2f stops", controlWidth)) {
                viewTransform["exposure"] = viewExposure;
                changed = true;
                viewTransformChangedByUser = true;
            }
            TooltipIfHovered("Display-stage exposure offset inside the View Transform. Use RAW Exposure for scene-linear capture placement.");
            if (ImGuiExtras::NodeSliderFloat(viewLabel("Black EV"), "##RawViewBlackEv", &blackEv, -16.0f, 0.0f, "%.2f", controlWidth)) {
                viewTransform["blackEv"] = blackEv;
                changed = true;
                viewTransformChangedByUser = true;
            }
            TooltipIfHovered("Scene EV below Middle Grey that maps near display black after black subtraction.");
            if (ImGuiExtras::NodeSliderFloat(viewLabel("White EV"), "##RawViewWhiteEv", &whiteEv, 0.0f, 16.0f, "%.2f", controlWidth)) {
                viewTransform["whiteEv"] = whiteEv;
                changed = true;
                viewTransformChangedByUser = true;
            }
            TooltipIfHovered("Scene EV above Middle Grey that anchors display white before shoulder rolloff.");
            if (ImGuiExtras::NodeSliderFloat(viewLabel("Middle Grey"), "##RawViewMiddleGrey", &middleGrey, 0.01f, 1.0f, "%.3f", controlWidth)) {
                viewTransform["middleGrey"] = middleGrey;
                changed = true;
                viewTransformChangedByUser = true;
            }
            TooltipIfHovered("Scene-linear luma anchor for the EV scale. Auto Fit sets this from median input luma.");
            if (ImGuiExtras::NodeSliderFloat(viewLabel("Shoulder"), "##RawViewShoulder", &shoulder, 0.05f, 4.0f, "%.2f", controlWidth)) {
                viewTransform["shoulder"] = shoulder;
                changed = true;
                viewTransformChangedByUser = true;
            }
            if (ImGuiExtras::NodeSliderFloat(viewLabel("Toe"), "##RawViewToe", &toe, 0.0f, 1.0f, "%.2f", controlWidth)) {
                viewTransform["toe"] = toe;
                changed = true;
                viewTransformChangedByUser = true;
            }
            if (ImGuiExtras::NodeSliderFloat("Contrast", "##RawViewContrast", &contrast, 0.25f, 2.5f, "%.2f", controlWidth)) {
                viewTransform["contrast"] = contrast;
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
            ImGui::TreePop();
        }

        if (viewTransformChangedByUser) {
            MarkRawWorkspaceViewTransformUserEdited();
        }
    }

    if (ImGui::CollapsingHeader("White Balance", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* labels[] = { "As Shot", "Auto", "Custom", "Gray Point" };
        int wbIndex = WhiteBalanceModeToIndex(editedRecipe.whiteBalance.mode);
        if (ImGuiExtras::NodeCombo("Mode", "##RawWhiteBalanceMode", &wbIndex, labels, IM_ARRAYSIZE(labels), controlWidth)) {
            editedRecipe.whiteBalance.mode = WhiteBalanceModeFromIndex(wbIndex);
            if (editedRecipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers) {
                editedRecipe.whiteBalance.hasTemperatureKelvin = true;
                editedRecipe.whiteBalance.temperatureKelvin =
                    editedRecipe.whiteBalance.temperatureKelvin <= 0.0f ? 5500.0f : editedRecipe.whiteBalance.temperatureKelvin;
                editedRecipe.whiteBalance.hasTint = true;
                editedRecipe.whiteBalance.hasMultipliers = true;
            }
            if (editedRecipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::SampledGrayPoint) {
                editedRecipe.whiteBalance.hasSamplePoint = true;
            }
            changed = true;
        }

        if (editedRecipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers) {
            float temperature = editedRecipe.whiteBalance.hasTemperatureKelvin
                ? editedRecipe.whiteBalance.temperatureKelvin
                : 5500.0f;
            float tint = editedRecipe.whiteBalance.hasTint ? editedRecipe.whiteBalance.tint : 0.0f;
            if (ImGuiExtras::NodeSliderFloat("Temperature", "##RawWbTemperature", &temperature, 2000.0f, 12000.0f, "%.0f K", controlWidth)) {
                editedRecipe.whiteBalance.temperatureKelvin = temperature;
                editedRecipe.whiteBalance.hasTemperatureKelvin = true;
                changed = true;
            }
            if (ImGuiExtras::NodeSliderFloat("Tint", "##RawWbTint", &tint, -150.0f, 150.0f, "%+.0f", controlWidth)) {
                editedRecipe.whiteBalance.tint = tint;
                editedRecipe.whiteBalance.hasTint = true;
                changed = true;
            }
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
        } else if (editedRecipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::SampledGrayPoint) {
            float sampleX = editedRecipe.whiteBalance.sampleX;
            float sampleY = editedRecipe.whiteBalance.sampleY;
            if (ImGuiExtras::NodeSliderFloat("Gray Point X", "##RawGrayPointX", &sampleX, 0.0f, 1.0f, "%.3f", controlWidth)) {
                editedRecipe.whiteBalance.sampleX = sampleX;
                editedRecipe.whiteBalance.hasSamplePoint = true;
                changed = true;
            }
            if (ImGuiExtras::NodeSliderFloat("Gray Point Y", "##RawGrayPointY", &sampleY, 0.0f, 1.0f, "%.3f", controlWidth)) {
                editedRecipe.whiteBalance.sampleY = sampleY;
                editedRecipe.whiteBalance.hasSamplePoint = true;
                changed = true;
            }
            ImGui::BeginDisabled();
            ImGui::Button("Pick Gray Point", ImVec2(-1.0f, 0.0f));
            ImGui::EndDisabled();
            TooltipIfHovered("Gray-point picking is reserved for the final picker backend.", ImGuiHoveredFlags_AllowWhenDisabled);
        }
    }

    ImGui::Separator();

    if (RenderRawWorkspaceLocalRangeControls(selectedSource, editedRecipe, controlWidth)) {
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

        const char* domainLabels[] = { "Scene Linear", "Log Scene" };
        int domain = std::clamp(JsonInt(finishTone, "domain", 1), 0, 1);
        if (ImGuiExtras::NodeCombo("Curve Domain", "##RawFinishToneDomain", &domain, domainLabels, IM_ARRAYSIZE(domainLabels), controlWidth)) {
            finishTone["domain"] = domain;
            changed = true;
        }

        std::vector<Stack::RawRecipe::RawToneCurvePoint> points = BuildFinishToneUiPoints(finishTone);
        if (DrawToneCurveWidget(
                points,
                ImVec2(controlWidth, std::clamp(controlWidth * 0.42f, 170.0f, 220.0f)))) {
            StoreFinishToneUiPoints(finishTone, points);
            changed = true;
        }
        TooltipIfHovered("Curve points edit the graph-compatible finish tone layer.");

        if (ImGui::TreeNodeEx("Advanced##RawFinishToneAdvanced")) {
            if (domain == 1) {
                float logMinEv = JsonNumber(finishTone, "logMinEv", -10.0f);
                float logMaxEv = JsonNumber(finishTone, "logMaxEv", 6.0f);
                if (ImGuiExtras::NodeSliderFloat("Graph Black EV", "##RawFinishToneLogMinEv", &logMinEv, -20.0f, 0.0f, "%.2f", controlWidth)) {
                    finishTone["logMinEv"] = logMinEv;
                    changed = true;
                }
                if (ImGuiExtras::NodeSliderFloat("Graph White EV", "##RawFinishToneLogMaxEv", &logMaxEv, 0.0f, 20.0f, "%.2f", controlWidth)) {
                    finishTone["logMaxEv"] = logMaxEv;
                    changed = true;
                }
                if (logMaxEv <= logMinEv + 0.1f) {
                    finishTone["logMaxEv"] = logMinEv + 0.1f;
                    changed = true;
                }
            }

            if (ImGuiExtras::RichFullWidthButton("Reset Curve", controlWidth, 0.0f)) {
                nlohmann::json resetTone = Stack::RawRecipe::DefaultFinishToneJson();
                resetTone["mode"] = mode;
                resetTone["domain"] = domain;
                finishTone = std::move(resetTone);
                changed = true;
            }
            ImGui::TreePop();
        }
    }

    RenderRawWorkspaceAnalysisPanel(controlWidth);

    if (ImGui::CollapsingHeader("Crop & Rotate", ImGuiTreeNodeFlags_DefaultOpen)) {
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
    }

    if (ImGui::CollapsingHeader("Preview & Output", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* previewLabels[] = { "Developed Preview", "Neutral Preview" };
        const char* previewValues[] = { "developed-preview", "neutral-preview" };
        int previewIndex = editedRecipe.previewOutput.previewIntent == "neutral-preview" ? 1 : 0;
        if (ImGuiExtras::NodeCombo("Preview Intent", "##RawPreviewIntent", &previewIndex, previewLabels, IM_ARRAYSIZE(previewLabels), controlWidth)) {
            editedRecipe.previewOutput.previewIntent = previewValues[std::clamp(previewIndex, 0, 1)];
            changed = true;
        }

        const char* colorLabels[] = { "sRGB", "Display P3" };
        const char* colorValues[] = { "sRGB", "Display P3" };
        int colorIndex = editedRecipe.previewOutput.outputColorSpace == "Display P3" ? 1 : 0;
        if (ImGuiExtras::NodeCombo("Output Color Space", "##RawOutputColorSpace", &colorIndex, colorLabels, IM_ARRAYSIZE(colorLabels), controlWidth)) {
            editedRecipe.previewOutput.outputColorSpace = colorValues[std::clamp(colorIndex, 0, 1)];
            changed = true;
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
        m_ActiveRawWorkspaceSourceKey == selectedSource->relativePathKey;
    const bool selectedPreviewStageQueued =
        m_RawWorkspacePreviewStageQueued &&
        m_RawWorkspacePreviewStageSourceKey == selectedSource->relativePathKey;
    const bool localRangeActive =
        selectedProjectActive &&
        Stack::RawRecipe::IsLocalRangeEnabled(m_ActiveRawWorkspaceRecipe.localRange);
    const bool localRangeMaskAvailable =
        selectedProjectActive &&
        (m_ActiveRawWorkspaceRecipe.localRange.regionMaskEnabled ||
         m_ActiveRawWorkspaceRecipe.localRange.colorMaskEnabled);

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
    ImGui::BeginDisabled(true);
    ImGui::Button("Risk", ImVec2(54.0f, 0.0f));
    ImGui::EndDisabled();
    TooltipIfHovered("Highlight Risk is reported in Diagnostics until a viewport overlay exists.", ImGuiHoveredFlags_AllowWhenDisabled);

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
        m_Pipeline.GetOutputTexture() != 0 &&
        m_Pipeline.GetCanvasWidth() > 0 &&
        m_Pipeline.GetCanvasHeight() > 0) {
        const ImVec2 imageSize = FitImageSize(
            static_cast<float>(m_Pipeline.GetCanvasWidth()),
            static_cast<float>(m_Pipeline.GetCanvasHeight()),
            imageBounds);
        const ImVec2 imageMin(
            bounds.Min.x + (imageBounds.x - imageSize.x) * 0.5f,
            bounds.Min.y + std::max(0.0f, (imageBounds.y - imageSize.y) * 0.48f));
        const ImRect imageRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
        const float uInset = m_Pipeline.GetCanvasWidth() > 1
            ? (0.5f / static_cast<float>(m_Pipeline.GetCanvasWidth()))
            : 0.0f;
        const float vInset = m_Pipeline.GetCanvasHeight() > 1
            ? (0.5f / static_cast<float>(m_Pipeline.GetCanvasHeight()))
            : 0.0f;
        drawList->AddImage(
            (ImTextureID)(intptr_t)m_Pipeline.GetOutputTexture(),
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
        int textureWidth = 0;
        int textureHeight = 0;
        const unsigned int texture = GetRawWorkspaceThumbnailTexture(*selectedSource, &textureWidth, &textureHeight);
        const ImVec2 imageSize = texture != 0
            ? FitImageSize(static_cast<float>(textureWidth), static_cast<float>(textureHeight), imageBounds)
            : FitImageSize(4.0f, 3.0f, imageBounds);
        const ImVec2 imageMin(
            bounds.Min.x + (imageBounds.x - imageSize.x) * 0.5f,
            bounds.Min.y + std::max(0.0f, (imageBounds.y - imageSize.y) * 0.48f));
        const ImRect imageRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
        if (texture != 0) {
            drawList->AddImage(
                (ImTextureID)(intptr_t)texture,
                imageRect.Min,
                imageRect.Max,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
        } else {
            DrawRawPlaceholder(drawList, imageRect, true);
        }

    }

    ImGui::Dummy(ImVec2(imageBounds.x, imageBounds.y));
}

void EditorModule::RenderRawWorkspaceEmptyState(const RawWorkspaceScanSnapshot& scanSnapshot) {
    ImGui::TextUnformatted("RAW Workspace");
    ImGui::Spacing();
    if (ImGui::Button("Open RAW Folder", ImVec2(180.0f, 0.0f))) {
        OpenRawWorkspaceFolderDialog();
    }

    if (!scanSnapshot.statusText.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", scanSnapshot.statusText.c_str());
    }

    if (!m_RawWorkspace.recentWorkspaceRoots.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Recent Workspaces");
        for (std::size_t index = 0; index < m_RawWorkspace.recentWorkspaceRoots.size(); ++index) {
            const std::filesystem::path& recent = m_RawWorkspace.recentWorkspaceRoots[index];
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(recent.string().c_str(), false)) {
                RequestOpenRawWorkspace(recent);
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

    auto renderToggleButton = [](const char* label, bool active, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        const bool clicked = ImGui::Button(label, size);
        if (active) {
            ImGui::PopStyleColor(3);
        }
        return clicked;
    };

    if (ImGui::Button("Open RAW Folder", ImVec2(150.0f, 0.0f))) {
        OpenRawWorkspaceFolderDialog();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan", ImVec2(90.0f, 0.0f))) {
        RescanRawWorkspace();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(80.0f, 0.0f))) {
        m_RawWorkspaceGalleryWindowOpen = false;
        ClearRawWorkspaceForUser();
        return;
    }
    ImGui::SameLine(0.0f, 22.0f);
    if (renderToggleButton("Grid", m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::Grid, ImVec2(70.0f, 0.0f))) {
        m_RawWorkspaceGalleryDisplayMode = Stack::RawWorkspace::GalleryDisplayMode::Grid;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (renderToggleButton("List", m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::List, ImVec2(70.0f, 0.0f))) {
        m_RawWorkspaceGalleryDisplayMode = Stack::RawWorkspace::GalleryDisplayMode::List;
    }
    ImGui::SameLine(0.0f, 18.0f);
    if (renderToggleButton("Gallery", m_RawWorkspaceGalleryWindowOpen, ImVec2(90.0f, 0.0f))) {
        m_RawWorkspaceGalleryWindowOpen = !m_RawWorkspaceGalleryWindowOpen;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", m_RawWorkspaceGalleryWindowOpen ? "Close gallery" : "Open gallery");
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
        if (clicked) {
            SelectRawWorkspaceSource(source.relativePathKey);
        }
        if (hovered) {
            ImGui::SetTooltip("%s", source.relativePathKey.c_str());
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect tileRect(
            tileMin,
            ImVec2(tileMin.x + tileSize.x, tileMin.y + tileSize.y));
        const bool selected = view.selected || source.relativePathKey == m_RawWorkspace.selectedSourceKey;
        if (hovered || selected) {
            ImU32 tileFill = ImGui::GetColorU32(
                selected ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBgHovered,
                selected ? 0.58f : 0.34f);
            drawList->AddRectFilled(tileRect.Min, tileRect.Max, tileFill, 6.0f);
        }

        int textureWidth = 0;
        int textureHeight = 0;
        const unsigned int texture = GetRawWorkspaceThumbnailTexture(source, &textureWidth, &textureHeight);
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
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
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
                    const bool selected = view.selected || source->relativePathKey == m_RawWorkspace.selectedSourceKey;
                    const std::string row =
                        source->fileName + "    " +
                        FormatFileSize(source->fileSizeBytes) + "    " +
                        Stack::RawWorkspace::ThumbnailStatusLabel(source->thumbnail.status) + "    Project " +
                        Stack::RawWorkspace::ProjectStatusLabel(view.projectStatus);
                    if (ImGui::Selectable(row.c_str(), selected)) {
                        SelectRawWorkspaceSource(source->relativePathKey);
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
    const ImVec2 bodyAvail = ImGui::GetContentRegionAvail();
    m_RawWorkspaceLayoutUi.controlsPanelWidth =
        NormalizeRawWorkspaceControlsPanelWidth(m_RawWorkspaceLayoutUi.controlsPanelWidth);
    const float splitterFootprint =
        kRawWorkspaceSplitterWidth + (kRawWorkspaceSplitterSideGap * 2.0f);
    auto resolveControlsWidth = [&](float reservedWidth, float targetPreviewWidth) {
        return ResolveRawWorkspaceControlsPanelWidth(
            m_RawWorkspaceLayoutUi.controlsPanelWidth,
            bodyAvail.x,
            reservedWidth,
            targetPreviewWidth);
    };
    auto renderControlsPane = [&](const char* id, float controlsWidth, float height = 0.0f) {
        ImGui::BeginChild(id, ImVec2(controlsWidth, height), true);
        RenderRawWorkspaceControlsPanel(selectedSource, panelState);
        ImGui::EndChild();
    };
    auto renderControlsSplitter = [&](const char* id, float height) {
        if (RenderRawWorkspaceControlsSplitter(
                id,
                height,
                &m_RawWorkspaceLayoutUi.controlsPanelWidth)) {
            SaveRawWorkspaceAppState();
        }
    };

    const float controlsWidth = resolveControlsWidth(splitterFootprint, kRawWorkspacePreviewMinWidth);
    renderControlsPane("RawWorkspaceControlsPane", controlsWidth);
    renderControlsSplitter("##RawWorkspaceControlsSplitter", bodyAvail.y);
    ImGui::SameLine(0.0f, kRawWorkspaceSplitterSideGap);
    ImGui::BeginChild("RawWorkspacePreviewPane", ImVec2(0.0f, 0.0f), false);
    RenderRawWorkspacePreviewPanel(selectedSource, panelState);
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
                constexpr float closeWidth = 68.0f;

                ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
                ImGui::InvisibleButton(
                    "##RawWorkspaceGalleryHeaderDragZone",
                    ImVec2(std::max(10.0f, windowSize.x - closeWidth - 24.0f), 42.0f));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    ImGui::SetWindowPos(ImVec2(windowPos.x + delta.x, windowPos.y + delta.y));
                }

                ImGui::SetCursorPos(savedCursorPos);
            }

            ImGui::TextUnformatted("Gallery");
            ImGui::SameLine();
            constexpr float closeWidth = 68.0f;
            ImGui::SetCursorPosX(std::max(
                ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - closeWidth));
            if (ImGui::Button("Close", ImVec2(closeWidth, 0.0f))) {
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
    EnsureRawWorkspaceLoaded();
    PumpNonRenderingWork(2.5);
    PumpRawWorkspaceThumbnailTextureUploads();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
    ImGui::BeginChild(
        "RawWorkspaceRoot",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const RawWorkspaceScanSnapshot scanSnapshot = GetRawWorkspaceScanSnapshot();
    const RawWorkspaceThumbnailSnapshot thumbnailSnapshot = GetRawWorkspaceThumbnailSnapshot();
    if (m_RawWorkspace.workspaceRoot.empty()) {
        RenderRawWorkspaceEmptyState(scanSnapshot);
    } else {
        if (scanSnapshot.state == Async::TaskState::Idle) {
            QueueSelectedRawWorkspaceSourcePreviewStaging();
        }
        RenderRawWorkspaceBrowser(scanSnapshot, thumbnailSnapshot);
    }
    RenderRawWorkspaceLifecyclePopups();
    const bool rawProjectLoadBusy = IsRawWorkspaceProjectLoadBusy();
    const bool rawProjectSaveBusy =
        Async::IsBusy(m_RawWorkspaceProjectSaveTaskState) ||
        m_RawWorkspaceProjectSaveInFlightCount > 0;
    const bool rawThumbnailBusy = Async::IsBusy(thumbnailSnapshot.state);
    if (Async::IsBusy(scanSnapshot.state)) {
        ImGuiExtras::RenderBusyOverlay(
            scanSnapshot.statusText.empty()
                ? "Loading RAW Workspace..."
                : scanSnapshot.statusText.c_str());
    } else if (rawProjectLoadBusy) {
        const std::string status = GetRawWorkspaceProjectLoadStatusText();
        ImGuiExtras::RenderBusyOverlay(
            status.empty()
                ? "Loading RAW project..."
                : status.c_str());
    } else if (rawProjectSaveBusy) {
        ImGuiExtras::RenderBusyOverlay(
            m_RawWorkspaceProjectSaveStatusText.empty()
                ? "Saving RAW project..."
                : m_RawWorkspaceProjectSaveStatusText.c_str());
    } else if (rawThumbnailBusy) {
        const Stack::RawWorkspace::ThumbnailProgress& progress = thumbnailSnapshot.progress;
        const int denominator = std::max(1, progress.total);
        const float fraction = static_cast<float>(
            std::clamp(progress.completed + progress.failed, 0, denominator)) /
            static_cast<float>(denominator);
        ImGuiExtras::RenderProgressOverlay(
            thumbnailSnapshot.statusText.empty()
                ? "Generating RAW thumbnails..."
                : thumbnailSnapshot.statusText.c_str(),
            fraction);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
