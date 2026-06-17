#pragma once

#include "Composite/CompositeModule.h"
#include <filesystem>
#include <string>
#include <vector>
#include <imgui.h>

inline constexpr int kCompositeFormatVersion = 5;
inline constexpr float kCompositeToolbarHeight = 68.0f;
inline constexpr const char* kDefaultCompositeFontRelativePath = "Assets/Fonts/Roboto-Medium.ttf";
inline constexpr const char* kCompositeLayersWindowName = "Composite Layers";
inline constexpr const char* kCompositeSelectedWindowName = "Composite Selected";
inline constexpr const char* kCompositeViewWindowName = "Composite View";
inline constexpr const char* kCompositeExportWindowName = "Composite Export";
inline constexpr const char* kCompositeCanvasWindowName = "Composite Canvas";
inline constexpr const char* kCompositeDockSpaceName = "CompositeDockSpace";

struct FloatRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct AffineTransform2D {
    float m00 = 1.0f;
    float m01 = 0.0f;
    float m02 = 0.0f;
    float m10 = 0.0f;
    float m11 = 1.0f;
    float m12 = 0.0f;
};

struct LayerFileLoadData {
    LayerKind kind = LayerKind::Image;
    std::string name;
    std::vector<uint8_t> previewPixels;
    int previewW = 0;
    int previewH = 0;
    int logicalW = 0;
    int logicalH = 0;
    std::string embeddedProjectJson = StackBinaryFormat::json::array().dump();
    std::vector<uint8_t> originalSourcePng;
    std::string linkedProjectFileName;
    std::string linkedProjectName;
    bool generatedFromImage = false;
};

float Clamp01(float value);
float DegreesToRadians(float degrees);
float RadiansToDegrees(float radians);
std::string TrimWhitespace(const std::string& value);
std::string NewLayerId();
std::filesystem::path FindBundledCompositeFontPath();
bool ReadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& outBytes);
int SafeCompositeTextTextureLimit();
float EffectiveTextRasterZoom(float viewZoom);
void FillSolidRgba(
    std::vector<uint8_t>& outPixels,
    int width,
    int height,
    const std::array<float, 4>& color,
    bool circleMask);
bool BuildTextRgba(
    const std::vector<unsigned char>& fontBytes,
    const std::string& text,
    float fontSize,
    const std::array<float, 4>& color,
    float stretchX,
    float stretchY,
    std::vector<uint8_t>& outPixels,
    int& outWidth,
    int& outHeight);
const char* LayerKindBadge(LayerKind kind);
const char* LayerKindToToken(LayerKind kind);
LayerKind LayerKindFromToken(const std::string& token);
bool IsEditorBridgeLayer(const CompositeLayer& layer);
bool IsGeneratedLayer(const CompositeLayer& layer);
const char* BlendModeToToken(CompositeBlendMode mode);
CompositeBlendMode BlendModeFromToken(const std::string& token);
const char* ExportBoundsModeToToken(CompositeExportBoundsMode mode);
CompositeExportBoundsMode ExportBoundsModeFromToken(const std::string& token);
const char* ExportBackgroundModeToToken(CompositeExportBackgroundMode mode);
CompositeExportBackgroundMode ExportBackgroundModeFromToken(const std::string& token);
const char* ExportAspectPresetToToken(CompositeExportAspectPreset preset);
CompositeExportAspectPreset ExportAspectPresetFromToken(const std::string& token);
const char* ExportAspectPresetToLabel(CompositeExportAspectPreset preset);
float ExportAspectRatioValue(const CompositeExportSettings& settings);
CompositeLayer* FindLayerById(std::vector<CompositeLayer>& layers, const std::string& layerId);
const CompositeLayer* FindLayerById(const std::vector<CompositeLayer>& layers, const std::string& layerId);
float LayerBaseWidth(const CompositeLayer& layer);
float LayerBaseHeight(const CompositeLayer& layer);
float LayerWorldWidth(const CompositeLayer& layer);
float LayerWorldHeight(const CompositeLayer& layer);
AffineTransform2D IdentityTransform2D();
AffineTransform2D Multiply(const AffineTransform2D& a, const AffineTransform2D& b);
AffineTransform2D Inverse(const AffineTransform2D& matrix);
ImVec2 TransformPoint(const AffineTransform2D& matrix, const ImVec2& point);
ImVec2 TransformVector(const AffineTransform2D& matrix, const ImVec2& vector);
AffineTransform2D BuildWorldTransform(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer);
ImVec2 GetWorldCenter(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer);
std::array<ImVec2, 4> ComputeLayerQuadWorld(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer);
FloatRect ComputeLayerBoundsWorld(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer);
AffineTransform2D GetParentWorldTransform(const std::vector<CompositeLayer>& layers, const CompositeLayer& layer);
ImVec2 LayerLocalCenter(const CompositeLayer& layer);
ImVec2 LayerLocalAxisX(const CompositeLayer& layer);
ImVec2 LayerLocalAxisY(const CompositeLayer& layer);
std::array<float, 16> AffineToGlMatrix4(const AffineTransform2D& matrix);
void ApplyAffineToLayerParameters(CompositeLayer& layer, const AffineTransform2D& matrix);
bool WouldCreateParentCycle(
    const std::vector<CompositeLayer>& layers,
    const CompositeLayer& layer,
    const std::string& newParentId);
bool SetLayerParentPreserveWorld(
    std::vector<CompositeLayer>& layers,
    CompositeLayer& layer,
    const std::string& newParentId);
bool IsLayerDescendantOf(
    const std::vector<CompositeLayer>& layers,
    const std::string& layerId,
    const std::string& potentialAncestorId);
void DetachChildrenFromParent(std::vector<CompositeLayer>& layers, const std::string& parentId);
bool IsRectValid(const FloatRect& rect);
float RectAspectRatio(const FloatRect& rect);
FloatRect MakeNormalizedRect(float x1, float y1, float x2, float y2);
FloatRect ComputeViewWorldRect(float canvasWidth, float canvasHeight, float viewZoom, float viewPanX, float viewPanY);
ImVec2 WorldToScreen(const ImVec2& canvasCenter, float viewZoom, float viewPanX, float viewPanY, const ImVec2& worldPoint);
ImVec2 ScreenToWorld(const ImVec2& canvasCenter, float viewZoom, float viewPanX, float viewPanY, const ImVec2& screenPoint);
bool MapWorldToLayerUv(
    const std::vector<CompositeLayer>& layers,
    const CompositeLayer& layer,
    float worldX,
    float worldY,
    float& outU,
    float& outV);
CompositeLayer* FindTopMostVisibleLayerAtWorldPoint(std::vector<CompositeLayer>& layers, float worldX, float worldY);
bool HasMeaningfulPixels(const std::vector<uint8_t>& pixels);
bool EncodePngBytes(const std::vector<uint8_t>& rgbaTopLeft, int width, int height, std::vector<uint8_t>& outPng);
void ResizeNearestRgba(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh, std::vector<uint8_t>& dst);
bool BuildTopLeftPngFromBottomLeftRgba(const std::vector<uint8_t>& rgbaBottomLeft, int width, int height, std::vector<uint8_t>& outPng);
bool LoadCompositeDisplayRgbaImageFromFile(
    const std::filesystem::path& path,
    std::vector<uint8_t>& outPixels,
    int& outWidth,
    int& outHeight);
bool LoadImageLayerDataFromFile(const std::string& path, LayerFileLoadData& outData);
bool LoadEditorProjectLayerDataFromFile(const std::string& path, bool limitResolution, LayerFileLoadData& outData);
void ApplyImportDataToNewLayer(const LayerFileLoadData& data, float canvasW, float canvasH, std::vector<CompositeLayer>& layers, std::string& selectedId);
void ApplyImportDataToExistingLayer(const LayerFileLoadData& data, CompositeLayer& layer);
void QueueImportedExternalAssetMirror(const std::string& path, const LayerFileLoadData& data);
bool BuildLayerSourcePngIfMissing(CompositeLayer& layer);
void ReassignZFromTopOrder(const std::vector<CompositeLayer*>& topToBottomLayers);
bool ComputeAutoBounds(
    const std::vector<CompositeLayer>& allLayers,
    const std::vector<const CompositeLayer*>& layers,
    FloatRect& outBounds);
void RasterizeLayersToTopLeftRgba(
    const std::vector<CompositeLayer>& allLayers,
    const std::vector<const CompositeLayer*>& layers,
    const FloatRect& worldBounds,
    int outWidth,
    int outHeight,
    CompositeExportBackgroundMode backgroundMode,
    const std::array<float, 4>& backgroundColor,
    std::vector<uint8_t>& outRgba);
