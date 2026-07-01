#pragma once

#include "Color/LutData.h"
#include "Develop/DevelopTypes.h"
#include "Editor/NodeGraph/NodeGraphTypes.h"
#include "MFSR/MFSRTypes.h"
#include "NeuralDenoise/NeuralDenoiseTypes.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawImageData.h"

#include <memory>
#include <string>
#include <vector>

namespace EditorNodeGraph {

struct ImagePayload {
    std::string label;
    std::string sourcePath;
    std::vector<unsigned char> pngBytes;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    int originalChannels = 4;
    mutable std::shared_ptr<const std::vector<unsigned char>> sharedPixels;
    mutable std::size_t pixelsFingerprint = 0;
};

inline void InvalidateImagePayloadRuntime(ImagePayload& payload) {
    payload.sharedPixels.reset();
    payload.pixelsFingerprint = 0;
}

struct RawSourcePayload {
    std::string label;
    std::string sourcePath;
    Raw::RawMetadata metadata;
};

struct RawDevelopmentPayload {
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    std::string projectStatus = "Unknown";
    bool edited = false;
    bool autosaved = false;
};

using LutPayload = ColorLut::LutPayload;

using RawDevelopUiMode = Stack::Develop::RawDevelopUiMode;
using DevelopAutoIntent = Stack::Develop::AutoIntent;
using DevelopAutoGuidance = Stack::Develop::AutoGuidance;
using DevelopSubjectImportanceMode = Stack::Develop::SubjectImportanceMode;
using DevelopSubjectImportanceRegion = Stack::Develop::SubjectImportanceRegion;
using DevelopSubjectImportanceStrokePoint = Stack::Develop::SubjectImportanceStrokePoint;
using DevelopSubjectImportanceStroke = Stack::Develop::SubjectImportanceStroke;
using DevelopSubjectImportanceMap = Stack::Develop::SubjectImportanceMap;
using RawDevelopPayload = Stack::Develop::RawDevelopPayload;

inline const char* DevelopAutoIntentStableString(DevelopAutoIntent intent) {
    return Stack::Develop::AutoIntentStableString(intent);
}

inline const char* DevelopAutoIntentLabel(DevelopAutoIntent intent) {
    return Stack::Develop::AutoIntentLabel(intent);
}

inline const char* DevelopAutoIntentDescription(DevelopAutoIntent intent) {
    return Stack::Develop::AutoIntentDescription(intent);
}

inline DevelopAutoIntent DevelopAutoIntentFromStableString(const std::string& value) {
    return Stack::Develop::AutoIntentFromStableString(value);
}

inline const char* DevelopSubjectImportanceModeStableString(DevelopSubjectImportanceMode mode) {
    return Stack::Develop::SubjectImportanceModeStableString(mode);
}

inline const char* DevelopSubjectImportanceModeLabel(DevelopSubjectImportanceMode mode) {
    return Stack::Develop::SubjectImportanceModeLabel(mode);
}

inline const char* DevelopSubjectImportanceModeDescription(DevelopSubjectImportanceMode mode) {
    return Stack::Develop::SubjectImportanceModeDescription(mode);
}

inline DevelopSubjectImportanceMode DevelopSubjectImportanceModeFromStableString(const std::string& value) {
    return Stack::Develop::SubjectImportanceModeFromStableString(value);
}

struct RawDecodePayload {
    Raw::RawDevelopSettings settings;
};

struct RawNeuralDenoisePayload {
    NeuralDenoise::NeuralDenoiseSettings settings;
};

struct RawDetailFusionPayload {
    Raw::RawDetailFusionSettings settings;
};

struct RawDetailAutoMaskPayload {
    Raw::RawDetailFusionSettings settings;
};

struct HdrMergePayload {
    Raw::HdrMergeSettings settings;
};

inline constexpr const char* kMfsrPhase2PlaceholderStatus =
    "Phase 2 placeholder: output passes through Reference; no MFSR reconstruction yet.";

struct MfsrPayload {
    Stack::Mfsr::MfsrSettings settings;
    Stack::Mfsr::MfsrDiagnosticsSummary diagnostics;
    Stack::Mfsr::MfsrCacheKey cacheKey;
    bool hasPlaceholderCachedOutput = false;
    std::string placeholderStatus = kMfsrPhase2PlaceholderStatus;
    std::string errorMessage;
};

struct MaskGeneratorSettings {
    float value = 1.0f;
    float angle = 0.0f;
    float offset = 0.0f;
    float scale = 1.0f;
    float centerX = 0.5f;
    float centerY = 0.5f;
    float radius = 0.45f;
    float feather = 0.2f;
    bool invert = false;
};

struct MaskUtilitySettings {
    float blackPoint = 0.0f;
    float whitePoint = 1.0f;
    float gamma = 1.0f;
    float threshold = 0.5f;
    float softness = 0.0f;
    bool invert = false;
};

struct CustomMaskObject {
    int id = 0;
    CustomMaskObjectType type = CustomMaskObjectType::Rectangle;
    CustomMaskOperation operation = CustomMaskOperation::Add;
    std::vector<Vec2> points;
    bool enabled = true;
    bool invert = false;
    float strength = 1.0f;
    float feather = 0.0f;
    float blur = 0.0f;
};

struct CustomMaskPayload {
    int schemaVersion = 1;
    CustomMaskReferenceMode referenceMode = CustomMaskReferenceMode::CustomSize;
    int referenceNodeId = -1;
    std::string referenceSocketId;
    int width = 1024;
    int height = 1024;
    bool aspectLocked = true;
    std::vector<float> rasterLayer;
    std::vector<CustomMaskObject> objects;
    int nextObjectId = 1;
    bool invert = false;
    float blurRadius = 0.0f;
    float expandContract = 0.0f;
    CustomMaskTool activeTool = CustomMaskTool::Brush;
    float brushSize = 48.0f;
    float brushSoftness = 0.45f;
    float brushOpacity = 1.0f;
    bool showCanvasReferenceImage = true;
    bool showCanvasMaskImpact = true;
    bool showCanvasMaskStrength = true;
    int selectedObjectId = -1;
};

struct ImageToMaskSettings {
    float low = 0.0f;
    float high = 1.0f;
    float softness = 0.0f;
    bool invert = false;
    int sampleCount = 1;
    float sampleRgb[3] = { 0.5f, 0.5f, 0.5f };
    float sampleLuma = 0.5f;
    float extraSampleRgb[4][3] = {
        { 0.5f, 0.5f, 0.5f },
        { 0.5f, 0.5f, 0.5f },
        { 0.5f, 0.5f, 0.5f },
        { 0.5f, 0.5f, 0.5f }
    };
    float extraSampleLuma[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
    float sampleU = 0.5f;
    float sampleV = 0.5f;
    float toneSimilarity = 0.12f;
    float colorSimilarity = 0.18f;
    float regionRadius = 0.35f;
    float regionFeather = 0.35f;
    float edgeSensitivity = 0.45f;
    float localCoherence = 0.45f;
};

struct ImageGeneratorSettings {
    float colorA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float colorB[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float angle = 0.0f;
    float offset = 0.0f;
    std::string text = "Text";
    float fontSize = 96.0f;
    float textBackdropBlur = 0.0f;
    float textBackdropOpacity = 0.0f;
    float textBackdropPadding = 12.0f;
};

struct DataMathSettings {
    float constantA = 0.0f;
    float constantB = 1.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float outMin = 0.0f;
    float outMax = 1.0f;
};

} // namespace EditorNodeGraph
