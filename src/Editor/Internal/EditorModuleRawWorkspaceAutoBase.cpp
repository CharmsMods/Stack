#include "Editor/EditorModule.h"

#include "Raw/RawAutoBase.h"
#include "Raw/RawLoader.h"
#include "Utils/ImGuiExtras.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

void TooltipIfHovered(const char* text, ImGuiHoveredFlags flags = 0) {
    if (text != nullptr && text[0] != '\0' && ImGui::IsItemHovered(flags)) {
        ImGui::SetTooltip("%s", text);
    }
}

std::uint64_t MixHash(std::uint64_t seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

std::uint64_t HashString64(const std::string& value) {
    return static_cast<std::uint64_t>(std::hash<std::string>{}(value));
}

std::uint64_t HashFloat64(float value) {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }
    return static_cast<std::uint64_t>(std::hash<float>{}(value));
}

std::uint64_t BuildAnalysisHash(const Stack::RawAnalysis::RawImageAnalysis& analysis) {
    const Stack::RawAnalysis::PercentileStats& stats = analysis.currentFrameStats;
    std::uint64_t seed = HashString64(analysis.sourceKey);
    seed = MixHash(seed, HashFloat64(stats.p01Ev));
    seed = MixHash(seed, HashFloat64(stats.p05Ev));
    seed = MixHash(seed, HashFloat64(stats.p50Ev));
    seed = MixHash(seed, HashFloat64(stats.p99Ev));
    seed = MixHash(seed, HashFloat64(stats.p999Ev));
    seed = MixHash(seed, HashFloat64(stats.dynamicRangeEv));
    seed = MixHash(seed, HashFloat64(analysis.highlight.displayClipPercent));
    seed = MixHash(seed, HashFloat64(analysis.highlight.hdrPixelPercent));
    return seed;
}

bool SourceHasStoredProject(const Stack::RawWorkspace::SourceRecord& source) {
    return source.project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        source.project.status == Stack::RawWorkspace::ProjectStatus::Embedded;
}

std::string BuildAppliedSummary() {
    return "Auto Base applied: View fit from current frame. RAW Exposure unchanged.";
}

std::string BuildUnchangedSummary() {
    return "RAW Exposure unchanged. White balance unchanged. No local edits applied.";
}

std::string FormatSignedEv(float value) {
    std::ostringstream out;
    out << (value >= 0.0f ? "+" : "") << std::fixed << std::setprecision(2) << value << " EV";
    return out.str();
}

const char* WhiteBalanceMethodLabel(Stack::RawAutoBase::WhiteBalanceRecommendation::Method method) {
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

struct AutoBaseRecommendationCounts {
    int suggestions = 0;
    int warnings = 0;
    bool hasNoiseAdvisory = false;
};

AutoBaseRecommendationCounts CountAutoBaseRecommendations(
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations) {
    AutoBaseRecommendationCounts counts;

    const Stack::RawAutoBase::RawExposureRecommendation& exposure =
        recommendations.exposure;
    if (exposure.valid &&
        exposure.action == Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue &&
        !exposure.blockedByHighlightRisk) {
        ++counts.suggestions;
    }
    if (exposure.blockedByHighlightRisk) {
        ++counts.warnings;
    }

    const Stack::RawAutoBase::WhiteBalanceRecommendation& whiteBalance =
        recommendations.whiteBalance;
    if (whiteBalance.alternateCandidateAvailable &&
        whiteBalance.action == Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue &&
        !whiteBalance.manualWhiteBalanceProtected) {
        ++counts.suggestions;
    }

    const Stack::RawAutoBase::HighlightRecommendation& highlight =
        recommendations.highlight;
    if (highlight.recommendProtectiveViewShoulder &&
        highlight.protectionAction == Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue) {
        ++counts.suggestions;
    }
    if (highlight.recommendNoPositiveRawExposure ||
        highlight.recommendReconstruction ||
        highlight.recommendAchromaticClip) {
        ++counts.warnings;
    }

    for (const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion :
         recommendations.localAdjustments) {
        if (suggestion.valid) {
            ++counts.suggestions;
        }
    }

    const Stack::RawAutoBase::NoiseDetailRecommendation& noiseDetail =
        recommendations.noiseDetail;
    counts.hasNoiseAdvisory =
        noiseDetail.suggestChromaDenoise ||
        noiseDetail.suggestLumaDenoise ||
        noiseDetail.suggestReduceSharpening ||
        noiseDetail.shadowLiftEv >= 0.5f;
    return counts;
}

std::string FormatCountBadge(int count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

const char* WhiteBalanceBadgeLabel(Stack::RawRecipe::WhiteBalanceMode mode) {
    switch (mode) {
        case Stack::RawRecipe::WhiteBalanceMode::AsShot:
            return "WB as shot";
        case Stack::RawRecipe::WhiteBalanceMode::Auto:
            return "WB auto";
        case Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers:
            return "WB custom";
        case Stack::RawRecipe::WhiteBalanceMode::SampledGrayPoint:
            return "WB gray point";
        default:
            return "WB set";
    }
}

void RenderAutoBasePill(const char* label, bool warning = false) {
    const ImVec4 base = ImGui::GetStyleColorVec4(warning ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg);
    const ImVec4 hover = ImGui::GetStyleColorVec4(warning ? ImGuiCol_ButtonHovered : ImGuiCol_FrameBgHovered);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, base);
    ImGui::Button(label);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

} // namespace

void EditorModule::ResetRawWorkspaceAutoBaseState() {
    m_RawWorkspaceAutoBaseUi = RawWorkspaceAutoBaseUiState();
}

std::uint64_t EditorModule::BuildRawWorkspaceAutoBaseSourceHash(
    const Stack::RawWorkspace::SourceRecord& source) const {
    std::uint64_t seed = HashString64(source.relativePathKey);
    seed = MixHash(seed, HashString64(source.absolutePath.string()));
    seed = MixHash(seed, HashString64(source.fingerprint));
    seed = MixHash(seed, static_cast<std::uint64_t>(source.fileSizeBytes));
    seed = MixHash(seed, static_cast<std::uint64_t>(source.modifiedTimeTicks));
    return seed;
}

bool EditorModule::RawWorkspaceRecipeLooksDefaultForAutoBase(
    const Stack::RawWorkspace::SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) const {
    Stack::RawRecipe::RawDevelopmentRecipe defaults = BuildRawWorkspaceDefaultRecipe(source);
    return Stack::RawRecipe::SerializeRecipe(defaults).dump() ==
        Stack::RawRecipe::SerializeRecipe(recipe).dump();
}

bool EditorModule::RawWorkspaceViewTransformAutoOwnedForSource(const std::string& sourceKey) const {
    return !sourceKey.empty() &&
        m_RawWorkspaceAutoBaseUi.sourceKey == sourceKey &&
        m_RawWorkspaceAutoBaseUi.viewTransformOwner == RawAutoValueOwner::AutoBase;
}

Stack::RawAnalysis::RawMetadataSummary EditorModule::ResolveRawWorkspaceMetadataSummaryForAutoBase() const {
    const std::string activeSourcePath = m_ActiveRawWorkspaceRecipe.source.sourcePath;
    auto summaryFromMetadata = [](const Raw::RawMetadata& metadata) {
        return Stack::RawAnalysis::BuildRawMetadataSummary(metadata);
    };

    if (m_ActiveManagedRawSection.rawSourceNodeId > 0) {
        const EditorNodeGraph::Node* rawSourceNode =
            m_NodeGraph.FindNode(m_ActiveManagedRawSection.rawSourceNodeId);
        if (rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
            return summaryFromMetadata(rawSourceNode->rawSource.metadata);
        }
    }

    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawSource) {
            continue;
        }
        if (activeSourcePath.empty() || node.rawSource.sourcePath == activeSourcePath) {
            return summaryFromMetadata(node.rawSource.metadata);
        }
    }

    if (!activeSourcePath.empty()) {
        Raw::RawMetadata metadata;
        Raw::RawLoader::LoadMetadata(activeSourcePath, metadata);
        return summaryFromMetadata(metadata);
    }

    return Stack::RawAnalysis::RawMetadataSummary();
}

void EditorModule::RefreshRawWorkspaceAutoBaseRecommendations(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const Stack::RawAutoBase::AutoBaseRecommendations previousRecommendations =
        m_RawWorkspaceAutoBaseUi.recommendations;
    if (m_ActiveRawWorkspaceSourceKey.empty() ||
        m_RawWorkspaceAnalysis.sourceKey != m_ActiveRawWorkspaceSourceKey ||
        !m_RawWorkspaceAnalysis.currentFrameStats.valid) {
        m_RawWorkspaceAutoBaseUi.recommendations =
            Stack::RawAutoBase::AutoBaseRecommendations();
        return;
    }

    if (m_RawWorkspaceAutoBaseUi.hasMetadataSummary) {
        m_RawWorkspaceAnalysis.metadata = m_RawWorkspaceAutoBaseUi.metadataSummary;
    }
    Stack::RawAutoBase::AutoBaseRecommendations updated =
        Stack::RawAutoBase::BuildAutoBaseRecommendations(m_RawWorkspaceAnalysis, recipe);
    if (previousRecommendations.localReport.valid ||
        !previousRecommendations.localAdjustments.empty() ||
        !previousRecommendations.localSuggestionRationale.empty()) {
        updated.localAdjustments = previousRecommendations.localAdjustments;
        updated.localReport = previousRecommendations.localReport;
        updated.localSuggestionRationale = previousRecommendations.localSuggestionRationale;
    }
    updated.noiseDetail =
        Stack::RawAutoBase::BuildNoiseDetailRecommendation(
            m_RawWorkspaceAnalysis,
            recipe,
            &updated.localAdjustments,
            false);
    m_RawWorkspaceAutoBaseUi.recommendations = std::move(updated);
}

void EditorModule::MarkRawWorkspaceViewTransformUserEdited() {
    if (m_ActiveRawWorkspaceSourceKey.empty() ||
        m_RawWorkspaceAutoBaseUi.sourceKey != m_ActiveRawWorkspaceSourceKey) {
        return;
    }
    if (m_RawWorkspaceAutoBaseUi.viewTransformOwner != RawAutoValueOwner::AutoBase) {
        return;
    }
    m_RawWorkspaceAutoBaseUi.viewTransformOwner = RawAutoValueOwner::User;
    m_RawWorkspaceAutoBaseUi.summary =
        "View Transform edited manually. RAW Exposure unchanged. No local edits applied.";
}

bool EditorModule::ApplyRawWorkspaceAutoBaseViewFitForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    bool explicitApply) {
    if (source.relativePathKey.empty() ||
        source.relativePathKey != m_ActiveRawWorkspaceSourceKey ||
        source.relativePathKey != m_RawWorkspace.selectedSourceKey) {
        return false;
    }

    const std::uint64_t sourceHash = BuildRawWorkspaceAutoBaseSourceHash(source);
    if (m_RawWorkspaceAutoBaseUi.sourceKey != source.relativePathKey ||
        m_RawWorkspaceAutoBaseUi.sourceHash != sourceHash) {
        ResetRawWorkspaceAutoBaseState();
        m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
        m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    }

    if (!explicitApply) {
        if (m_RawWorkspaceAutoBaseUi.hasAppliedViewFit ||
            m_RawWorkspaceAutoBaseUi.viewTransformOwner == RawAutoValueOwner::User) {
            return false;
        }
        if (SourceHasStoredProject(source) ||
            !RawWorkspaceRecipeLooksDefaultForAutoBase(source, recipe)) {
            m_RawWorkspaceAutoBaseUi.summary =
                "Auto Base ready: existing or edited RAW recipes are not changed automatically.";
            return false;
        }
    }

    const Stack::RawAutoBase::ViewFitDecision decision =
        Stack::RawAutoBase::BuildAutoBaseViewFitDecision(m_RawWorkspaceAnalysis, recipe);
    if (!decision.canApply) {
        m_RawWorkspaceAutoBaseUi.summary = decision.summary.empty()
            ? "Auto Base pending: render preview to analyze the frame."
            : decision.summary;
        if (explicitApply) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "Render a RAW preview before applying Auto Base.",
                "raw-workspace-auto-base-no-stats");
        }
        return false;
    }

    if (!m_RawWorkspaceAutoBaseUi.hasRevertSnapshot ||
        m_RawWorkspaceAutoBaseUi.sourceKey != source.relativePathKey) {
        m_RawWorkspaceAutoBaseUi.beforeAutoBase = recipe;
        m_RawWorkspaceAutoBaseUi.hasRevertSnapshot = true;
    }

    Stack::RawAutoBase::ApplyViewTransformFitToRecipe(recipe, decision.fit);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    recipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.sourceKey = source.relativePathKey;
    m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    m_RawWorkspaceAutoBaseUi.appliedAnalysisHash = BuildAnalysisHash(m_RawWorkspaceAnalysis);
    m_RawWorkspaceAutoBaseUi.hasAppliedViewFit = true;
    m_RawWorkspaceAutoBaseUi.viewTransformOwner = RawAutoValueOwner::AutoBase;
    m_RawWorkspaceAutoBaseUi.summary = BuildAppliedSummary() + " " + BuildUnchangedSummary();
    RefreshRawWorkspaceAutoBaseRecommendations(recipe);
    return true;
}

bool EditorModule::ApplyRawWorkspaceAutoBaseExposureSuggestion(
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    const Stack::RawAutoBase::RawExposureRecommendation recommendation =
        m_RawWorkspaceAutoBaseUi.recommendations.exposure;
    if (!recommendation.valid ||
        recommendation.action != Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue ||
        recommendation.blockedByHighlightRisk) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    recipe.preToneExposureEv = std::clamp(recommendation.suggestedEv, -8.0f, 8.0f);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.summary =
        "Applied RAW Exposure suggestion: " + FormatSignedEv(recommendation.deltaEv) + ".";
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    return true;
}

bool EditorModule::ApplyRawWorkspaceAutoBaseWhiteBalanceSuggestion(
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    const Stack::RawAutoBase::WhiteBalanceRecommendation recommendation =
        m_RawWorkspaceAutoBaseUi.recommendations.whiteBalance;
    if (!recommendation.valid ||
        recommendation.action != Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue ||
        !recommendation.alternateCandidateAvailable ||
        recommendation.manualWhiteBalanceProtected) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    Stack::RawAutoBase::ApplyWhiteBalanceRecommendationToRecipe(recipe, recommendation);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.summary =
        std::string("Applied WB suggestion: ") + WhiteBalanceMethodLabel(recommendation.method) + ".";
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    return true;
}

bool EditorModule::ApplyRawWorkspaceAutoBaseHighlightProtection(
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    const Stack::RawAutoBase::HighlightRecommendation recommendation =
        m_RawWorkspaceAutoBaseUi.recommendations.highlight;
    if (!recommendation.valid ||
        recommendation.protectionAction != Stack::RawAutoBase::RecommendationAction::ApplyVisibleRecipeValue ||
        !recommendation.recommendProtectiveViewShoulder) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    const Stack::RawAutoBase::ViewTransformFit fit =
        Stack::RawAutoBase::FitViewTransformFromAnalysis(m_RawWorkspaceAnalysis);
    Stack::RawAutoBase::ApplyHighlightProtectionToRecipe(recipe, recommendation, fit);
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.viewTransformOwner = RawAutoValueOwner::AutoBase;
    m_RawWorkspaceAutoBaseUi.summary =
        "Applied highlight protection to View Transform shoulder/white EV. RAW Exposure unchanged.";
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    return true;
}

bool EditorModule::ApplyRawWorkspaceAutoBaseLocalSuggestion(
    std::size_t suggestionIndex,
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& suggestions =
        m_RawWorkspaceAutoBaseUi.recommendations.localAdjustments;
    if (suggestionIndex >= suggestions.size()) {
        return false;
    }

    const Stack::RawAutoBase::SuggestedLocalAdjustment suggestion =
        suggestions[suggestionIndex];
    Stack::RawRecipe::RawDevelopmentRecipe recipe = editedRecipe;
    if (!Stack::RawAutoBase::ApplySuggestedLocalAdjustment(suggestion, recipe)) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Local Range suggestion overlaps an existing point or the graph is full.",
            "raw-workspace-auto-base-local-overlap");
        return false;
    }

    if (!m_RawWorkspaceAutoBaseUi.hasRevertSnapshot ||
        m_RawWorkspaceAutoBaseUi.sourceKey != m_ActiveRawWorkspaceSourceKey) {
        m_RawWorkspaceAutoBaseUi.beforeAutoBase = editedRecipe;
        m_RawWorkspaceAutoBaseUi.hasRevertSnapshot = true;
    }

    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(recipe, false)) {
        return false;
    }

    editedRecipe = m_ActiveRawWorkspaceRecipe;
    m_RawWorkspaceAutoBaseUi.sourceKey = m_ActiveRawWorkspaceSourceKey;
    if (const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey)) {
        m_RawWorkspaceAutoBaseUi.sourceHash = BuildRawWorkspaceAutoBaseSourceHash(*source);
    }
    m_RawWorkspaceAutoBaseUi.summary =
        "Applied Local Range suggestion: " +
        (suggestion.label.empty()
            ? std::string(Stack::RawAutoBase::SuggestedLocalAdjustmentKindLabel(suggestion.kind))
            : suggestion.label) +
        ".";
    m_RawWorkspaceLocalRangeOverlayMode =
        suggestion.colorQualifierEnabled ? "region-mask" : "affected-tones";
    ClearRawWorkspaceLocalRangeOverlayState();
    MarkRenderRefreshDirty();
    RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    return true;
}

bool EditorModule::RevertRawWorkspaceAutoBaseForSelectedSource() {
    if (!m_RawWorkspaceAutoBaseUi.hasRevertSnapshot ||
        m_RawWorkspaceAutoBaseUi.sourceKey.empty() ||
        m_RawWorkspaceAutoBaseUi.sourceKey != m_RawWorkspace.selectedSourceKey) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe revertRecipe =
        m_RawWorkspaceAutoBaseUi.beforeAutoBase;
    if (!ApplyRawWorkspaceRecipeEditForSelectedSource(revertRecipe, false)) {
        return false;
    }

    const std::string sourceKey = m_ActiveRawWorkspaceSourceKey;
    const std::uint64_t sourceHash = m_RawWorkspaceAutoBaseUi.sourceHash;
    ResetRawWorkspaceAutoBaseState();
    m_RawWorkspaceAutoBaseUi.sourceKey = sourceKey;
    m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    m_RawWorkspaceAutoBaseUi.summary = "Auto Base reverted. RAW Exposure unchanged.";
    return true;
}

void EditorModule::TryApplyRawWorkspaceAutoBaseOnAnalysis() {
    if (m_ActiveRawWorkspaceSourceKey.empty() ||
        m_RawWorkspaceAnalysis.sourceKey != m_ActiveRawWorkspaceSourceKey ||
        !m_RawWorkspaceAnalysis.currentFrameStats.valid) {
        return;
    }

    const Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
    if (!source) {
        return;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = m_ActiveRawWorkspaceRecipe;
    if (!m_RawWorkspaceAutoBaseUi.hasMetadataSummary ||
        m_RawWorkspaceAutoBaseUi.sourceKey != m_ActiveRawWorkspaceSourceKey) {
        m_RawWorkspaceAutoBaseUi.metadataSummary = ResolveRawWorkspaceMetadataSummaryForAutoBase();
        m_RawWorkspaceAutoBaseUi.hasMetadataSummary = true;
    }
    m_RawWorkspaceAnalysis.metadata = m_RawWorkspaceAutoBaseUi.metadataSummary;
    RefreshRawWorkspaceAutoBaseRecommendations(recipe);
    ApplyRawWorkspaceAutoBaseViewFitForSource(*source, recipe, false);
}

bool EditorModule::RenderRawWorkspaceAutoBasePanel(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe,
    float controlWidth) {
    ImGui::SeparatorText("Auto Base");

    if (selectedSource == nullptr) {
        ImGui::TextDisabled("Auto Base unavailable: no RAW selected.");
        return false;
    }

    const std::uint64_t sourceHash = BuildRawWorkspaceAutoBaseSourceHash(*selectedSource);
    if (!m_RawWorkspaceAutoBaseUi.sourceKey.empty() &&
        (m_RawWorkspaceAutoBaseUi.sourceKey != selectedSource->relativePathKey ||
         m_RawWorkspaceAutoBaseUi.sourceHash != sourceHash)) {
        ResetRawWorkspaceAutoBaseState();
    }
    if (m_RawWorkspaceAutoBaseUi.sourceKey.empty()) {
        m_RawWorkspaceAutoBaseUi.sourceKey = selectedSource->relativePathKey;
        m_RawWorkspaceAutoBaseUi.sourceHash = sourceHash;
    }

    bool recipeUpdated = false;
    const bool hasUsableStats =
        selectedSource->relativePathKey == m_ActiveRawWorkspaceSourceKey &&
        m_RawWorkspaceAnalysis.sourceKey == selectedSource->relativePathKey &&
        m_RawWorkspaceAnalysis.currentFrameStats.valid;
    if (hasUsableStats) {
        RefreshRawWorkspaceAutoBaseRecommendations(editedRecipe);
    }
    const bool canRevert =
        m_RawWorkspaceAutoBaseUi.hasRevertSnapshot &&
        m_RawWorkspaceAutoBaseUi.sourceKey == selectedSource->relativePathKey;

    const float buttonGap = 6.0f;
    const float buttonWidth = std::max(72.0f, (controlWidth - buttonGap * 2.0f) / 3.0f);

    if (ImGuiExtras::RichFullWidthButton("Analyze", buttonWidth, 0.0f)) {
        MarkRenderRefreshDirty();
        m_RawWorkspaceAutoBaseUi.summary = m_RawWorkspaceAnalysis.currentFrameStats.valid
            ? "Auto Base analyzed current frame. Apply Auto Base to use the view fit."
            : "Auto Base pending: render preview to analyze the frame.";
    }
    TooltipIfHovered("Refreshes current-frame analysis without changing recipe values.");

    ImGui::SameLine(0.0f, buttonGap);
    ImGui::BeginDisabled(!hasUsableStats);
    if (ImGuiExtras::RichFullWidthButton("Apply", buttonWidth, 0.0f)) {
        recipeUpdated = ApplyRawWorkspaceAutoBaseViewFitForSource(*selectedSource, editedRecipe, true);
    }
    ImGui::EndDisabled();
    TooltipIfHovered(
        "Fits the display rendering for this RAW file using robust scene-linear frame statistics. This makes the image readable without changing RAW Exposure.",
        ImGuiHoveredFlags_AllowWhenDisabled);

    ImGui::SameLine(0.0f, buttonGap);
    ImGui::BeginDisabled(!canRevert);
    if (ImGuiExtras::RichFullWidthButton("Undo", buttonWidth, 0.0f)) {
        recipeUpdated = RevertRawWorkspaceAutoBaseForSelectedSource();
        if (recipeUpdated) {
            editedRecipe = m_ActiveRawWorkspaceRecipe;
        }
    }
    ImGui::EndDisabled();
    TooltipIfHovered(
        "Restores the recipe values from before Auto Base was applied.",
        ImGuiHoveredFlags_AllowWhenDisabled);

    return recipeUpdated;
}
