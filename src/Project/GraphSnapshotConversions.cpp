#include "Project/GraphSnapshotConversions.h"
#include <algorithm>

namespace Stack::Project::GraphSnapshotInternal {

RenderMaskGeneratorKind ToRenderMaskKind(EditorNodeGraph::MaskGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskGeneratorKind::Solid: return RenderMaskGeneratorKind::Solid;
        case EditorNodeGraph::MaskGeneratorKind::LinearGradient: return RenderMaskGeneratorKind::LinearGradient;
        case EditorNodeGraph::MaskGeneratorKind::RadialGradient: return RenderMaskGeneratorKind::RadialGradient;
        case EditorNodeGraph::MaskGeneratorKind::Noise: return RenderMaskGeneratorKind::Noise;
        case EditorNodeGraph::MaskGeneratorKind::Square: return RenderMaskGeneratorKind::Square;
        case EditorNodeGraph::MaskGeneratorKind::RawGradient: return RenderMaskGeneratorKind::RawGradient;
        case EditorNodeGraph::MaskGeneratorKind::PaintedArea: return RenderMaskGeneratorKind::PaintedArea;
    }
    return RenderMaskGeneratorKind::Solid;
}

RenderMaskSettings ToRenderMaskSettings(const EditorNodeGraph::MaskGeneratorSettings& settings) {
    RenderMaskSettings result;
    result.value = settings.value;
    result.angle = settings.angle;
    result.offset = settings.offset;
    result.scale = settings.scale;
    result.centerX = settings.centerX;
    result.centerY = settings.centerY;
    result.radius = settings.radius;
    result.radiusY = settings.radiusY;
    result.feather = settings.feather;
    result.invert = settings.invert;
    return result;
}

RenderMixBlendMode ToRenderMixBlendMode(EditorNodeGraph::MixBlendMode mode) {
    switch (mode) {
        case EditorNodeGraph::MixBlendMode::Normal: return RenderMixBlendMode::Normal;
        case EditorNodeGraph::MixBlendMode::Average: return RenderMixBlendMode::Average;
        case EditorNodeGraph::MixBlendMode::Add: return RenderMixBlendMode::Add;
        case EditorNodeGraph::MixBlendMode::Multiply: return RenderMixBlendMode::Multiply;
        case EditorNodeGraph::MixBlendMode::Screen: return RenderMixBlendMode::Screen;
        case EditorNodeGraph::MixBlendMode::StraightSourceOver: return RenderMixBlendMode::StraightSourceOver;
        case EditorNodeGraph::MixBlendMode::PremultipliedSourceOver: return RenderMixBlendMode::PremultipliedSourceOver;
    }
    return RenderMixBlendMode::Normal;
}

RenderMaskUtilityKind ToRenderMaskUtilityKind(EditorNodeGraph::MaskUtilityKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskUtilityKind::Invert: return RenderMaskUtilityKind::Invert;
        case EditorNodeGraph::MaskUtilityKind::Levels: return RenderMaskUtilityKind::Levels;
        case EditorNodeGraph::MaskUtilityKind::Threshold: return RenderMaskUtilityKind::Threshold;
    }
    return RenderMaskUtilityKind::Invert;
}

RenderMaskCombineMode ToRenderMaskCombineMode(EditorNodeGraph::MaskCombineMode mode) {
    switch (mode) {
        case EditorNodeGraph::MaskCombineMode::Add: return RenderMaskCombineMode::Add;
        case EditorNodeGraph::MaskCombineMode::Subtract: return RenderMaskCombineMode::Subtract;
        case EditorNodeGraph::MaskCombineMode::Intersect: return RenderMaskCombineMode::Intersect;
        case EditorNodeGraph::MaskCombineMode::Exclude: return RenderMaskCombineMode::Exclude;
    }
    return RenderMaskCombineMode::Intersect;
}

RenderCustomMaskObjectType ToRenderCustomMaskObjectType(EditorNodeGraph::CustomMaskObjectType type) {
    switch (type) {
        case EditorNodeGraph::CustomMaskObjectType::Rectangle: return RenderCustomMaskObjectType::Rectangle;
        case EditorNodeGraph::CustomMaskObjectType::Ellipse: return RenderCustomMaskObjectType::Ellipse;
        case EditorNodeGraph::CustomMaskObjectType::Polygon: return RenderCustomMaskObjectType::Polygon;
        case EditorNodeGraph::CustomMaskObjectType::FreeformPath: return RenderCustomMaskObjectType::FreeformPath;
    }
    return RenderCustomMaskObjectType::Rectangle;
}

RenderCustomMaskOperation ToRenderCustomMaskOperation(EditorNodeGraph::CustomMaskOperation operation) {
    switch (operation) {
        case EditorNodeGraph::CustomMaskOperation::Add: return RenderCustomMaskOperation::Add;
        case EditorNodeGraph::CustomMaskOperation::Subtract: return RenderCustomMaskOperation::Subtract;
        case EditorNodeGraph::CustomMaskOperation::Intersect: return RenderCustomMaskOperation::Intersect;
        case EditorNodeGraph::CustomMaskOperation::Exclude: return RenderCustomMaskOperation::Exclude;
    }
    return RenderCustomMaskOperation::Add;
}

RenderCustomMaskPayload ToRenderCustomMaskPayload(const EditorNodeGraph::CustomMaskPayload& payload) {
    RenderCustomMaskPayload result;
    result.width = std::max(1, payload.width);
    result.height = std::max(1, payload.height);
    result.rasterLayer = payload.rasterLayer;
    result.invert = payload.invert;
    result.blurRadius = payload.blurRadius;
    result.expandContract = payload.expandContract;
    result.objects.reserve(payload.objects.size());
    for (const EditorNodeGraph::CustomMaskObject& object : payload.objects) {
        RenderCustomMaskObject renderObject;
        renderObject.id = object.id;
        renderObject.type = ToRenderCustomMaskObjectType(object.type);
        renderObject.operation = ToRenderCustomMaskOperation(object.operation);
        renderObject.enabled = object.enabled;
        renderObject.invert = object.invert;
        renderObject.strength = object.strength;
        renderObject.feather = object.feather;
        renderObject.blur = object.blur;
        renderObject.points.reserve(object.points.size());
        for (const EditorNodeGraph::Vec2& point : object.points) {
            renderObject.points.push_back(RenderCustomMaskPoint{ point.x, point.y });
        }
        result.objects.push_back(std::move(renderObject));
    }
    return result;
}

RenderImageToMaskKind ToRenderImageToMaskKind(EditorNodeGraph::ImageToMaskKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageToMaskKind::Luminance: return RenderImageToMaskKind::Luminance;
        case EditorNodeGraph::ImageToMaskKind::SampledRange: return RenderImageToMaskKind::SampledRange;
    }
    return RenderImageToMaskKind::Luminance;
}

RenderImageGeneratorKind ToRenderImageGeneratorKind(EditorNodeGraph::ImageGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageGeneratorKind::SolidColor: return RenderImageGeneratorKind::SolidColor;
        case EditorNodeGraph::ImageGeneratorKind::ColorGradient: return RenderImageGeneratorKind::ColorGradient;
        case EditorNodeGraph::ImageGeneratorKind::Square: return RenderImageGeneratorKind::Square;
        case EditorNodeGraph::ImageGeneratorKind::Circle: return RenderImageGeneratorKind::Circle;
        case EditorNodeGraph::ImageGeneratorKind::Text: return RenderImageGeneratorKind::Text;
    }
    return RenderImageGeneratorKind::SolidColor;
}

RenderMaskUtilitySettings ToRenderMaskUtilitySettings(const EditorNodeGraph::MaskUtilitySettings& settings) {
    RenderMaskUtilitySettings result;
    result.blackPoint = settings.blackPoint;
    result.whitePoint = settings.whitePoint;
    result.gamma = settings.gamma;
    result.threshold = settings.threshold;
    result.softness = settings.softness;
    result.enabled = settings.enabled;
    result.invert = settings.invert;
    return result;
}

RenderImageToMaskSettings ToRenderImageToMaskSettings(const EditorNodeGraph::ImageToMaskSettings& settings) {
    RenderImageToMaskSettings result;
    result.low = settings.low;
    result.high = settings.high;
    result.softness = settings.softness;
    result.invert = settings.invert;
    result.sampleCount = std::clamp(settings.sampleCount, 1, 5);
    result.sampleRgb[0] = settings.sampleRgb[0];
    result.sampleRgb[1] = settings.sampleRgb[1];
    result.sampleRgb[2] = settings.sampleRgb[2];
    result.sampleLuma = settings.sampleLuma;
    for (int i = 0; i < 4; ++i) {
        result.extraSampleRgb[i][0] = settings.extraSampleRgb[i][0];
        result.extraSampleRgb[i][1] = settings.extraSampleRgb[i][1];
        result.extraSampleRgb[i][2] = settings.extraSampleRgb[i][2];
        result.extraSampleLuma[i] = settings.extraSampleLuma[i];
    }
    result.sampleU = settings.sampleU;
    result.sampleV = settings.sampleV;
    result.toneSimilarity = settings.toneSimilarity;
    result.colorSimilarity = settings.colorSimilarity;
    result.regionRadius = settings.regionRadius;
    result.regionFeather = settings.regionFeather;
    result.edgeSensitivity = settings.edgeSensitivity;
    result.localCoherence = settings.localCoherence;
    return result;
}

RenderImageGeneratorSettings ToRenderImageGeneratorSettings(const EditorNodeGraph::ImageGeneratorSettings& settings) {
    RenderImageGeneratorSettings result;
    for (int i = 0; i < 4; ++i) {
        result.colorA[i] = settings.colorA[i];
        result.colorB[i] = settings.colorB[i];
    }
    result.angle = settings.angle;
    result.offset = settings.offset;
    result.text = settings.text;
    result.fontSize = settings.fontSize;
    result.textBackdropBlur = settings.textBackdropBlur;
    result.textBackdropOpacity = settings.textBackdropOpacity;
    result.textBackdropPadding = settings.textBackdropPadding;
    return result;
}

RenderFrequencyResponseSettings ToRenderFrequencyResponseSettings(
    const EditorNodeGraph::FrequencyResponseSettings& settings) {
    RenderFrequencyResponseSettings result;
    result.mode = static_cast<RenderFrequencyFilterMode>(settings.mode);
    result.profile = static_cast<RenderFrequencyTransitionProfile>(settings.profile);
    result.lowCutoff = settings.lowCutoff;
    result.highCutoff = settings.highCutoff;
    result.transitionWidth = settings.transitionWidth;
    result.butterworthOrder = settings.butterworthOrder;
    result.notches.reserve(settings.notches.size());
    for (const EditorNodeGraph::FrequencyNotch& notch : settings.notches) {
        result.notches.push_back({
            notch.id,
            notch.frequency,
            notch.directionDegrees,
            notch.width
        });
    }
    return result;
}

} // namespace Stack::Project::GraphSnapshotInternal
