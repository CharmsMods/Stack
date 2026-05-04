#include "RenderTab.h"

#include "Async/TaskSystem.h"
#include "Async/TaskState.h"
#include "Library/LibraryManager.h"
#include "RenderTab/Foundation/RenderFoundationPreview.h"
#include "RenderTab/Foundation/RenderFoundationSerialization.h"
#include "RenderTab/Contracts/RenderContracts.h"
#include "RenderTab/Runtime/Geometry/RenderSceneGeometry.h"
#include "RenderTab/Runtime/Debug/ValidationScenes.h"
#include "ThirdParty/stb_image_write.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

namespace {

using namespace RenderFoundation;

constexpr float kSplitterWidth = 6.0f;
constexpr int kInteractivePathTraceMaxWidth = 1280;
constexpr int kInteractivePathTraceMaxHeight = 720;

const char* ViewModeLabel(ViewMode mode) {
    switch (mode) {
        case ViewMode::Unlit:
            return "Unlit";
        case ViewMode::PathTrace:
            return "Path Trace";
    }
    return "Unknown";
}

const char* PathTraceTransportModeLabel(PathTraceTransportMode mode) {
    switch (mode) {
        case PathTraceTransportMode::Preview:
            return "Preview";
        case PathTraceTransportMode::CausticsReference:
            return "Caustics Reference";
    }
    return "Unknown";
}

const char* PathTraceTerminationModeLabel(PathTraceTerminationMode mode) {
    switch (mode) {
        case PathTraceTerminationMode::BruteForce:
            return "Brute Force";
        case PathTraceTerminationMode::Optimized:
            return "Optimized";
        case PathTraceTerminationMode::Smart:
            return "Smart";
    }
    return "Unknown";
}

const char* DenoiserModeLabel(DenoiserMode mode) {
    switch (mode) {
        case DenoiserMode::Off:
            return "Off";
        case DenoiserMode::Bilateral:
            return "Bilateral";
        case DenoiserMode::ATrous:
            return "A-Trous";
    }
    return "Unknown";
}

const char* DenoiserDebugViewLabel(DenoiserDebugView view) {
    switch (view) {
        case DenoiserDebugView::FinalOutput:
            return "Final Output";
        case DenoiserDebugView::NoisyBeauty:
            return "Noisy Beauty";
        case DenoiserDebugView::DenoisedBeauty:
            return "Denoised Beauty";
        case DenoiserDebugView::CurrentSample:
            return "Current Sample";
        case DenoiserDebugView::GuideAlbedo:
            return "Guide Albedo";
        case DenoiserDebugView::GuideNormal:
            return "Guide Normal";
        case DenoiserDebugView::GuideDepth:
            return "Guide Depth";
        case DenoiserDebugView::VarianceHeatmap:
            return "Variance";
        case DenoiserDebugView::DifferenceHeatmap:
            return "Difference Heatmap";
    }
    return "Unknown";
}

const char* DenoiserExportModeLabel(DenoiserExportMode mode) {
    switch (mode) {
        case DenoiserExportMode::Noisy:
            return "Noisy";
        case DenoiserExportMode::Denoised:
            return "Denoised";
    }
    return "Unknown";
}

const char* ViewportPerformanceModeLabel(ViewportPerformanceMode mode) {
    switch (mode) {
        case ViewportPerformanceMode::Auto:
            return "Auto";
        case ViewportPerformanceMode::Exact:
            return "Exact";
        case ViewportPerformanceMode::Proxy:
            return "Proxy";
    }
    return "Unknown";
}

const char* ImportedModelScaleModeLabel(ImportedModelScaleMode mode) {
    switch (mode) {
        case ImportedModelScaleMode::KeepOriginal:
            return "Keep Original Scale";
        case ImportedModelScaleMode::AutoFit:
            return "Auto Fit Scene";
    }
    return "Unknown";
}

const RenderValidationSceneOption* FindValidationSceneOption(RenderValidationSceneId id) {
    const auto& options = GetRenderValidationSceneOptions();
    for (const RenderValidationSceneOption& option : options) {
        if (option.id == id) {
            return &option;
        }
    }
    return nullptr;
}

const char* PathTraceDebugModeLabel(PathTraceDebugMode mode) {
    switch (mode) {
        case PathTraceDebugMode::SelectedRayLog:
            return "Selected Ray Log";
        case PathTraceDebugMode::RefractedSourceClass:
            return "Refracted Source";
        case PathTraceDebugMode::SelfHitHeatmap:
            return "Self-Hit Heatmap";
        case PathTraceDebugMode::None:
        default:
            return "None";
    }
}

const char* PathTraceDebugDecisionLabel(int decision) {
    switch (decision) {
        case 1:
            return "Reflect";
        case 2:
            return "Refract";
        case 3:
            return "Thin-Sheet Pass";
        case 4:
            return "TIR Reflect";
        case 5:
            return "Fog Scatter";
        default:
            return "None";
    }
}

const char* TransformModeLabel(TransformMode mode) {
    switch (mode) {
        case TransformMode::Translate:
            return "Translate";
        case TransformMode::Rotate:
            return "Rotate";
        case TransformMode::Scale:
            return "Scale";
    }
    return "Unknown";
}

const char* TransformSpaceLabel(TransformSpace space) {
    switch (space) {
        case TransformSpace::Local:
            return "Local";
        case TransformSpace::World:
            return "World";
    }
    return "Unknown";
}

bool DenoiserUsesFilteredOutput(const DenoiserSettings& denoiser) {
    return denoiser.enabled && denoiser.mode != DenoiserMode::Off;
}

bool ClampInteractivePathTraceResolution(int maxWidth, int maxHeight, int& width, int& height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width <= maxWidth && height <= maxHeight) {
        return false;
    }

    const float widthScale = static_cast<float>(maxWidth) / static_cast<float>(width);
    const float heightScale = static_cast<float>(maxHeight) / static_cast<float>(height);
    const float scale = std::min(widthScale, heightScale);
    width = std::max(1, static_cast<int>(std::round(static_cast<float>(width) * scale)));
    height = std::max(1, static_cast<int>(std::round(static_cast<float>(height) * scale)));
    return true;
}

float GaussianCpu(float value, float mean, float sigma) {
    const float delta = (value - mean) / std::max(sigma, 0.001f);
    return std::exp(-0.5f * delta * delta);
}

Vec3 WavelengthToDisplayRgbCpu(float wavelengthNm) {
    Vec3 rgb {
        GaussianCpu(wavelengthNm, 610.0f, 48.0f) + 0.35f * GaussianCpu(wavelengthNm, 700.0f, 35.0f),
        GaussianCpu(wavelengthNm, 545.0f, 38.0f),
        GaussianCpu(wavelengthNm, 450.0f, 32.0f) + 0.18f * GaussianCpu(wavelengthNm, 500.0f, 45.0f)
    };
    const float peak = std::max({ rgb.x, rgb.y, rgb.z, 0.0001f });
    return Clamp01(rgb / peak);
}

Vec3 GetLaserDisplayColor(float wavelengthNm) {
    return WavelengthToDisplayRgbCpu(std::clamp(wavelengthNm, 380.0f, 720.0f));
}

Vec3 GetLightDisplayColor(const Light& light) {
    return light.type == LightType::Laser
        ? GetLaserDisplayColor(light.laserWavelengthNm)
        : Clamp01(light.color);
}

Vec3 GetValidationLightDisplayColor(const RenderLight& light) {
    return light.type == RenderLightType::Laser
        ? GetLaserDisplayColor(light.laserWavelengthNm)
        : Clamp01({ light.color.x, light.color.y, light.color.z });
}

bool SelectionSupportsScale(SelectionType selectionType) {
    return selectionType == SelectionType::Primitive;
}

const char* TransformShortcutHint(SelectionType selectionType) {
    return SelectionSupportsScale(selectionType) ? "W/E/R" : "W/E";
}

ImRect FitRenderTargetRect(const ImRect& outerRect, int renderWidth, int renderHeight) {
    const float outerWidth = std::max(outerRect.GetWidth(), 1.0f);
    const float outerHeight = std::max(outerRect.GetHeight(), 1.0f);
    const float targetAspect =
        static_cast<float>(std::max(renderWidth, 1)) / static_cast<float>(std::max(renderHeight, 1));
    const float outerAspect = outerWidth / outerHeight;

    float fittedWidth = outerWidth;
    float fittedHeight = outerHeight;
    if (targetAspect > outerAspect) {
        fittedHeight = fittedWidth / std::max(targetAspect, 0.0001f);
    } else {
        fittedWidth = fittedHeight * targetAspect;
    }

    const float offsetX = (outerWidth - fittedWidth) * 0.5f;
    const float offsetY = (outerHeight - fittedHeight) * 0.5f;
    return ImRect(
        ImVec2(outerRect.Min.x + offsetX, outerRect.Min.y + offsetY),
        ImVec2(outerRect.Min.x + offsetX + fittedWidth, outerRect.Min.y + offsetY + fittedHeight));
}

RenderBounds ToRenderBounds(const Vec3& minValue, const Vec3& maxValue) {
    return RenderBounds {
        MakeRenderFloat3(minValue.x, minValue.y, minValue.z),
        MakeRenderFloat3(maxValue.x, maxValue.y, maxValue.z)
    };
}

Vec3 TransformPointForBounds(const Transform& transform, const Vec3& localPoint) {
    const auto rotateAroundX = [](const Vec3& value, float radians) {
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        return Vec3 {
            value.x,
            value.y * cosine - value.z * sine,
            value.y * sine + value.z * cosine
        };
    };
    const auto rotateAroundY = [](const Vec3& value, float radians) {
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        return Vec3 {
            value.x * cosine + value.z * sine,
            value.y,
            -value.x * sine + value.z * cosine
        };
    };
    const auto rotateAroundZ = [](const Vec3& value, float radians) {
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        return Vec3 {
            value.x * cosine - value.y * sine,
            value.x * sine + value.y * cosine,
            value.z
        };
    };
    Vec3 point {
        localPoint.x * transform.scale.x,
        localPoint.y * transform.scale.y,
        localPoint.z * transform.scale.z
    };
    point = rotateAroundX(point, DegreesToRadians(transform.rotationDegrees.x));
    point = rotateAroundY(point, DegreesToRadians(transform.rotationDegrees.y));
    point = rotateAroundZ(point, DegreesToRadians(transform.rotationDegrees.z));
    return point + transform.translation;
}

RenderBounds TransformBounds(const RenderBounds& localBounds, const Transform& transform) {
    const std::array<Vec3, 8> corners = {
        Vec3 { localBounds.min.x, localBounds.min.y, localBounds.min.z },
        Vec3 { localBounds.min.x, localBounds.min.y, localBounds.max.z },
        Vec3 { localBounds.min.x, localBounds.max.y, localBounds.min.z },
        Vec3 { localBounds.min.x, localBounds.max.y, localBounds.max.z },
        Vec3 { localBounds.max.x, localBounds.min.y, localBounds.min.z },
        Vec3 { localBounds.max.x, localBounds.min.y, localBounds.max.z },
        Vec3 { localBounds.max.x, localBounds.max.y, localBounds.min.z },
        Vec3 { localBounds.max.x, localBounds.max.y, localBounds.max.z }
    };

    Vec3 minValue = TransformPointForBounds(transform, corners[0]);
    Vec3 maxValue = minValue;
    for (std::size_t index = 1; index < corners.size(); ++index) {
        const Vec3 point = TransformPointForBounds(transform, corners[index]);
        minValue.x = std::min(minValue.x, point.x);
        minValue.y = std::min(minValue.y, point.y);
        minValue.z = std::min(minValue.z, point.z);
        maxValue.x = std::max(maxValue.x, point.x);
        maxValue.y = std::max(maxValue.y, point.y);
        maxValue.z = std::max(maxValue.z, point.z);
    }
    return ToRenderBounds(minValue, maxValue);
}

void DrawReadOnlyColorSwatch(const char* label, const Vec3& color, const char* valueText = nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    const std::string buttonId = std::string("##") + label;
    ImGui::ColorButton(
        buttonId.c_str(),
        ImVec4(Clamp01(color.x), Clamp01(color.y), Clamp01(color.z), 1.0f),
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
        ImVec2(44.0f, ImGui::GetFrameHeight()));
    if (valueText != nullptr && valueText[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", valueText);
    }
}

Vec3 KelvinToRgbCpu(float kelvin) {
    const float temperature = std::clamp(kelvin, 1000.0f, 40000.0f) * 0.01f;
    float red = 255.0f;
    float green = 255.0f;
    float blue = 255.0f;

    if (temperature <= 66.0f) {
        red = 255.0f;
        green = 99.4708025861f * std::log(std::max(temperature, 0.0001f)) - 161.1195681661f;
        blue = temperature <= 19.0f
            ? 0.0f
            : 138.5177312231f * std::log(std::max(temperature - 10.0f, 0.0001f)) - 305.0447927307f;
    } else {
        red = 329.698727446f * std::pow(temperature - 60.0f, -0.1332047592f);
        green = 288.1221695283f * std::pow(temperature - 60.0f, -0.0755148492f);
        blue = 255.0f;
    }

    return Clamp01({ red / 255.0f, green / 255.0f, blue / 255.0f });
}

Vec3 WhiteBalanceGainCpu(float kelvin) {
    const Vec3 neutral = KelvinToRgbCpu(6500.0f);
    const Vec3 target = KelvinToRgbCpu(kelvin);
    Vec3 gain {
        neutral.x / std::max(target.x, 0.0001f),
        neutral.y / std::max(target.y, 0.0001f),
        neutral.z / std::max(target.z, 0.0001f)
    };
    const float average = std::max((gain.x + gain.y + gain.z) / 3.0f, 0.0001f);
    gain.x /= average;
    gain.y /= average;
    gain.z /= average;
    return gain;
}

Vec3 TonemapAcesCpu(const Vec3& color) {
    auto toneMapChannel = [](float value) {
        const float numerator = value * (2.51f * value + 0.03f);
        const float denominator = value * (2.43f * value + 0.59f) + 0.14f;
        return std::clamp(numerator / std::max(denominator, 0.0001f), 0.0f, 1.0f);
    };
    return {
        toneMapChannel(std::max(color.x, 0.0f)),
        toneMapChannel(std::max(color.y, 0.0f)),
        toneMapChannel(std::max(color.z, 0.0f))
    };
}

unsigned char FloatToByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<unsigned char>(std::round(clamped * 255.0f));
}

void ConvertLinearHdrToPngPixels(
    const std::vector<float>& linearPixels,
    int width,
    int height,
    const Camera& camera,
    std::vector<unsigned char>& outPixels) {

    outPixels.clear();
    if (width <= 0 || height <= 0 || linearPixels.size() < static_cast<std::size_t>(width * height * 4)) {
        return;
    }

    outPixels.resize(static_cast<std::size_t>(width * height * 4), 0);
    const Vec3 whiteBalanceGain = WhiteBalanceGainCpu(camera.whiteBalanceTemperature);
    for (int pixelIndex = 0; pixelIndex < width * height; ++pixelIndex) {
        const std::size_t sourceOffset = static_cast<std::size_t>(pixelIndex) * 4;
        Vec3 color {
            std::max(linearPixels[sourceOffset + 0], 0.0f) * whiteBalanceGain.x * camera.exposure,
            std::max(linearPixels[sourceOffset + 1], 0.0f) * whiteBalanceGain.y * camera.exposure,
            std::max(linearPixels[sourceOffset + 2], 0.0f) * whiteBalanceGain.z * camera.exposure
        };
        color = TonemapAcesCpu(color);
        const std::size_t targetOffset = static_cast<std::size_t>(pixelIndex) * 4;
        outPixels[targetOffset + 0] = FloatToByte(color.x);
        outPixels[targetOffset + 1] = FloatToByte(color.y);
        outPixels[targetOffset + 2] = FloatToByte(color.z);
        outPixels[targetOffset + 3] = 255;
    }
}

const char* PrimitiveTypeLabel(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Sphere:
            return "Sphere";
        case PrimitiveType::Cube:
            return "Cube";
        case PrimitiveType::Plane:
            return "Plane";
        case PrimitiveType::ImportedMesh:
            return "Imported Mesh";
    }
    return "Primitive";
}

const char* LightTypeLabel(LightType type) {
    switch (type) {
        case LightType::Point:
            return "Point";
        case LightType::Spot:
            return "Spot";
        case LightType::Area:
            return "Area";
        case LightType::Directional:
            return "Directional";
        case LightType::Laser:
            return "Laser";
    }
    return "Light";
}

const char* BaseMaterialLabel(BaseMaterial material) {
    switch (material) {
        case BaseMaterial::Diffuse:
            return "Diffuse";
        case BaseMaterial::Metal:
            return "Metal";
        case BaseMaterial::Glass:
            return "Glass";
        case BaseMaterial::Emissive:
            return "Emissive";
    }
    return "Material";
}

int BaseLayerComboIndex(const Material::Layer& layer) {
    switch (layer.type) {
        case MaterialLayerType::BaseMetal:
            return 1;
        case MaterialLayerType::BaseDielectric:
            return 2;
        case MaterialLayerType::BaseDiffuse:
        case MaterialLayerType::ClearCoat:
        default:
            return 0;
    }
}

void ApplyBaseLayerType(Material::Layer& layer, int comboIndex) {
    layer.color = Clamp01(layer.color);
    layer.weight = 1.0f;
    layer.roughness = Clamp01(layer.roughness);
    layer.ior = std::max(layer.ior, 1.0f);

    switch (comboIndex) {
        case 1:
            layer.type = MaterialLayerType::BaseMetal;
            layer.metallic = 1.0f;
            layer.transmission = 0.0f;
            layer.transmissionRoughness = 0.0f;
            layer.thinWalled = false;
            break;
        case 2:
            layer.type = MaterialLayerType::BaseDielectric;
            layer.metallic = 0.0f;
            layer.transmission = std::max(layer.transmission, 0.85f);
            layer.ior = std::max(layer.ior, 1.0f);
            break;
        case 0:
        default:
            layer.type = MaterialLayerType::BaseDiffuse;
            layer.metallic = 0.0f;
            layer.transmission = 0.0f;
            layer.transmissionRoughness = 0.0f;
            layer.thinWalled = false;
            break;
    }
}

const char* BaseSurfaceComboItems() {
    return "No Material / Matte\0Metal\0Glass\0";
}

bool MaterialHasClearCoatLayer(const Material& material) {
    return FindMaterialLayer(material, MaterialLayerType::ClearCoat) != nullptr;
}

ImVec4 ToImColor(const Vec3& value, float alpha = 1.0f) {
    return ImVec4(Clamp01(value.x), Clamp01(value.y), Clamp01(value.z), alpha);
}

ImVec2 OffsetPoint(const ImVec2& point, const ImVec2& offset) {
    return ImVec2(point.x + offset.x, point.y + offset.y);
}

bool EditStringField(const char* label, std::string& value) {
    std::array<char, 256> buffer {};
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

bool EditVec3Field(const char* label, Vec3& value, float speed = 0.05f) {
    float components[3] = { value.x, value.y, value.z };
    if (!ImGui::DragFloat3(label, components, speed)) {
        return false;
    }
    value = { components[0], components[1], components[2] };
    return true;
}

bool EditColor3Field(const char* label, Vec3& value) {
    float components[3] = { value.x, value.y, value.z };
    if (!ImGui::ColorEdit3(label, components)) {
        return false;
    }
    value = { components[0], components[1], components[2] };
    return true;
}

bool EditColor3Field(const char* label, RenderFloat3& value) {
    float components[3] = { value.x, value.y, value.z };
    if (!ImGui::ColorEdit3(label, components)) {
        return false;
    }
    value = MakeRenderFloat3(components[0], components[1], components[2]);
    return true;
}

float DistanceSquared(const ImVec2& a, const ImVec2& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool HitTestElement(const ProjectedElement& element, const ImVec2& point) {
    const float dx = std::abs(point.x - element.center.x);
    const float dy = std::abs(point.y - element.center.y);

    switch (element.glyph) {
        case PreviewGlyph::Sphere: {
            const float radius = std::max(element.halfSize.x, element.halfSize.y);
            return (dx * dx + dy * dy) <= (radius * radius);
        }
        case PreviewGlyph::LightPoint:
        case PreviewGlyph::LightSpot:
        case PreviewGlyph::LightDirectional:
        case PreviewGlyph::LightLaser:
            return dx + dy <= std::max(element.halfSize.x, element.halfSize.y);
        case PreviewGlyph::Cube:
        case PreviewGlyph::Plane:
        case PreviewGlyph::LightArea:
            return dx <= element.halfSize.x && dy <= element.halfSize.y;
    }

    return false;
}

void DrawViewportBackground(ImDrawList* drawList, const ImRect& rect, const State& state) {
    Vec3 topColor { 0.11f, 0.14f, 0.18f };
    Vec3 bottomColor { 0.04f, 0.05f, 0.07f };
    if (state.GetSettings().viewMode == ViewMode::PathTrace) {
        topColor = topColor + Vec3 { 0.03f, 0.02f, 0.015f };
    }
    topColor = topColor + state.GetSceneMetadata().fogColor * (0.15f * state.GetSceneMetadata().environmentIntensity);
    topColor = Clamp01(topColor);
    bottomColor = Clamp01(bottomColor);

    drawList->AddRectFilledMultiColor(
        rect.Min,
        rect.Max,
        ImGui::ColorConvertFloat4ToU32(ToImColor(topColor)),
        ImGui::ColorConvertFloat4ToU32(ToImColor(topColor)),
        ImGui::ColorConvertFloat4ToU32(ToImColor(bottomColor)),
        ImGui::ColorConvertFloat4ToU32(ToImColor(bottomColor)));

    const float gridSpacing = 48.0f;
    const ImU32 gridColor = IM_COL32(255, 255, 255, 18);
    for (float x = rect.Min.x; x <= rect.Max.x; x += gridSpacing) {
        drawList->AddLine(ImVec2(x, rect.Min.y), ImVec2(x, rect.Max.y), gridColor, 1.0f);
    }
    for (float y = rect.Min.y; y <= rect.Max.y; y += gridSpacing) {
        drawList->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), gridColor, 1.0f);
    }
}

void DrawProjectedElement(ImDrawList* drawList, const ImVec2& origin, const ProjectedElement& element) {
    const ImVec2 center(origin.x + element.center.x, origin.y + element.center.y);
    const ImVec4 color = ToImColor(element.color, 1.0f);
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(color);
    const ImU32 outline = element.selected ? IM_COL32(255, 238, 164, 255) : IM_COL32(255, 255, 255, 110);
    const ImU32 shadow = IM_COL32(0, 0, 0, 60);
    const ImVec2 shadowOffset(4.0f, 5.0f);

    switch (element.glyph) {
        case PreviewGlyph::Sphere: {
            const float radius = std::max(element.halfSize.x, element.halfSize.y);
            drawList->AddCircleFilled(OffsetPoint(center, shadowOffset), radius + 1.0f, shadow, 24);
            drawList->AddCircleFilled(center, radius, fill, 32);
            drawList->AddCircle(center, radius, outline, 32, element.selected ? 2.5f : 1.25f);
            break;
        }
        case PreviewGlyph::Cube: {
            const ImVec2 min(center.x - element.halfSize.x, center.y - element.halfSize.y);
            const ImVec2 max(center.x + element.halfSize.x, center.y + element.halfSize.y);
            drawList->AddRectFilled(OffsetPoint(min, shadowOffset), OffsetPoint(max, shadowOffset), shadow, 6.0f);
            drawList->AddRectFilled(min, max, fill, 6.0f);
            drawList->AddRect(min, max, outline, 6.0f, 0, element.selected ? 2.0f : 1.0f);
            break;
        }
        case PreviewGlyph::Plane:
        case PreviewGlyph::LightArea: {
            const ImVec2 min(center.x - element.halfSize.x, center.y - element.halfSize.y);
            const ImVec2 max(center.x + element.halfSize.x, center.y + element.halfSize.y);
            drawList->AddRectFilled(OffsetPoint(min, shadowOffset), OffsetPoint(max, shadowOffset), shadow, 4.0f);
            drawList->AddRectFilled(min, max, fill, 4.0f);
            drawList->AddRect(min, max, outline, 4.0f, 0, element.selected ? 2.0f : 1.0f);
            break;
        }
        case PreviewGlyph::LightPoint:
        case PreviewGlyph::LightSpot:
        case PreviewGlyph::LightDirectional: {
            const float radius = std::max(element.halfSize.x, element.halfSize.y);
            const ImVec2 top(center.x, center.y - radius);
            const ImVec2 right(center.x + radius, center.y);
            const ImVec2 bottom(center.x, center.y + radius);
            const ImVec2 left(center.x - radius, center.y);
            drawList->AddQuadFilled(
                OffsetPoint(top, shadowOffset),
                OffsetPoint(right, shadowOffset),
                OffsetPoint(bottom, shadowOffset),
                OffsetPoint(left, shadowOffset),
                shadow);
            drawList->AddQuadFilled(top, right, bottom, left, fill);
            drawList->AddQuad(top, right, bottom, left, outline, element.selected ? 2.0f : 1.0f);
            break;
        }
    }

    if (element.selected) {
        drawList->AddText(
            ImVec2(center.x + element.halfSize.x + 10.0f, center.y - element.halfSize.y - 4.0f),
            IM_COL32(255, 244, 186, 255),
            element.label.c_str());
    }
}

bool ProjectPointToViewport(
    const RenderFoundation::Camera& viewCamera,
    const ImRect& rect,
    const RenderFoundation::Vec3& worldPoint,
    ImVec2& outScreenPoint,
    float* outDepth = nullptr) {

    const float width = rect.GetWidth();
    const float height = rect.GetHeight();
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    const RenderFoundation::Vec3 forward = ForwardFromYawPitch(viewCamera.yawDegrees, viewCamera.pitchDegrees);
    const RenderFoundation::Vec3 right = RightFromForward(forward);
    const RenderFoundation::Vec3 up = UpFromBasis(right, forward);
    const RenderFoundation::Vec3 relative = worldPoint - viewCamera.position;
    const float cameraX = Dot(relative, right);
    const float cameraY = Dot(relative, up);
    const float cameraZ = Dot(relative, forward);
    if (outDepth != nullptr) {
        *outDepth = cameraZ;
    }
    if (cameraZ <= 0.001f) {
        return false;
    }

    const float aspect = width / height;
    const float tanHalfFov = std::tan(DegreesToRadians(viewCamera.fieldOfViewDegrees) * 0.5f);
    const float ndcX = cameraX / (cameraZ * tanHalfFov * aspect);
    const float ndcY = cameraY / (cameraZ * tanHalfFov);
    if (std::fabs(ndcX) > 1.5f || std::fabs(ndcY) > 1.5f) {
        return false;
    }

    outScreenPoint.x = rect.Min.x + (ndcX * 0.5f + 0.5f) * width;
    outScreenPoint.y = rect.Min.y + (-ndcY * 0.5f + 0.5f) * height;
    return true;
}

bool ProjectPointToViewportLoose(
    const RenderFoundation::Camera& viewCamera,
    const ImRect& rect,
    const RenderFoundation::Vec3& worldPoint,
    ImVec2& outScreenPoint,
    float* outDepth = nullptr) {

    const float width = rect.GetWidth();
    const float height = rect.GetHeight();
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    const RenderFoundation::Vec3 forward = ForwardFromYawPitch(viewCamera.yawDegrees, viewCamera.pitchDegrees);
    const RenderFoundation::Vec3 right = RightFromForward(forward);
    const RenderFoundation::Vec3 up = UpFromBasis(right, forward);
    const RenderFoundation::Vec3 relative = worldPoint - viewCamera.position;
    const float cameraX = Dot(relative, right);
    const float cameraY = Dot(relative, up);
    const float cameraZ = Dot(relative, forward);
    if (outDepth != nullptr) {
        *outDepth = cameraZ;
    }
    if (cameraZ <= 0.001f) {
        return false;
    }

    const float aspect = width / height;
    const float tanHalfFov = std::tan(DegreesToRadians(viewCamera.fieldOfViewDegrees) * 0.5f);
    const float ndcX = cameraX / (cameraZ * tanHalfFov * aspect);
    const float ndcY = cameraY / (cameraZ * tanHalfFov);

    outScreenPoint.x = rect.Min.x + (ndcX * 0.5f + 0.5f) * width;
    outScreenPoint.y = rect.Min.y + (-ndcY * 0.5f + 0.5f) * height;
    return true;
}

void DrawCameraFrustumOverlay(
    ImDrawList* drawList,
    const ImRect& rect,
    const RenderFoundation::Camera& editorCamera,
    const RenderFoundation::Camera& renderCamera,
    bool selected) {

    if (Length(renderCamera.position - editorCamera.position) <= 0.05f &&
        std::fabs(renderCamera.yawDegrees - editorCamera.yawDegrees) <= 0.05f &&
        std::fabs(renderCamera.pitchDegrees - editorCamera.pitchDegrees) <= 0.05f) {
        return;
    }

    ImVec2 cameraCenter {};
    float cameraDepth = 0.0f;
    if (!ProjectPointToViewport(editorCamera, rect, renderCamera.position, cameraCenter, &cameraDepth)) {
        return;
    }

    const RenderFoundation::Vec3 forward = ForwardFromYawPitch(renderCamera.yawDegrees, renderCamera.pitchDegrees);
    const RenderFoundation::Vec3 right = RightFromForward(forward);
    const RenderFoundation::Vec3 up = UpFromBasis(right, forward);
    const float frustumDistance = std::max(0.8f, renderCamera.focusDistance * 0.18f);
    const float aspect = std::max(rect.GetWidth(), 1.0f) / std::max(rect.GetHeight(), 1.0f);
    const float halfHeight = std::tan(DegreesToRadians(renderCamera.fieldOfViewDegrees) * 0.5f) * frustumDistance;
    const float halfWidth = halfHeight * aspect;
    const RenderFoundation::Vec3 frustumCenter = renderCamera.position + forward * frustumDistance;
    const std::array<RenderFoundation::Vec3, 4> corners = {
        frustumCenter + right * halfWidth + up * halfHeight,
        frustumCenter - right * halfWidth + up * halfHeight,
        frustumCenter - right * halfWidth - up * halfHeight,
        frustumCenter + right * halfWidth - up * halfHeight
    };

    std::array<ImVec2, 4> projectedCorners {};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        if (!ProjectPointToViewport(editorCamera, rect, corners[i], projectedCorners[i], nullptr)) {
            return;
        }
    }

    const ImU32 outline = selected ? IM_COL32(255, 236, 166, 255) : IM_COL32(130, 206, 255, 255);
    const ImU32 fill = selected ? IM_COL32(255, 236, 166, 70) : IM_COL32(130, 206, 255, 38);
    const ImU32 lensFill = selected ? IM_COL32(255, 236, 166, 210) : IM_COL32(130, 206, 255, 210);
    drawList->AddConvexPolyFilled(projectedCorners.data(), static_cast<int>(projectedCorners.size()), fill);
    drawList->AddPolyline(projectedCorners.data(), static_cast<int>(projectedCorners.size()), outline, ImDrawFlags_Closed, selected ? 2.4f : 1.4f);
    for (const ImVec2& corner : projectedCorners) {
        drawList->AddLine(cameraCenter, corner, outline, selected ? 2.0f : 1.2f);
    }
    drawList->AddCircleFilled(cameraCenter, selected ? 6.0f : 5.0f, lensFill, 24);
    drawList->AddCircle(cameraCenter, selected ? 7.5f : 6.0f, IM_COL32(255, 255, 255, 180), 24, 1.4f);
    drawList->AddText(ImVec2(cameraCenter.x + 10.0f, cameraCenter.y - 18.0f), outline, "Camera");
}

RenderFoundation::Vec3 RotateAroundX(const RenderFoundation::Vec3& value, const float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {
        value.x,
        value.y * cosine - value.z * sine,
        value.y * sine + value.z * cosine
    };
}

RenderFoundation::Vec3 RotateAroundY(const RenderFoundation::Vec3& value, const float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {
        value.x * cosine + value.z * sine,
        value.y,
        -value.x * sine + value.z * cosine
    };
}

RenderFoundation::Vec3 RotateAroundZ(const RenderFoundation::Vec3& value, const float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {
        value.x * cosine - value.y * sine,
        value.x * sine + value.y * cosine,
        value.z
    };
}

RenderFoundation::Vec3 RotateDirection(const RenderFoundation::Vec3& direction, const RenderFoundation::Vec3& rotationDegrees) {
    RenderFoundation::Vec3 result = direction;
    result = RotateAroundX(result, DegreesToRadians(rotationDegrees.x));
    result = RotateAroundY(result, DegreesToRadians(rotationDegrees.y));
    result = RotateAroundZ(result, DegreesToRadians(rotationDegrees.z));
    return Normalize(result);
}

RenderFoundation::Vec3 GetLightForward(const RenderFoundation::Light& light) {
    return RotateDirection({ 1.0f, 0.0f, 0.0f }, light.transform.rotationDegrees);
}

RenderFoundation::Vec3 GetLightRight(const RenderFoundation::Light& light) {
    return RotateDirection({ 0.0f, 0.0f, 1.0f }, light.transform.rotationDegrees);
}

RenderFoundation::Vec3 GetLightUp(const RenderFoundation::Light& light) {
    return RotateDirection({ 0.0f, 1.0f, 0.0f }, light.transform.rotationDegrees);
}

ImVec2 Perp(const ImVec2& value) {
    return ImVec2(-value.y, value.x);
}

void DrawScreenBeam(
    ImDrawList* drawList,
    const ImVec2& origin,
    const ImVec2& tip,
    float originHalfWidth,
    float tipHalfWidth,
    ImU32 fill,
    ImU32 outline,
    float outlineThickness) {

    const ImVec2 delta(tip.x - origin.x, tip.y - origin.y);
    const float length = std::hypot(delta.x, delta.y);
    if (length <= 0.001f) {
        drawList->AddCircleFilled(origin, std::max(originHalfWidth, tipHalfWidth), fill, 24);
        drawList->AddCircle(origin, std::max(originHalfWidth, tipHalfWidth) + 1.0f, outline, 24, outlineThickness);
        return;
    }

    const ImVec2 direction(delta.x / length, delta.y / length);
    const ImVec2 normal = Perp(direction);
    const ImVec2 originLeft(origin.x + normal.x * originHalfWidth, origin.y + normal.y * originHalfWidth);
    const ImVec2 originRight(origin.x - normal.x * originHalfWidth, origin.y - normal.y * originHalfWidth);
    const ImVec2 tipLeft(tip.x + normal.x * tipHalfWidth, tip.y + normal.y * tipHalfWidth);
    const ImVec2 tipRight(tip.x - normal.x * tipHalfWidth, tip.y - normal.y * tipHalfWidth);
    const std::array<ImVec2, 4> beamOutlinePoints { originLeft, tipLeft, tipRight, originRight };

    drawList->AddQuadFilled(originLeft, tipLeft, tipRight, originRight, fill);
    drawList->AddPolyline(beamOutlinePoints.data(), 4, outline, ImDrawFlags_Closed, outlineThickness);
    drawList->AddLine(origin, tip, outline, outlineThickness);
}

void DrawLightOverlay(
    ImDrawList* drawList,
    const ImRect& rect,
    const RenderFoundation::Camera& editorCamera,
    const RenderFoundation::Light& light,
    const RenderContracts::CompiledScene& compiledScene,
    const RenderContracts::ViewportController& viewportController,
    bool selected) {

    if (!light.enabled) {
        return;
    }

    ImVec2 originScreen {};
    if (!ProjectPointToViewportLoose(editorCamera, rect, light.transform.translation, originScreen, nullptr)) {
        return;
    }

    const RenderFoundation::Vec3 forward = GetLightForward(light);
    const RenderFoundation::Vec3 right = GetLightRight(light);
    const RenderFoundation::Vec3 up = GetLightUp(light);
    const float worldBeamLength = light.type == LightType::Directional ? 2.8f : std::max(light.range, 0.5f);
    const RenderFoundation::Vec3 tipWorld = light.transform.translation + forward * worldBeamLength;
    ImVec2 tipScreen {};
    const bool tipVisible = ProjectPointToViewportLoose(editorCamera, rect, tipWorld, tipScreen, nullptr);

    const Vec3 displayColor = GetLightDisplayColor(light);
    const ImVec4 tint = ToImColor(displayColor, light.enabled ? 1.0f : 0.35f);
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(tint.x, tint.y, tint.z, selected ? 0.24f : 0.12f));
    const ImU32 outline = ImGui::ColorConvertFloat4ToU32(ImVec4(
        std::min(1.0f, tint.x + 0.15f),
        std::min(1.0f, tint.y + 0.15f),
        std::min(1.0f, tint.z + 0.15f),
        selected ? 1.0f : 0.88f));
    const ImU32 core = ImGui::ColorConvertFloat4ToU32(ImVec4(
        std::min(1.0f, tint.x + 0.20f),
        std::min(1.0f, tint.y + 0.20f),
        std::min(1.0f, tint.z + 0.20f),
        selected ? 1.0f : 0.95f));
    const float originRadius = selected ? 7.0f : 5.0f;

    switch (light.type) {
        case LightType::Point: {
            drawList->AddCircleFilled(originScreen, originRadius, fill, 24);
            drawList->AddCircle(originScreen, originRadius + 2.0f, outline, 24, selected ? 2.2f : 1.3f);
            drawList->AddLine(
                ImVec2(originScreen.x - originRadius, originScreen.y),
                ImVec2(originScreen.x + originRadius, originScreen.y),
                outline,
                1.4f);
            drawList->AddLine(
                ImVec2(originScreen.x, originScreen.y - originRadius),
                ImVec2(originScreen.x, originScreen.y + originRadius),
                outline,
                1.4f);
            break;
        }
        case LightType::Spot: {
            if (!tipVisible) {
                return;
            }
            const float beamLength = std::max(24.0f, std::hypot(tipScreen.x - originScreen.x, tipScreen.y - originScreen.y));
            const float tipHalfWidth = std::clamp(beamLength * std::tan(DegreesToRadians(std::max(light.outerConeDegrees, light.innerConeDegrees))), 8.0f, 240.0f);
            const float innerHalfWidth = std::clamp(beamLength * std::tan(DegreesToRadians(std::max(0.1f, light.innerConeDegrees))), 4.0f, tipHalfWidth);
            DrawScreenBeam(drawList, originScreen, tipScreen, originRadius, tipHalfWidth, fill, outline, selected ? 2.0f : 1.2f);
            DrawScreenBeam(drawList, originScreen, tipScreen, originRadius * 0.8f, innerHalfWidth, IM_COL32(255, 255, 255, 26), core, 1.0f);
            break;
        }
        case LightType::Area: {
            const float halfWidth = std::max(0.12f, light.areaSize.x * 0.5f);
            const float halfHeight = std::max(0.12f, light.areaSize.y * 0.5f);
            std::array<ImVec2, 4> corners {};
            const RenderFoundation::Vec3 origin = light.transform.translation;
            const RenderFoundation::Vec3 worldCorners[4] = {
                origin + right * halfWidth + up * halfHeight,
                origin - right * halfWidth + up * halfHeight,
                origin - right * halfWidth - up * halfHeight,
                origin + right * halfWidth - up * halfHeight
            };
            for (std::size_t index = 0; index < corners.size(); ++index) {
                if (!ProjectPointToViewportLoose(editorCamera, rect, worldCorners[index], corners[index], nullptr)) {
                    return;
                }
            }
            drawList->AddQuadFilled(corners[0], corners[1], corners[2], corners[3], fill);
            drawList->AddQuad(corners[0], corners[1], corners[2], corners[3], outline, selected ? 2.0f : 1.2f);
            break;
        }
        case LightType::Directional: {
            if (!tipVisible) {
                return;
            }
            const ImVec2 delta(tipScreen.x - originScreen.x, tipScreen.y - originScreen.y);
            const float length = std::hypot(delta.x, delta.y);
            const ImVec2 direction = length <= 0.001f ? ImVec2(1.0f, 0.0f) : ImVec2(delta.x / length, delta.y / length);
            const ImVec2 normal = Perp(direction);
            const ImVec2 tailA(originScreen.x - normal.x * 10.0f, originScreen.y - normal.y * 10.0f);
            const ImVec2 tailB(originScreen.x + normal.x * 10.0f, originScreen.y + normal.y * 10.0f);
            const ImVec2 arrowTip(tipScreen.x, tipScreen.y);
            const ImVec2 arrowLeft(arrowTip.x - direction.x * 18.0f + normal.x * 10.0f, arrowTip.y - direction.y * 18.0f + normal.y * 10.0f);
            const ImVec2 arrowRight(arrowTip.x - direction.x * 18.0f - normal.x * 10.0f, arrowTip.y - direction.y * 18.0f - normal.y * 10.0f);
            drawList->AddLine(originScreen, tipScreen, outline, selected ? 2.2f : 1.4f);
            drawList->AddTriangleFilled(arrowTip, arrowLeft, arrowRight, fill);
            drawList->AddLine(tailA, tipScreen, outline, 1.4f);
            drawList->AddLine(tailB, tipScreen, outline, 1.4f);
            break;
        }
        case LightType::Laser:
        default: {
            const float guideLength = std::clamp(std::min(light.range, 2.5f), 0.75f, 2.5f);
            Vec3 guideEndWorld = light.transform.translation + forward * guideLength;
            Vec3 hitPoint {};
            Vec3 hitNormal {};
            float hitDistance = 0.0f;
            const bool hasFirstHit =
                selected &&
                viewportController.TraceFirstPrimitiveHit(
                    compiledScene,
                    light.transform.translation,
                    forward,
                    hitPoint,
                    hitNormal,
                    hitDistance);
            if (hasFirstHit) {
                guideEndWorld = hitPoint;
            }

            ImVec2 guideEndScreen {};
            if (!ProjectPointToViewportLoose(editorCamera, rect, guideEndWorld, guideEndScreen, nullptr)) {
                return;
            }

            drawList->AddCircleFilled(originScreen, originRadius + 1.0f, fill, 24);
            drawList->AddCircle(originScreen, originRadius + 3.0f, outline, 24, selected ? 2.2f : 1.2f);
            drawList->AddLine(originScreen, guideEndScreen, outline, selected ? 2.6f : 1.7f);

            const ImVec2 lineDelta(guideEndScreen.x - originScreen.x, guideEndScreen.y - originScreen.y);
            const float lineLength = std::hypot(lineDelta.x, lineDelta.y);
            if (lineLength > 0.001f) {
                const ImVec2 direction(lineDelta.x / lineLength, lineDelta.y / lineLength);
                const ImVec2 normal = Perp(direction);
                const float arrowLength = hasFirstHit ? 10.0f : 14.0f;
                const float arrowWidth = hasFirstHit ? 5.5f : 7.0f;
                const ImVec2 arrowLeft(
                    guideEndScreen.x - direction.x * arrowLength + normal.x * arrowWidth,
                    guideEndScreen.y - direction.y * arrowLength + normal.y * arrowWidth);
                const ImVec2 arrowRight(
                    guideEndScreen.x - direction.x * arrowLength - normal.x * arrowWidth,
                    guideEndScreen.y - direction.y * arrowLength - normal.y * arrowWidth);
                drawList->AddTriangleFilled(guideEndScreen, arrowLeft, arrowRight, fill);
            }

            if (hasFirstHit) {
                drawList->AddCircleFilled(guideEndScreen, 4.5f, core, 20);
                drawList->AddCircle(guideEndScreen, 8.0f, outline, 24, 2.0f);
                ImVec2 normalTipScreen {};
                if (ProjectPointToViewportLoose(editorCamera, rect, hitPoint + hitNormal * 0.25f, normalTipScreen, nullptr)) {
                    drawList->AddLine(guideEndScreen, normalTipScreen, IM_COL32(255, 255, 255, 180), 1.4f);
                }
            }
            break;
        }
    }

    if (selected) {
        const std::string label = light.name.empty()
            ? std::string(LightTypeLabel(light.type))
            : light.name + " (" + LightTypeLabel(light.type) + ")";
        drawList->AddText(ImVec2(originScreen.x + 12.0f, originScreen.y - 18.0f), outline, label.c_str());
    }
}

void DrawViewportOverlay(
    ImDrawList* drawList,
    const ImRect& rect,
    const State& state,
    bool navigationActive,
    const char* testSceneLabel) {
    const std::string lineOne =
        std::string("Mode: ") + ViewModeLabel(state.GetSettings().viewMode) +
        (state.GetSettings().viewMode == ViewMode::PathTrace
            ? std::string(" / ") + PathTraceTransportModeLabel(state.GetSettings().pathTraceTransportMode)
            : std::string()) +
        "  |  Samples: " + std::to_string(state.GetAccumulatedSamples()) +
        "  |  Transport Epoch: " + std::to_string(state.GetTransportEpoch());
    const std::string lineTwo =
        std::string("Shortcuts ") + TransformShortcutHint(state.GetSelection().type) + ": " + TransformModeLabel(state.GetSettings().transformMode) +
        "  |  Space: " + TransformSpaceLabel(state.GetSettings().transformSpace) +
        (testSceneLabel != nullptr && testSceneLabel[0] != '\0'
            ? std::string("  |  Test Scene: ") + testSceneLabel + " (read-only)"
            : std::string()) +
        "  |  " + (navigationActive ? "Navigating camera" : "RMB fly camera, click to select, Del deletes");

    drawList->AddRectFilled(
        ImVec2(rect.Min.x + 12.0f, rect.Max.y - 52.0f),
        ImVec2(rect.Min.x + 520.0f, rect.Max.y - 10.0f),
        IM_COL32(0, 0, 0, 120),
        8.0f);
    drawList->AddText(ImVec2(rect.Min.x + 24.0f, rect.Max.y - 46.0f), IM_COL32(240, 240, 240, 255), lineOne.c_str());
    drawList->AddText(ImVec2(rect.Min.x + 24.0f, rect.Max.y - 28.0f), IM_COL32(190, 198, 208, 255), lineTwo.c_str());

    if (testSceneLabel != nullptr && testSceneLabel[0] != '\0') {
        const std::string testSceneLine = std::string("Validation Scene: ") + testSceneLabel;
        const float badgeWidth = std::max(280.0f, ImGui::CalcTextSize(testSceneLine.c_str()).x + 32.0f);
        const float badgeTop = rect.Min.y + 108.0f;
        drawList->AddRectFilled(
            ImVec2(rect.Max.x - badgeWidth - 12.0f, badgeTop),
            ImVec2(rect.Max.x - 12.0f, badgeTop + 42.0f),
            IM_COL32(24, 30, 42, 208),
            8.0f);
        drawList->AddText(
            ImVec2(rect.Max.x - badgeWidth, badgeTop + 8.0f),
            IM_COL32(255, 233, 186, 255),
            "Test Scene");
        drawList->AddText(
            ImVec2(rect.Max.x - badgeWidth, badgeTop + 26.0f),
            IM_COL32(210, 214, 220, 255),
            testSceneLine.c_str());
    }

    if (state.GetSettings().viewMode == ViewMode::PathTrace) {
        const float overlayHeight =
            state.GetSettings().pathTraceDebugMode == PathTraceDebugMode::None ? 62.0f : 78.0f;
        drawList->AddRectFilled(
            ImVec2(rect.Max.x - 280.0f, rect.Min.y + 12.0f),
            ImVec2(rect.Max.x - 12.0f, rect.Min.y + overlayHeight),
            IM_COL32(0, 0, 0, 110),
            8.0f);
        drawList->AddText(
            ImVec2(rect.Max.x - 264.0f, rect.Min.y + 20.0f),
            IM_COL32(255, 233, 186, 255),
            "Spectral PT Core");
        drawList->AddText(
            ImVec2(rect.Max.x - 264.0f, rect.Min.y + 38.0f),
            IM_COL32(210, 214, 220, 255),
            "Progressive compute transport is active in this viewport.");
        if (state.GetSettings().pathTraceDebugMode != PathTraceDebugMode::None) {
            drawList->AddText(
                ImVec2(rect.Max.x - 264.0f, rect.Min.y + 54.0f),
                IM_COL32(255, 215, 170, 255),
                PathTraceDebugModeLabel(state.GetSettings().pathTraceDebugMode));
        }
    }
}

} // namespace

void RenderTab::Initialize() {
    m_State.ResetToDefaultScene();
    ResetEditorCameraToDefault();
    ResetAuxiliaryAccumulation();
    m_LastAuxiliaryTransportEpoch = m_State.GetTransportEpoch();
    InvalidateCompiledSceneTargets();
    m_ViewportController.Reset();
    m_LastViewportError.clear();
    m_LastCameraPreviewError.clear();
    m_LastFinalRenderError.clear();
    m_StatusText = "Cornell box baseline loaded.";
    m_LatestFinalAssetFileName.clear();
}

void RenderTab::Shutdown() {
    m_RenderDelegator.Shutdown();
}

void RenderTab::RenderUI() {
    m_State.TickFrame();
    SyncAuxiliaryAccumulationEpochs();

    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        if (HasVirtualSelection()) {
            SelectNone();
            m_StatusText = "Selection cleared.";
        } else if (m_State.DeleteSelection()) {
            m_StatusText = "Selection deleted.";
        }
    }

    const bool renderSessionActive = m_FinalRenderSession.active;
    if (renderSessionActive) {
        ImGui::BeginDisabled();
    }
    RenderMenuBar();
    if (renderSessionActive) {
        ImGui::EndDisabled();
    }
      if (!renderSessionActive) {
          RenderBody();
          RenderCameraWindow();
          RenderFinalRenderSettingsWindow();
          RenderTestScenesWindow();
          RenderDiagnosticsWindow();
          RenderResetScenePopup();
          RenderImportModelPopup();
      }
    RenderFinalRenderWindow();

    if (Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetSaveStatusText().c_str());
    } else if (Async::IsBusy(m_ImportTask.state)) {
        ImGuiExtras::RenderBusyOverlay("Importing model...");
    }
}

bool RenderTab::TryBuildImportedPrimitiveBounds(
    const Primitive& primitive,
    RenderBounds& outLocalBounds,
    RenderBounds& outWorldBounds) const {
    if (primitive.type != PrimitiveType::ImportedMesh) {
        return false;
    }

    const auto& importedMeshes = m_State.GetImportedMeshes();
    if (primitive.meshIndex >= 0 && primitive.meshIndex < static_cast<int>(importedMeshes.size())) {
        outLocalBounds = importedMeshes[static_cast<std::size_t>(primitive.meshIndex)].localBounds;
    } else {
        outLocalBounds = ToRenderBounds(primitive.importedLocalBoundsMin, primitive.importedLocalBoundsMax);
    }
    outWorldBounds = TransformBounds(outLocalBounds, primitive.transform);
    return true;
}

void RenderTab::FrameEditorCameraToBounds(const RenderBounds& bounds) {
    const Vec3 center {
        (bounds.min.x + bounds.max.x) * 0.5f,
        (bounds.min.y + bounds.max.y) * 0.5f,
        (bounds.min.z + bounds.max.z) * 0.5f
    };
    const Vec3 extent {
        std::max(bounds.max.x - bounds.min.x, 0.001f),
        std::max(bounds.max.y - bounds.min.y, 0.001f),
        std::max(bounds.max.z - bounds.min.z, 0.001f)
    };
    const float aspect = std::clamp(m_LastViewportImageAspect, 0.2f, 5.0f);
    const float halfFovY = DegreesToRadians(std::clamp(m_EditorCamera.fieldOfViewDegrees, 20.0f, 110.0f)) * 0.5f;
    const float halfFovX = std::atan(std::tan(halfFovY) * aspect);
    const Vec3 forward = ForwardFromYawPitch(m_EditorCamera.yawDegrees, m_EditorCamera.pitchDegrees);
    const Vec3 right = RightFromForward(forward);
    const Vec3 up = UpFromBasis(right, forward);
    const std::array<Vec3, 8> corners = {
        Vec3 { bounds.min.x, bounds.min.y, bounds.min.z },
        Vec3 { bounds.min.x, bounds.min.y, bounds.max.z },
        Vec3 { bounds.min.x, bounds.max.y, bounds.min.z },
        Vec3 { bounds.min.x, bounds.max.y, bounds.max.z },
        Vec3 { bounds.max.x, bounds.min.y, bounds.min.z },
        Vec3 { bounds.max.x, bounds.min.y, bounds.max.z },
        Vec3 { bounds.max.x, bounds.max.y, bounds.min.z },
        Vec3 { bounds.max.x, bounds.max.y, bounds.max.z }
    };

    float fitDistance = 0.0f;
    const float tanHalfFovY = std::max(std::tan(halfFovY), 0.1f);
    const float tanHalfFovX = std::max(std::tan(halfFovX), 0.1f);
    for (const Vec3& corner : corners) {
        const Vec3 offset = corner - center;
        const float cornerX = std::fabs(Dot(offset, right));
        const float cornerY = std::fabs(Dot(offset, up));
        const float cornerZ = Dot(offset, forward);
        fitDistance = std::max(fitDistance, cornerX / tanHalfFovX - cornerZ);
        fitDistance = std::max(fitDistance, cornerY / tanHalfFovY - cornerZ);
    }

    const float diagonal = std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);
    fitDistance = std::max(fitDistance * 1.06f, diagonal * 0.08f);
    m_EditorCamera.position = center - forward * fitDistance;
    m_EditorCamera.focusDistance = fitDistance;
}

bool RenderTab::IsViewportProxyActive() const {
    const Settings& settings = m_State.GetSettings();
    if (settings.viewMode == ViewMode::PathTrace) {
        return false;
    }

    if (settings.viewportPerformanceMode == ViewportPerformanceMode::Proxy) {
        return true;
    }
    if (settings.viewportPerformanceMode == ViewportPerformanceMode::Exact) {
        return false;
    }

    int importedTriangleCount = 0;
    for (const Primitive& primitive : m_State.GetPrimitives()) {
        if (primitive.type == PrimitiveType::ImportedMesh && primitive.visible) {
            importedTriangleCount += primitive.importedTriangleCount;
        }
    }
    if (importedTriangleCount > 250000) {
        return true;
    }

    return (m_ViewportCompile.lastCompileMs + m_LastViewportUploadMs) > 120.0;
}

void RenderTab::DrawViewportProxyPreview(ImDrawList* drawList, const ImRect& imageRect) const {
    const auto drawBoundsEdges = [&](const Primitive& primitive, const RenderBounds& localBounds) {
        const std::array<Vec3, 8> localCorners = {
            Vec3 { localBounds.min.x, localBounds.min.y, localBounds.min.z },
            Vec3 { localBounds.min.x, localBounds.min.y, localBounds.max.z },
            Vec3 { localBounds.min.x, localBounds.max.y, localBounds.min.z },
            Vec3 { localBounds.min.x, localBounds.max.y, localBounds.max.z },
            Vec3 { localBounds.max.x, localBounds.min.y, localBounds.min.z },
            Vec3 { localBounds.max.x, localBounds.min.y, localBounds.max.z },
            Vec3 { localBounds.max.x, localBounds.max.y, localBounds.min.z },
            Vec3 { localBounds.max.x, localBounds.max.y, localBounds.max.z }
        };
        const std::array<std::pair<int, int>, 12> edges = {
            std::pair<int, int> { 0, 1 }, { 0, 2 }, { 0, 4 }, { 1, 3 },
            { 1, 5 }, { 2, 3 }, { 2, 6 }, { 3, 7 },
            { 4, 5 }, { 4, 6 }, { 5, 7 }, { 6, 7 }
        };

        std::array<Vec3, 8> worldCorners {};
        for (std::size_t index = 0; index < localCorners.size(); ++index) {
            worldCorners[index] = TransformPointForBounds(primitive.transform, localCorners[index]);
        }

        const bool selected =
            m_State.GetSelection().type == SelectionType::Primitive &&
            m_State.GetSelection().id == primitive.id;
        const ImU32 color = selected ? IM_COL32(255, 228, 150, 235) : IM_COL32(128, 196, 255, 220);
        const float thickness = selected ? 2.8f : 1.6f;
        for (const auto& edge : edges) {
            ImVec2 a {};
            ImVec2 b {};
            if (!ProjectPointToViewportLoose(m_EditorCamera, imageRect, worldCorners[static_cast<std::size_t>(edge.first)], a, nullptr) ||
                !ProjectPointToViewportLoose(m_EditorCamera, imageRect, worldCorners[static_cast<std::size_t>(edge.second)], b, nullptr)) {
                continue;
            }
            drawList->AddLine(a, b, color, thickness);
        }

        const Vec3 center {
            (localBounds.min.x + localBounds.max.x) * 0.5f,
            (localBounds.min.y + localBounds.max.y) * 0.5f,
            (localBounds.min.z + localBounds.max.z) * 0.5f
        };
        ImVec2 labelPoint {};
        if (ProjectPointToViewportLoose(m_EditorCamera, imageRect, TransformPointForBounds(primitive.transform, center), labelPoint, nullptr)) {
            drawList->AddText(ImVec2(labelPoint.x + 8.0f, labelPoint.y - 18.0f), color, primitive.name.c_str());
        }
    };

    for (const Primitive& primitive : m_State.GetPrimitives()) {
        if (!primitive.visible) {
            continue;
        }

        RenderBounds localBounds {};
        bool canDraw = true;
        switch (primitive.type) {
            case PrimitiveType::Sphere:
            case PrimitiveType::Cube:
                localBounds = ToRenderBounds({ -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f });
                break;
            case PrimitiveType::Plane:
                localBounds = ToRenderBounds({ -0.5f, 0.0f, -0.5f }, { 0.5f, 0.02f, 0.5f });
                break;
            case PrimitiveType::ImportedMesh: {
                RenderBounds worldBounds {};
                canDraw = TryBuildImportedPrimitiveBounds(primitive, localBounds, worldBounds);
                break;
            }
        }

        if (canDraw) {
            drawBoundsEdges(primitive, localBounds);
        }
    }
}

void RenderTab::InvalidateCompiledSceneTargets() {
    m_CompiledScene = {};
    m_CameraPreviewScene = {};
    m_FinalRenderScene = {};
    m_ViewportCompile = {};
    m_CameraPreviewCompile = {};
    m_LastViewportUploadMs = 0.0;
    m_LastViewportError.clear();
    m_LastCameraPreviewError.clear();
}

void RenderTab::ResetEditorCameraToDefault() {
    m_EditorCamera = {};
    m_EditorCamera.position = { -5.6f, 3.1f, -10.8f };
    m_EditorCamera.yawDegrees = 67.0f;
    m_EditorCamera.pitchDegrees = -10.0f;
    m_EditorCamera.fieldOfViewDegrees = 80.0f;
    m_EditorCamera.focusDistance = 10.5f;
    m_EditorCamera.apertureRadius = 0.0f;
}

void RenderTab::ResetEditorCameraFromRenderCamera() {
    m_EditorCamera = m_State.GetCamera();
}

void RenderTab::ResetAuxiliaryAccumulation() {
    m_CameraAccumulationManager.ResetForScene(BuildCameraPreviewSettings());
    m_FinalRenderAccumulationManager.ResetForScene(BuildFinalRenderSettings());
    m_LastCameraPreviewError.clear();
    m_LastFinalRenderError.clear();
}

void RenderTab::SyncAuxiliaryAccumulationEpochs() {
    const std::uint64_t transportEpoch = m_State.GetTransportEpoch();
    if (transportEpoch == m_LastAuxiliaryTransportEpoch) {
        return;
    }

    m_LastAuxiliaryTransportEpoch = transportEpoch;
    m_CameraAccumulationManager.ResetForScene(BuildCameraPreviewSettings());
    if (!m_FinalRenderSession.active) {
        m_FinalRenderAccumulationManager.ResetForScene(BuildFinalRenderSettings());
    }
    m_LastCameraPreviewError.clear();
    if (!m_FinalRenderSession.active) {
        m_LastFinalRenderError.clear();
    }
}

bool RenderTab::BuildLibraryProjectPayload(
    StackBinaryFormat::json& outPayload,
    std::vector<unsigned char>& outBeautyPixels,
    int& outWidth,
    int& outHeight,
    std::string& errorMessage) const {

    errorMessage.clear();
    outPayload = RenderFoundation::Serialization::BuildPayload(m_State, m_LatestFinalAssetFileName);

    outWidth = std::clamp(m_State.GetSettings().finalRender.resolutionX, 640, 4096);
    outHeight = std::clamp(m_State.GetSettings().finalRender.resolutionY, 360, 4096);
    RenderContracts::CompiledScene compiledScene;
    const RenderContracts::SceneSnapshot snapshot = m_State.BuildSnapshot();
    const RenderFoundation::Settings finalSettings = BuildFinalRenderSettings();
    if (!m_SceneCompiler.Compile(snapshot, snapshot.camera, finalSettings, compiledScene, errorMessage)) {
        RenderFoundation::BuildPreviewPixels(
            m_State,
            outWidth,
            outHeight,
            std::max(m_State.GetSettings().finalRender.sampleTarget, 64),
            outBeautyPixels);
        return !outBeautyPixels.empty();
    }

    RenderFoundation::BuildPreviewPixels(
        m_State,
        outWidth,
        outHeight,
        std::max(m_State.GetSettings().finalRender.sampleTarget, 64),
        outBeautyPixels);
    return !outBeautyPixels.empty();
}

RenderFoundation::Settings RenderTab::BuildCameraPreviewSettings() const {
    RenderFoundation::Settings settings = m_State.GetSettings();
    settings.viewMode = ViewMode::PathTrace;
    settings.accumulationEnabled = true;
    settings.resolutionX = std::max(1, m_CameraPreviewRenderWidth);
    settings.resolutionY = std::max(1, m_CameraPreviewRenderHeight);
    settings.cameraPreview.resolutionX = settings.resolutionX;
    settings.cameraPreview.resolutionY = settings.resolutionY;
    settings.maxBounceCount = 512;
    settings.cameraPreview.maxBounceCount = 512;
    settings.pathTraceTerminationMode = PathTraceTerminationMode::Smart;
    settings.cameraPreview.terminationMode = PathTraceTerminationMode::Smart;
    return settings;
}

RenderFoundation::Settings RenderTab::BuildFinalRenderSettings() const {
    RenderFoundation::Settings settings = m_State.GetSettings();
    settings.viewMode = ViewMode::PathTrace;
    settings.accumulationEnabled = true;
    settings.resolutionX = settings.finalRender.resolutionX;
    settings.resolutionY = settings.finalRender.resolutionY;
    settings.maxBounceCount = settings.finalRender.maxBounceCount;
    settings.pathTraceTerminationMode = settings.finalRender.terminationMode;
    return settings;
}

bool RenderTab::ApplyLibraryProjectPayload(
    const StackBinaryFormat::json& payload,
    const std::string& projectName,
    const std::string& projectFileName,
    std::string& errorMessage) {

    const bool applied = RenderFoundation::Serialization::ApplyPayload(
        payload,
        m_State,
        projectName,
        projectFileName,
        m_LatestFinalAssetFileName,
        errorMessage);
    if (applied) {
        DisableTestScenePreview();
        ResetEditorCameraFromRenderCamera();
        ResetAuxiliaryAccumulation();
        m_LastAuxiliaryTransportEpoch = m_State.GetTransportEpoch();
        InvalidateCompiledSceneTargets();
        m_ViewportController.Reset();
        m_LastViewportError.clear();
        m_StatusText = std::string("Loaded render project: ") + projectName;
    }
    return applied;
}

bool RenderTab::HasUnsavedChanges() const {
    return m_State.HasUnsavedChanges();
}

const std::string& RenderTab::GetCurrentProjectName() const {
    if (!m_State.GetProjectName().empty()) {
        return m_State.GetProjectName();
    }
    return m_State.GetSceneMetadata().label;
}

const std::string& RenderTab::GetCurrentProjectFileName() const {
    return m_State.GetProjectFileName();
}

void RenderTab::RenderMenuBar() {
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::BeginChild("RenderMenuHost", ImVec2(0.0f, ImGui::GetFrameHeight() + 8.0f), false, flags);

    if (ImGui::BeginMenuBar()) {
        const bool saveBusy = Async::IsBusy(LibraryManager::Get().GetSaveTaskState());
        if (ImGui::BeginMenu("File")) {
            ImGui::BeginDisabled(saveBusy);
            if (ImGui::MenuItem(saveBusy ? "Saving..." : "Save Render Project", "Ctrl+S")) {
                RequestSaveProject();
            }
            ImGui::EndDisabled();
            if (ImGui::MenuItem("New Scene")) {
                if (m_State.HasUnsavedChanges()) {
                    m_OpenResetSceneConfirm = true;
                    ImGui::OpenPopup("Discard Unsaved Render Scene");
                } else {
                    DisableTestScenePreview();
                    m_State.ResetToDefaultScene();
                    ResetEditorCameraToDefault();
                    ResetAuxiliaryAccumulation();
                    m_LastAuxiliaryTransportEpoch = m_State.GetTransportEpoch();
                    InvalidateCompiledSceneTargets();
                    m_ViewportController.Reset();
                    m_LastViewportError.clear();
                    m_LatestFinalAssetFileName.clear();
                    SelectCamera();
                    m_StatusText = "Cornell box baseline loaded.";
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import Model...")) {
                const std::string filePath = FileDialogs::OpenRenderGltfFileDialog();
                if (!filePath.empty()) {
                    BeginImportModelFlow(filePath);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Delete Selection", "Del", false, !HasVirtualSelection() && m_State.GetSelection().type != SelectionType::None)) {
                if (m_State.DeleteSelection()) {
                    m_StatusText = "Selection deleted.";
                }
            }
            if (ImGui::MenuItem("Clear Selection", nullptr, false, HasVirtualSelection() || m_State.GetSelection().type != SelectionType::None)) {
                SelectNone();
                m_StatusText = "Selection cleared.";
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Add")) {
            ImGui::BeginDisabled(m_TestScenePreviewEnabled);
            if (ImGui::BeginMenu("Primitive")) {
                if (ImGui::MenuItem("Sphere")) { SelectPrimitive(m_State.AddPrimitive(PrimitiveType::Sphere)); }
                if (ImGui::MenuItem("Cube")) { SelectPrimitive(m_State.AddPrimitive(PrimitiveType::Cube)); }
                if (ImGui::MenuItem("Plane")) { SelectPrimitive(m_State.AddPrimitive(PrimitiveType::Plane)); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Light")) {
                if (ImGui::MenuItem("Point Light")) { SelectLight(m_State.AddLight(LightType::Point)); }
                if (ImGui::MenuItem("Spot Light")) { SelectLight(m_State.AddLight(LightType::Spot)); }
                if (ImGui::MenuItem("Area Light")) { SelectLight(m_State.AddLight(LightType::Area)); }
                if (ImGui::MenuItem("Directional Light")) { SelectLight(m_State.AddLight(LightType::Directional)); }
                if (ImGui::MenuItem("Laser Light")) { SelectLight(m_State.AddLight(LightType::Laser)); }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Import Model...")) {
                const std::string filePath = FileDialogs::OpenRenderGltfFileDialog();
                if (!filePath.empty()) {
                    BeginImportModelFlow(filePath);
                }
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Select")) {
            if (ImGui::MenuItem("Camera")) {
                SelectCamera();
            }
            if (ImGui::MenuItem("Environment")) {
                SelectVirtual(VirtualSelection::Environment);
            }
            if (ImGui::MenuItem("Fog")) {
                SelectVirtual(VirtualSelection::Fog);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            int viewMode = static_cast<int>(m_State.GetSettings().viewMode);
            if (ImGui::MenuItem("Unlit", nullptr, viewMode == static_cast<int>(ViewMode::Unlit))) {
                m_State.GetSettings().viewMode = ViewMode::Unlit;
                m_State.MarkTransportDirty("Viewport mode changed.");
            }
            if (ImGui::MenuItem("Path Trace", nullptr, viewMode == static_cast<int>(ViewMode::PathTrace))) {
                m_State.GetSettings().viewMode = ViewMode::PathTrace;
                m_State.MarkTransportDirty("Viewport mode changed.");
            }
            if (m_State.GetSettings().viewMode == ViewMode::PathTrace && ImGui::BeginMenu("Path Trace Transport")) {
                if (ImGui::MenuItem("Preview", nullptr, m_State.GetSettings().pathTraceTransportMode == PathTraceTransportMode::Preview)) {
                    m_State.GetSettings().pathTraceTransportMode = PathTraceTransportMode::Preview;
                    m_State.MarkTransportDirty("Path trace transport mode changed.");
                }
                if (ImGui::MenuItem("Caustics Reference", nullptr, m_State.GetSettings().pathTraceTransportMode == PathTraceTransportMode::CausticsReference)) {
                    m_State.GetSettings().pathTraceTransportMode = PathTraceTransportMode::CausticsReference;
                    m_State.MarkTransportDirty("Path trace transport mode changed.");
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Transform Mode")) {
                if (ImGui::MenuItem("Move", "W", m_State.GetSettings().transformMode == TransformMode::Translate)) {
                    m_State.GetSettings().transformMode = TransformMode::Translate;
                    m_State.MarkDisplayDirty("Transform mode set to translate.");
                }
                if (ImGui::MenuItem("Rotate", "E", m_State.GetSettings().transformMode == TransformMode::Rotate)) {
                    m_State.GetSettings().transformMode = TransformMode::Rotate;
                    m_State.MarkDisplayDirty("Transform mode set to rotate.");
                }
                if (ImGui::MenuItem("Scale", "R", m_State.GetSettings().transformMode == TransformMode::Scale)) {
                    m_State.GetSettings().transformMode = TransformMode::Scale;
                    m_State.MarkDisplayDirty("Transform mode set to scale.");
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Toggle Local/World Space", "T")) {
                m_State.GetSettings().transformSpace =
                    m_State.GetSettings().transformSpace == TransformSpace::Local ? TransformSpace::World : TransformSpace::Local;
                m_State.MarkDisplayDirty("Transform space toggled.");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Object")) {
            if (ImGui::MenuItem("Frame Selection", "F", false, m_State.GetSelection().type == SelectionType::Primitive)) {
                if (const Primitive* primitive = m_State.FindPrimitive(m_State.GetSelection().id)) {
                    RenderBounds localBounds {};
                    RenderBounds worldBounds {};
                    if (TryBuildImportedPrimitiveBounds(*primitive, localBounds, worldBounds)) {
                        FrameEditorCameraToBounds(worldBounds);
                    }
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Render")) {
            if (ImGui::MenuItem("Final Render Settings...")) {
                m_ShowFinalRenderSettingsWindow = true;
            }
            if (ImGui::MenuItem("Start Final Render...")) {
                m_ShowFinalRenderSettingsWindow = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
                if (ImGui::MenuItem("Camera", nullptr, m_ShowCameraWindow)) {
                m_ShowCameraWindow = true;
            }
            if (ImGui::MenuItem("Final Render Settings", nullptr, m_ShowFinalRenderSettingsWindow)) {
                m_ShowFinalRenderSettingsWindow = true;
            }
            if (ImGui::MenuItem("Validation Scenes", nullptr, m_ShowTestScenesWindow)) {
                m_ShowTestScenesWindow = true;
            }
            if (ImGui::MenuItem("Render Diagnostics", nullptr, m_ShowDiagnosticsWindow)) {
                m_ShowDiagnosticsWindow = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Render Layout")) {
                RequestResetRenderLayout();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            ImGui::TextDisabled("Render workspace");
            ImGui::TextDisabled("Viewport navigation and transform shortcuts remain active.");
            ImGui::EndMenu();
        }

        const std::string saveStatus = LibraryManager::Get().GetSaveStatusText();
        const std::string loadStatus = LibraryManager::Get().GetProjectLoadStatusText();
        const std::string toolbarStatus =
            !saveStatus.empty() ? saveStatus :
            (!loadStatus.empty() ? loadStatus : m_StatusText);
        if (!toolbarStatus.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", toolbarStatus.c_str());
        }

        ImGui::EndMenuBar();
    }

    ImGui::EndChild();
}

void RenderTab::BeginImportModelFlow(const std::string& filePath) {
    m_ImportDialog.filePath = filePath;
    m_ImportDialog.options = {};
    m_ImportDialog.open = true;
}

void RenderTab::SubmitPendingModelImport() {
    if (m_ImportDialog.filePath.empty() || Async::IsBusy(m_ImportTask.state)) {
        return;
    }

    m_ImportTask.generation += 1;
    m_ImportTask.state = Async::TaskState::Running;
    m_ImportTask.filePath = m_ImportDialog.filePath;
    m_ImportTask.options = m_ImportDialog.options;
    m_ImportDialog.filePath.clear();
    m_StatusText = std::string("Importing model... ") + std::filesystem::path(m_ImportTask.filePath).filename().string();

    const std::uint64_t generation = m_ImportTask.generation;
    const std::string filePath = m_ImportTask.filePath;
    const ImportedModelOptions options = m_ImportTask.options;
    Async::TaskSystem::Get().Submit([this, generation, filePath, options]() mutable {
        ImportedModelResult result;
        std::string errorMessage;
        const auto importStart = std::chrono::steady_clock::now();
        const bool success = RenderFoundation::ImportGltfModel(filePath, options, result, errorMessage);
        const double importMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - importStart).count();

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             success,
                                             result = std::move(result),
                                             errorMessage = std::move(errorMessage),
                                             importMs]() mutable {
            if (generation != m_ImportTask.generation) {
                return;
            }

            m_ImportTask.state = Async::TaskState::Applying;
            if (!success) {
                m_ImportTask.state = Async::TaskState::Failed;
                m_StatusText = errorMessage.empty() ? "Model import failed." : errorMessage;
                return;
            }

            ImportedModelDiagnostics diagnostics;
            Id importedPrimitiveId = 0;
            if (!m_State.ApplyImportedModelResult(std::move(result), &importedPrimitiveId, &diagnostics)) {
                m_ImportTask.state = Async::TaskState::Failed;
                m_StatusText = "Model import failed while applying the imported object.";
                return;
            }

            if (Primitive* importedPrimitive = m_State.FindPrimitive(importedPrimitiveId)) {
                RenderBounds localBounds {};
                RenderBounds worldBounds {};
                if (TryBuildImportedPrimitiveBounds(*importedPrimitive, localBounds, worldBounds)) {
                    FrameEditorCameraToBounds(worldBounds);
                }
            }

            ResetAuxiliaryAccumulation();
            m_LastAuxiliaryTransportEpoch = m_State.GetTransportEpoch();
            InvalidateCompiledSceneTargets();
            m_ViewportController.Reset();
            m_ImportTask.state = Async::TaskState::Idle;
            m_StatusText = std::string("Imported ")
                + (diagnostics.assetLabel.empty() ? "model" : diagnostics.assetLabel)
                + " ("
                + std::to_string(diagnostics.triangleCount)
                + " triangles, "
                + ImportedModelScaleModeLabel(diagnostics.scaleMode)
                + ", "
                + std::to_string(static_cast<int>(std::round(importMs)))
                + " ms).";
        });
    });
}

void RenderTab::RenderImportModelPopup() {
    if (m_ImportDialog.open) {
        ImGui::OpenPopup("Import glTF Model");
        m_ImportDialog.open = false;
    }

    bool open = true;
    if (!ImGui::BeginPopupModal("Import glTF Model", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Import `%s` as one authorable scene object.", std::filesystem::path(m_ImportDialog.filePath).filename().string().c_str());
    ImGui::Spacing();

    int scaleMode = static_cast<int>(m_ImportDialog.options.scaleMode);
    if (ImGui::RadioButton("Keep Original Scale", scaleMode == static_cast<int>(ImportedModelScaleMode::KeepOriginal))) {
        m_ImportDialog.options.scaleMode = ImportedModelScaleMode::KeepOriginal;
    }
    if (ImGui::RadioButton("Auto Fit Scene", scaleMode == static_cast<int>(ImportedModelScaleMode::AutoFit))) {
        m_ImportDialog.options.scaleMode = ImportedModelScaleMode::AutoFit;
    }
    ImGui::TextDisabled("Keep Original uses glTF world scale. Auto Fit scales the imported bounds so the widest extent becomes 2.0 world units.");
    ImGui::TextDisabled("Both options rebase the imported object to center it in X/Z and place its lowest point on Y=0.");

    if (Async::IsBusy(m_ImportTask.state)) {
        ImGui::Spacing();
        ImGui::TextDisabled("Import in progress...");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool importBusy = Async::IsBusy(m_ImportTask.state);
    ImGui::BeginDisabled(importBusy);
    if (ImGui::Button("Import", ImVec2(120.0f, 0.0f))) {
        SubmitPendingModelImport();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        m_ImportDialog.filePath.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void RenderTab::RenderBody() {
    ImGui::BeginChild(
        "RenderWorkspaceHost",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    m_RenderDockId = ImGui::GetID("RenderDockSpace");
    ImGui::DockSpace(m_RenderDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (m_ResetRenderDockLayout) {
        m_RenderDockLayoutBuilt = false;
        m_ResetRenderDockLayout = false;
    }

    if (!m_RenderDockLayoutBuilt) {
        ImGui::DockBuilderRemoveNode(m_RenderDockId);
        ImGui::DockBuilderAddNode(m_RenderDockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(m_RenderDockId, ImGui::GetWindowSize());

        ImGuiID mainDockId = m_RenderDockId;
        ImGuiID rightDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Right, 0.28f, nullptr, &mainDockId);
        ImGuiID outlinerDockId = 0;
        ImGuiID inspectorDockId = 0;
        ImGui::DockBuilderSplitNode(rightDockId, ImGuiDir_Down, 0.58f, &inspectorDockId, &outlinerDockId);

        ImGui::DockBuilderDockWindow("Viewport", mainDockId);
        ImGui::DockBuilderDockWindow("Outliner", outlinerDockId);
        ImGui::DockBuilderDockWindow("Inspector", inspectorDockId);
        ImGui::DockBuilderFinish(m_RenderDockId);
        m_RenderDockLayoutBuilt = true;
    }

    ImGui::EndChild();

    RenderViewportPanel();
    RenderOutlinerPanel();
    RenderInspectorPanel();
}

void RenderTab::RenderInspectorPanel() {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    if (m_TestScenePreviewEnabled) {
        ImGui::TextWrapped(
            "Validation scene active: %s. Project scene authoring is read-only while the test scene override is on.",
            GetRenderValidationSceneLabel(m_TestScenePreviewId));
        ImGui::TextDisabled("Use the camera, render settings, and validation-scene controls to tune visibility.");
        ImGui::Separator();
    }

    const bool pathTraceMode = m_State.GetSettings().viewMode == ViewMode::PathTrace;
    Selection selection = m_State.GetSelection();
    if (m_VirtualSelection == VirtualSelection::Fog) {
        ImGui::TextDisabled("Fog");
        ImGui::Separator();
        if (ImGui::Checkbox("Enabled", &m_State.GetSceneMetadata().fogEnabled)) {
            m_State.MarkTransportDirty("Fog enabled state changed.");
        }
        if (EditColor3Field("Color", m_State.GetSceneMetadata().fogColor)) {
            m_State.MarkTransportDirty("Fog color changed.");
        }
        if (ImGui::SliderFloat("Density", &m_State.GetSceneMetadata().fogDensity, 0.0f, 1.0f)) {
            m_State.MarkTransportDirty("Fog density changed.");
        }
        if (ImGui::SliderFloat("Anisotropy", &m_State.GetSceneMetadata().fogAnisotropy, -0.95f, 0.95f)) {
            m_State.MarkTransportDirty("Fog anisotropy changed.");
        }
        ImGui::TextDisabled("Fog is represented as a scene item now and can become a movable object in a later pass.");
    } else if (m_VirtualSelection == VirtualSelection::Environment) {
        ImGui::TextDisabled("Environment");
        ImGui::Separator();
        if (ImGui::Checkbox("Enabled", &m_State.GetSceneMetadata().environmentEnabled)) {
            m_State.MarkTransportDirty("Environment enabled state changed.");
        }
        if (ImGui::SliderFloat("Intensity", &m_State.GetSceneMetadata().environmentIntensity, 0.0f, 4.0f)) {
            m_State.MarkTransportDirty("Environment intensity changed.", RenderContracts::ResetClass::PartialAccumulation);
        }
    } else if (selection.type == SelectionType::None) {
        ImGui::TextDisabled("Select an object, light, camera, fog, or environment item to edit it.");
    } else if (selection.type == SelectionType::Camera) {
        ImGui::TextDisabled("Camera");

        if (ImGui::Button("Open Camera Preview")) {
            m_ShowCameraWindow = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Look Through Camera")) {
            ResetEditorCameraFromRenderCamera();
            m_State.AccessAccumulationManager().ResetForScene(m_State.GetSettings());
            m_LastViewportError.clear();
        }
        ImGui::Separator();

        if (EditVec3Field("Position", m_State.GetCamera().position)) {
            m_State.MarkTransportDirty("Camera position changed.");
        }
        if (ImGui::SliderFloat("Yaw", &m_State.GetCamera().yawDegrees, -180.0f, 180.0f)) {
            m_State.MarkTransportDirty("Camera yaw changed.");
        }
        if (ImGui::SliderFloat("Pitch", &m_State.GetCamera().pitchDegrees, -89.0f, 89.0f)) {
            m_State.MarkTransportDirty("Camera pitch changed.");
        }
        if (ImGui::SliderFloat("FOV", &m_State.GetCamera().fieldOfViewDegrees, 20.0f, 110.0f)) {
            m_State.MarkTransportDirty("Camera FOV changed.");
        }
        if (ImGui::SliderFloat("Focus Distance", &m_State.GetCamera().focusDistance, 0.1f, 20.0f)) {
            m_State.MarkTransportDirty("Camera focus distance changed.");
        }
        if (ImGui::SliderFloat("Aperture Radius", &m_State.GetCamera().apertureRadius, 0.0f, 1.0f)) {
            m_State.MarkTransportDirty("Camera aperture changed.");
        }
        if (ImGui::SliderFloat("Exposure", &m_State.GetCamera().exposure, 0.1f, 4.0f)) {
            m_State.MarkDisplayDirty("Camera exposure changed.");
        }
        if (ImGui::SliderFloat("White Balance (K)", &m_State.GetCamera().whiteBalanceTemperature, 1000.0f, 15000.0f, "%.0f K")) {
            m_State.MarkDisplayDirty("Camera white balance changed.");
        }
        if (ImGui::SliderFloat("Move Speed", &m_State.GetCamera().movementSpeed, 1.0f, 20.0f)) {
            m_State.MarkDisplayDirty("Camera move speed changed.");
        }
    } else if (selection.type == SelectionType::Primitive) {
        Primitive* primitive = m_State.FindPrimitive(selection.id);
        if (primitive != nullptr) {
            ImGui::TextDisabled("%s", PrimitiveTypeLabel(primitive->type));
            const bool previewLocked = m_TestScenePreviewEnabled;
            if (previewLocked) {
                ImGui::BeginDisabled();
            }
            if (EditStringField("Name", primitive->name)) {
                m_State.MarkDisplayDirty("Primitive renamed.");
            }

            if (EditVec3Field("Position", primitive->transform.translation)) {
                m_State.MarkTransportDirty("Primitive moved.");
            }
            if (EditVec3Field("Rotation", primitive->transform.rotationDegrees, 0.5f)) {
                m_State.MarkTransportDirty("Primitive rotated.");
            }
            if (EditVec3Field("Scale", primitive->transform.scale, 0.05f)) {
                primitive->transform.scale.x = std::max(0.05f, primitive->transform.scale.x);
                primitive->transform.scale.y = std::max(0.05f, primitive->transform.scale.y);
                primitive->transform.scale.z = std::max(0.05f, primitive->transform.scale.z);
                m_State.MarkTransportDirty("Primitive scaled.");
            }
            if (EditColor3Field("Object Tint", primitive->colorTint)) {
                m_State.MarkTransportDirty("Primitive tint changed.", RenderContracts::ResetClass::PartialAccumulation);
            }
            ImGui::TextDisabled("Object tint multiplies the assigned material on this object only.");

            if (primitive->type == PrimitiveType::ImportedMesh) {
                ImGui::Spacing();
                ImGui::Text("Imported Model");
                ImGui::Separator();

                const auto& importedMeshes = m_State.GetImportedMeshes();
                const RenderMeshDefinition* sourceMesh =
                    primitive->meshIndex >= 0 && primitive->meshIndex < static_cast<int>(importedMeshes.size())
                        ? &importedMeshes[static_cast<std::size_t>(primitive->meshIndex)]
                        : nullptr;
                const RenderImportedAsset* sourceAsset = nullptr;
                if (sourceMesh != nullptr) {
                    const auto& importedAssets = m_State.GetImportedAssets();
                    if (sourceMesh->sourceAssetIndex >= 0 &&
                        sourceMesh->sourceAssetIndex < static_cast<int>(importedAssets.size())) {
                        sourceAsset = &importedAssets[static_cast<std::size_t>(sourceMesh->sourceAssetIndex)];
                    }
                }

                if (sourceMesh != nullptr) {
                    ImGui::TextDisabled("Source Asset: %s", primitive->importedAssetLabel.empty() ? sourceMesh->name.c_str() : primitive->importedAssetLabel.c_str());
                    ImGui::TextDisabled("Source Mesh: %s", sourceMesh->sourceMeshName.empty() ? sourceMesh->name.c_str() : sourceMesh->sourceMeshName.c_str());
                    ImGui::TextDisabled("Parts: %d", primitive->importedPartCount);
                    ImGui::TextDisabled("Triangles: %d", static_cast<int>(sourceMesh->triangles.size()));
                } else {
                    ImGui::TextDisabled("Source mesh data is missing.");
                }
                if (sourceAsset != nullptr) {
                    ImGui::TextDisabled("Asset: %s", sourceAsset->name.c_str());
                }
                ImGui::TextDisabled("Imported Materials: %d", primitive->importedMaterialCount);
                ImGui::TextDisabled(
                    "Scale Mode: %s  |  Applied Scale: %.4f",
                    ImportedModelScaleModeLabel(primitive->importedScaleMode),
                    primitive->importedAppliedScale);

                RenderBounds localBounds {};
                RenderBounds worldBounds {};
                if (TryBuildImportedPrimitiveBounds(*primitive, localBounds, worldBounds)) {
                    ImGui::TextDisabled(
                        "Local Bounds Min: %.3f, %.3f, %.3f",
                        localBounds.min.x,
                        localBounds.min.y,
                        localBounds.min.z);
                    ImGui::TextDisabled(
                        "Local Bounds Max: %.3f, %.3f, %.3f",
                        localBounds.max.x,
                        localBounds.max.y,
                        localBounds.max.z);
                    ImGui::TextDisabled(
                        "World Bounds Min: %.3f, %.3f, %.3f",
                        worldBounds.min.x,
                        worldBounds.min.y,
                        worldBounds.min.z);
                    ImGui::TextDisabled(
                        "World Bounds Max: %.3f, %.3f, %.3f",
                        worldBounds.max.x,
                        worldBounds.max.y,
                        worldBounds.max.z);
                    if (ImGui::Button("Frame Selection", ImVec2(150.0f, 0.0f))) {
                        FrameEditorCameraToBounds(worldBounds);
                        m_State.AccessAccumulationManager().ResetForScene(m_State.GetSettings());
                        m_LastViewportError.clear();
                    }
                }
                if (!primitive->importedWarningText.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.52f, 1.0f), "Import Warning: %s", primitive->importedWarningText.c_str());
                }

                int materialMode = static_cast<int>(primitive->importedMaterialMode);
                if (ImGui::Combo("Material Source", &materialMode, "Use Imported\0Override Only\0Blend Imported + Override\0")) {
                    primitive->importedMaterialMode = static_cast<ImportedMaterialMode>(materialMode);
                    m_State.MarkTransportDirty(
                        "Imported material mode changed.",
                        RenderContracts::ResetClass::FullAccumulation,
                        RenderContracts::DirtyFlags::SceneStructure | RenderContracts::DirtyFlags::SceneContent | RenderContracts::DirtyFlags::Viewport);
                }

                if (primitive->importedMaterialMode != ImportedMaterialMode::UseImported) {
                    const Material* assignedMaterial = m_State.FindMaterial(primitive->materialId);
                    const char* previewName = assignedMaterial != nullptr ? assignedMaterial->name.c_str() : "Select Material";
                    if (ImGui::BeginCombo("Override Material", previewName)) {
                        for (const Material& candidate : m_State.GetMaterials()) {
                            const bool selected = candidate.id == primitive->materialId;
                            if (ImGui::Selectable(candidate.name.c_str(), selected)) {
                                primitive->materialId = candidate.id;
                                m_State.MarkTransportDirty(
                                    "Imported override material changed.",
                                    RenderContracts::ResetClass::FullAccumulation,
                                    RenderContracts::DirtyFlags::SceneStructure | RenderContracts::DirtyFlags::SceneContent | RenderContracts::DirtyFlags::Viewport);
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                if (primitive->importedMaterialMode == ImportedMaterialMode::Blend) {
                    float blendAmount = primitive->importedMaterialBlend;
                    if (ImGui::SliderFloat("Imported Blend", &blendAmount, 0.0f, 1.0f)) {
                        primitive->importedMaterialBlend = blendAmount;
                        m_State.MarkTransportDirty(
                            "Imported material blend changed.",
                            RenderContracts::ResetClass::FullAccumulation,
                            RenderContracts::DirtyFlags::SceneStructure | RenderContracts::DirtyFlags::SceneContent | RenderContracts::DirtyFlags::Viewport);
                    }
                    ImGui::TextDisabled("0 uses the baked model material. 1 uses the selected override material.");
                } else if (primitive->importedMaterialMode == ImportedMaterialMode::OverrideOnly) {
                    ImGui::TextDisabled("All sub-mesh materials are replaced with the selected override material.");
                } else {
                    ImGui::TextDisabled("Using baked model materials and textures exactly as imported.");
                }
            }

            const bool showMaterialEditor =
                primitive->type != PrimitiveType::ImportedMesh ||
                primitive->importedMaterialMode != ImportedMaterialMode::UseImported;
            if (showMaterialEditor) {
            Material* material = nullptr;
            Material::Layer* clearCoatLayer = nullptr;
            Material::Layer* baseLayer = nullptr;
            auto refreshMaterialState = [&]() {
                material = m_State.FindMaterial(primitive->materialId);
                if (material == nullptr) {
                    clearCoatLayer = nullptr;
                    baseLayer = nullptr;
                    return false;
                }
                if (material->layers.empty()) {
                    SyncMaterialLayersFromLegacy(*material);
                } else {
                    SyncLegacyMaterialFromLayers(*material);
                }
                clearCoatLayer = FindMaterialLayer(*material, MaterialLayerType::ClearCoat);
                baseLayer = FindBaseMaterialLayer(*material);
                return true;
            };
            auto ensureEditableMaterial = [&]() {
                Material* editable = m_State.EnsureEditableMaterialForPrimitive(primitive->id);
                if (editable == nullptr) {
                    return false;
                }
                return refreshMaterialState();
            };
            auto markLayoutDirty = [this, primitive](const char* reason) {
                if (Material* activeMaterial = m_State.FindMaterial(primitive->materialId)) {
                    SyncLegacyMaterialFromLayers(*activeMaterial);
                }
                m_State.MarkTransportDirty(
                    reason,
                    RenderContracts::ResetClass::FullAccumulation,
                    RenderContracts::DirtyFlags::SceneStructure | RenderContracts::DirtyFlags::SceneContent | RenderContracts::DirtyFlags::Viewport);
            };
            auto markMaterialPartial = [this, primitive](const char* reason) {
                if (Material* activeMaterial = m_State.FindMaterial(primitive->materialId)) {
                    UpdateLegacyMaterialFieldsFromLayerStack(*activeMaterial);
                }
                m_State.MarkTransportDirty(reason, RenderContracts::ResetClass::PartialAccumulation);
            };
            auto markMaterialFull = [this, primitive](const char* reason) {
                if (Material* activeMaterial = m_State.FindMaterial(primitive->materialId)) {
                    UpdateLegacyMaterialFieldsFromLayerStack(*activeMaterial);
                }
                m_State.MarkTransportDirty(reason);
            };

            if (refreshMaterialState()) {
                ImGui::Spacing();
                ImGui::Text("Material");
                ImGui::Separator();
                ImGui::TextDisabled("Base Surface keeps object editing simple. Saved copies are the reusable named materials.");

                const bool ptThinWallEnabled = primitive->type == PrimitiveType::Plane;
                const int materialUserCount = m_State.CountMaterialUsers(material->id);
                const bool materialShared = materialUserCount > 1;

                if (baseLayer != nullptr) {
                    int baseSurfaceType = BaseLayerComboIndex(*baseLayer);
                    if (ImGui::Combo("Base Surface", &baseSurfaceType, BaseSurfaceComboItems())) {
                        if (ensureEditableMaterial() && baseLayer != nullptr) {
                            ApplyBaseLayerType(*baseLayer, baseSurfaceType);
                            markMaterialFull("Base surface changed.");
                            refreshMaterialState();
                        }
                    }
                    ImGui::TextDisabled("Choose the underlying model first. Emission stays separate below.");
                }

                const char* savedCopyPreview = material->isTemplate ? "None (using starter base)" : material->name.c_str();
                if (ImGui::BeginCombo("Apply Saved Copy", savedCopyPreview)) {
                    bool anySavedCopies = false;
                    for (const Material& candidate : m_State.GetMaterials()) {
                        if (candidate.isTemplate) {
                            continue;
                        }
                        anySavedCopies = true;
                        const bool selected = candidate.id == primitive->materialId;
                        if (ImGui::Selectable(candidate.name.c_str(), selected)) {
                            primitive->materialId = candidate.id;
                            m_State.MarkTransportDirty("Saved material applied.", RenderContracts::ResetClass::PartialAccumulation);
                            refreshMaterialState();
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    if (!anySavedCopies) {
                        ImGui::TextDisabled("No saved copies yet.");
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button(material->isTemplate ? "Create Saved Copy" : "Duplicate Saved Copy")) {
                    if (m_State.DuplicateMaterialForPrimitive(primitive->id) != nullptr) {
                        m_State.MarkTransportDirty("Saved material copy created.", RenderContracts::ResetClass::PartialAccumulation);
                        refreshMaterialState();
                    }
                }

                if (material->isTemplate) {
                    ImGui::TextDisabled(
                        "Using starter base template: %s. The first object edit will automatically branch into a saved copy.",
                        material->name.c_str());
                } else if (materialShared) {
                    ImGui::TextDisabled(
                        "Saved copy '%s' is shared by %d objects. The next edit on this object will create its own copy.",
                        material->name.c_str(),
                        materialUserCount);
                } else {
                    ImGui::TextDisabled("This object already owns its saved copy.");
                }

                std::string materialName = material->name;
                if (EditStringField("Saved Copy Name", materialName)) {
                    if (ensureEditableMaterial() && material != nullptr) {
                        material->name = materialName;
                        m_State.MarkDisplayDirty("Material renamed.");
                        refreshMaterialState();
                    }
                }

                ImGui::Spacing();
                ImGui::Text("Layer Stack");
                ImGui::Separator();
                ImGui::TextDisabled("Current stack is always Clear Coat above one base layer.");

                const bool hasClearCoat = clearCoatLayer != nullptr;
                if (!hasClearCoat) {
                    if (ImGui::Button("Add Clear Coat")) {
                        if (ensureEditableMaterial() && material != nullptr) {
                            material->layers.insert(material->layers.begin(), MakeDefaultClearCoatLayer(0.85f));
                            markLayoutDirty("Clear coat layer added.");
                            refreshMaterialState();
                        }
                    }
                } else {
                    if (ImGui::Button("Remove Clear Coat")) {
                        if (ensureEditableMaterial() && material != nullptr) {
                            material->layers.erase(
                                std::remove_if(
                                    material->layers.begin(),
                                    material->layers.end(),
                                    [](const Material::Layer& layer) {
                                        return layer.type == MaterialLayerType::ClearCoat;
                                    }),
                                material->layers.end());
                            markLayoutDirty("Clear coat layer removed.");
                            refreshMaterialState();
                        }
                    }
                }

                if (clearCoatLayer != nullptr) {
                    ImGui::Spacing();
                    ImGui::PushID("ClearCoatLayer");
                    ImGui::BeginDisabled();
                    ImGui::ArrowButton("MoveUp", ImGuiDir_Up);
                    ImGui::SameLine();
                    ImGui::ArrowButton("MoveDown", ImGuiDir_Down);
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextDisabled("Layer 1  |  %s", MaterialLayerTypeLabel(clearCoatLayer->type));

                    float clearCoatWeight = clearCoatLayer->weight;
                    if (ImGui::SliderFloat("Clear Coat", &clearCoatWeight, 0.0f, 1.0f)) {
                        if (ensureEditableMaterial() && clearCoatLayer != nullptr) {
                            clearCoatLayer->weight = clearCoatWeight;
                            markMaterialPartial("Clear coat strength changed.");
                            refreshMaterialState();
                        }
                    }

                    float clearCoatRoughness = clearCoatLayer->roughness;
                    if (ImGui::SliderFloat("Coat Roughness", &clearCoatRoughness, 0.0f, 0.25f)) {
                        if (ensureEditableMaterial() && clearCoatLayer != nullptr) {
                            clearCoatLayer->roughness = clearCoatRoughness;
                            markMaterialPartial("Clear coat roughness changed.");
                            refreshMaterialState();
                        }
                    }
                    ImGui::PopID();
                }

                if (baseLayer != nullptr) {
                    ImGui::Spacing();
                    ImGui::PushID("BaseLayer");
                    ImGui::BeginDisabled();
                    ImGui::ArrowButton("MoveUp", ImGuiDir_Up);
                    ImGui::SameLine();
                    ImGui::ArrowButton("MoveDown", ImGuiDir_Down);
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextDisabled(
                        "Layer %d  |  %s",
                        clearCoatLayer != nullptr ? 2 : 1,
                        MaterialLayerTypeLabel(baseLayer->type));

                    ImGui::TextDisabled(
                        "Surface Model: %s",
                        baseLayer->type == MaterialLayerType::BaseDielectric
                            ? "Glass / dielectric"
                            : (baseLayer->type == MaterialLayerType::BaseMetal ? "Metal / conductor" : "No material / matte"));

                    Vec3 baseColor = baseLayer->color;
                    if (EditColor3Field("Base Color", baseColor)) {
                        if (ensureEditableMaterial() && baseLayer != nullptr) {
                            baseLayer->color = baseColor;
                            markMaterialPartial("Material color changed.");
                            refreshMaterialState();
                        }
                    }

                    if (baseLayer->type == MaterialLayerType::BaseDiffuse ||
                        baseLayer->type == MaterialLayerType::BaseMetal) {
                        float roughness = baseLayer->roughness;
                        if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f)) {
                            if (ensureEditableMaterial() && baseLayer != nullptr) {
                                baseLayer->roughness = roughness;
                                markMaterialPartial("Material roughness changed.");
                                refreshMaterialState();
                            }
                        }
                    }

                    if (baseLayer->type == MaterialLayerType::BaseDielectric) {
                        float transmission = baseLayer->transmission;
                        if (ImGui::SliderFloat("Transmission", &transmission, 0.0f, 1.0f)) {
                            if (ensureEditableMaterial() && baseLayer != nullptr) {
                                baseLayer->transmission = transmission;
                                markMaterialPartial("Material transmission changed.");
                                refreshMaterialState();
                            }
                        }

                        float ior = baseLayer->ior;
                        if (ImGui::SliderFloat("IOR", &ior, 1.0f, 2.6f)) {
                            if (ensureEditableMaterial() && baseLayer != nullptr) {
                                baseLayer->ior = ior;
                                markMaterialPartial("Material IOR changed.");
                                refreshMaterialState();
                            }
                        }

                        if (!pathTraceMode || ptThinWallEnabled) {
                            bool thinWalled = baseLayer->thinWalled;
                            if (ImGui::Checkbox("Thin Walled", &thinWalled)) {
                                if (ensureEditableMaterial() && baseLayer != nullptr) {
                                    baseLayer->thinWalled = thinWalled;
                                    markMaterialPartial("Thin wall state changed.");
                                    refreshMaterialState();
                                }
                            }
                        } else {
                            ImGui::BeginDisabled(true);
                            bool thinWalled = baseLayer->thinWalled;
                            ImGui::Checkbox("Thin Walled", &thinWalled);
                            ImGui::EndDisabled();
                            ImGui::TextDisabled("Thin-walled PT is currently sheet-only; closed primitives compile as solid glass.");
                        }

                        float transmissionRoughness = baseLayer->transmissionRoughness;
                        if (ImGui::SliderFloat("Transmission Roughness", &transmissionRoughness, 0.0f, 1.0f)) {
                            if (ensureEditableMaterial() && baseLayer != nullptr) {
                                baseLayer->transmissionRoughness = transmissionRoughness;
                                markMaterialPartial("Transmission roughness changed.");
                                refreshMaterialState();
                            }
                        }

                        Vec3 absorptionColor = material->absorptionColor;
                        if (EditColor3Field("Absorption Color", absorptionColor)) {
                            if (ensureEditableMaterial() && material != nullptr) {
                                material->absorptionColor = absorptionColor;
                                markMaterialPartial("Absorption color changed.");
                                refreshMaterialState();
                            }
                        }

                        float absorptionDistance = material->absorptionDistance;
                        if (ImGui::SliderFloat("Absorption Distance", &absorptionDistance, 0.1f, 10.0f)) {
                            if (ensureEditableMaterial() && material != nullptr) {
                                material->absorptionDistance = absorptionDistance;
                                markMaterialPartial("Absorption distance changed.");
                                refreshMaterialState();
                            }
                        }
                    }
                    ImGui::PopID();
                }

                Vec3 emissionColor = material->emissionColor;
                if (EditColor3Field("Emission Color", emissionColor)) {
                    if (ensureEditableMaterial() && material != nullptr) {
                        material->emissionColor = emissionColor;
                        markMaterialPartial("Emission color changed.");
                        refreshMaterialState();
                    }
                }

                float emissionStrength = material->emissionStrength;
                if (ImGui::SliderFloat("Emission Strength", &emissionStrength, 0.0f, 24.0f)) {
                    if (ensureEditableMaterial() && material != nullptr) {
                        material->emissionStrength = emissionStrength;
                        markMaterialPartial("Emission strength changed.");
                        refreshMaterialState();
                    }
                }

                if (pathTraceMode) {
                    ImGui::TextDisabled("PT layer foundation now supports clear coat over matte, metal, and glass bases.");
                }
                ImGui::TextDisabled("Thin film and subsurface remain deferred until later slices.");
            }
            }
            if (previewLocked) {
                ImGui::EndDisabled();
            }
        }
    } else if (selection.type == SelectionType::Light) {
        Light* light = m_State.FindLight(selection.id);
        if (light != nullptr) {
            const bool previewLocked = m_TestScenePreviewEnabled;
            if (previewLocked) {
                ImGui::BeginDisabled();
            }
            int lightType = static_cast<int>(light->type);
            if (ImGui::Combo("Type", &lightType, "Point\0Spot\0Area\0Directional\0Laser\0")) {
                light->type = static_cast<LightType>(lightType);
                if (light->type == LightType::Laser) {
                    light->color = GetLaserDisplayColor(light->laserWavelengthNm);
                }
                m_State.MarkTransportDirty("Light type changed.");
            }
            if (EditStringField("Name", light->name)) {
                m_State.MarkDisplayDirty("Light renamed.");
            }
            if (EditVec3Field("Position", light->transform.translation)) {
                m_State.MarkTransportDirty("Light moved.");
            }
            if (EditVec3Field("Rotation", light->transform.rotationDegrees, 0.5f)) {
                m_State.MarkTransportDirty("Light rotated.");
            }
            if (light->type == LightType::Laser) {
                char wavelengthLabel[32] {};
                std::snprintf(wavelengthLabel, sizeof(wavelengthLabel), "%.0f nm", light->laserWavelengthNm);
                DrawReadOnlyColorSwatch("Spectral Swatch", GetLightDisplayColor(*light), wavelengthLabel);
                ImGui::TextDisabled("Laser color follows wavelength; the stored tint stays compatibility-only.");
            } else if (EditColor3Field("Color", light->color)) {
                m_State.MarkTransportDirty("Light color changed.", RenderContracts::ResetClass::PartialAccumulation);
            }
            const float intensityLimit = light->type == LightType::Laser ? 400.0f : 80.0f;
            if (ImGui::SliderFloat("Intensity", &light->intensity, 0.0f, intensityLimit)) {
                m_State.MarkTransportDirty("Light intensity changed.", RenderContracts::ResetClass::PartialAccumulation);
            }
            if (light->type == LightType::Point ||
                light->type == LightType::Spot ||
                light->type == LightType::Laser) {
                if (ImGui::SliderFloat("Range", &light->range, 0.1f, 60.0f)) {
                    m_State.MarkTransportDirty("Light range changed.");
                }
            }
            if (light->type == LightType::Area) {
                if (ImGui::SliderFloat2("Area Size", &light->areaSize.x, 0.1f, 10.0f)) {
                    m_State.MarkTransportDirty("Area light size changed.");
                }
            }
            if (light->type == LightType::Spot) {
                if (ImGui::SliderFloat("Inner Cone", &light->innerConeDegrees, 1.0f, 80.0f)) {
                    light->outerConeDegrees = std::max(light->outerConeDegrees, light->innerConeDegrees);
                    m_State.MarkTransportDirty("Spot inner cone changed.");
                }
                if (ImGui::SliderFloat("Outer Cone", &light->outerConeDegrees, 1.0f, 89.0f)) {
                    light->outerConeDegrees = std::max(light->outerConeDegrees, light->innerConeDegrees);
                    m_State.MarkTransportDirty("Spot outer cone changed.");
                }
            }
            if (ImGui::Checkbox("Enabled", &light->enabled)) {
                m_State.MarkTransportDirty("Light enabled state changed.");
            }
            if (light->type == LightType::Laser) {
                ImGui::Spacing();
                ImGui::TextDisabled("Laser spectrum is driven by wavelength and linewidth.");
                ImGui::TextDisabled("A realistic laser is only visible on surfaces or through fog / scattering media.");
                if (ImGui::SliderFloat("Wavelength (nm)", &light->laserWavelengthNm, 380.0f, 720.0f, "%.0f nm")) {
                    light->color = GetLaserDisplayColor(light->laserWavelengthNm);
                    m_State.MarkTransportDirty("Laser wavelength changed.", RenderContracts::ResetClass::PartialAccumulation);
                }
                if (ImGui::SliderFloat("Linewidth (nm)", &light->laserLinewidthNm, 0.1f, 20.0f, "%.1f nm")) {
                    m_State.MarkTransportDirty("Laser linewidth changed.", RenderContracts::ResetClass::PartialAccumulation);
                }
                if (ImGui::SliderFloat("Aperture Radius", &light->laserApertureRadius, 0.0005f, 0.25f)) {
                    m_State.MarkTransportDirty("Laser aperture radius changed.");
                }
                if (ImGui::SliderFloat("Beam Waist Radius", &light->laserBeamWaistRadius, 0.0005f, 0.25f)) {
                    m_State.MarkTransportDirty("Laser beam waist changed.");
                }
                if (ImGui::SliderFloat("Beam Quality (M^2)", &light->laserBeamQuality, 1.0f, 10.0f)) {
                    m_State.MarkTransportDirty("Laser beam quality changed.");
                }
            } else if (light->type == LightType::Area) {
                ImGui::Spacing();
                ImGui::TextDisabled("Area lights emit across their rectangular surface.");
            } else if (light->type == LightType::Spot) {
                ImGui::Spacing();
                ImGui::TextDisabled("Spot lights use range plus inner and outer cone falloff.");
            } else if (light->type == LightType::Directional) {
                ImGui::Spacing();
                ImGui::TextDisabled("Directional lights are infinite-distance emitters with no local range.");
            }
            if (previewLocked) {
                ImGui::EndDisabled();
            }
        }
    } else {
        ImGui::TextWrapped("Select a primitive, light, or the camera in the viewport or scene list.");
    }

    ImGui::Spacing();
    ImGui::Text("Active Constraints");
    ImGui::Separator();
    ImGui::BulletText("Viewport now renders from the compiled 3D scene contract.");
    ImGui::BulletText("CPU ray/BVH picking is authoritative for authoring selection.");
    ImGui::BulletText("Path Trace uses the same immutable snapshot and dual-slot accumulation contract.");
    ImGui::BulletText("Project payload remains Library-compatible and snapshot-versioned.");

    ImGui::End();
}

void RenderTab::RenderViewportPanel() {
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Viewport", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x = std::max(canvasSize.x, 64.0f);
    canvasSize.y = std::max(canvasSize.y, 64.0f);

    RenderFoundation::Settings& viewportSettings = m_State.GetSettings();
    const SelectionType selectionType = m_State.GetSelection().type;
    if (!SelectionSupportsScale(selectionType) && viewportSettings.transformMode == TransformMode::Scale) {
        viewportSettings.transformMode = TransformMode::Translate;
    }

    viewportSettings.viewportResolutionOverrideEnabled = false;
    viewportSettings.viewportPerformanceMode = ViewportPerformanceMode::Exact;
    viewportSettings.maxBounceCount = 512;
    viewportSettings.pathTraceTerminationMode = PathTraceTerminationMode::Smart;

    const int renderWidth = std::max(1, static_cast<int>(std::round(canvasSize.x)));
    const int renderHeight = std::max(1, static_cast<int>(std::round(canvasSize.y)));

    m_State.AccessAccumulationManager().UpdateViewportExtent(
        renderWidth,
        renderHeight,
        viewportSettings);
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const ImRect panelRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y));
    const ImRect imageRect = panelRect;
    if (imageRect.GetWidth() > 1.0f && imageRect.GetHeight() > 1.0f) {
        m_LastViewportImageAspect = imageRect.GetWidth() / imageRect.GetHeight();
    }
    const bool compiledReady = EnsureViewportSceneCompilation();
    const bool proxyActive = IsViewportProxyActive();
    unsigned int viewportTextureId = 0;
    m_LastViewportUploadMs = 0.0;
    if (compiledReady && !proxyActive) {
        std::string renderError;
        if (!m_RenderDelegator.RenderViewport(
                m_CompiledScene,
                viewportSettings,
                m_State.AccessAccumulationManager(),
                renderWidth,
                renderHeight,
                GetSelectedRuntimeObjectId(),
                viewportTextureId,
                renderError)) {
            m_LastViewportError = renderError;
        } else {
            m_LastViewportUploadMs = m_RenderDelegator.GetRasterPreviewUploadMilliseconds();
        }
    }

    const int visibleTriangleCount = m_CompiledScene.valid
        ? m_CompiledScene.scene.GetResolvedTriangleCount()
        : [&]() {
            int total = 0;
            for (const Primitive& primitive : m_State.GetPrimitives()) {
                if (primitive.type == PrimitiveType::ImportedMesh && primitive.visible) {
                    total += primitive.importedTriangleCount;
                }
            }
            return total;
        }();
    const double exactCostMs = m_ViewportCompile.lastCompileMs + m_LastViewportUploadMs;
    (void)exactCostMs;

    ImGui::Dummy(canvasSize);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    DrawViewportBackground(drawList, panelRect, m_State);
    if (viewportTextureId != 0) {
        drawList->AddImage(
            (ImTextureID)(intptr_t)viewportTextureId,
            imageRect.Min,
            imageRect.Max,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
        drawList->AddRect(imageRect.Min, imageRect.Max, IM_COL32(255, 255, 255, 34), 0.0f, 0, 1.0f);
    } else if (proxyActive) {
        DrawViewportProxyPreview(drawList, imageRect);
    }

    ImGui::SetCursorScreenPos(imageRect.Min);
    ImGui::InvisibleButton(
        "RenderViewportCanvas",
        imageRect.GetSize(),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    const RenderContracts::ViewportInputFrame inputFrame {
        imageRect,
        ImGui::GetIO().MousePos,
        ImGui::GetIO().MouseDelta,
        ImGui::GetIO().DeltaTime,
        hovered,
        active,
        hovered || active,
        (hovered || active || m_ViewportController.IsNavigationActive()) && !ImGui::GetIO().WantTextInput,
        ImGui::IsMouseClicked(ImGuiMouseButton_Left),
        ImGui::IsMouseReleased(ImGuiMouseButton_Left),
        ImGui::IsMouseDown(ImGuiMouseButton_Left),
        ImGui::IsMouseClicked(ImGuiMouseButton_Right),
        ImGui::IsMouseReleased(ImGuiMouseButton_Right),
        ImGui::IsMouseDown(ImGuiMouseButton_Right)
    };
    RenderContracts::ViewportInputFrame interactionFrame = inputFrame;
    if (m_TestScenePreviewEnabled) {
        interactionFrame.leftClicked = false;
        interactionFrame.leftReleased = false;
        interactionFrame.leftDown = false;
    }

    if (compiledReady) {
        bool openContextMenu = false;
        const RenderContracts::SceneChangeSet changeSet =
            m_ViewportController.HandleInput(interactionFrame, m_ActiveSnapshot, m_CompiledScene, m_EditorCamera, m_State, openContextMenu);
        ApplySceneChange(changeSet);
        if (HasVirtualSelection() && interactionFrame.leftClicked) {
            m_VirtualSelection = VirtualSelection::None;
        }
        if (openContextMenu && !m_TestScenePreviewEnabled) {
            ImGui::OpenPopup("RenderViewportContext");
        }

        m_ViewportController.DrawGizmo(drawList, inputFrame, m_ActiveSnapshot, m_CompiledScene, m_EditorCamera, m_State);
    }

    for (const RenderFoundation::Light& light : m_State.GetLights()) {
        const bool selected =
            m_State.GetSelection().type == SelectionType::Light &&
            m_State.GetSelection().id == light.id;
        DrawLightOverlay(drawList, imageRect, m_EditorCamera, light, m_CompiledScene, m_ViewportController, selected);
    }

    DrawCameraFrustumOverlay(
        drawList,
        imageRect,
        m_EditorCamera,
        m_State.GetCamera(),
        m_State.GetSelection().type == SelectionType::Camera);

    DrawViewportOverlay(
        drawList,
        imageRect,
        m_State,
        m_ViewportController.IsNavigationActive(),
        m_TestScenePreviewEnabled ? GetRenderValidationSceneLabel(m_TestScenePreviewId) : nullptr);

    const std::string viewportStatus =
        std::string(ViewModeLabel(viewportSettings.viewMode)) +
        (viewportSettings.viewMode == ViewMode::PathTrace
            ? std::string(" / ") + PathTraceTransportModeLabel(viewportSettings.pathTraceTransportMode)
            : std::string()) +
        "  |  " + TransformModeLabel(viewportSettings.transformMode) +
        " / " + TransformSpaceLabel(viewportSettings.transformSpace) +
        "  |  " + std::to_string(renderWidth) + "x" + std::to_string(renderHeight) +
        " exact" +
        "  |  Samples " + std::to_string(m_State.GetAccumulatedSamples());
    const ImVec2 overlayPadding(10.0f, 7.0f);
    const ImVec2 statusTextSize = ImGui::CalcTextSize(viewportStatus.c_str());
    const ImVec2 statusMin(imageRect.Min.x + 12.0f, imageRect.Min.y + 12.0f);
    const ImVec2 statusMax(statusMin.x + statusTextSize.x + overlayPadding.x * 2.0f, statusMin.y + statusTextSize.y + overlayPadding.y * 2.0f);
    drawList->AddRectFilled(statusMin, statusMax, IM_COL32(10, 12, 16, 190), 8.0f);
    drawList->AddText(ImVec2(statusMin.x + overlayPadding.x, statusMin.y + overlayPadding.y), IM_COL32(226, 232, 242, 245), viewportStatus.c_str());

    const std::string compileStatus =
        std::string("Compile ") + Async::ToString(m_ViewportCompile.state) +
        "  |  Triangles " + std::to_string(visibleTriangleCount) +
        (proxyActive ? "  |  Proxy" : "");
    const ImVec2 compileTextSize = ImGui::CalcTextSize(compileStatus.c_str());
    const ImVec2 compileMax(imageRect.Max.x - 12.0f, imageRect.Max.y - 12.0f);
    const ImVec2 compileMin(compileMax.x - compileTextSize.x - overlayPadding.x * 2.0f, compileMax.y - compileTextSize.y - overlayPadding.y * 2.0f);
    drawList->AddRectFilled(compileMin, compileMax, IM_COL32(10, 12, 16, 170), 8.0f);
    drawList->AddText(ImVec2(compileMin.x + overlayPadding.x, compileMin.y + overlayPadding.y), IM_COL32(180, 190, 207, 230), compileStatus.c_str());

    if (!m_LastViewportError.empty()) {
        drawList->AddRectFilled(
            ImVec2(imageRect.Min.x + 12.0f, imageRect.Min.y + 12.0f),
            ImVec2(imageRect.Min.x + 420.0f, imageRect.Min.y + 56.0f),
            IM_COL32(48, 16, 16, 210),
            8.0f);
        drawList->AddText(ImVec2(imageRect.Min.x + 24.0f, imageRect.Min.y + 22.0f), IM_COL32(255, 212, 200, 255), m_LastViewportError.c_str());
    }

    if (ImGui::BeginPopup("RenderViewportContext")) {
        if (ImGui::BeginMenu("Add Primitive")) {
            if (ImGui::MenuItem("Sphere")) {
                SelectPrimitive(m_State.AddPrimitive(PrimitiveType::Sphere));
            }
            if (ImGui::MenuItem("Cube")) {
                SelectPrimitive(m_State.AddPrimitive(PrimitiveType::Cube));
            }
            if (ImGui::MenuItem("Plane")) {
                SelectPrimitive(m_State.AddPrimitive(PrimitiveType::Plane));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add Light")) {
            if (ImGui::MenuItem("Point Light")) {
                SelectLight(m_State.AddLight(LightType::Point));
            }
            if (ImGui::MenuItem("Spot Light")) {
                SelectLight(m_State.AddLight(LightType::Spot));
            }
            if (ImGui::MenuItem("Area Light")) {
                SelectLight(m_State.AddLight(LightType::Area));
            }
            if (ImGui::MenuItem("Directional Light")) {
                SelectLight(m_State.AddLight(LightType::Directional));
            }
            if (ImGui::MenuItem("Laser Light")) {
                SelectLight(m_State.AddLight(LightType::Laser));
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Import Model...")) {
            const std::string filePath = FileDialogs::OpenRenderGltfFileDialog();
            if (!filePath.empty()) {
                BeginImportModelFlow(filePath);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Camera", nullptr, false, m_State.GetSelection().type == SelectionType::Camera)) {
            m_ShowCameraWindow = true;
        }
        if (ImGui::MenuItem("Frame View", "F")) {
            if (const Primitive* primitive = m_State.FindPrimitive(m_State.GetSelection().id)) {
                RenderBounds localBounds {};
                RenderBounds worldBounds {};
                if (TryBuildImportedPrimitiveBounds(*primitive, localBounds, worldBounds)) {
                    FrameEditorCameraToBounds(worldBounds);
                }
            }
        }
        ImGui::TextDisabled("Viewport renders 1:1 at the current panel pixel size with Smart transport.");
        ImGui::EndPopup();
    }

    ImGui::End();
}

void RenderTab::RenderOutlinerPanel() {
    if (!ImGui::Begin("Outliner")) {
        ImGui::End();
        return;
    }

    if (m_TestScenePreviewEnabled) {
        ImGui::TextWrapped(
            "Validation scene active: %s.",
            GetRenderValidationSceneLabel(m_TestScenePreviewId));
        if (ImGui::Button("Return To Project Scene")) {
            DisableTestScenePreview();
        }
        ImGui::Separator();
    }

    ImGui::BeginDisabled(m_TestScenePreviewEnabled);

    if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const Primitive& primitive : m_State.GetPrimitives()) {
            ImGui::PushID(static_cast<int>(primitive.id));
            const bool selected =
                !HasVirtualSelection() &&
                m_State.GetSelection().type == SelectionType::Primitive &&
                m_State.GetSelection().id == primitive.id;
            const std::string label = std::string(PrimitiveTypeLabel(primitive.type)) + "  " + primitive.name;
            if (ImGui::Selectable(label.c_str(), selected)) {
                SelectPrimitive(primitive.id);
            }
            if (ImGui::BeginPopupContextItem("ObjectContext")) {
                if (ImGui::MenuItem("Select")) {
                    SelectPrimitive(primitive.id);
                }
                if (ImGui::MenuItem("Frame Selection")) {
                    RenderBounds localBounds {};
                    RenderBounds worldBounds {};
                    if (TryBuildImportedPrimitiveBounds(primitive, localBounds, worldBounds)) {
                        FrameEditorCameraToBounds(worldBounds);
                    }
                }
                if (ImGui::MenuItem("Delete")) {
                    SelectPrimitive(primitive.id);
                    if (m_State.DeleteSelection()) {
                        m_StatusText = "Selection deleted.";
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const Light& light : m_State.GetLights()) {
            ImGui::PushID(static_cast<int>(light.id));
            const bool selected =
                !HasVirtualSelection() &&
                m_State.GetSelection().type == SelectionType::Light &&
                m_State.GetSelection().id == light.id;
            const std::string label = std::string(LightTypeLabel(light.type)) + "  " + light.name;
            if (ImGui::Selectable(label.c_str(), selected)) {
                SelectLight(light.id);
            }
            if (ImGui::BeginPopupContextItem("LightContext")) {
                if (ImGui::MenuItem("Select")) {
                    SelectLight(light.id);
                }
                if (ImGui::MenuItem("Delete")) {
                    SelectLight(light.id);
                    if (m_State.DeleteSelection()) {
                        m_StatusText = "Selection deleted.";
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        const bool cameraSelected =
            !HasVirtualSelection() &&
            m_State.GetSelection().type == SelectionType::Camera;
        if (ImGui::Selectable("Camera", cameraSelected)) {
            SelectCamera();
        }
        if (ImGui::BeginPopupContextItem("CameraContext")) {
            if (ImGui::MenuItem("Select")) {
                SelectCamera();
            }
            if (ImGui::MenuItem("Open Camera")) {
                SelectCamera();
                m_ShowCameraWindow = true;
            }
            if (ImGui::MenuItem("Look Through Camera")) {
                ResetEditorCameraFromRenderCamera();
                m_State.AccessAccumulationManager().ResetForScene(m_State.GetSettings());
                m_LastViewportError.clear();
            }
            ImGui::EndPopup();
        }
    }

    if (ImGui::CollapsingHeader("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
        const bool fogSelected = m_VirtualSelection == VirtualSelection::Fog;
        if (ImGui::Selectable("Fog", fogSelected)) {
            SelectVirtual(VirtualSelection::Fog);
        }
        const bool environmentSelected = m_VirtualSelection == VirtualSelection::Environment;
        if (ImGui::Selectable("Environment", environmentSelected)) {
            SelectVirtual(VirtualSelection::Environment);
        }
    }
    ImGui::EndDisabled();

    ImGui::End();
}

void RenderTab::RenderResetScenePopup() {
    if (!m_OpenResetSceneConfirm) {
        return;
    }

    if (!ImGui::BeginPopupModal("Discard Unsaved Render Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Start a new Cornell box baseline and discard the current unsaved foundation state?");
    ImGui::TextDisabled("This clears the current in-memory Cornell box baseline.");
    ImGui::Spacing();

    if (ImGui::Button("Discard And Reset", ImVec2(160.0f, 0.0f))) {
        DisableTestScenePreview();
        m_State.ResetToDefaultScene();
        ResetEditorCameraToDefault();
        ResetAuxiliaryAccumulation();
        m_LastAuxiliaryTransportEpoch = m_State.GetTransportEpoch();
        m_ViewportController.Reset();
        m_LastViewportError.clear();
        m_LatestFinalAssetFileName.clear();
        m_StatusText = "Cornell box baseline loaded.";
        m_OpenResetSceneConfirm = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Keep Editing", ImVec2(140.0f, 0.0f))) {
        m_OpenResetSceneConfirm = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void RenderTab::RenderCameraWindow() {
    if (!m_ShowCameraWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(520.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Camera", &m_ShowCameraWindow)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Select Camera")) {
        SelectCamera();
    }
    ImGui::SameLine();
    if (ImGui::Button("Look Through Camera")) {
        ResetEditorCameraFromRenderCamera();
        m_State.AccessAccumulationManager().ResetForScene(m_State.GetSettings());
        m_LastViewportError.clear();
    }

    ImGui::Separator();
    ImGui::TextWrapped("This window always shows the path-traced render-camera view. The main viewport stays an editor view, while this preview follows the scene camera object.");

    const ImVec2 previewOuterPos = ImGui::GetCursorScreenPos();
    ImVec2 previewOuterSize = ImGui::GetContentRegionAvail();
    previewOuterSize.x = std::max(previewOuterSize.x, 64.0f);
    previewOuterSize.y = std::max(previewOuterSize.y, 64.0f);
    m_CameraPreviewRenderWidth = std::max(1, static_cast<int>(std::round(previewOuterSize.x)));
    m_CameraPreviewRenderHeight = std::max(1, static_cast<int>(std::round(previewOuterSize.y)));

    const RenderFoundation::Settings previewSettings = BuildCameraPreviewSettings();
    const int previewWidth = previewSettings.resolutionX;
    const int previewHeight = previewSettings.resolutionY;
    m_CameraAccumulationManager.UpdateViewportExtent(previewWidth, previewHeight, previewSettings);

    std::string previewError;
    unsigned int previewTextureId = 0;
    const bool previewReady = EnsureCameraPreviewSceneCompilation();
    if (previewReady) {
        if (!m_RenderDelegator.RenderPathTraceTarget(
                RenderContracts::RenderDelegator::PathTraceTarget::CameraPreview,
                m_CameraPreviewScene,
                previewSettings,
                m_CameraAccumulationManager,
                previewWidth,
                previewHeight,
                true,
                previewTextureId,
                previewError)) {
            m_LastCameraPreviewError = previewError;
        }
    } else {
        if (previewError.empty()) {
            previewError = m_LastCameraPreviewError;
        }
        m_LastCameraPreviewError = previewError;
    }

    const ImRect previewOuterRect(previewOuterPos, ImVec2(previewOuterPos.x + previewOuterSize.x, previewOuterPos.y + previewOuterSize.y));
    const ImRect previewImageRect = previewOuterRect;
    ImGui::Dummy(previewOuterSize);
    ImDrawList* previewDrawList = ImGui::GetWindowDrawList();
    previewDrawList->AddRectFilled(previewOuterRect.Min, previewOuterRect.Max, IM_COL32(18, 18, 24, 255), 6.0f);
    if (previewTextureId != 0) {
        previewDrawList->AddImage(
            (ImTextureID)(intptr_t)previewTextureId,
            previewImageRect.Min,
            previewImageRect.Max,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
        previewDrawList->AddRect(previewImageRect.Min, previewImageRect.Max, IM_COL32(255, 255, 255, 34), 0.0f, 0, 1.0f);
    } else if (!m_LastCameraPreviewError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.68f, 1.0f), "%s", m_LastCameraPreviewError.c_str());
    }

    const std::string cameraStatus =
        std::string("Path Trace / ") +
        PathTraceTransportModeLabel(m_State.GetSettings().pathTraceTransportMode) +
        " / " + PathTraceTerminationModeLabel(previewSettings.pathTraceTerminationMode) +
        "  |  " + std::to_string(previewWidth) + "x" + std::to_string(previewHeight) +
        " exact  |  Samples " + std::to_string(m_CameraAccumulationManager.GetAccumulatedSamples());
    const ImVec2 overlayPadding(10.0f, 7.0f);
    const ImVec2 cameraStatusSize = ImGui::CalcTextSize(cameraStatus.c_str());
    const ImVec2 cameraStatusMin(previewImageRect.Min.x + 12.0f, previewImageRect.Min.y + 12.0f);
    const ImVec2 cameraStatusMax(
        cameraStatusMin.x + cameraStatusSize.x + overlayPadding.x * 2.0f,
        cameraStatusMin.y + cameraStatusSize.y + overlayPadding.y * 2.0f);
    previewDrawList->AddRectFilled(cameraStatusMin, cameraStatusMax, IM_COL32(10, 12, 16, 190), 8.0f);
    previewDrawList->AddText(
        ImVec2(cameraStatusMin.x + overlayPadding.x, cameraStatusMin.y + overlayPadding.y),
        IM_COL32(226, 232, 242, 245),
        cameraStatus.c_str());

    const std::string cameraCompileStatus =
        std::string("Compile ") + Async::ToString(m_CameraPreviewCompile.state) +
        "  |  Denoiser " +
        (DenoiserUsesFilteredOutput(m_State.GetSettings().denoiser)
            ? DenoiserModeLabel(m_State.GetSettings().denoiser.mode)
            : "Noisy");
    const ImVec2 cameraCompileSize = ImGui::CalcTextSize(cameraCompileStatus.c_str());
    const ImVec2 cameraCompileMax(previewImageRect.Max.x - 12.0f, previewImageRect.Max.y - 12.0f);
    const ImVec2 cameraCompileMin(
        cameraCompileMax.x - cameraCompileSize.x - overlayPadding.x * 2.0f,
        cameraCompileMax.y - cameraCompileSize.y - overlayPadding.y * 2.0f);
    previewDrawList->AddRectFilled(cameraCompileMin, cameraCompileMax, IM_COL32(10, 12, 16, 170), 8.0f);
    previewDrawList->AddText(
        ImVec2(cameraCompileMin.x + overlayPadding.x, cameraCompileMin.y + overlayPadding.y),
        IM_COL32(180, 190, 207, 230),
        cameraCompileStatus.c_str());

    ImGui::End();
}

void RenderTab::RenderFinalRenderSettingsWindow() {
    if (!m_ShowFinalRenderSettingsWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Final Render Settings", &m_ShowFinalRenderSettingsWindow)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Use this window when you want to produce a final PNG render from the scene camera. The editor viewport and camera preview stay separate from this export session.");
    ImGui::Spacing();

    if (EditStringField("Output Name##FinalRender", m_State.GetSettings().finalRender.outputName)) {
        m_State.MarkDisplayDirty("Final render output name changed.");
    }

    ImGui::Text("Resolution");
    ImGui::Separator();
    if (ImGui::Button("1080p##FinalRender")) {
        m_State.GetSettings().finalRender.resolutionX = 1920;
        m_State.GetSettings().finalRender.resolutionY = 1080;
        m_State.MarkDisplayDirty("Final render resolution changed.");
    }
    ImGui::SameLine();
    if (ImGui::Button("4K##FinalRender")) {
        m_State.GetSettings().finalRender.resolutionX = 3840;
        m_State.GetSettings().finalRender.resolutionY = 2160;
        m_State.MarkDisplayDirty("Final render resolution changed.");
    }
    if (ImGui::InputInt("Width##FinalRender", &m_State.GetSettings().finalRender.resolutionX)) {
        m_State.GetSettings().finalRender.resolutionX = std::clamp(m_State.GetSettings().finalRender.resolutionX, 320, 4096);
        m_State.MarkDisplayDirty("Final render width changed.");
    }
    if (ImGui::InputInt("Height##FinalRender", &m_State.GetSettings().finalRender.resolutionY)) {
        m_State.GetSettings().finalRender.resolutionY = std::clamp(m_State.GetSettings().finalRender.resolutionY, 180, 4096);
        m_State.MarkDisplayDirty("Final render height changed.");
    }

    if (ImGui::InputInt("Samples##FinalRender", &m_State.GetSettings().finalRender.sampleTarget)) {
        m_State.GetSettings().finalRender.sampleTarget = std::clamp(m_State.GetSettings().finalRender.sampleTarget, 1, 8192);
        m_State.MarkDisplayDirty("Final render sample target changed.");
    }
    int finalTerminationMode = static_cast<int>(m_State.GetSettings().finalRender.terminationMode);
    if (ImGui::Combo("Termination##FinalRender", &finalTerminationMode, "Brute Force\0Optimized\0Smart\0")) {
        m_State.GetSettings().finalRender.terminationMode = static_cast<PathTraceTerminationMode>(finalTerminationMode);
        m_State.MarkDisplayDirty("Final render termination mode changed.");
    }
    const bool smartFinalTermination =
        m_State.GetSettings().finalRender.terminationMode == PathTraceTerminationMode::Smart;
    if (smartFinalTermination) {
        ImGui::BeginDisabled();
    }
    if (ImGui::InputInt("Max Bounces##FinalRender", &m_State.GetSettings().finalRender.maxBounceCount)) {
        m_State.GetSettings().finalRender.maxBounceCount = std::clamp(m_State.GetSettings().finalRender.maxBounceCount, 1, 512);
        m_State.MarkDisplayDirty("Final render bounce count changed.");
    }
    if (smartFinalTermination) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("Smart termination chooses bounce depth automatically for the final render.");
    }

    int exportMode = static_cast<int>(m_State.GetSettings().denoiser.exportMode);
    if (ImGui::Combo("Export Output", &exportMode, "Noisy\0Denoised\0")) {
        m_State.GetSettings().denoiser.exportMode = static_cast<DenoiserExportMode>(exportMode);
        m_State.MarkDisplayDirty("Final render export output changed.");
    }
    ImGui::TextDisabled(
        "Current denoiser: %s  |  View: %s",
        DenoiserUsesFilteredOutput(m_State.GetSettings().denoiser)
            ? DenoiserModeLabel(m_State.GetSettings().denoiser.mode)
            : "Noisy",
        DenoiserDebugViewLabel(m_State.GetSettings().denoiser.debugView));

    ImGui::Spacing();
    if (ImGui::Button("Start Render", ImVec2(160.0f, 0.0f))) {
        StartFinalRenderSession();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%dx%d  |  %d samples",
        m_State.GetSettings().finalRender.resolutionX,
        m_State.GetSettings().finalRender.resolutionY,
        m_State.GetSettings().finalRender.sampleTarget);

    ImGui::End();
}

bool RenderTab::StartFinalRenderSession() {
    const std::string defaultFileName =
        m_State.GetSettings().finalRender.outputName.empty()
            ? "final_render.png"
            : m_State.GetSettings().finalRender.outputName + ".png";
    const std::string outputPath = FileDialogs::SavePngFileDialog("Save Final Render PNG", defaultFileName.c_str());
    if (outputPath.empty()) {
        m_StatusText = "Final render canceled before start.";
        return false;
    }

    m_FinalRenderSession = {};
    m_FinalRenderSession.active = true;
    m_FinalRenderSession.outputPath = outputPath;
    m_FinalRenderSession.statusText = "Rendering from the scene camera...";
    m_FinalRenderAccumulationManager.ResetForScene(BuildFinalRenderSettings());
    m_LastFinalRenderError.clear();
    m_ShowFinalRenderSettingsWindow = false;
    return true;
}

void RenderTab::CancelFinalRenderSession() {
    m_FinalRenderSession = {};
    m_FinalRenderAccumulationManager.ResetForScene(BuildFinalRenderSettings());
    m_LastFinalRenderError.clear();
    m_StatusText = "Final render canceled.";
}

bool RenderTab::CompleteFinalRenderSession(std::string& errorMessage) {
    std::vector<float> linearPixels;
    std::vector<unsigned char> pixels;
    const RenderFoundation::Settings finalSettings = BuildFinalRenderSettings();
    const RenderPathTrace::LinearCaptureMode captureMode =
        finalSettings.denoiser.exportMode == DenoiserExportMode::Denoised
            ? RenderPathTrace::LinearCaptureMode::Denoised
            : RenderPathTrace::LinearCaptureMode::Noisy;
    if (!m_RenderDelegator.CapturePathTraceTargetLinearPixels(
            RenderContracts::RenderDelegator::PathTraceTarget::FinalRender,
            finalSettings,
            finalSettings.resolutionX,
            finalSettings.resolutionY,
            m_FinalRenderAccumulationManager,
            captureMode,
            linearPixels,
            errorMessage)) {
        return false;
    }

    if (linearPixels.empty()) {
        errorMessage = "No final render HDR pixels were available to export.";
        return false;
    }

    ConvertLinearHdrToPngPixels(
        linearPixels,
        finalSettings.resolutionX,
        finalSettings.resolutionY,
        m_State.GetCamera(),
        pixels);
    if (pixels.empty()) {
        errorMessage = "Failed to tone-map the final render.";
        return false;
    }

    if (stbi_write_png(
            m_FinalRenderSession.outputPath.c_str(),
            finalSettings.resolutionX,
            finalSettings.resolutionY,
            4,
            pixels.data(),
            finalSettings.resolutionX * 4) == 0) {
        errorMessage = "Failed to write the final render PNG.";
        return false;
    }

    return true;
}

void RenderTab::RenderFinalRenderWindow() {
    if (!m_FinalRenderSession.active) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(760.0f, 820.0f), ImGuiCond_FirstUseEver);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking;
    if (!ImGui::Begin("Final Render", nullptr, flags)) {
        ImGui::End();
        return;
    }

    const RenderFoundation::Settings finalSettings = BuildFinalRenderSettings();
    const int renderWidth = std::clamp(finalSettings.finalRender.resolutionX, 320, 4096);
    const int renderHeight = std::clamp(finalSettings.finalRender.resolutionY, 180, 4096);
    m_FinalRenderAccumulationManager.UpdateViewportExtent(renderWidth, renderHeight, finalSettings);

    std::string renderError;
    unsigned int renderTextureId = 0;
    const bool renderReady = SyncCompiledFinalRenderScene(renderError);
    const bool sampleLimitReached =
        m_FinalRenderAccumulationManager.GetAccumulatedSamples() >= finalSettings.finalRender.sampleTarget;
    if (renderReady) {
        if (!m_RenderDelegator.RenderPathTraceTarget(
                RenderContracts::RenderDelegator::PathTraceTarget::FinalRender,
                m_FinalRenderScene,
                finalSettings,
                m_FinalRenderAccumulationManager,
                renderWidth,
                renderHeight,
                !sampleLimitReached,
                renderTextureId,
                renderError)) {
            m_LastFinalRenderError = renderError;
        }
    } else {
        m_LastFinalRenderError = renderError;
    }

    ImGui::TextWrapped("Final render session is active. The editor is locked while this render converges from the scene camera.");
    ImGui::TextDisabled(
        "%dx%d  |  Samples %d / %d  |  %s",
        renderWidth,
        renderHeight,
        m_FinalRenderAccumulationManager.GetAccumulatedSamples(),
        finalSettings.finalRender.sampleTarget,
        PathTraceTerminationModeLabel(finalSettings.finalRender.terminationMode));
    ImGui::TextDisabled(
        "Denoiser: %s  |  View: %s  |  Export: %s",
        DenoiserUsesFilteredOutput(finalSettings.denoiser)
            ? DenoiserModeLabel(finalSettings.denoiser.mode)
            : "Noisy",
        DenoiserDebugViewLabel(finalSettings.denoiser.debugView),
        DenoiserExportModeLabel(finalSettings.denoiser.exportMode));

    if (ImGui::Button("Cancel Render", ImVec2(140.0f, 0.0f))) {
        CancelFinalRenderSession();
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_FinalRenderSession.outputPath.c_str());

    const ImVec2 availablePreviewRegion = ImGui::GetContentRegionAvail();
    const float availableWidth = std::max(availablePreviewRegion.x, 220.0f);
    const float availableHeight = std::max(availablePreviewRegion.y - 8.0f, 180.0f);
    const float aspect = static_cast<float>(renderWidth) / static_cast<float>(std::max(renderHeight, 1));
    float previewWidth = availableWidth;
    float previewHeight = previewWidth / std::max(aspect, 0.01f);
    if (previewHeight > availableHeight) {
        previewHeight = availableHeight;
        previewWidth = previewHeight * std::max(aspect, 0.01f);
    }
    const ImVec2 finalPreviewPos = ImGui::GetCursorScreenPos();
    const ImVec2 finalPreviewSize(previewWidth, previewHeight);
    const ImRect finalPreviewOuterRect(finalPreviewPos, ImVec2(finalPreviewPos.x + finalPreviewSize.x, finalPreviewPos.y + finalPreviewSize.y));
    const ImRect finalPreviewImageRect = FitRenderTargetRect(finalPreviewOuterRect, renderWidth, renderHeight);
    ImGui::Dummy(finalPreviewSize);
    ImDrawList* finalPreviewDrawList = ImGui::GetWindowDrawList();
    finalPreviewDrawList->AddRectFilled(finalPreviewOuterRect.Min, finalPreviewOuterRect.Max, IM_COL32(18, 18, 24, 255), 6.0f);
    if (renderTextureId != 0) {
        finalPreviewDrawList->AddImage(
            (ImTextureID)(intptr_t)renderTextureId,
            finalPreviewImageRect.Min,
            finalPreviewImageRect.Max,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
        finalPreviewDrawList->AddRect(finalPreviewImageRect.Min, finalPreviewImageRect.Max, IM_COL32(255, 255, 255, 34), 0.0f, 0, 1.0f);
    }

    if (!m_LastFinalRenderError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.68f, 1.0f), "%s", m_LastFinalRenderError.c_str());
    }

    if (sampleLimitReached && !m_FinalRenderAccumulationManager.IsSubmissionInFlight() && !m_FinalRenderSession.exportStarted) {
        m_FinalRenderSession.exportStarted = true;
        std::string exportError;
        if (CompleteFinalRenderSession(exportError)) {
            m_FinalRenderSession.completed = true;
            m_StatusText = std::string("Final render exported to ") + m_FinalRenderSession.outputPath + ".";
            m_FinalRenderSession.active = false;
        } else {
            m_FinalRenderSession.exportStarted = false;
            m_LastFinalRenderError = exportError;
        }
    }

    ImGui::End();
}

void RenderTab::RenderTestScenesWindow() {
    if (!m_ShowTestScenesWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(430.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Test Scenes", &m_ShowTestScenesWindow)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Validation scenes use the same path tracer as the project view. Selecting one opens an editable preview override for camera, render settings, and the active validation scene template.");
    ImGui::Spacing();

    if (m_TestScenePreviewEnabled) {
        ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.54f, 1.0f), "Active: %s", GetRenderValidationSceneLabel(m_TestScenePreviewId));
        if (ImGui::Button("Return To Project")) {
            DisableTestScenePreview();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Preview Tweaks")) {
            ResetTestScenePreviewTemplate();
            ResetTestScenePreviewAccumulation();
        }
    } else {
        ImGui::TextDisabled("Project scene active.");
    }

    ImGui::Separator();

    if (ImGui::BeginChild("TestScenesList", ImVec2(0.0f, 0.0f), true)) {
        const auto& options = GetRenderValidationSceneOptions();
        for (const RenderValidationSceneOption& option : options) {
            const bool selected = m_TestScenePreviewEnabled && m_TestScenePreviewId == option.id;
            ImGui::PushID(static_cast<int>(option.id));
            if (ImGui::Selectable(option.label, selected)) {
                EnableTestScenePreview(option.id);
            }
            ImGui::TextDisabled("%s", option.description);
            ImGui::Spacing();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void RenderTab::RenderDiagnosticsWindow() {
    if (!m_ShowDiagnosticsWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Render Diagnostics", &m_ShowDiagnosticsWindow)) {
        ImGui::End();
        return;
    }

    const bool pathTraceMode = m_State.GetSettings().viewMode == ViewMode::PathTrace;
    ImGui::TextDisabled("Viewport");
    ImGui::Separator();
    if (pathTraceMode) {
        ImGui::TextDisabled("Path Trace / %s / %s",
            PathTraceTransportModeLabel(m_State.GetSettings().pathTraceTransportMode),
            PathTraceTerminationModeLabel(m_State.GetSettings().pathTraceTerminationMode));
    } else if (ImGui::SliderInt("Preview Samples", &m_State.GetSettings().previewSampleTarget, 1, 1024)) {
        m_State.MarkTransportDirty("Preview sample target changed.");
    }
    if (ImGui::Checkbox("Accumulate", &m_State.GetSettings().accumulationEnabled)) {
        m_State.MarkTransportDirty("Accumulation state changed.");
    }
    if (pathTraceMode) {
        ImGui::BeginDisabled();
        ImGui::SliderInt("Max Bounces", &m_State.GetSettings().maxBounceCount, 1, 512);
        int terminationMode = static_cast<int>(m_State.GetSettings().pathTraceTerminationMode);
        ImGui::Combo("Termination", &terminationMode, "Brute Force\0Optimized\0Smart\0");
        ImGui::EndDisabled();
        ImGui::TextDisabled("Viewport and Camera previews are locked to Smart transport at exact panel pixels.");

        ImGui::Spacing();
        ImGui::TextDisabled("Path Trace Debug");
        ImGui::Separator();
        int debugMode = static_cast<int>(m_State.GetSettings().pathTraceDebugMode);
        if (ImGui::Combo(
                "Debug View",
                &debugMode,
                "None\0Selected Ray Log\0Refracted Source Class\0Self-Hit Heatmap\0")) {
            m_State.GetSettings().pathTraceDebugMode = static_cast<PathTraceDebugMode>(debugMode);
            m_State.MarkDisplayDirty("Path-trace debug view changed.");
        }
        if (m_State.GetSettings().pathTraceDebugMode == PathTraceDebugMode::SelectedRayLog) {
            if (ImGui::InputInt("Debug Pixel X", &m_State.GetSettings().pathTraceDebugPixelX)) {
                m_State.GetSettings().pathTraceDebugPixelX = std::max(-1, m_State.GetSettings().pathTraceDebugPixelX);
                m_State.MarkDisplayDirty("Path-trace debug pixel X changed.");
            }
            if (ImGui::InputInt("Debug Pixel Y", &m_State.GetSettings().pathTraceDebugPixelY)) {
                m_State.GetSettings().pathTraceDebugPixelY = std::max(-1, m_State.GetSettings().pathTraceDebugPixelY);
                m_State.MarkDisplayDirty("Path-trace debug pixel Y changed.");
            }
            const RenderPathTrace::PathTraceDebugReadback& debugReadback =
                m_RenderDelegator.GetPathTraceDebugReadback();
            ImGui::TextDisabled(
                debugReadback.valid ? "Ray log available from latest sample." : "Ray log populates after the next completed sample.");
        }

        DenoiserSettings& denoiser = m_State.GetSettings().denoiser;
        ImGui::Spacing();
        ImGui::TextDisabled("Denoiser");
        ImGui::Separator();
        if (ImGui::Checkbox("Enable Denoiser", &denoiser.enabled)) {
            m_State.MarkDisplayDirty("Path-trace denoiser enabled state changed.");
        }
        int denoiserMode = static_cast<int>(denoiser.mode);
        if (ImGui::Combo("Mode", &denoiserMode, "Off\0Bilateral\0A-Trous\0")) {
            denoiser.mode = static_cast<DenoiserMode>(denoiserMode);
            m_State.MarkDisplayDirty("Path-trace denoiser mode changed.");
        }
        int debugView = static_cast<int>(denoiser.debugView);
        if (ImGui::Combo(
                "Denoiser View",
                &debugView,
                "Final Output\0Noisy Beauty\0Denoised Beauty\0Current Sample\0Guide Albedo\0Guide Normal\0Guide Depth\0Variance Heatmap\0Difference Heatmap\0")) {
            denoiser.debugView = static_cast<DenoiserDebugView>(debugView);
            m_State.MarkDisplayDirty("Path-trace denoiser view changed.");
        }
        if (ImGui::Checkbox("Firefly Clamp", &denoiser.fireflyClampEnabled)) {
            m_State.MarkTransportDirty("Path-trace firefly clamp changed.");
        }
        if (ImGui::SliderFloat("Firefly Threshold", &denoiser.fireflyClampThreshold, 0.5f, 32.0f, "%.2f")) {
            m_State.MarkTransportDirty("Path-trace firefly threshold changed.");
        }
        if (ImGui::SliderInt("Bilateral Radius", &denoiser.bilateralRadius, 1, 8)) {
            m_State.MarkDisplayDirty("Path-trace bilateral radius changed.");
        }
        if (ImGui::SliderInt("A-Trous Passes", &denoiser.atrousPassCount, 1, 5)) {
            m_State.MarkDisplayDirty("Path-trace a-trous pass count changed.");
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Compile: %s  |  Viewport %.1f ms",
        Async::ToString(m_ViewportCompile.state),
        m_ViewportCompile.lastCompileMs);
    ImGui::TextDisabled("Camera Preview: %s  |  %.1f ms",
        Async::ToString(m_CameraPreviewCompile.state),
        m_CameraPreviewCompile.lastCompileMs);

    ImGui::End();
}

bool RenderTab::EnsureViewportSceneCompilation() {
    m_ActiveSnapshot = m_State.BuildSnapshot();
    const std::uint64_t desiredSceneRevision =
        m_State.GetSceneRevision()
        ^ (m_TestScenePreviewEnabled ? (static_cast<std::uint64_t>(m_TestScenePreviewId) << 48u) : 0ull)
        ^ (m_TestScenePreviewEnabled ? (m_TestScenePreviewRevision << 8u) : 0ull);

    const bool needsCompile =
        m_ViewportCompile.requestedSceneRevision != desiredSceneRevision ||
        (m_ViewportCompile.requestedSceneRevision == 0 && !m_CompiledScene.valid && !Async::IsBusy(m_ViewportCompile.state));
    if (needsCompile && !Async::IsBusy(m_ViewportCompile.state)) {
        m_ViewportCompile.generation += 1;
        m_ViewportCompile.requestedSceneRevision = desiredSceneRevision;
        m_ViewportCompile.state = Async::TaskState::Running;
        m_ViewportCompile.lastError.clear();

        const std::uint64_t generation = m_ViewportCompile.generation;
        const RenderContracts::SceneSnapshot snapshot = m_ActiveSnapshot;
        const Camera cameraOverride = m_EditorCamera;
        const Camera validationCamera = m_State.GetCamera();
        const Settings settings = m_State.GetSettings();
        const bool validationScene = m_TestScenePreviewEnabled;
        const RenderValidationSceneTemplate validationTemplate = m_TestScenePreviewTemplate;
        Async::TaskSystem::Get().Submit([this, generation, snapshot, cameraOverride, validationCamera, settings, validationScene, validationTemplate]() mutable {
            RenderContracts::SceneCompiler compiler;
            RenderContracts::CompiledScene compiledScene;
            std::string errorMessage;
            const auto compileStart = std::chrono::steady_clock::now();
            const bool success = validationScene
                ? compiler.CompileValidationScene(validationTemplate, validationCamera, settings, compiledScene, errorMessage)
                : compiler.Compile(snapshot, cameraOverride, settings, compiledScene, errorMessage);
            const double compileMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - compileStart).count();

            Async::TaskSystem::Get().PostToMain([this,
                                                 generation,
                                                 success,
                                                 compiledScene = std::move(compiledScene),
                                                 errorMessage = std::move(errorMessage),
                                                 compileMs]() mutable {
                if (generation != m_ViewportCompile.generation) {
                    return;
                }
                m_ViewportCompile.state = success ? Async::TaskState::Idle : Async::TaskState::Failed;
                m_ViewportCompile.lastCompileMs = compileMs;
                m_ViewportCompile.lastError = errorMessage;
                if (success) {
                    m_CompiledScene = std::move(compiledScene);
                    m_CompiledScene.valid = true;
                    m_ViewportCompile.appliedSceneRevision = m_ViewportCompile.requestedSceneRevision;
                    m_LastViewportError.clear();
                } else if (!errorMessage.empty()) {
                    m_LastViewportError = errorMessage;
                }
            });
        });
    }

    if (Async::IsBusy(m_ViewportCompile.state) && !m_CompiledScene.valid) {
        m_LastViewportError = "Compiling scene...";
    } else if (!m_ViewportCompile.lastError.empty() && !m_CompiledScene.valid) {
        m_LastViewportError = m_ViewportCompile.lastError;
    }

    if (m_CompiledScene.valid && !m_TestScenePreviewEnabled) {
        m_CompiledScene.camera.ApplySnapshot(
            MakeRenderFloat3(m_EditorCamera.position.x, m_EditorCamera.position.y, m_EditorCamera.position.z),
            m_EditorCamera.yawDegrees,
            m_EditorCamera.pitchDegrees,
            m_EditorCamera.fieldOfViewDegrees,
            m_EditorCamera.focusDistance,
            m_EditorCamera.apertureRadius,
            m_EditorCamera.exposure,
            m_EditorCamera.whiteBalanceTemperature,
            "Viewport camera override refreshed.");
    }

    return m_CompiledScene.valid;
}

bool RenderTab::EnsureCameraPreviewSceneCompilation() {
    m_ActiveSnapshot = m_State.BuildSnapshot();
    const Settings previewSettings = BuildCameraPreviewSettings();
    const std::uint64_t desiredSceneRevision =
        (m_State.GetSceneRevision() << 1u)
        ^ (static_cast<std::uint64_t>(previewSettings.cameraPreview.resolutionX) << 24u)
        ^ (static_cast<std::uint64_t>(previewSettings.cameraPreview.resolutionY) << 8u)
        ^ static_cast<std::uint64_t>(previewSettings.cameraPreview.maxBounceCount)
        ^ (static_cast<std::uint64_t>(previewSettings.cameraPreview.terminationMode) << 40u)
        ^ (m_TestScenePreviewEnabled ? (static_cast<std::uint64_t>(m_TestScenePreviewId) << 52u) : 0ull)
        ^ (m_TestScenePreviewEnabled ? (m_TestScenePreviewRevision << 12u) : 0ull);

    const bool needsCompile =
        m_CameraPreviewCompile.requestedSceneRevision != desiredSceneRevision ||
        (m_CameraPreviewCompile.requestedSceneRevision == 0 && !m_CameraPreviewScene.valid && !Async::IsBusy(m_CameraPreviewCompile.state));
    if (needsCompile && !Async::IsBusy(m_CameraPreviewCompile.state)) {
        m_CameraPreviewCompile.generation += 1;
        m_CameraPreviewCompile.requestedSceneRevision = desiredSceneRevision;
        m_CameraPreviewCompile.state = Async::TaskState::Running;
        m_CameraPreviewCompile.lastError.clear();

        const std::uint64_t generation = m_CameraPreviewCompile.generation;
        const RenderContracts::SceneSnapshot snapshot = m_ActiveSnapshot;
        const Camera cameraOverride = m_State.GetCamera();
        const bool validationScene = m_TestScenePreviewEnabled;
        const RenderValidationSceneTemplate validationTemplate = m_TestScenePreviewTemplate;
        Async::TaskSystem::Get().Submit([this, generation, snapshot, cameraOverride, previewSettings, validationScene, validationTemplate]() mutable {
            RenderContracts::SceneCompiler compiler;
            RenderContracts::CompiledScene compiledScene;
            std::string errorMessage;
            const auto compileStart = std::chrono::steady_clock::now();
            const bool success = validationScene
                ? compiler.CompileValidationScene(validationTemplate, cameraOverride, previewSettings, compiledScene, errorMessage)
                : compiler.Compile(snapshot, cameraOverride, previewSettings, compiledScene, errorMessage);
            const double compileMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - compileStart).count();

            Async::TaskSystem::Get().PostToMain([this,
                                                 generation,
                                                 success,
                                                 compiledScene = std::move(compiledScene),
                                                 errorMessage = std::move(errorMessage),
                                                 compileMs]() mutable {
                if (generation != m_CameraPreviewCompile.generation) {
                    return;
                }
                m_CameraPreviewCompile.state = success ? Async::TaskState::Idle : Async::TaskState::Failed;
                m_CameraPreviewCompile.lastCompileMs = compileMs;
                m_CameraPreviewCompile.lastError = errorMessage;
                if (success) {
                    m_CameraPreviewScene = std::move(compiledScene);
                    m_CameraPreviewScene.valid = true;
                    m_CameraPreviewCompile.appliedSceneRevision = m_CameraPreviewCompile.requestedSceneRevision;
                    m_LastCameraPreviewError.clear();
                } else if (!errorMessage.empty()) {
                    m_LastCameraPreviewError = errorMessage;
                }
            });
        });
    }

    if (Async::IsBusy(m_CameraPreviewCompile.state) && !m_CameraPreviewScene.valid) {
        m_LastCameraPreviewError = "Compiling scene...";
    } else if (!m_CameraPreviewCompile.lastError.empty() && !m_CameraPreviewScene.valid) {
        m_LastCameraPreviewError = m_CameraPreviewCompile.lastError;
    }

    if (m_CameraPreviewScene.valid) {
        const Camera& renderCamera = m_State.GetCamera();
        m_CameraPreviewScene.camera.ApplySnapshot(
            MakeRenderFloat3(renderCamera.position.x, renderCamera.position.y, renderCamera.position.z),
            renderCamera.yawDegrees,
            renderCamera.pitchDegrees,
            renderCamera.fieldOfViewDegrees,
            renderCamera.focusDistance,
            renderCamera.apertureRadius,
            renderCamera.exposure,
            renderCamera.whiteBalanceTemperature,
            "Camera preview override refreshed.");
    }

    return m_CameraPreviewScene.valid;
}

bool RenderTab::SyncCompiledFinalRenderScene(std::string& errorMessage) {
    m_ActiveSnapshot = m_State.BuildSnapshot();
    m_LastFinalRenderError.clear();
    const RenderFoundation::Settings finalSettings = BuildFinalRenderSettings();
    if (m_TestScenePreviewEnabled) {
        if (!m_SceneCompiler.CompileValidationScene(
                m_TestScenePreviewTemplate,
                m_State.GetCamera(),
                finalSettings,
                m_FinalRenderScene,
                errorMessage)) {
            m_FinalRenderScene.valid = false;
            return false;
        }
        return true;
    }

    if (!m_SceneCompiler.Compile(m_ActiveSnapshot, m_State.GetCamera(), finalSettings, m_FinalRenderScene, errorMessage)) {
        m_FinalRenderScene.valid = false;
        return false;
    }
    return true;
}

void RenderTab::EnableTestScenePreview(RenderValidationSceneId id) {
    m_TestScenePreviewEnabled = true;
    m_TestScenePreviewId = id;
    ResetTestScenePreviewTemplate();
    InvalidateCompiledSceneTargets();
    m_ViewportController.Reset();
    SelectCamera();
    m_State.AccessAccumulationManager().ResetForScene(m_State.GetSettings());
    ResetAuxiliaryAccumulation();
    m_LastViewportError.clear();

    const RenderValidationSceneOption* option = FindValidationSceneOption(id);
    if (option != nullptr) {
        m_StatusText = std::string("Test scene loaded: ") + option->label + ".";
    } else {
        m_StatusText = std::string("Test scene loaded: ") + GetRenderValidationSceneLabel(id) + ".";
    }
}

void RenderTab::DisableTestScenePreview() {
    if (!m_TestScenePreviewEnabled) {
        return;
    }

    m_TestScenePreviewEnabled = false;
    m_TestScenePreviewTemplate = {};
    InvalidateCompiledSceneTargets();
    m_ViewportController.Reset();
    SelectNone();
    m_State.AccessAccumulationManager().ResetForScene(m_State.GetSettings());
    ResetAuxiliaryAccumulation();
    m_LastViewportError.clear();
    m_StatusText = "Returned to project scene.";
}

void RenderTab::ResetTestScenePreviewTemplate() {
    m_TestScenePreviewTemplate = BuildRenderValidationScene(m_TestScenePreviewId);
    m_TestScenePreviewRevision += 1;
}

void RenderTab::ResetTestScenePreviewAccumulation() {
    m_TestScenePreviewRevision += 1;
    m_State.AccessAccumulationManager().ResetForScene(m_State.GetSettings());
    ResetAuxiliaryAccumulation();
    m_LastViewportError.clear();
}

void RenderTab::ApplySceneChange(const RenderContracts::SceneChangeSet& changeSet) {
    if (changeSet.resetClass == RenderContracts::ResetClass::None) {
        return;
    }

    m_State.ApplyExternalChange(changeSet);
    if (changeSet.resetClass == RenderContracts::ResetClass::DisplayOnly) {
        const RenderFoundation::Settings previewSettings = BuildCameraPreviewSettings();
        const RenderFoundation::Settings finalSettings = BuildFinalRenderSettings();
        m_CameraAccumulationManager.ApplyChange(changeSet, previewSettings);
        m_FinalRenderAccumulationManager.ApplyChange(changeSet, finalSettings);
    } else if (changeSet.resetClass == RenderContracts::ResetClass::PartialAccumulation ||
        changeSet.resetClass == RenderContracts::ResetClass::FullAccumulation) {
        ResetAuxiliaryAccumulation();
        m_LastAuxiliaryTransportEpoch = m_State.GetTransportEpoch();
    }
    if (!changeSet.reason.empty()) {
        m_StatusText = changeSet.reason;
    }
}

void RenderTab::SelectNone() {
    m_VirtualSelection = VirtualSelection::None;
    m_State.SelectNone();
}

void RenderTab::SelectPrimitive(RenderFoundation::Id id) {
    m_VirtualSelection = VirtualSelection::None;
    m_State.SelectPrimitive(id);
}

void RenderTab::SelectLight(RenderFoundation::Id id) {
    m_VirtualSelection = VirtualSelection::None;
    m_State.SelectLight(id);
}

void RenderTab::SelectCamera() {
    m_VirtualSelection = VirtualSelection::None;
    m_State.SelectCamera();
}

void RenderTab::SelectVirtual(VirtualSelection selection) {
    m_VirtualSelection = selection;
    m_State.SelectNone();
}

bool RenderTab::HasVirtualSelection() const {
    return m_VirtualSelection != VirtualSelection::None;
}

void RenderTab::RequestResetRenderLayout() {
    m_ResetRenderDockLayout = true;
    m_ShowCameraWindow = false;
    m_ShowFinalRenderSettingsWindow = false;
    m_ShowTestScenesWindow = false;
    m_ShowDiagnosticsWindow = false;
    m_StatusText = "Render layout reset.";
}

int RenderTab::GetSelectedRuntimeObjectId() const {
    const RenderFoundation::Selection selection = m_State.GetSelection();
    if (selection.type == RenderFoundation::SelectionType::Primitive ||
        selection.type == RenderFoundation::SelectionType::Light) {
        return static_cast<int>(selection.id);
    }
    return -1;
}

bool RenderTab::RequestSaveProject() {
    StackBinaryFormat::json payload;
    std::vector<unsigned char> beautyPixels;
    int width = 0;
    int height = 0;
    std::string errorMessage;
    if (!BuildLibraryProjectPayload(payload, beautyPixels, width, height, errorMessage)) {
        m_StatusText = errorMessage.empty() ? "Failed to build render payload." : errorMessage;
        return false;
    }

    const std::string saveName =
        m_State.GetProjectName().empty() ? m_State.GetSceneMetadata().label : m_State.GetProjectName();

    LibraryManager::Get().RequestSaveRenderProject(
        saveName,
        payload,
        beautyPixels,
        width,
        height,
        m_State.GetProjectFileName(),
        [this, saveName](bool success, const std::string& projectFileName, const std::string& assetFileName) {
            if (!success) {
                m_StatusText = "Failed to save the render project.";
                return;
            }

            m_State.MarkSaved(saveName, projectFileName);
            m_LatestFinalAssetFileName = assetFileName;
            m_StatusText = std::string("Saved render project to ") + projectFileName + ".";
        });

    return true;
}
