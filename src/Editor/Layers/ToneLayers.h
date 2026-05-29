#pragma once

#include "LayerBase.h"
#include "Renderer/GLHelpers.h"

#include <array>
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

enum class ToneCurveOutputMode {
    SceneLinear,
    Display
};

struct ToneCurvePoint {
    float x = 0.0f;
    float y = 0.0f;
};

class ToneCurveLayer : public LayerBase {
public:
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

private:
    unsigned int m_ShaderProgram = 0;
    unsigned int m_LutTexture = 0;
    bool m_LutDirty = true;
    ToneCurveMode m_Mode = ToneCurveMode::RGB;
    ToneCurveDomain m_Domain = ToneCurveDomain::LogScene;
    ToneCurveOutputMode m_OutputMode = ToneCurveOutputMode::SceneLinear;
    std::vector<ToneCurvePoint> m_Points;
    int m_SelectedPoint = -1;
    int m_DraggingPoint = -1;
    bool m_FreeEndpoints = false;
    bool m_ShowFinalCurve = true;
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

    void ResetLinear();
    void ResetToneShape();
    void ResetDynamicRange();
    void ApplySoftContrastPreset();
    void ApplyFilmicShoulderPreset();
    void ApplyStrongSCurvePreset();
    void SanitizePoints();
    void RenderCurveEditor(float width, float height);
    void AddPointAt(float x, float y);
    void DeleteSelectedPoint();
    void MovePoint(int index, float x, float y);
    void UpdateLut();
    float EvaluateCurve(float x) const;
    float EvaluateFinalCurve(float x) const;
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
