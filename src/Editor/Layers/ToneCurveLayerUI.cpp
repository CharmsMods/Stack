#include "ToneLayers.h"

#include "Editor/EditorModule.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <type_traits>
#include <vector>

namespace {

const char* ToneCurveTargetScopeLabel(ToneCurveTargetScope scope) {
    switch (scope) {
        case ToneCurveTargetScope::ScopedMask: return "Scoped Tone + Mask";
        case ToneCurveTargetScope::Global:
        default: return "Global Tone";
    }
}

const char* ToneCurveScopeMaskActionLabel(ToneCurveScopeMaskAction action) {
    switch (action) {
        case ToneCurveScopeMaskAction::Add: return "Add To Existing Mask";
        case ToneCurveScopeMaskAction::Subtract: return "Subtract From Existing Mask";
        case ToneCurveScopeMaskAction::Intersect: return "Intersect Existing Mask";
        case ToneCurveScopeMaskAction::NewMask:
        default: return "New Scoped Mask";
    }
}

const char* ToneCurveScopeMaskActionButtonLabel(ToneCurveScopeMaskAction action) {
    switch (action) {
        case ToneCurveScopeMaskAction::Add: return "Add Tone Scope To Mask";
        case ToneCurveScopeMaskAction::Subtract: return "Subtract Tone Scope From Mask";
        case ToneCurveScopeMaskAction::Intersect: return "Intersect Tone Scope With Mask";
        case ToneCurveScopeMaskAction::NewMask:
        default: return "Create New Scoped Mask";
    }
}

const char* ToneCurveTargetingModeLabel(ToneCurveTargetingMode mode) {
    switch (mode) {
        case ToneCurveTargetingMode::PointTarget: return "Point Target";
        case ToneCurveTargetingMode::RegionTarget:
        default: return "Region Target";
    }
}

const char* ToneFoundationRegionLabel(int index) {
    switch (index) {
        case 0: return "Shadows";
        case 1: return "Darks";
        case 2: return "Midtones";
        case 3: return "Lights";
        case 4: return "Highlights";
        default: return "Midtones";
    }
}

const char* ToneCurveAutoSceneProfileLabel(ToneCurveAutoSceneProfile profile) {
    switch (profile) {
        case ToneCurveAutoSceneProfile::HighlightHeavy: return "Highlight-Heavy HDR";
        case ToneCurveAutoSceneProfile::ShadowHeavy: return "Shadow-Heavy";
        case ToneCurveAutoSceneProfile::Flat: return "Flat / Low Contrast";
        case ToneCurveAutoSceneProfile::NoisyLowLight: return "Noisy Low Light";
        case ToneCurveAutoSceneProfile::Balanced:
        default: return "Balanced";
    }
}

const char* ToneCurveAutoVariantLabel(ToneCurveAutoVariant variant) {
    switch (variant) {
        case ToneCurveAutoVariant::OpenShadows: return "Open Shadows";
        case ToneCurveAutoVariant::ProtectHighlights: return "Protect Highlights";
        case ToneCurveAutoVariant::MoreContrast: return "More Contrast";
        case ToneCurveAutoVariant::Recommended:
        default: return "Recommended";
    }
}

const char* ToneCurveGraphViewLabel(ToneCurveGraphView view) {
    switch (view) {
        case ToneCurveGraphView::Prepared: return "Prepared Graph";
        case ToneCurveGraphView::Finish:
        default: return "Finish Graph";
    }
}

bool ToneCurvePointArraysEqual(const std::vector<ToneCurvePoint>& a, const std::vector<ToneCurvePoint>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].x - b[i].x) > 0.0001f ||
            std::abs(a[i].y - b[i].y) > 0.0001f ||
            a[i].shape != b[i].shape) {
            return false;
        }
    }
    return true;
}

template <typename T>
bool ApplyResettableSliderValue(T* value, T resetValue, bool changed, float epsilon = 0.0001f) {
    const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
    if (!state.lastHovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return changed;
    }
    if constexpr (std::is_floating_point_v<T>) {
        if (std::abs(*value - resetValue) <= epsilon) {
            return changed;
        }
    } else {
        if (*value == resetValue) {
            return changed;
        }
    }
    *value = resetValue;
    return true;
}

bool ResettableToneSliderFloat(
    const char* label,
    const char* id,
    float* value,
    float resetValue,
    float minValue,
    float maxValue,
    const char* format,
    float controlWidth) {
    const bool changed = ImGuiExtras::NodeSliderFloat(label, id, value, minValue, maxValue, format, controlWidth);
    return ApplyResettableSliderValue(value, resetValue, changed);
}

bool ResettableToneSliderInt(
    const char* label,
    const char* id,
    int* value,
    int resetValue,
    int minValue,
    int maxValue,
    const char* format,
    float controlWidth) {
    const bool changed = ImGuiExtras::NodeSliderInt(label, id, value, minValue, maxValue, format, controlWidth);
    return ApplyResettableSliderValue(value, resetValue, changed, 0.0f);
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

constexpr int kToneCurveMaxPoints = 12;
constexpr float kToneCurveHitRadius = 22.0f;

const char* ToneCurveSegmentShapeLabel(ToneCurveSegmentShape shape) {
    switch (shape) {
        case ToneCurveSegmentShape::Smooth: return "Smooth";
        case ToneCurveSegmentShape::Linear: return "Linear";
        case ToneCurveSegmentShape::Hold: return "Hold";
    }
    return "Smooth";
}

const char* ToneCurveSamplingBasisLabel(ToneCurveSamplingBasis basis) {
    switch (basis) {
        case ToneCurveSamplingBasis::CurveInput: return "Curve Input";
        case ToneCurveSamplingBasis::FinalPreview: return "Final Preview";
    }
    return "Curve Input";
}

struct CurveGraphRect {
    ImVec2 min;
    ImVec2 max;

    float Width() const { return max.x - min.x; }
    float Height() const { return max.y - min.y; }
};

ImVec2 CurveToScreen(const CurveGraphRect& graphRect, const ToneCurvePoint& point) {
    return ImVec2(
        graphRect.min.x + point.x * graphRect.Width(),
        graphRect.max.y - point.y * graphRect.Height());
}

ToneCurvePoint ScreenToCurve(const CurveGraphRect& graphRect, const ImVec2& screen) {
    return {
        Clamp01((screen.x - graphRect.min.x) / std::max(1.0f, graphRect.Width())),
        Clamp01((graphRect.max.y - screen.y) / std::max(1.0f, graphRect.Height()))
    };
}

} // namespace

void ToneCurveLayer::RenderUI() {
    ImGui::TextDisabled("Double-click for curve controls.");
}

bool ToneCurveLayer::RenderDevelopBridgeControls(float controlWidth, bool showExtendedGuidance) {
    bool changed = false;
    const float buttonGap = 8.0f;
    const float buttonWidth = std::max(110.0f, (controlWidth - buttonGap) * 0.5f);
    if (ImGuiExtras::RichFullWidthButton("Auto Calibrate Finish", buttonWidth, 0.0f)) {
        RequestAutoCalibration(ToneCurveAutoVariant::Recommended, true);
        changed = true;
    }
    ImGui::SameLine(0.0f, buttonGap);
    if (ImGuiExtras::RichFullWidthButton("Reset Finish Curve", buttonWidth, 0.0f)) {
        m_Points = {
            ToneCurvePoint{ 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
            ToneCurvePoint{ 1.0f, 1.0f, ToneCurveSegmentShape::Linear }
        };
        m_SelectedPoint = -1;
        m_DraggingPoint = -1;
        SanitizePoints();
        changed = true;
    }

    const bool autoStrengthChanged = ResettableToneSliderFloat(
        "Auto Strength",
        "##ToneCurveDevelopBridgeAutoStrength",
        &m_AutoSceneAssistStrength,
        0.78f,
        0.0f,
        2.4f,
        "%.2f",
        controlWidth);
    const bool autoDynamicRangeChanged = ResettableToneSliderFloat(
        "Auto Dynamic Range",
        "##ToneCurveDevelopBridgeDynamicRange",
        &m_AutoDynamicRange,
        1.0f,
        0.25f,
        3.00f,
        "%.2f",
        controlWidth);
    const bool autoContrastBiasChanged = ResettableToneSliderFloat(
        "Contrast Bias",
        "##ToneCurveDevelopBridgeContrastBias",
        &m_AutoContrastBias,
        0.0f,
        -1.25f,
        1.25f,
        "%.2f",
        controlWidth);
    const bool autoHighlightCharacterChanged = ResettableToneSliderFloat(
        "Highlight Character",
        "##ToneCurveDevelopBridgeHighlightCharacter",
        &m_AutoHighlightCharacter,
        0.0f,
        -1.25f,
        1.25f,
        "%.2f",
        controlWidth);

    bool guidanceChanged = autoStrengthChanged || autoDynamicRangeChanged || autoContrastBiasChanged || autoHighlightCharacterChanged;
    changed |= guidanceChanged;

    bool autoShadowBiasChanged = false;
    bool autoHighlightBiasChanged = false;
    if (showExtendedGuidance) {
        autoShadowBiasChanged = ResettableToneSliderFloat(
            "Shadow Lift",
            "##ToneCurveDevelopBridgeShadowBias",
            &m_AutoShadowBias,
            0.0f,
            -1.25f,
            1.25f,
            "%.2f",
            controlWidth);
        autoHighlightBiasChanged = ResettableToneSliderFloat(
            "Highlight Guard",
            "##ToneCurveDevelopBridgeHighlightBias",
            &m_AutoHighlightBias,
            0.0f,
            -1.25f,
            1.25f,
            "%.2f",
            controlWidth);
        guidanceChanged = guidanceChanged || autoShadowBiasChanged || autoHighlightBiasChanged;
        changed |= autoShadowBiasChanged || autoHighlightBiasChanged;
    }

    if (guidanceChanged) {
        RequestAutoCalibration(ToneCurveAutoVariant::Recommended, !m_AutoSceneStatsValid);
    }

    if (m_AutoCalibratePending) {
        ImGui::TextDisabled("Finish calibration queued for the next render.");
    }
    if (m_AutoSceneStatsValid) {
        ImGui::TextDisabled(
            "Finish profile: %s  (spread %.2f EV)",
            ToneCurveAutoSceneProfileLabel(m_AutoSceneProfile),
            m_AutoSceneHdrSpreadEv);
    } else {
        ImGui::TextDisabled("Run finish calibration to analyze the current prepared image before final curve edits.");
    }
    ImGui::TextDisabled("These are the same shared finish-guidance controls used by Develop Auto Calibrate.");
    ImGui::TextDisabled("Highlight Character: lower keeps more color/headroom, higher allows brighter / whiter highlight pop.");
    ImGui::TextDisabled("The finish curve stays user-authored on top of the prepared tone state.");
    return changed;
}

bool ToneCurveLayer::RenderDevelopFinishGraphPanel(float controlWidth, bool showDetails, bool allowPreparedEditing) {
    const std::vector<ToneCurvePoint> pointsBefore = m_Points;
    const std::vector<ToneCurvePoint> preparedPointsBefore = m_PreparedPoints;
    const ToneCurveDomain domainBefore = m_Domain;
    const ToneCurveGraphView previousView = m_ActiveGraphView;

    if (m_ActiveGraphView != ToneCurveGraphView::Prepared) {
        m_ActiveGraphView = ToneCurveGraphView::Finish;
    }
    RefreshProbeOutput();

    bool changed = false;
    const char* graphLabels[] = { "Final Graph", "Prepared Graph" };
    int graphView = m_ActiveGraphView == ToneCurveGraphView::Prepared ? 1 : 0;
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Graph", &graphView, graphLabels, 2)) {
        m_ActiveGraphView = graphView == 1 ? ToneCurveGraphView::Prepared : ToneCurveGraphView::Finish;
        m_SelectedPoint = -1;
        m_DraggingPoint = -1;
        m_ContextPoint = -1;
        changed = true;
    }
    changed |= ImGuiExtras::NodeCheckbox(
        "Overlay Final Curve",
        "##ToneCurveDevelopShowFinalOverlay",
        &m_ShowFinalCurve,
        controlWidth);
    ImGui::TextDisabled("Final Graph is the user-owned finish curve. Prepared Graph is the auto-authored curve feeding it.");
    const float graphSize = std::min(controlWidth, showDetails ? 320.0f : 280.0f);
    const bool preparedInspectOnly = m_ActiveGraphView == ToneCurveGraphView::Prepared && !allowPreparedEditing;
    if (preparedInspectOnly) {
        ImGui::BeginDisabled();
    }
    RenderCurveEditor(graphSize, graphSize);
    if (preparedInspectOnly) {
        ImGui::EndDisabled();
    }

    const char* domainLabel = m_Domain == ToneCurveDomain::LogScene ? "Log Scene" : "Scene Linear";
    ImGui::TextDisabled("%s Domain: %s", ToneCurveGraphViewLabel(m_ActiveGraphView), domainLabel);
    if (showDetails) {
        if (preparedInspectOnly) {
            ImGui::TextDisabled("Prepared Graph is inspect-only in normal Develop. Use Advanced if the prep curve itself needs editing.");
        } else {
            ImGui::TextDisabled("Right-click a point to delete it or confirm it is linear. Auto recalibration can refresh Prepared Graph.");
        }
    } else {
        ImGui::TextDisabled("Toggle between final taste edits and prepared auto graph inspection.");
    }

    const std::vector<ToneCurvePoint>& activePoints = EditablePoints();
    if (m_SelectedPoint >= 0 && m_SelectedPoint < static_cast<int>(activePoints.size())) {
        const ToneCurvePoint& point = activePoints[static_cast<std::size_t>(m_SelectedPoint)];
        ImGui::TextDisabled("Selected %s point: input %.3f  output %.3f", ToneCurveGraphViewLabel(m_ActiveGraphView), point.x, point.y);
    } else {
        ImGui::TextDisabled("%s points: %zu / %d", ToneCurveGraphViewLabel(m_ActiveGraphView), activePoints.size(), kToneCurveMaxPoints);
    }

    changed |= !ToneCurvePointArraysEqual(pointsBefore, m_Points);
    changed |= !ToneCurvePointArraysEqual(preparedPointsBefore, m_PreparedPoints);
    changed |= domainBefore != m_Domain;
    changed |= previousView != m_ActiveGraphView;
    return changed;
}

bool ToneCurveLayer::RenderDevelopPreparedControlsPanel(float controlWidth, bool showDetails) {
    AutoAuthoredState authoredResetState = m_LastAutoAuthoredStateValid ? m_LastAutoAuthoredState : CaptureCurrentAutoAuthoredState();

    bool changed = false;
    ImGui::TextDisabled("Prepared tone guidance steers the algorithm-owned prep state before your Finish Graph.");
    ImGui::TextDisabled("Use this when automatic prep needs help opening shadows, holding bright detail, or staying more stable on difficult scenes.");
    ImGui::TextDisabled("Normal taste edits should still stay on Finish Graph.");

    changed |= ImGuiExtras::NodeCheckbox(
        "Automatic Local Baseline",
        "##ToneCurveDevelopPreparedLocalBaselineEnabled",
        &m_LocalBaselineEnabled,
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Local Strength",
        "##ToneCurveDevelopPreparedLocalBaselineStrength",
        &m_LocalBaselineStrength,
        authoredResetState.localBaseline.strength,
        0.0f,
        1.6f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Shadow Opening",
        "##ToneCurveDevelopPreparedLocalShadowOpening",
        &m_LocalShadowOpening,
        authoredResetState.localBaseline.shadowOpening,
        0.0f,
        2.2f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Highlight Compression",
        "##ToneCurveDevelopPreparedLocalHighlightCompression",
        &m_LocalHighlightCompression,
        authoredResetState.localBaseline.highlightCompression,
        0.0f,
        2.2f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Edge Protection",
        "##ToneCurveDevelopPreparedLocalEdgeProtection",
        &m_LocalEdgeProtection,
        authoredResetState.localBaseline.edgeProtection,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Range Protection",
        "##ToneCurveDevelopPreparedLocalRangeProtection",
        &m_LocalRangeProtection,
        authoredResetState.localBaseline.rangeProtection,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Adaptive Assist",
        "##ToneCurveDevelopPreparedFoundationAdaptiveAssist",
        &m_FoundationAdaptiveAssist,
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Assist Strength",
        "##ToneCurveDevelopPreparedFoundationAssistStrength",
        &m_FoundationAssistStrength,
        authoredResetState.foundationAssistStrength,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);

    if (showDetails) {
        ImGui::TextDisabled("Right-click any prepared control to reset it to the last auto-authored prepared recommendation.");
        ImGui::TextDisabled("Use the Develop manual controls when you want radius, foundation band shaping, middle grey, or on-image targeting.");
    }

    return changed;
}

bool ToneCurveLayer::RenderDevelopFoundationControlsPanel(float controlWidth, bool showDetails) {
    AutoAuthoredState authoredResetState = m_LastAutoAuthoredStateValid ? m_LastAutoAuthoredState : CaptureCurrentAutoAuthoredState();

    bool changed = false;
    ImGui::TextDisabled("Foundation tone sets the broad global balance after local prep and before your finish-graph taste edits.");
    if (m_AutoSceneStatsValid && m_AutoSceneAssistStrength > 0.001f) {
        const float effectiveFoundationAssist = ComputeEffectiveFoundationAssistStrength();
        const float effectiveFoundationBandWidth = ComputeEffectiveFoundationBandWidth();
        ImGui::TextDisabled(
            "Last recommended foundation: %.2f %.2f %.2f %.2f %.2f EV",
            m_AutoRecommendedFoundationEv[0] * m_AutoSceneAssistStrength,
            m_AutoRecommendedFoundationEv[1] * m_AutoSceneAssistStrength,
            m_AutoRecommendedFoundationEv[2] * m_AutoSceneAssistStrength,
            m_AutoRecommendedFoundationEv[3] * m_AutoSceneAssistStrength,
            m_AutoRecommendedFoundationEv[4] * m_AutoSceneAssistStrength);
        ImGui::TextDisabled(
            "Recommended shaping: strength %.2f  band %.2f EV",
            effectiveFoundationAssist,
            effectiveFoundationBandWidth);
    }

    changed |= ResettableToneSliderFloat(
        "Shadows",
        "##ToneCurveDevelopFoundationShadows",
        &m_FoundationShadows,
        authoredResetState.foundationRegionEv[0],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Darks",
        "##ToneCurveDevelopFoundationDarks",
        &m_FoundationDarks,
        authoredResetState.foundationRegionEv[1],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Midtones",
        "##ToneCurveDevelopFoundationMidtones",
        &m_FoundationMidtones,
        authoredResetState.foundationRegionEv[2],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Lights",
        "##ToneCurveDevelopFoundationLights",
        &m_FoundationLights,
        authoredResetState.foundationRegionEv[3],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Highlights",
        "##ToneCurveDevelopFoundationHighlights",
        &m_FoundationHighlights,
        authoredResetState.foundationRegionEv[4],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Adaptive Assist",
        "##ToneCurveDevelopFoundationAdaptiveAssist",
        &m_FoundationAdaptiveAssist,
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Assist Strength",
        "##ToneCurveDevelopFoundationAssistStrength",
        &m_FoundationAssistStrength,
        authoredResetState.foundationAssistStrength,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Middle Grey",
        "##ToneCurveDevelopFoundationMiddleGrey",
        &m_MiddleGrey,
        authoredResetState.middleGrey,
        0.01f,
        1.0f,
        "%.3f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Band Width",
        "##ToneCurveDevelopFoundationBandWidth",
        &m_FoundationBandWidth,
        authoredResetState.foundationBandWidth,
        0.5f,
        8.0f,
        "%.2f EV",
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Preserve Hue",
        "##ToneCurveDevelopFoundationPreserveHue",
        &m_FoundationPreserveHue,
        controlWidth);

    if (showDetails) {
        ImGui::TextDisabled("Use these when auto gets the scene into the right zone but the broad tonal balance still needs a more deliberate push.");
    }

    return changed;
}

bool ToneCurveLayer::RenderDevelopTargetingPanel(EditorModule* editor, int nodeId, float controlWidth, bool showDetails) {
    AutoAuthoredState authoredResetState = m_LastAutoAuthoredStateValid ? m_LastAutoAuthoredState : CaptureCurrentAutoAuthoredState();

    bool changed = false;
    ImGui::TextDisabled("On-image targeting samples the viewport and nudges this finish stage where the image actually lands.");
    ImGui::TextDisabled("Use it when broad auto plus graph edits are close, but a specific sampled region still needs to move.");

    const bool targetActive = editor && editor->IsCanvasToolActiveForNode(nodeId, EditorModule::CanvasToolKind::ToneCurveTarget);
    if (targetActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 128, 176, 215));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(72, 146, 198, 235));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 156, 210, 255));
    }
    if (ImGuiExtras::RichFullWidthButton(targetActive ? "Stop On-Image Target" : "On-Image Target", controlWidth, 0.0f)) {
        if (editor) {
            if (targetActive) {
                editor->CancelCanvasTool();
            } else {
                editor->BeginToneCurveTargeting(
                    nodeId,
                    m_TargetingMode == ToneCurveTargetingMode::RegionTarget
                        ? "Drag in the main viewport to rebalance local baseline and nearby tonal regions."
                        : "Click and drag in the main viewport to move a point on the finish curve.");
            }
        }
    }
    if (targetActive) {
        ImGui::PopStyleColor(3);
    }

    const char* samplingLabels[] = { "Curve Input", "Final Preview" };
    int samplingBasis = static_cast<int>(m_SamplingBasis);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Sampling", &samplingBasis, samplingLabels, 2)) {
        m_SamplingBasis = static_cast<ToneCurveSamplingBasis>(std::clamp(samplingBasis, 0, 1));
        changed = true;
    }

    const char* targetingLabels[] = { "Region Target", "Point Target" };
    int targetingMode = static_cast<int>(m_TargetingMode);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Targeting Mode", &targetingMode, targetingLabels, 2)) {
        m_TargetingMode = static_cast<ToneCurveTargetingMode>(std::clamp(targetingMode, 0, 1));
        changed = true;
    }

    changed |= ResettableToneSliderFloat(
        "Affect Width",
        "##ToneCurveDevelopTargetAffectWidth",
        &m_TargetAffectWidth,
        authoredResetState.targetAffectWidth,
        0.02f,
        0.30f,
        "%.3f",
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Auto Anchor Protection",
        "##ToneCurveDevelopAutoAnchorProtection",
        &m_AutoAnchorProtection,
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Protect Endpoints",
        "##ToneCurveDevelopProtectEndpointsDuringTargeting",
        &m_ProtectEndpointsDuringTargeting,
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Protect Shadows",
        "##ToneCurveDevelopTargetShadowProtection",
        &m_TargetShadowProtection,
        authoredResetState.targetShadowProtection,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Protect Highlights",
        "##ToneCurveDevelopTargetHighlightProtection",
        &m_TargetHighlightProtection,
        authoredResetState.targetHighlightProtection,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);

    if (m_ProbeValid) {
        ImGui::TextDisabled(
            "Sample %.3f -> %.3f  (%s)",
            m_ProbeInputX,
            m_ProbeOutputY,
            ToneCurveSamplingBasisLabel(m_ProbeSamplingBasis));
        if (m_TargetingMode == ToneCurveTargetingMode::RegionTarget) {
            const std::array<float, 5> regionWeights = ComputeFoundationTargetWeights(m_ProbeSceneValue);
            int dominantRegion = 0;
            float dominantWeight = regionWeights[0];
            for (int i = 1; i < 5; ++i) {
                if (regionWeights[static_cast<std::size_t>(i)] > dominantWeight) {
                    dominantWeight = regionWeights[static_cast<std::size_t>(i)];
                    dominantRegion = i;
                }
            }
            const char* regionNames[] = { "Shadows", "Darks", "Midtones", "Lights", "Highlights" };
            ImGui::TextDisabled(
                "Dominant region: %s  (weight %.2f)",
                regionNames[dominantRegion],
                dominantWeight);
        }
    } else {
        ImGui::TextDisabled("Hover the main viewport to inspect the current sampled tone before you drag.");
    }

    if (targetActive) {
        const char* statusText = editor && !editor->GetCanvasToolStatusText().empty()
            ? editor->GetCanvasToolStatusText().c_str()
            : (m_TargetingMode == ToneCurveTargetingMode::RegionTarget
                ? "Click and drag in the main viewport to rebalance local baseline and nearby tonal regions"
                : "Click and drag in the main viewport to adjust a point on the finish curve");
        ImGui::TextDisabled("%s", statusText);
    }

    if (showDetails) {
        ImGui::TextDisabled("Capture a hover seed here, then use Local Scope / Masking below when the same move should only affect part of the frame.");
    }

    return changed;
}

bool ToneCurveLayer::RenderDevelopScopedMaskPanel(EditorModule* editor, int nodeId, float controlWidth, bool showDetails) {
    return RenderScopedMaskPanel(editor, nodeId, controlWidth, showDetails, true);
}

bool ToneCurveLayer::RenderScopedMaskPanel(
    EditorModule* editor,
    int nodeId,
    float controlWidth,
    bool showDetails,
    bool embeddedInDevelop) {
    bool changed = false;
    ImGuiExtras::RichSectionLabel("Local Scope / Masking");
    const bool maskConnected = editor &&
        editor->GetNodeGraph().FindInputLink(nodeId, EditorNodeGraph::kMaskInputSocketId) != nullptr;
    ImGui::TextDisabled("Use this only when the same tonal move should apply to part of the frame, not the whole image.");
    if (embeddedInDevelop) {
        ImGui::TextDisabled("This creates or refines Develop's Finish Mask while keeping the actual finish processing inside the merged path.");
    }
    if (maskConnected) {
        ImGui::TextDisabled("Current area scope: masked region only.");
    } else {
        ImGui::TextDisabled("Current area scope: whole image. Capture a seed only when you want a local refinement.");
    }

    const char* actionLabels[] = {
        "New Scoped Mask",
        "Add To Existing Mask",
        "Subtract From Existing Mask",
        "Intersect Existing Mask"
    };
    int scopedAction = static_cast<int>(m_ScopedMaskAction);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo(embeddedInDevelop ? "Mask Action##ToneCurveDevelopScopedMaskAction" : "Mask Action", &scopedAction, actionLabels, 4)) {
        m_ScopedMaskAction = static_cast<ToneCurveScopeMaskAction>(std::clamp(scopedAction, 0, 3));
        changed = true;
    }

    if (ImGuiExtras::RichFullWidthButton(
            "Capture Hover As Region Seed",
            std::min(controlWidth, controlWidth * 0.62f + (embeddedInDevelop ? controlWidth * 0.18f : 0.0f)),
            0.0f) &&
        m_ProbeValid) {
        CaptureSelectionSeedFromProbe();
        changed = true;
    }
    if (!m_ProbeValid) {
        ImGui::SameLine();
        ImGui::TextDisabled("Hover the viewport first.");
    }

    if (m_SelectionSeedValid) {
        ImGui::TextDisabled(
            "Seed tone %.3f  RGB %.3f %.3f %.3f",
            m_SelectionSeedSceneValue,
            m_SelectionSeedRgba[0],
            m_SelectionSeedRgba[1],
            m_SelectionSeedRgba[2]);
        changed |= ResettableToneSliderFloat(
            "Tone Similarity",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionToneSimilarity" : "##ToneCurveSelectionToneSimilarity",
            &m_SelectionToneSimilarity,
            0.12f,
            0.02f,
            0.35f,
            "%.3f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Color Similarity",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionColorSimilarity" : "##ToneCurveSelectionColorSimilarity",
            &m_SelectionColorSimilarity,
            0.18f,
            0.02f,
            0.50f,
            "%.3f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Area Radius",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionRegionRadius" : "##ToneCurveSelectionRegionRadius",
            &m_SelectionRegionRadius,
            0.35f,
            0.05f,
            1.0f,
            "%.2f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Region Feather",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionFeather" : "##ToneCurveSelectionFeather",
            &m_SelectionFeather,
            0.35f,
            0.0f,
            1.0f,
            "%.2f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Edge Sensitivity",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionEdgeSensitivity" : "##ToneCurveSelectionEdgeSensitivity",
            &m_SelectionEdgeSensitivity,
            0.45f,
            0.0f,
            1.0f,
            "%.2f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Local Coherence",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionLocalCoherence" : "##ToneCurveSelectionLocalCoherence",
            &m_SelectionLocalCoherence,
            0.45f,
            0.0f,
            1.0f,
            "%.2f",
            controlWidth);
        if (editor && ImGuiExtras::RichFullWidthButton(ToneCurveScopeMaskActionButtonLabel(m_ScopedMaskAction), controlWidth, 0.0f)) {
            const float low = CurveCoordToScene(std::max(0.0f, m_SelectionSeedInputX - std::max(m_TargetAffectWidth, m_SelectionToneSimilarity)));
            const float high = CurveCoordToScene(std::min(1.0f, m_SelectionSeedInputX + std::max(m_TargetAffectWidth, m_SelectionToneSimilarity)));
            const float softness = std::clamp(
                0.02f + 0.18f * m_SelectionFeather + 0.15f * m_SelectionColorSimilarity,
                0.0f,
                0.5f);
            editor->CreateToneCurveSelectionMask(
                nodeId,
                low,
                high,
                softness,
                m_SelectionSeedRgba,
                m_SelectionSeedSceneValue,
                m_SelectionSeedU,
                m_SelectionSeedV,
                m_SelectionToneSimilarity,
                m_SelectionColorSimilarity,
                m_SelectionRegionRadius,
                m_SelectionFeather,
                m_SelectionEdgeSensitivity,
                m_SelectionLocalCoherence,
                m_ScopedMaskAction);
        }
        if (ImGui::Button(embeddedInDevelop ? "Clear Region Seed##ToneCurveDevelopScopedMaskSeed" : "Clear Region Seed")) {
            ClearSelectionSeed();
            changed = true;
        }
        if (showDetails) {
            ImGui::TextDisabled("Repeated capture can add up to five tone/color samples to the tone scope mask, closer to Adobe-style range refinement.");
        }
    } else {
        ImGui::TextDisabled("No region seed captured yet.");
        if (showDetails && embeddedInDevelop) {
            ImGui::TextDisabled("Capture from the inline Develop targeting probe, then build or refine a Finish Mask without leaving the merged workflow.");
        }
    }

    return changed;
}

bool ToneCurveLayer::RenderDevelopPreparedGraphPreviewPanel(float controlWidth, bool showDetails) {
    const ToneCurveGraphView previousView = m_ActiveGraphView;
    m_ActiveGraphView = ToneCurveGraphView::Prepared;
    RefreshProbeOutput();

    ImGui::TextDisabled("Prepared Graph preview only. This is the algorithm-owned prep curve that feeds your Finish Graph.");
    ImGui::TextDisabled("Use this to understand what the automatic processing is doing, not as the main place for taste edits.");
    const float graphSize = std::min(controlWidth, showDetails ? 300.0f : 260.0f);
    RenderCurveEditor(graphSize, graphSize, false);
    if (showDetails) {
        ImGui::TextDisabled("Normal user edits should stay on Finish Graph while the prepared preview remains a read-only reference.");
    }

    m_ActiveGraphView = previousView;
    return false;
}

void ToneCurveLayer::NotifyUpstreamDevelopChanged() {
    if (!HasAutoPreparedState()) {
        return;
    }
    RequestAutoCalibration(ToneCurveAutoVariant::Recommended, true);
}

NodeSurfaceSpec ToneCurveLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 460.0f;
    spec.maxWidth = 540.0f;
    spec.usesCanvasTool = true;
    return spec;
}

void ToneCurveLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    bool changed = false;
    m_FreeEndpoints = true;
    RefreshProbeOutput();
    AutoAuthoredState authoredResetState = m_LastAutoAuthoredStateValid ? m_LastAutoAuthoredState : CaptureCurrentAutoAuthoredState();
    if (m_ActiveGraphView != ToneCurveGraphView::Finish) {
        m_ActiveGraphView = ToneCurveGraphView::Finish;
        m_SelectedPoint = -1;
        m_DraggingPoint = -1;
        m_ContextPoint = -1;
    }

    ImGuiExtras::RichSectionLabel("Tone Curve");
    const struct ModeButton { ToneCurveMode mode; const char* label; } modeButtons[] = {
        { ToneCurveMode::Luminance, "Y" },
        { ToneCurveMode::RGB, "RGB" },
        { ToneCurveMode::Red, "R" },
        { ToneCurveMode::Green, "G" },
        { ToneCurveMode::Blue, "B" }
    };
    const float modeGap = 6.0f;
    const float modeWidth = std::max(40.0f, (context.safeContentWidth - (modeGap * 4.0f)) / 5.0f);
    for (int i = 0; i < 5; ++i) {
        const bool selected = m_Mode == modeButtons[i].mode;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 128, 176, 215));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(72, 146, 198, 235));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 156, 210, 255));
        }
        if (ImGuiExtras::RichFullWidthButton(modeButtons[i].label, modeWidth, 0.0f)) {
            m_Mode = modeButtons[i].mode;
            changed = true;
        }
        if (selected) {
            ImGui::PopStyleColor(3);
        }
        if (i < 4) {
            ImGui::SameLine(0.0f, modeGap);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, context.itemGap));
    const char* domainLabels[] = { "Scene Linear", "Log Scene" };
    int domain = static_cast<int>(m_Domain);
    ImGui::SetNextItemWidth(context.safeContentWidth);
    if (ImGui::Combo("Curve Domain", &domain, domainLabels, 2)) {
        m_Domain = static_cast<ToneCurveDomain>(std::clamp(domain, 0, 1));
        changed = true;
    }

    const float actionGap = 8.0f;
    const float actionWidth = std::max(110.0f, (context.safeContentWidth - actionGap) * 0.5f);
    if (ImGuiExtras::RichFullWidthButton("Reset Curve", actionWidth, 0.0f)) {
        ResetActiveCurveToLinear();
        changed = true;
    }
    ImGui::SameLine(0.0f, actionGap);
    if (ImGuiExtras::RichFullWidthButton("Reset Domain", actionWidth, 0.0f)) {
        m_Domain = ToneCurveDomain::Linear;
        m_LogMinEv = authoredResetState.logMinEv;
        m_LogMaxEv = authoredResetState.logMaxEv;
        changed = true;
    }
    ImGui::TextDisabled("Manual scene-referred finish curve. Usually place View Transform downstream.");

    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::RichSectionLabel("Curve");
    ImGui::TextDisabled("Right-click a point for point actions. This standalone node edits the finish curve directly.");
    const float graphSize = std::min(context.safeContentWidth, 390.0f * std::max(0.75f, context.layoutScale));
    RenderCurveEditor(graphSize, graphSize);
    changed |= m_LutDirty;

    ImGui::Dummy(ImVec2(0.0f, context.itemGap));
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (m_SelectedPoint >= 0 && m_SelectedPoint < static_cast<int>(editablePoints.size())) {
        const ToneCurvePoint& point = editablePoints[static_cast<std::size_t>(m_SelectedPoint)];
        ImGui::TextDisabled("Selected point: input %.3f  output %.3f", point.x, point.y);
        ImGui::SameLine();
        ImGui::TextDisabled("Segment: %s", ToneCurveSegmentShapeLabel(point.shape));
        ImGui::SameLine();
        ImGui::BeginDisabled(
            editablePoints.size() <= 2 ||
            m_SelectedPoint == 0 ||
            m_SelectedPoint == static_cast<int>(editablePoints.size()) - 1);
        if (ImGui::Button("Delete Point")) {
            DeleteSelectedPoint();
            changed = true;
        }
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("Finish Graph: %zu / %d points", editablePoints.size(), kToneCurveMaxPoints);
    }

    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::RichSectionLabel("On-Image Targeting");
    changed |= RenderDevelopTargetingPanel(editor, context.nodeId, context.safeContentWidth, false);
    if (context.canvasToolStatusText && context.canvasToolStatusText[0] != '\0') {
        ImGui::TextDisabled("%s", context.canvasToolStatusText);
    }

    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::RichSectionLabel("Curve Domain");
    ImGui::TextDisabled("Use Scene Linear for most manual RAW/HDR work. Switch to Log Scene for a broader tonal graph domain.");
    if (m_Domain == ToneCurveDomain::LogScene) {
        changed |= ResettableToneSliderFloat(
            "Graph Black EV",
            "##ToneCurveLogMinEv",
            &m_LogMinEv,
            authoredResetState.logMinEv,
            -20.0f,
            0.0f,
            "%.2f",
            context.safeContentWidth);
        changed |= ResettableToneSliderFloat(
            "Graph White EV",
            "##ToneCurveLogMaxEv",
            &m_LogMaxEv,
            authoredResetState.logMaxEv,
            0.0f,
            20.0f,
            "%.2f",
            context.safeContentWidth);
        if (m_LogMaxEv <= m_LogMinEv + 0.1f) {
            m_LogMaxEv = m_LogMinEv + 0.1f;
            changed = true;
        }
    } else {
        ImGui::TextDisabled("0.0 - 1.0");
    }

    if (changed) {
        SanitizePoints();
        m_LutDirty = true;
    }
}

void ToneCurveLayer::RenderCurveEditor(float width, float height, bool allowEditing) {
    SanitizePoints();
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    ImGui::PushID("ToneCurveGraph");
    ImGui::PushID(static_cast<int>(m_ActiveGraphView));
    ImGui::PushID(allowEditing ? "editable" : "readonly");
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(160.0f, width), std::max(160.0f, height));
    ImGui::InvisibleButton("canvas", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const CurveGraphRect rect { start, ImVec2(start.x + size.x, start.y + size.y) };
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImU32 bg = IM_COL32(16, 24, 28, 255);
    const ImU32 grid = IM_COL32(90, 115, 125, 58);
    const ImU32 diagonal = IM_COL32(150, 165, 170, 85);
    const ImU32 curveColor = IM_COL32(118, 190, 255, 245);
    const ImU32 supportingCurveColor = IM_COL32(244, 197, 95, 145);
    const ImU32 finalCurveColor = IM_COL32(102, 238, 191, 245);
    const ImU32 pointColor = IM_COL32(235, 240, 242, 255);
    const ImU32 selectedColor = IM_COL32(75, 175, 255, 255);
    const ImU32 probeGuideColor = IM_COL32(118, 190, 255, 108);
    const ImU32 probeRangeFillColor = IM_COL32(118, 190, 255, 26);
    const ImU32 probeInputColor = IM_COL32(235, 240, 242, 210);
    const ImU32 probeOutputColor = IM_COL32(102, 238, 191, 230);

    drawList->AddRectFilled(rect.min, rect.max, bg, 6.0f);
    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float x = rect.min.x + rect.Width() * t;
        const float y = rect.min.y + rect.Height() * t;
        drawList->AddLine(ImVec2(x, rect.min.y), ImVec2(x, rect.max.y), grid, 1.0f);
        drawList->AddLine(ImVec2(rect.min.x, y), ImVec2(rect.max.x, y), grid, 1.0f);
    }
    drawList->AddLine(ImVec2(rect.min.x, rect.max.y), ImVec2(rect.max.x, rect.min.y), diagonal, 1.0f);

    const auto inactiveCurveEval = [&](float x) {
        return m_ActiveGraphView == ToneCurveGraphView::Prepared
            ? EvaluateFinishCurve(x)
            : EvaluatePreparedCurve(x);
    };
    ImVec2 previous = CurveToScreen(rect, { 0.0f, inactiveCurveEval(0.0f) });
    for (int i = 1; i <= 96; ++i) {
        const float x = static_cast<float>(i) / 96.0f;
        const ImVec2 current = CurveToScreen(rect, { x, Clamp01(inactiveCurveEval(x)) });
        drawList->AddLine(previous, current, supportingCurveColor, 1.15f);
        previous = current;
    }

    previous = CurveToScreen(rect, { 0.0f, EvaluateCurve(0.0f) });
    for (int i = 1; i <= 96; ++i) {
        const float x = static_cast<float>(i) / 96.0f;
        const ImVec2 current = CurveToScreen(rect, { x, Clamp01(EvaluateCurve(x)) });
        drawList->AddLine(previous, current, curveColor, 2.25f);
        previous = current;
    }
    if (m_ShowFinalCurve) {
        previous = CurveToScreen(rect, { 0.0f, Clamp01(EvaluateFinalCurve(0.0f)) });
        for (int i = 1; i <= 96; ++i) {
            const float x = static_cast<float>(i) / 96.0f;
            const ImVec2 current = CurveToScreen(rect, { x, Clamp01(EvaluateFinalCurve(x)) });
            drawList->AddLine(previous, current, finalCurveColor, 1.65f);
            previous = current;
        }
    }
    if (m_ProbeValid) {
        RefreshProbeOutput();
        const float probeX = Clamp01(m_ProbeInputX);
        const float leftX = Clamp01(probeX - std::clamp(m_TargetAffectWidth, 0.0f, 0.35f));
        const float rightX = Clamp01(probeX + std::clamp(m_TargetAffectWidth, 0.0f, 0.35f));
        const ImVec2 rangeMin = CurveToScreen(rect, { leftX, 0.0f });
        const ImVec2 rangeMax = CurveToScreen(rect, { rightX, 1.0f });
        drawList->AddRectFilled(
            ImVec2(rangeMin.x, rect.min.y),
            ImVec2(rangeMax.x, rect.max.y),
            probeRangeFillColor,
            0.0f);
        const ImVec2 probeInput = CurveToScreen(rect, { probeX, probeX });
        const ImVec2 probeOutput = CurveToScreen(rect, { probeX, Clamp01(m_ProbeOutputY) });
        drawList->AddLine(
            ImVec2(probeInput.x, rect.min.y),
            ImVec2(probeInput.x, rect.max.y),
            probeGuideColor,
            1.0f);
        drawList->AddCircleFilled(probeInput, 5.0f, probeInputColor, 20);
        drawList->AddCircle(probeInput, 6.5f, IM_COL32(0, 0, 0, 120), 20, 1.0f);
        drawList->AddCircleFilled(probeOutput, 5.5f, probeOutputColor, 20);
        drawList->AddCircle(probeOutput, 7.0f, IM_COL32(0, 0, 0, 120), 20, 1.2f);
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hoveredPoint = -1;
    float bestDist2 = kToneCurveHitRadius * kToneCurveHitRadius;
    for (int i = 0; i < static_cast<int>(editablePoints.size()); ++i) {
        const ImVec2 point = CurveToScreen(rect, editablePoints[static_cast<std::size_t>(i)]);
        const float dx = point.x - mouse.x;
        const float dy = point.y - mouse.y;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 <= bestDist2) {
            bestDist2 = dist2;
            hoveredPoint = i;
        }
    }

    if (allowEditing && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredPoint >= 0) {
            m_SelectedPoint = hoveredPoint;
            m_DraggingPoint = hoveredPoint;
        } else {
            const ToneCurvePoint point = ScreenToCurve(rect, mouse);
            AddPointAt(point.x, point.y);
            m_DraggingPoint = m_SelectedPoint;
        }
    }
    if (allowEditing && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_DraggingPoint >= 0) {
        const ToneCurvePoint point = ScreenToCurve(rect, mouse);
        MovePoint(m_DraggingPoint, point.x, point.y);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_DraggingPoint = -1;
    }
    if (allowEditing && (hovered || active) && m_SelectedPoint >= 0 && !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        DeleteSelectedPoint();
    }

    if (allowEditing && hovered && hoveredPoint >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_SelectedPoint = hoveredPoint;
        m_ContextPoint = hoveredPoint;
        ImGui::OpenPopup("ToneCurvePointShapeMenu");
    }

    if (allowEditing && ImGui::BeginPopup("ToneCurvePointShapeMenu")) {
        std::vector<ToneCurvePoint>& editablePointsMutable = EditablePoints();
        if (m_ContextPoint >= 0 && m_ContextPoint < static_cast<int>(editablePointsMutable.size())) {
            ToneCurvePoint& point = editablePointsMutable[static_cast<std::size_t>(m_ContextPoint)];
            ImGui::TextDisabled("Point %.3f, %.3f", point.x, point.y);
            ImGui::Separator();

            const bool linearSelected = point.shape == ToneCurveSegmentShape::Linear;

            if (ImGui::MenuItem("Linear", nullptr, linearSelected)) {
                point.shape = ToneCurveSegmentShape::Linear;
                m_LutDirty = true;
            }
            ImGui::TextDisabled("Tone Curve now uses linear point connections only.");

            ImGui::Separator();
            ImGui::BeginDisabled(editablePointsMutable.size() <= 2 || m_ContextPoint == 0 || m_ContextPoint == static_cast<int>(editablePointsMutable.size()) - 1);
            if (ImGui::MenuItem("Delete Point")) {
                m_SelectedPoint = m_ContextPoint;
                DeleteSelectedPoint();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }

    for (int i = 0; i < static_cast<int>(editablePoints.size()); ++i) {
        const ImVec2 point = CurveToScreen(rect, editablePoints[static_cast<std::size_t>(i)]);
        const bool selected = i == m_SelectedPoint;
        drawList->AddCircleFilled(point, selected ? 8.5f : 7.0f, selected ? selectedColor : pointColor, 24);
        drawList->AddCircle(point, selected ? 10.0f : 8.5f, IM_COL32(0, 0, 0, 145), 24, 1.35f);
    }
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
}