#include "ToneLayers.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

constexpr int kToneCurveMaxPoints = 12;

std::vector<ToneCurvePoint> BuildLinearToneCurvePoints() {
    return {
        { 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
        { 1.0f, 1.0f, ToneCurveSegmentShape::Linear }
    };
}

} // namespace

void ToneCurveLayer::ResetLinear() {
    m_PreparedPoints = BuildLinearToneCurvePoints();
    m_Points = BuildLinearToneCurvePoints();
    m_ActiveGraphView = ToneCurveGraphView::Finish;
    m_SelectedPoint = -1;
    m_DraggingPoint = -1;
    m_ContextPoint = -1;
    m_LastAutoAuthoredStateValid = false;
    m_LastAutoAuthoredState = {};
    m_LutDirty = true;
}

void ToneCurveLayer::ResetActiveCurveToLinear() {
    EditablePoints() = BuildLinearToneCurvePoints();
    m_SelectedPoint = -1;
    m_DraggingPoint = -1;
    m_ContextPoint = -1;
    SanitizePoints();
    m_LutDirty = true;
}

std::vector<ToneCurvePoint>& ToneCurveLayer::EditablePoints() {
    return m_ActiveGraphView == ToneCurveGraphView::Prepared ? m_PreparedPoints : m_Points;
}

const std::vector<ToneCurvePoint>& ToneCurveLayer::EditablePoints() const {
    return m_ActiveGraphView == ToneCurveGraphView::Prepared ? m_PreparedPoints : m_Points;
}

void ToneCurveLayer::ResetToneShape() {
    m_Shoulder = 0.55f;
    m_Toe = 0.18f;
    m_Contrast = 1.0f;
    m_LutDirty = true;
}

void ToneCurveLayer::ResetDynamicRange() {
    m_Shadows = 0.0f;
    m_Highlights = 0.0f;
    m_Whites = 0.0f;
    m_Blacks = 0.0f;
    m_MidtoneContrast = 0.0f;
    m_LutDirty = true;
}

void ToneCurveLayer::ApplySoftContrastPreset() {
    EditablePoints() = {
        { 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
        { 0.25f, 0.18f, ToneCurveSegmentShape::Linear },
        { 0.5f, 0.5f, ToneCurveSegmentShape::Linear },
        { 0.75f, 0.82f, ToneCurveSegmentShape::Linear },
        { 1.0f, 1.0f, ToneCurveSegmentShape::Linear }
    };
    m_SelectedPoint = -1;
    m_ContextPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::ApplyFilmicShoulderPreset() {
    EditablePoints() = {
        { 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
        { 0.18f, 0.16f, ToneCurveSegmentShape::Linear },
        { 0.5f, 0.56f, ToneCurveSegmentShape::Linear },
        { 0.82f, 0.9f, ToneCurveSegmentShape::Linear },
        { 1.0f, 0.98f, ToneCurveSegmentShape::Linear }
    };
    m_Domain = ToneCurveDomain::LogScene;
    m_SelectedPoint = -1;
    m_ContextPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::ApplyStrongSCurvePreset() {
    EditablePoints() = {
        { 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
        { 0.18f, 0.08f, ToneCurveSegmentShape::Linear },
        { 0.42f, 0.36f, ToneCurveSegmentShape::Linear },
        { 0.62f, 0.72f, ToneCurveSegmentShape::Linear },
        { 0.86f, 0.95f, ToneCurveSegmentShape::Linear },
        { 1.0f, 1.0f, ToneCurveSegmentShape::Linear }
    };
    m_SelectedPoint = -1;
    m_ContextPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::SanitizePoints() {
    m_ActiveGraphView = static_cast<ToneCurveGraphView>(std::clamp(static_cast<int>(m_ActiveGraphView), 0, 1));
    m_TargetingMode = static_cast<ToneCurveTargetingMode>(std::clamp(static_cast<int>(m_TargetingMode), 0, 1));
    m_TargetAffectWidth = std::clamp(m_TargetAffectWidth, 0.02f, 0.30f);
    m_TargetShadowProtection = std::clamp(m_TargetShadowProtection, 0.0f, 1.0f);
    m_TargetHighlightProtection = std::clamp(m_TargetHighlightProtection, 0.0f, 1.0f);
    m_AutoSceneAssistStrength = std::clamp(m_AutoSceneAssistStrength, 0.0f, 2.4f);
    m_AutoDynamicRange = std::clamp(m_AutoDynamicRange, 0.25f, 3.0f);
    m_AutoShadowBias = std::clamp(m_AutoShadowBias, -1.25f, 1.25f);
    m_AutoHighlightBias = std::clamp(m_AutoHighlightBias, -1.25f, 1.25f);
    m_AutoHighlightCharacter = std::clamp(m_AutoHighlightCharacter, -1.25f, 1.25f);
    m_AutoContrastBias = std::clamp(m_AutoContrastBias, -1.25f, 1.25f);
    m_LocalBaselineStrength = std::clamp(m_LocalBaselineStrength, 0.0f, 1.6f);
    m_LocalShadowOpening = std::clamp(m_LocalShadowOpening, 0.0f, 2.2f);
    m_LocalHighlightCompression = std::clamp(m_LocalHighlightCompression, 0.0f, 2.2f);
    m_LocalBaselineRadius = std::clamp(m_LocalBaselineRadius, 8.0f, 220.0f);
    m_LocalEdgeProtection = std::clamp(m_LocalEdgeProtection, 0.0f, 1.0f);
    m_LocalRangeProtection = std::clamp(m_LocalRangeProtection, 0.0f, 1.0f);
    m_FoundationShadows = std::clamp(m_FoundationShadows, -5.0f, 5.0f);
    m_FoundationDarks = std::clamp(m_FoundationDarks, -5.0f, 5.0f);
    m_FoundationMidtones = std::clamp(m_FoundationMidtones, -5.0f, 5.0f);
    m_FoundationLights = std::clamp(m_FoundationLights, -5.0f, 5.0f);
    m_FoundationHighlights = std::clamp(m_FoundationHighlights, -5.0f, 5.0f);
    m_FoundationAssistStrength = std::clamp(m_FoundationAssistStrength, 0.0f, 1.0f);
    m_FoundationBandWidth = std::clamp(m_FoundationBandWidth, 0.5f, 8.0f);
    m_MiddleGrey = std::clamp(m_MiddleGrey, 0.01f, 1.0f);
    m_TargetScope = static_cast<ToneCurveTargetScope>(std::clamp(static_cast<int>(m_TargetScope), 0, 1));
    m_ScopedMaskAction = static_cast<ToneCurveScopeMaskAction>(std::clamp(static_cast<int>(m_ScopedMaskAction), 0, 3));
    m_SelectionToneSimilarity = std::clamp(m_SelectionToneSimilarity, 0.02f, 0.35f);
    m_SelectionColorSimilarity = std::clamp(m_SelectionColorSimilarity, 0.02f, 0.50f);
    m_SelectionRegionRadius = std::clamp(m_SelectionRegionRadius, 0.05f, 1.0f);
    m_SelectionFeather = std::clamp(m_SelectionFeather, 0.0f, 1.0f);
    m_SelectionEdgeSensitivity = std::clamp(m_SelectionEdgeSensitivity, 0.0f, 1.0f);
    m_SelectionLocalCoherence = std::clamp(m_SelectionLocalCoherence, 0.0f, 1.0f);
    auto sanitizeCurve = [&](std::vector<ToneCurvePoint>& points) {
        for (ToneCurvePoint& point : points) {
            point.x = Clamp01(point.x);
            point.y = Clamp01(point.y);
            point.shape = ToneCurveSegmentShape::Linear;
        }
        std::sort(points.begin(), points.end(), [](const ToneCurvePoint& a, const ToneCurvePoint& b) {
            return a.x < b.x;
        });
        if (points.empty()) {
            points = BuildLinearToneCurvePoints();
        }
        if (points.size() == 1) {
            points.push_back({ 1.0f, points.front().y, points.front().shape });
        }
        if (points.size() > kToneCurveMaxPoints) {
            points.resize(kToneCurveMaxPoints);
        }
        if (!m_FreeEndpoints && points.size() >= 2) {
            points.front().x = 0.0f;
            points.back().x = 1.0f;
        }
    };
    sanitizeCurve(m_PreparedPoints);
    sanitizeCurve(m_Points);
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (m_SelectedPoint >= static_cast<int>(editablePoints.size())) {
        m_SelectedPoint = -1;
    }
    if (m_ContextPoint >= static_cast<int>(editablePoints.size())) {
        m_ContextPoint = -1;
    }
}

void ToneCurveLayer::AddPointAt(float x, float y) {
    std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (editablePoints.size() >= kToneCurveMaxPoints) {
        return;
    }
    editablePoints.push_back({ Clamp01(x), Clamp01(y), ToneCurveSegmentShape::Linear });
    SanitizePoints();
    const std::vector<ToneCurvePoint>& sanitizedPoints = EditablePoints();
    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(sanitizedPoints.size()); ++i) {
        const float dx = sanitizedPoints[static_cast<std::size_t>(i)].x - x;
        const float dy = sanitizedPoints[static_cast<std::size_t>(i)].y - y;
        const float distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    m_SelectedPoint = bestIndex;
    m_LutDirty = true;
}

void ToneCurveLayer::DeleteSelectedPoint() {
    std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (m_SelectedPoint < 0 || m_SelectedPoint >= static_cast<int>(editablePoints.size()) || editablePoints.size() <= 2) {
        return;
    }
    if (m_SelectedPoint == 0 || m_SelectedPoint == static_cast<int>(editablePoints.size()) - 1) {
        return;
    }
    editablePoints.erase(editablePoints.begin() + m_SelectedPoint);
    m_SelectedPoint = -1;
    m_DraggingPoint = -1;
    m_ContextPoint = -1;
    SanitizePoints();
    m_LutDirty = true;
}

void ToneCurveLayer::MovePoint(int index, float x, float y) {
    std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (index < 0 || index >= static_cast<int>(editablePoints.size())) {
        return;
    }
    float nextX = Clamp01(x);
    if (!m_FreeEndpoints && index == 0) {
        nextX = 0.0f;
    } else if (!m_FreeEndpoints && index == static_cast<int>(editablePoints.size()) - 1) {
        nextX = 1.0f;
    }
    ToneCurvePoint& point = editablePoints[static_cast<std::size_t>(index)];
    point.x = nextX;
    point.y = Clamp01(y);
    SanitizePoints();
    const std::vector<ToneCurvePoint>& sanitizedPoints = EditablePoints();
    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(sanitizedPoints.size()); ++i) {
        const float dx = sanitizedPoints[static_cast<std::size_t>(i)].x - nextX;
        const float dy = sanitizedPoints[static_cast<std::size_t>(i)].y - Clamp01(y);
        const float distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    m_SelectedPoint = bestIndex;
    m_DraggingPoint = bestIndex;
    m_LutDirty = true;
}

float ToneCurveLayer::SceneToCurveCoord(float x) const {
    if (m_Domain == ToneCurveDomain::LogScene) {
        const float ev = std::log2(std::max(x, 0.000001f) / std::max(m_MiddleGrey, 0.000001f));
        return Clamp01((ev - m_LogMinEv) / std::max(0.0001f, m_LogMaxEv - m_LogMinEv));
    }
    return Clamp01(x);
}

float ToneCurveLayer::CurveCoordToScene(float coord) const {
    if (m_Domain == ToneCurveDomain::LogScene) {
        const float ev = m_LogMinEv + Clamp01(coord) * (m_LogMaxEv - m_LogMinEv);
        return std::max(m_MiddleGrey, 0.000001f) * std::exp2(ev);
    }
    return Clamp01(coord);
}

float ToneCurveLayer::ClampTargetInputX(float x) const {
    const float clamped = Clamp01(x);
    if (!m_ProtectEndpointsDuringTargeting) {
        return clamped;
    }
    const float affectWidth = ComputeEffectiveTargetAffectWidth();
    const float shadowProtection = ComputeEffectiveTargetShadowProtection();
    const float highlightProtection = ComputeEffectiveTargetHighlightProtection();
    const float edgeGuard = std::clamp(
        affectWidth * (0.42f + 0.18f * std::max(shadowProtection, highlightProtection)),
        0.015f,
        0.18f);
    return std::clamp(clamped, edgeGuard, 1.0f - edgeGuard);
}

float ToneCurveLayer::ProbeSceneValueForSample(const std::array<float, 4>& rgba) const {
    const float r = std::max(0.0f, rgba[0]);
    const float g = std::max(0.0f, rgba[1]);
    const float b = std::max(0.0f, rgba[2]);
    switch (m_Mode) {
        case ToneCurveMode::Red: return r;
        case ToneCurveMode::Green: return g;
        case ToneCurveMode::Blue: return b;
        case ToneCurveMode::RGB:
        case ToneCurveMode::Luminance:
        default:
            return std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
    }
}

int ToneCurveLayer::FindNearbyPointByInput(float x, float tolerance) const {
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    const float clampedX = Clamp01(x);
    int bestIndex = -1;
    float bestDistance = std::max(0.0f, tolerance);
    for (int i = 0; i < static_cast<int>(editablePoints.size()); ++i) {
        const float distance = std::abs(editablePoints[static_cast<std::size_t>(i)].x - clampedX);
        if (distance <= bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

int ToneCurveLayer::EnsurePointAtInput(float x, float tolerance, bool avoidEndpoints) {
    const float clampedX = Clamp01(x);
    int pointIndex = FindNearbyPointByInput(clampedX, tolerance);
    if (avoidEndpoints && IsEndpointIndex(pointIndex)) {
        pointIndex = -1;
    }
    if (pointIndex < 0) {
        AddPointAt(clampedX, EvaluateCurve(clampedX));
        pointIndex = m_SelectedPoint;
        if (avoidEndpoints && IsEndpointIndex(pointIndex)) {
            pointIndex = -1;
        }
    } else {
        m_SelectedPoint = pointIndex;
        m_DraggingPoint = pointIndex;
    }
    return pointIndex;
}

bool ToneCurveLayer::IsEndpointIndex(int index) const {
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    return index == 0 || index == static_cast<int>(editablePoints.size()) - 1;
}

void ToneCurveLayer::EnsureTargetProtectionPoints(float centerX) {
    if (!m_AutoAnchorProtection) {
        return;
    }

    const float halfWidth = std::clamp(ComputeEffectiveTargetAffectWidth(), 0.02f, 0.35f);
    const float shadowProtection = ComputeEffectiveTargetShadowProtection();
    const float highlightProtection = ComputeEffectiveTargetHighlightProtection();
    const float tolerance = std::max(0.0125f, halfWidth * 0.22f);
    const float leftDistance = halfWidth * (1.0f - 0.65f * shadowProtection);
    const float rightDistance = halfWidth * (1.0f - 0.65f * highlightProtection);
    const float leftX = Clamp01(centerX - leftDistance);
    const float rightX = Clamp01(centerX + rightDistance);
    const float edgeGuard = std::clamp(halfWidth * 0.35f, 0.01f, 0.08f);

    if (leftX > edgeGuard) {
        EnsurePointAtInput(leftX, tolerance, false);
    }
    if (rightX < 1.0f - edgeGuard) {
        EnsurePointAtInput(rightX, tolerance, false);
    }
    RefreshProbeOutput();
}

void ToneCurveLayer::CaptureSelectionSeedFromProbe() {
    if (!m_ProbeValid) {
        return;
    }
    m_SelectionSeedValid = true;
    m_SelectionSeedU = m_ProbeU;
    m_SelectionSeedV = m_ProbeV;
    m_SelectionSeedInputX = m_ProbeInputX;
    m_SelectionSeedSceneValue = m_ProbeSceneValue;
    m_SelectionSeedRgba = m_ProbeRgba;
}

void ToneCurveLayer::ClearSelectionSeed() {
    m_SelectionSeedValid = false;
    m_SelectionSeedU = 0.0f;
    m_SelectionSeedV = 0.0f;
    m_SelectionSeedInputX = 0.0f;
    m_SelectionSeedSceneValue = 0.0f;
    m_SelectionSeedRgba = { 0.0f, 0.0f, 0.0f, 1.0f };
}

void ToneCurveLayer::RefreshProbeOutput() {
    if (!m_ProbeValid) {
        return;
    }
    m_ProbeInputX = Clamp01(m_ProbeInputX);
    if (m_ProbeSamplingBasis == ToneCurveSamplingBasis::CurveInput) {
        const float localScene = ApplyApproximateLocalBaselineToSceneValue(m_ProbeSceneValue);
        const float foundationScene = ApplyFoundationToSceneValue(localScene);
        const float preparedCoord = EvaluatePreparedCurve(SceneToCurveCoord(foundationScene));
        const float preparedScene = CurveCoordToScene(preparedCoord);
        const float finishCoord = EvaluateFinishCurve(SceneToCurveCoord(preparedScene));
        const float finalScene = CurveCoordToScene(finishCoord);
        m_ProbeOutputY = Clamp01(SceneToCurveCoord(finalScene));
        return;
    }
    m_ProbeOutputY = Clamp01(EvaluateCombinedOutputCoord(m_ProbeInputX));
}

void ToneCurveLayer::ClearViewportProbe() {
    m_ProbeValid = false;
    m_ProbeOutputY = 0.0f;
    m_ProbeSceneValue = 0.0f;
}

ToneCurveLayer::ViewportInteractionState ToneCurveLayer::CaptureViewportInteractionState() const {
    ViewportInteractionState state;
    state.probeValid = m_ProbeValid;
    state.probeSamplingBasis = m_ProbeSamplingBasis;
    state.probeU = m_ProbeU;
    state.probeV = m_ProbeV;
    state.probeRgba = m_ProbeRgba;
    state.selectionSeedValid = m_SelectionSeedValid;
    state.selectionSeedU = m_SelectionSeedU;
    state.selectionSeedV = m_SelectionSeedV;
    state.selectionSeedInputX = m_SelectionSeedInputX;
    state.selectionSeedSceneValue = m_SelectionSeedSceneValue;
    state.selectionSeedRgba = m_SelectionSeedRgba;
    state.onImageDragPointIndex = m_OnImageDragPointIndex;
    state.onImageDragAnchorInputX = m_OnImageDragAnchorInputX;
    state.onImageDragAnchorOutputY = m_OnImageDragAnchorOutputY;
    return state;
}

void ToneCurveLayer::RestoreViewportInteractionState(const ViewportInteractionState& state) {
    ClearViewportProbe();
    ClearSelectionSeed();
    EndViewportTargetDrag();
    m_ProbeSamplingBasis = state.probeSamplingBasis;
    m_ProbeU = Clamp01(state.probeU);
    m_ProbeV = Clamp01(state.probeV);
    m_ProbeRgba = state.probeRgba;
    if (state.probeValid) {
        UpdateViewportProbe(state.probeSamplingBasis, state.probeU, state.probeV, state.probeRgba);
    }
    m_SelectionSeedValid = state.selectionSeedValid;
    m_SelectionSeedU = Clamp01(state.selectionSeedU);
    m_SelectionSeedV = Clamp01(state.selectionSeedV);
    m_SelectionSeedInputX = Clamp01(state.selectionSeedInputX);
    m_SelectionSeedSceneValue = std::max(0.0f, state.selectionSeedSceneValue);
    m_SelectionSeedRgba = state.selectionSeedRgba;
    m_OnImageDragPointIndex = state.onImageDragPointIndex;
    m_OnImageDragAnchorInputX = state.onImageDragAnchorInputX;
    m_OnImageDragAnchorOutputY = state.onImageDragAnchorOutputY;
}

void ToneCurveLayer::UpdateViewportProbe(
    ToneCurveSamplingBasis basis,
    float u,
    float v,
    const std::array<float, 4>& rgba) {
    m_ProbeValid = true;
    m_ProbeSamplingBasis = basis;
    m_ProbeU = Clamp01(u);
    m_ProbeV = Clamp01(v);
    m_ProbeRgba = rgba;
    m_ProbeSceneValue = ProbeSceneValueForSample(rgba);
    float probeInputX = SceneToCurveCoord(m_ProbeSceneValue);
    if (basis == ToneCurveSamplingBasis::CurveInput) {
        const float localScene = ApplyApproximateLocalBaselineToSceneValue(m_ProbeSceneValue);
        const float foundationScene = ApplyFoundationToSceneValue(localScene);
        if (m_ActiveGraphView == ToneCurveGraphView::Prepared) {
            probeInputX = SceneToCurveCoord(foundationScene);
        } else {
            const float preparedCoord = EvaluatePreparedCurve(SceneToCurveCoord(foundationScene));
            probeInputX = SceneToCurveCoord(CurveCoordToScene(preparedCoord));
        }
    }
    m_ProbeInputX = probeInputX;
    RefreshProbeOutput();
}

bool ToneCurveLayer::BeginViewportTargetDrag(
    ToneCurveSamplingBasis basis,
    float u,
    float v,
    const std::array<float, 4>& rgba) {
    UpdateViewportProbe(basis, u, v, rgba);
    if (!m_ProbeValid) {
        return false;
    }

    const float targetX = ClampTargetInputX(m_ProbeInputX);
    m_ProbeInputX = targetX;
    if (m_TargetingMode == ToneCurveTargetingMode::RegionTarget) {
        RefreshProbeOutput();
        return true;
    }
    EnsureTargetProtectionPoints(targetX);
    const float pointTolerance = std::max(0.015f, ComputeEffectiveTargetAffectWidth() * 0.28f);
    int pointIndex = EnsurePointAtInput(targetX, pointTolerance, m_ProtectEndpointsDuringTargeting);
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (pointIndex < 0 || pointIndex >= static_cast<int>(editablePoints.size())) {
        return false;
    }

    m_OnImageDragPointIndex = pointIndex;
    m_OnImageDragAnchorInputX = editablePoints[static_cast<std::size_t>(pointIndex)].x;
    m_OnImageDragAnchorOutputY = editablePoints[static_cast<std::size_t>(pointIndex)].y;
    return true;
}

void ToneCurveLayer::UpdateViewportTargetDrag(float deltaCurveY) {
    if (m_TargetingMode == ToneCurveTargetingMode::RegionTarget) {
        ApplyRegionTargetDelta(deltaCurveY);
        RefreshProbeOutput();
        m_LutDirty = true;
        return;
    }
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (m_OnImageDragPointIndex < 0 || m_OnImageDragPointIndex >= static_cast<int>(editablePoints.size())) {
        return;
    }
    const float shadowProtection = ComputeEffectiveTargetShadowProtection();
    const float highlightProtection = ComputeEffectiveTargetHighlightProtection();
    float deltaScale = 1.0f;
    if (m_OnImageDragAnchorInputX < 0.35f) {
        const float shadowRegion = 1.0f - std::clamp(m_OnImageDragAnchorInputX / 0.35f, 0.0f, 1.0f);
        deltaScale *= 1.0f - 0.55f * shadowProtection * shadowRegion;
    }
    if (m_OnImageDragAnchorInputX > 0.65f) {
        const float highlightRegion = std::clamp((m_OnImageDragAnchorInputX - 0.65f) / 0.35f, 0.0f, 1.0f);
        deltaScale *= 1.0f - 0.55f * highlightProtection * highlightRegion;
    }
    const float nextY = Clamp01(editablePoints[static_cast<std::size_t>(m_OnImageDragPointIndex)].y + deltaCurveY * deltaScale);
    MovePoint(
        m_OnImageDragPointIndex,
        m_OnImageDragAnchorInputX,
        nextY);
    m_OnImageDragPointIndex = m_SelectedPoint;
    const std::vector<ToneCurvePoint>& refreshedPoints = EditablePoints();
    if (m_OnImageDragPointIndex >= 0 && m_OnImageDragPointIndex < static_cast<int>(refreshedPoints.size())) {
        m_OnImageDragAnchorOutputY = refreshedPoints[static_cast<std::size_t>(m_OnImageDragPointIndex)].y;
    }
    RefreshProbeOutput();
}

void ToneCurveLayer::EndViewportTargetDrag() {
    m_OnImageDragPointIndex = -1;
}

float ToneCurveLayer::EvaluateCurve(float x) const {
    return EvaluateCurve(EditablePoints(), x);
}

float ToneCurveLayer::EvaluateCurve(const std::vector<ToneCurvePoint>& points, float x) const {
    if (points.empty()) return x;
    if (x <= points.front().x) return points.front().y;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const ToneCurvePoint& a = points[i - 1];
        const ToneCurvePoint& b = points[i];
        if (x <= b.x) {
            const float t = (x - a.x) / std::max(0.0001f, b.x - a.x);
            switch (a.shape) {
                case ToneCurveSegmentShape::Linear:
                    return a.y + (b.y - a.y) * t;
                case ToneCurveSegmentShape::Hold:
                    return a.y;
                case ToneCurveSegmentShape::Smooth:
                default: {
                    const float smoothT = t * t * (3.0f - 2.0f * t);
                    return a.y + (b.y - a.y) * smoothT;
                }
            }
        }
    }
    return points.back().y;
}

float ToneCurveLayer::EvaluatePreparedCurve(float x) const {
    return EvaluateCurve(m_PreparedPoints, x);
}

float ToneCurveLayer::EvaluateFinishCurve(float x) const {
    return EvaluateCurve(m_Points, x);
}

float ToneCurveLayer::EvaluateCombinedPointCurve(float x) const {
    const float preparedCoord = Clamp01(EvaluatePreparedCurve(x));
    const float preparedScene = CurveCoordToScene(preparedCoord);
    const float finishInput = SceneToCurveCoord(preparedScene);
    return Clamp01(EvaluateFinishCurve(finishInput));
}

float ToneCurveLayer::EvaluateFinalCurve(float x) const {
    float y = Clamp01(EvaluateCombinedOutputCoord(x));
    if (m_EnableFilmic) {
        y = std::pow(std::max(0.0f, y), std::max(0.05f, m_Contrast));
        const float toe = Clamp01(m_Toe);
        if (toe > 0.0001f) {
            const float lifted = (y + toe * y / (y + 0.18f)) / (1.0f + toe);
            y = y + (lifted - y) * toe;
        }
        const float shoulder = std::max(0.001f, m_Shoulder);
        const float whitePoint = std::max(0.001f, m_WhitePoint);
        const float scene = y * whitePoint;
        const float mapped = scene / (scene + shoulder);
        const float whiteMapped = whitePoint / (whitePoint + shoulder);
        y = mapped / std::max(0.0001f, whiteMapped);
    }
    if (m_EnableDynamicRange) {
        const float shadowMask = 1.0f - std::clamp((x - 0.0f) / 0.55f, 0.0f, 1.0f);
        const float highlightMask = std::clamp((x - 0.45f) / 0.55f, 0.0f, 1.0f);
        y += m_Shadows * shadowMask * (1.0f - std::exp(-std::max(0.0f, 1.0f - y) * 2.0f));
        y -= m_Highlights * highlightMask * y * 0.75f;
        y += m_Whites * std::clamp((x - 0.72f) / 0.28f, 0.0f, 1.0f) * 0.5f;
        y += m_Blacks * (1.0f - std::clamp(x / 0.28f, 0.0f, 1.0f)) * 0.5f;
        y = (y - 0.5f) * (1.0f + m_MidtoneContrast) + 0.5f;
    }
    return Clamp01(y);
}

std::array<float, 5> ToneCurveLayer::GetFoundationRegionValues() const {
    return { m_FoundationShadows, m_FoundationDarks, m_FoundationMidtones, m_FoundationLights, m_FoundationHighlights };
}