#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/MaskRenderTypes.h"

#include <array>
#include <cstdint>
#include <vector>

struct RenderTextureStats;

class ToneMapperLayer : public LayerBase {
public:
    ToneMapperLayer();
    ~ToneMapperLayer() override;

    const char* GetDefaultName() const override { return "Tone Mapper / Filmic"; }
    const char* GetCategory() const override { return "Tone"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;
    NodeSurfaceSpec GetNodeSurfaceSpec() const override;
    void RenderExpandedNodeSurface(class EditorModule* editor, const NodeSurfaceContext& context) override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;
    float m_Exposure = 0.0f;
    float m_Shoulder = 0.55f;
    float m_Toe = 0.18f;
    float m_Contrast = 1.0f;
    float m_WhitePoint = 4.0f;
    float m_BlackPoint = 0.0f;
    bool m_PreserveHue = true;
};

enum class ToneCurveMode {
    Luminance,
    RGB,
    Red,
    Green,
    Blue
};

enum class ToneCurveDomain {
    Linear,
    LogScene
};

enum class ToneCurveSamplingBasis : int {
    CurveInput = 0,
    FinalPreview = 1
};

enum class ToneCurveTargetingMode : int {
    RegionTarget = 0,
    PointTarget = 1
};

enum class ToneCurveTargetScope : int {
    Global = 0,
    ScopedMask = 1
};

enum class ToneCurveGraphView : int {
    Finish = 0,
    Prepared = 1
};

enum class ToneCurveAutoSceneProfile : int {
    Balanced = 0,
    HighlightHeavy = 1,
    ShadowHeavy = 2,
    Flat = 3,
    NoisyLowLight = 4
};

enum class ToneCurveAutoVariant : int {
    Recommended = 0,
    OpenShadows = 1,
    ProtectHighlights = 2,
    MoreContrast = 3
};

enum class ToneCurveScopeMaskAction : int {
    NewMask = 0,
    Add = 1,
    Subtract = 2,
    Intersect = 3
};

enum class ToneCurveOutputMode {
    SceneLinear,
    Display
};

enum class ToneCurveSegmentShape {
    Smooth,
    Linear,
    Hold
};

struct ToneCurvePoint {
    float x = 0.0f;
    float y = 0.0f;
    ToneCurveSegmentShape shape = ToneCurveSegmentShape::Linear;
};

class ToneCurveLayer : public LayerBase {
public:
    struct EffectiveLocalBaselineSettings {
        float strength = 0.0f;
        float shadowOpening = 0.0f;
        float highlightCompression = 0.0f;
        float radius = 0.0f;
        float edgeProtection = 0.0f;
        float rangeProtection = 0.0f;
    };

    struct AutoSceneStats {
        bool valid = false;
        float shadowPercentile = 0.02f;
        float midtonePercentile = 0.18f;
        float highlightPercentile = 0.85f;
        float clippingRatio = 0.0f;
        float noiseRisk = 0.0f;
        float highlightPressure = 0.0f;
        float textureConfidence = 0.5f;
        float hdrSpreadEv = 0.0f;
        ToneCurveAutoSceneProfile profile = ToneCurveAutoSceneProfile::Balanced;
        float recommendedBaseEv = 0.0f;
        float recommendedLocalStrength = 1.05f;
        float recommendedShadowOpening = 1.20f;
        float recommendedHighlightCompression = 1.25f;
        std::array<float, 5> recommendedFoundationEv { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct AutoToneIntent {
        EffectiveLocalBaselineSettings localBaseline;
        bool localBaselineEnabled = false;
        float middleGrey = 0.18f;
        float logMinEv = -10.0f;
        float logMaxEv = 6.0f;
        float targetAffectWidth = 0.08f;
        float targetShadowProtection = 0.65f;
        float targetHighlightProtection = 0.65f;
        bool foundationAdaptiveAssist = false;
        float foundationAssistStrength = 0.0f;
        float foundationBandWidth = 2.50f;
        bool foundationPreserveHue = true;
        std::array<float, 5> foundationRegionEv { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        std::array<float, 5> pointResidualEv { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct AutoAuthoredState {
        bool localBaselineEnabled = false;
        EffectiveLocalBaselineSettings localBaseline;
        float middleGrey = 0.18f;
        float logMinEv = -10.0f;
        float logMaxEv = 6.0f;
        float targetAffectWidth = 0.08f;
        float targetShadowProtection = 0.65f;
        float targetHighlightProtection = 0.65f;
        bool foundationAdaptiveAssist = false;
        float foundationAssistStrength = 0.0f;
        float foundationBandWidth = 2.50f;
        bool foundationPreserveHue = true;
        std::array<float, 5> foundationRegionEv { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        std::vector<ToneCurvePoint> points;
    };

    struct ViewportInteractionState {
        bool probeValid = false;
        ToneCurveSamplingBasis probeSamplingBasis = ToneCurveSamplingBasis::CurveInput;
        float probeU = 0.0f;
        float probeV = 0.0f;
        std::array<float, 4> probeRgba { 0.0f, 0.0f, 0.0f, 1.0f };
        bool selectionSeedValid = false;
        float selectionSeedU = 0.0f;
        float selectionSeedV = 0.0f;
        float selectionSeedInputX = 0.0f;
        float selectionSeedSceneValue = 0.0f;
        std::array<float, 4> selectionSeedRgba { 0.0f, 0.0f, 0.0f, 1.0f };
        int onImageDragPointIndex = -1;
        float onImageDragAnchorInputX = 0.0f;
        float onImageDragAnchorOutputY = 0.0f;
    };

    ToneCurveLayer();
    ~ToneCurveLayer() override;

    const char* GetDefaultName() const override { return "Tone Curve"; }
    const char* GetCategory() const override { return "Tone"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;
    NodeSurfaceSpec GetNodeSurfaceSpec() const override;
    void RenderExpandedNodeSurface(class EditorModule* editor, const NodeSurfaceContext& context) override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

    ToneCurveSamplingBasis GetSamplingBasis() const { return m_SamplingBasis; }
    void SetSamplingBasis(ToneCurveSamplingBasis basis) { m_SamplingBasis = basis; }
    ToneCurveTargetingMode GetTargetingMode() const { return m_TargetingMode; }
    void ClearViewportProbe();
    void UpdateViewportProbe(
        ToneCurveSamplingBasis basis,
        float u,
        float v,
        const std::array<float, 4>& rgba);
    bool BeginViewportTargetDrag(
        ToneCurveSamplingBasis basis,
        float u,
        float v,
        const std::array<float, 4>& rgba);
    void UpdateViewportTargetDrag(float deltaCurveY);
    void EndViewportTargetDrag();
    bool RenderDevelopBridgeControls(float controlWidth, bool showExtendedGuidance);
    bool RenderDevelopFinishGraphPanel(float controlWidth, bool showDetails, bool allowPreparedEditing = true);
    bool RenderDevelopPreparedControlsPanel(float controlWidth, bool showDetails);
    bool RenderDevelopFoundationControlsPanel(float controlWidth, bool showDetails);
    bool RenderDevelopTargetingPanel(class EditorModule* editor, int nodeId, float controlWidth, bool showDetails);
    bool RenderDevelopScopedMaskPanel(class EditorModule* editor, int nodeId, float controlWidth, bool showDetails);
    bool RenderDevelopPreparedGraphPreviewPanel(float controlWidth, bool showDetails);
    ViewportInteractionState CaptureViewportInteractionState() const;
    void RestoreViewportInteractionState(const ViewportInteractionState& state);
    bool HasAutoPreparedState() const { return m_LastAutoAuthoredStateValid || m_AutoSceneStatsValid; }
    void NotifyUpstreamDevelopChanged();
    void SetAutoRewriteRenderContext(int nodeId, std::uint64_t requestRevision);
    void SetDevelopScenePrepToneBudget(bool scenePrepApplied, float strength, float maxEvBias);
    bool HasPendingAutoRewriteFeedback() const { return m_PendingAutoRewriteFeedback.valid; }
    ToneCurveAutoRewriteFeedback TakePendingAutoRewriteFeedback();
    void ApplyAutoRewriteFeedback(const ToneCurveAutoRewriteFeedback& feedback);

private:
    unsigned int m_ShaderProgram = 0;
    unsigned int m_LutTexture = 0;
    bool m_LutDirty = true;
    ToneCurveMode m_Mode = ToneCurveMode::RGB;
    ToneCurveDomain m_Domain = ToneCurveDomain::LogScene;
    ToneCurveOutputMode m_OutputMode = ToneCurveOutputMode::SceneLinear;
    std::vector<ToneCurvePoint> m_PreparedPoints;
    std::vector<ToneCurvePoint> m_Points;
    int m_SelectedPoint = -1;
    int m_DraggingPoint = -1;
    int m_ContextPoint = -1;
    bool m_FreeEndpoints = true;
    bool m_ShowFinalCurve = true;
    ToneCurveGraphView m_ActiveGraphView = ToneCurveGraphView::Finish;
    bool m_EnableFilmic = false;
    bool m_EnableDynamicRange = false;
    bool m_PreserveHue = true;
    float m_Exposure = 0.0f;
    float m_BlackPoint = 0.0f;
    float m_WhitePoint = 1.0f;
    float m_Shoulder = 0.55f;
    float m_Toe = 0.18f;
    float m_Contrast = 1.0f;
    float m_Shadows = 0.0f;
    float m_Highlights = 0.0f;
    float m_Whites = 0.0f;
    float m_Blacks = 0.0f;
    float m_MidtoneContrast = 0.0f;
    float m_LogMinEv = -10.0f;
    float m_LogMaxEv = 6.0f;
    float m_MiddleGrey = 0.18f;
    ToneCurveSamplingBasis m_SamplingBasis = ToneCurveSamplingBasis::CurveInput;
    ToneCurveTargetingMode m_TargetingMode = ToneCurveTargetingMode::RegionTarget;
    bool m_ProbeValid = false;
    ToneCurveSamplingBasis m_ProbeSamplingBasis = ToneCurveSamplingBasis::CurveInput;
    float m_ProbeInputX = 0.0f;
    float m_ProbeOutputY = 0.0f;
    float m_ProbeSceneValue = 0.0f;
    float m_ProbeU = 0.0f;
    float m_ProbeV = 0.0f;
    std::array<float, 4> m_ProbeRgba { 0.0f, 0.0f, 0.0f, 1.0f };
    int m_OnImageDragPointIndex = -1;
    float m_OnImageDragAnchorInputX = 0.0f;
    float m_OnImageDragAnchorOutputY = 0.0f;
    float m_TargetAffectWidth = 0.08f;
    bool m_AutoAnchorProtection = true;
    bool m_ProtectEndpointsDuringTargeting = true;
    float m_TargetShadowProtection = 0.65f;
    float m_TargetHighlightProtection = 0.65f;
    bool m_ShowAdvancedControls = false;
    bool m_AutoCalibratePending = false;
    bool m_AutoCalibrateForceReanalysis = false;
    std::uint64_t m_AutoCalibrateRequestId = 0;
    ToneCurveAutoVariant m_AutoCalibrateVariant = ToneCurveAutoVariant::Recommended;
    float m_AutoSceneAssistStrength = 0.78f;
    float m_AutoDynamicRange = 1.0f;
    float m_AutoShadowBias = 0.0f;
    float m_AutoHighlightBias = 0.0f;
    float m_AutoHighlightCharacter = 0.0f;
    float m_AutoContrastBias = 0.0f;
    bool m_AutoSceneStatsValid = false;
    unsigned int m_AutoSceneAnalysisTexture = 0;
    int m_AutoSceneAnalysisWidth = 0;
    int m_AutoSceneAnalysisHeight = 0;
    int m_AutoSceneAnalysisFramesUntilRefresh = 0;
    float m_AutoSceneShadowPercentile = 0.02f;
    float m_AutoSceneMidtonePercentile = 0.18f;
    float m_AutoSceneHighlightPercentile = 0.85f;
    float m_AutoSceneClippingRatio = 0.0f;
    float m_AutoSceneNoiseRisk = 0.0f;
    float m_AutoSceneHighlightPressure = 0.0f;
    float m_AutoSceneTextureConfidence = 0.5f;
    float m_AutoSceneHdrSpreadEv = 0.0f;
    ToneCurveAutoSceneProfile m_AutoSceneProfile = ToneCurveAutoSceneProfile::Balanced;
    float m_AutoRecommendedBaseEv = 0.0f;
    float m_AutoRecommendedLocalStrength = 1.05f;
    float m_AutoRecommendedShadowOpening = 1.20f;
    float m_AutoRecommendedHighlightCompression = 1.25f;
    std::array<float, 5> m_AutoRecommendedFoundationEv { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    bool m_LocalBaselineEnabled = false;
    float m_LocalBaselineStrength = 0.0f;
    float m_LocalShadowOpening = 0.0f;
    float m_LocalHighlightCompression = 0.0f;
    float m_LocalBaselineRadius = 72.0f;
    float m_LocalEdgeProtection = 0.65f;
    float m_LocalRangeProtection = 0.45f;
    float m_FoundationShadows = 0.0f;
    float m_FoundationDarks = 0.0f;
    float m_FoundationMidtones = 0.0f;
    float m_FoundationLights = 0.0f;
    float m_FoundationHighlights = 0.0f;
    bool m_FoundationAdaptiveAssist = false;
    float m_FoundationAssistStrength = 0.68f;
    float m_FoundationBandWidth = 2.50f;
    bool m_FoundationPreserveHue = true;
    ToneCurveTargetScope m_TargetScope = ToneCurveTargetScope::Global;
    ToneCurveScopeMaskAction m_ScopedMaskAction = ToneCurveScopeMaskAction::NewMask;
    bool m_SelectionSeedValid = false;
    float m_SelectionSeedU = 0.0f;
    float m_SelectionSeedV = 0.0f;
    float m_SelectionSeedInputX = 0.0f;
    float m_SelectionSeedSceneValue = 0.0f;
    std::array<float, 4> m_SelectionSeedRgba { 0.0f, 0.0f, 0.0f, 1.0f };
    float m_SelectionToneSimilarity = 0.12f;
    float m_SelectionColorSimilarity = 0.18f;
    float m_SelectionRegionRadius = 0.35f;
    float m_SelectionFeather = 0.35f;
    float m_SelectionEdgeSensitivity = 0.45f;
    float m_SelectionLocalCoherence = 0.45f;
    AutoSceneStats m_AutoSceneStats;
    int m_AutoRewriteNodeId = -1;
    std::uint64_t m_AutoRewriteRequestRevision = 0;
    bool m_DevelopScenePrepToneBudgetActive = false;
    float m_DevelopScenePrepToneBudgetStrength = 0.0f;
    float m_DevelopScenePrepToneBudgetMaxEvBias = 0.0f;
    ToneCurveAutoRewriteFeedback m_PendingAutoRewriteFeedback;
    bool m_LastAutoAuthoredStateValid = false;
    AutoAuthoredState m_LastAutoAuthoredState;

    void ResetLinear();
    void ResetActiveCurveToLinear();
    void ResetToneShape();
    void ResetDynamicRange();
    void ApplySoftContrastPreset();
    void ApplyFilmicShoulderPreset();
    void ApplyStrongSCurvePreset();
    bool RenderScopedMaskPanel(class EditorModule* editor, int nodeId, float controlWidth, bool showDetails, bool embeddedInDevelop);
    void SanitizePoints();
    void RenderCurveEditor(float width, float height, bool allowEditing = true);
    std::vector<ToneCurvePoint>& EditablePoints();
    const std::vector<ToneCurvePoint>& EditablePoints() const;
    void AddPointAt(float x, float y);
    void DeleteSelectedPoint();
    void MovePoint(int index, float x, float y);
    void UpdateLut();
    float EvaluateCurve(float x) const;
    float EvaluateCurve(const std::vector<ToneCurvePoint>& points, float x) const;
    float EvaluatePreparedCurve(float x) const;
    float EvaluateFinishCurve(float x) const;
    float EvaluateCombinedPointCurve(float x) const;
    float EvaluateFinalCurve(float x) const;
    float SceneToCurveCoord(float x) const;
    float CurveCoordToScene(float x) const;
    float ClampTargetInputX(float x) const;
    float ProbeSceneValueForSample(const std::array<float, 4>& rgba) const;
    int FindNearbyPointByInput(float x, float tolerance) const;
    int EnsurePointAtInput(float x, float tolerance, bool avoidEndpoints);
    bool IsEndpointIndex(int index) const;
    void EnsureTargetProtectionPoints(float centerX);
    void CaptureSelectionSeedFromProbe();
    void ClearSelectionSeed();
    void RefreshProbeOutput();
    void RequestAutoCalibration(ToneCurveAutoVariant variant, bool forceReanalysis = true);
    void UpdateAutoSceneAnalysis(unsigned int inputTexture, int width, int height, bool forceRefresh);
    AutoToneIntent SolveAutoToneIntent() const;
    AutoAuthoredState BuildAutoAuthoredStateFromIntent(const AutoToneIntent& intent) const;
    AutoAuthoredState CaptureCurrentAutoAuthoredState() const;
    AutoAuthoredState ApplyUserAdjustmentsToAutoAuthoredState(const AutoAuthoredState& state) const;
    void ApplyAuthoredStateForRender(const AutoAuthoredState& state);
    void CapturePendingAutoRewriteFeedback();
    void ClearPendingAutoRewriteFeedback();
    EffectiveLocalBaselineSettings ComputeEffectiveLocalBaselineSettings() const;
    float ComputeEffectiveToneAnchor() const;
    float ComputeEffectiveFoundationAssistStrength() const;
    float ComputeEffectiveFoundationBandWidth() const;
    float ComputeEffectiveTargetAffectWidth() const;
    float ComputeEffectiveTargetShadowProtection() const;
    float ComputeEffectiveTargetHighlightProtection() const;
    std::array<float, 5> GetFoundationRegionValues() const;
    std::array<float, 5> ComputeEffectiveFoundationRegionValues() const;
    std::array<float, 5> ComputeFoundationTargetWeights(float sceneValue) const;
    float ComputeApproximateLocalBaselineGainEv(float sceneValue) const;
    float ApplyApproximateLocalBaselineToSceneValue(float sceneValue) const;
    float ApplyFoundationToSceneValue(float sceneValue) const;
    float ApplyFoundationToSceneValue(float sceneValue, float middleGrey, float bandWidth, const std::array<float, 5>& foundationRegionEv) const;
    float EvaluateCombinedOutputCoord(float inputCoord) const;
    void ApplyRegionTargetDelta(float deltaCurveY);
};

class ToneEqualizerLayer : public LayerBase {
public:
    ToneEqualizerLayer();
    ~ToneEqualizerLayer() override;

    const char* GetDefaultName() const override { return "Tone Equalizer"; }
    const char* GetCategory() const override { return "Tone"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;
    NodeSurfaceSpec GetNodeSurfaceSpec() const override;
    void RenderExpandedNodeSurface(class EditorModule* editor, const NodeSurfaceContext& context) override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;
    float m_ShadowsEv = 0.0f;
    float m_DarksEv = 0.0f;
    float m_MidtonesEv = 0.0f;
    float m_LightsEv = 0.0f;
    float m_HighlightsEv = 0.0f;
    float m_MiddleGrey = 0.18f;
    float m_Range = 4.0f;
    bool m_PreserveHue = true;
};

class ViewTransformLayer : public LayerBase {
public:
    ViewTransformLayer();
    ~ViewTransformLayer() override;

    const char* GetDefaultName() const override { return "View Transform"; }
    const char* GetCategory() const override { return "Tone"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;
    NodeSurfaceSpec GetNodeSurfaceSpec() const override;
    void RenderExpandedNodeSurface(class EditorModule* editor, const NodeSurfaceContext& context) override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;
    float m_Exposure = 0.0f;
    float m_BlackEv = -8.0f;
    float m_WhiteEv = 4.0f;
    float m_MiddleGrey = 0.18f;
    float m_Shoulder = 0.45f;
    float m_Toe = 0.18f;
    float m_Contrast = 1.0f;
    float m_Saturation = 1.0f;
    bool m_PreserveHue = true;
    bool m_DebugFalseColor = false;
    bool m_LastProbeValid = false;
    float m_LastMinRgb = 0.0f;
    float m_LastMaxRgb = 0.0f;
    float m_LastMinLuma = 0.0f;
    float m_LastMaxLuma = 0.0f;
    float m_LastP01Luma = 0.0f;
    float m_LastP50Luma = 0.0f;
    float m_LastP99Luma = 0.0f;
    float m_LastHdrPixelPercent = 0.0f;
    float m_LastDisplayEdgePercent = 0.0f;

    void ResetDisplayDefaults();
    void StoreProbeStats(const RenderTextureStats& stats);
    void ApplyAutoFromStats(const RenderTextureStats& stats);
};

class ShadowsHighlightsLayer : public LayerBase {
public:
    ShadowsHighlightsLayer();
    ~ShadowsHighlightsLayer() override;

    const char* GetDefaultName() const override { return "Shadows / Highlights"; }
    const char* GetCategory() const override { return "Tone"; }

    void InitializeGL() override;
    void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) override;
    void RenderUI() override;

    json Serialize() const override;
    void Deserialize(const json& j) override;

private:
    unsigned int m_ShaderProgram = 0;
    float m_Shadows = 0.0f;
    float m_Highlights = 0.0f;
    float m_Whites = 0.0f;
    float m_Blacks = 0.0f;
    float m_MidtoneContrast = 0.0f;
};
