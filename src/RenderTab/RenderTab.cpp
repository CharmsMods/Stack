#include "RenderTab.h"

#include "Async/TaskState.h"
#include "Library/LibraryManager.h"
#include "RenderTab/Foundation/RenderFoundationPreview.h"
#include "RenderTab/Foundation/RenderFoundationSerialization.h"
#include "RenderTab/Contracts/RenderContracts.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <imgui.h>
#include <imgui_internal.h>

namespace {

using namespace RenderFoundation;

constexpr float kSplitterWidth = 6.0f;

const char* ViewModeLabel(ViewMode mode) {
    switch (mode) {
        case ViewMode::Unlit:
            return "Unlit";
        case ViewMode::PathTrace:
            return "Path Trace";
    }
    return "Unknown";
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

const char* PrimitiveTypeLabel(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Sphere:
            return "Sphere";
        case PrimitiveType::Cube:
            return "Cube";
        case PrimitiveType::Plane:
            return "Plane";
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
            layer.roughness = std::max(layer.roughness, 0.08f);
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

void DrawViewportOverlay(ImDrawList* drawList, const ImRect& rect, const State& state, bool navigationActive) {
    const std::string lineOne =
        std::string("Mode: ") + ViewModeLabel(state.GetSettings().viewMode) +
        "  |  Samples: " + std::to_string(state.GetAccumulatedSamples()) +
        "  |  Transport Epoch: " + std::to_string(state.GetTransportEpoch());
    const std::string lineTwo =
        std::string("W/E/R: ") + TransformModeLabel(state.GetSettings().transformMode) +
        "  |  Space: " + TransformSpaceLabel(state.GetSettings().transformSpace) +
        "  |  " + (navigationActive ? "Navigating camera" : "RMB fly camera, click to select, Del deletes");

    drawList->AddRectFilled(
        ImVec2(rect.Min.x + 12.0f, rect.Max.y - 52.0f),
        ImVec2(rect.Min.x + 520.0f, rect.Max.y - 10.0f),
        IM_COL32(0, 0, 0, 120),
        8.0f);
    drawList->AddText(ImVec2(rect.Min.x + 24.0f, rect.Max.y - 46.0f), IM_COL32(240, 240, 240, 255), lineOne.c_str());
    drawList->AddText(ImVec2(rect.Min.x + 24.0f, rect.Max.y - 28.0f), IM_COL32(190, 198, 208, 255), lineTwo.c_str());

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
    m_ViewportController.Reset();
    m_LastViewportError.clear();
    m_StatusText = "Render foundation initialized.";
    m_LatestFinalAssetFileName.clear();
}

void RenderTab::Shutdown() {
    m_RenderDelegator.Shutdown();
}

void RenderTab::RenderUI() {
    m_State.TickFrame();

    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        if (m_State.DeleteSelection()) {
            m_StatusText = "Selection deleted.";
        }
    }

    RenderToolbar();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    RenderBody();
    RenderCameraWindow();
    RenderResetScenePopup();

    if (Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetSaveStatusText().c_str());
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

    outWidth = std::clamp(m_State.GetSettings().finalRender.resolutionX, 640, 1600);
    outHeight = std::clamp(m_State.GetSettings().finalRender.resolutionY, 360, 900);
    RenderContracts::CompiledScene compiledScene;
    const RenderContracts::SceneSnapshot snapshot = m_State.BuildSnapshot();
    if (!m_SceneCompiler.Compile(snapshot, m_State.GetSettings(), compiledScene, errorMessage)) {
        RenderFoundation::BuildPreviewPixels(
            m_State,
            outWidth,
            outHeight,
            std::max(m_State.GetSettings().finalRender.sampleTarget, 64),
            outBeautyPixels);
        return !outBeautyPixels.empty();
    }

    if (m_RenderDelegator.CaptureViewportPixels(
            compiledScene,
            m_State.GetSettings(),
            m_State.AccessAccumulationManager(),
            outWidth,
            outHeight,
            -1,
            outBeautyPixels,
            errorMessage)) {
        return true;
    }

    RenderFoundation::BuildPreviewPixels(
        m_State,
        outWidth,
        outHeight,
        std::max(m_State.GetSettings().finalRender.sampleTarget, 64),
        outBeautyPixels);
    return !outBeautyPixels.empty();
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

void RenderTab::RenderToolbar() {
    ImGui::BeginChild("RenderToolbar", ImVec2(0.0f, 72.0f), true);

    const bool saveBusy = Async::IsBusy(LibraryManager::Get().GetSaveTaskState());
    ImGui::BeginDisabled(saveBusy);
    if (ImGui::Button(saveBusy ? "Saving..." : "Save Render Project", ImVec2(160.0f, 0.0f))) {
        RequestSaveProject();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("New Scene", ImVec2(110.0f, 0.0f))) {
        if (m_State.HasUnsavedChanges()) {
            m_OpenResetSceneConfirm = true;
            ImGui::OpenPopup("Discard Unsaved Render Scene");
        } else {
            m_State.ResetToDefaultScene();
            m_ViewportController.Reset();
            m_LastViewportError.clear();
            m_LatestFinalAssetFileName.clear();
            m_StatusText = "Started a new foundation scene.";
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Add Sphere")) {
        m_State.AddPrimitive(PrimitiveType::Sphere);
        m_StatusText = "Sphere added.";
    }

    ImGui::SameLine();
    if (ImGui::Button("Add Light")) {
        m_State.AddLight(LightType::Point);
        m_StatusText = "Point light added.";
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    int viewMode = static_cast<int>(m_State.GetSettings().viewMode);
    if (ImGui::Combo("##RenderViewMode", &viewMode, "Unlit\0Path Trace\0")) {
        m_State.GetSettings().viewMode = static_cast<ViewMode>(viewMode);
        m_State.MarkTransportDirty("Viewport mode changed.");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Project: %s%s",
        GetCurrentProjectName().c_str(),
        m_State.HasUnsavedChanges() ? " *" : "");

    const std::string saveStatus = LibraryManager::Get().GetSaveStatusText();
    const std::string loadStatus = LibraryManager::Get().GetProjectLoadStatusText();
    const std::string toolbarStatus =
        !saveStatus.empty() ? saveStatus :
        (!loadStatus.empty() ? loadStatus : m_StatusText);

    if (!toolbarStatus.empty()) {
        ImGui::TextWrapped("%s", toolbarStatus.c_str());
    }

    ImGui::EndChild();
}

void RenderTab::RenderBody() {
    const float minInspector = 260.0f;
    const float maxInspector = 420.0f;
    const float minScene = 240.0f;
    const float maxScene = 380.0f;
    m_InspectorWidth = std::clamp(m_InspectorWidth, minInspector, maxInspector);
    m_SceneWidth = std::clamp(m_SceneWidth, minScene, maxScene);

    const float bodyHeight = ImGui::GetContentRegionAvail().y;

    RenderInspectorPanel(m_InspectorWidth);

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("RenderInspectorSplitter", ImVec2(kSplitterWidth, bodyHeight));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
        m_InspectorWidth = std::clamp(m_InspectorWidth + ImGui::GetIO().MouseDelta.x, minInspector, maxInspector);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetItemRectMin(),
        ImGui::GetItemRectMax(),
        ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered),
        2.0f);

    ImGui::SameLine(0.0f, 0.0f);
    const float viewportWidth =
        std::max(220.0f, ImGui::GetContentRegionAvail().x - m_SceneWidth - kSplitterWidth);
    RenderViewportPanel(viewportWidth);

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("RenderSceneSplitter", ImVec2(kSplitterWidth, bodyHeight));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
        m_SceneWidth = std::clamp(m_SceneWidth - ImGui::GetIO().MouseDelta.x, minScene, maxScene);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetItemRectMin(),
        ImGui::GetItemRectMax(),
        ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered),
        2.0f);

    ImGui::SameLine(0.0f, 0.0f);
    RenderScenePanel(m_SceneWidth);
}

void RenderTab::RenderInspectorPanel(float width) {
    ImGui::BeginChild("RenderInspector", ImVec2(width, 0.0f), true);
    ImGui::Text("Inspector");
    ImGui::Separator();

    const bool pathTraceMode = m_State.GetSettings().viewMode == ViewMode::PathTrace;
    Selection selection = m_State.GetSelection();
    if (selection.type == SelectionType::Camera) {
        ImGui::TextDisabled("Camera");

        if (EditVec3Field("Position", m_State.GetCamera().position)) {
            m_State.MarkTransportDirty("Camera position changed.");
        }
        if (ImGui::SliderFloat("Yaw", &m_State.GetCamera().yawDegrees, -180.0f, 180.0f)) {
            m_State.MarkTransportDirty("Camera yaw changed.");
        }
        if (ImGui::SliderFloat("Pitch", &m_State.GetCamera().pitchDegrees, -89.0f, 89.0f)) {
            m_State.MarkTransportDirty("Camera pitch changed.");
        }
        if (ImGui::SliderFloat("FOV", &m_State.GetCamera().fieldOfViewDegrees, 20.0f, 90.0f)) {
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
        if (ImGui::SliderFloat("Move Speed", &m_State.GetCamera().movementSpeed, 1.0f, 20.0f)) {
            m_State.MarkDisplayDirty("Camera move speed changed.");
        }
    } else if (selection.type == SelectionType::Primitive) {
        Primitive* primitive = m_State.FindPrimitive(selection.id);
        if (primitive != nullptr) {
            ImGui::TextDisabled("%s", PrimitiveTypeLabel(primitive->type));

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

            std::vector<const char*> materialNames;
            materialNames.reserve(m_State.GetMaterials().size());
            int selectedMaterialIndex = 0;
            for (std::size_t index = 0; index < m_State.GetMaterials().size(); ++index) {
                const Material& material = m_State.GetMaterials()[index];
                materialNames.push_back(material.name.c_str());
                if (material.id == primitive->materialId) {
                    selectedMaterialIndex = static_cast<int>(index);
                }
            }
            if (!materialNames.empty() && ImGui::Combo("Material", &selectedMaterialIndex, materialNames.data(), static_cast<int>(materialNames.size()))) {
                primitive->materialId = m_State.GetMaterials()[selectedMaterialIndex].id;
                m_State.MarkTransportDirty("Primitive material changed.");
            }

            Material* material = m_State.FindMaterial(primitive->materialId);
            if (material != nullptr) {
                ImGui::Spacing();
                ImGui::Text("Material");
                ImGui::Separator();

                if (material->layers.empty()) {
                    SyncMaterialLayersFromLegacy(*material);
                } else {
                    SyncLegacyMaterialFromLayers(*material);
                }

                const bool ptThinWallEnabled = primitive->type == PrimitiveType::Plane;
                auto markLayoutDirty = [this, material](const char* reason) {
                    SyncLegacyMaterialFromLayers(*material);
                    m_State.MarkTransportDirty(
                        reason,
                        RenderContracts::ResetClass::FullAccumulation,
                        RenderContracts::DirtyFlags::SceneStructure | RenderContracts::DirtyFlags::SceneContent | RenderContracts::DirtyFlags::Viewport);
                };
                auto markMaterialPartial = [this, material](const char* reason) {
                    UpdateLegacyMaterialFieldsFromLayerStack(*material);
                    m_State.MarkTransportDirty(reason, RenderContracts::ResetClass::PartialAccumulation);
                };
                auto markMaterialFull = [this, material](const char* reason) {
                    UpdateLegacyMaterialFieldsFromLayerStack(*material);
                    m_State.MarkTransportDirty(reason);
                };

                if (EditStringField("Material Name", material->name)) {
                    m_State.MarkDisplayDirty("Material renamed.");
                }

                ImGui::Text("Layer Stack");
                ImGui::Separator();
                ImGui::TextDisabled("This slice locks layer order to Clear Coat above the base layer.");

                if (!MaterialHasClearCoatLayer(*material)) {
                    if (ImGui::Button("Add Clear Coat")) {
                        material->layers.insert(material->layers.begin(), MakeDefaultClearCoatLayer(0.85f));
                        markLayoutDirty("Clear coat layer added.");
                    }
                } else {
                    if (ImGui::Button("Remove Clear Coat")) {
                        material->layers.erase(
                            std::remove_if(
                                material->layers.begin(),
                                material->layers.end(),
                                [](const Material::Layer& layer) {
                                    return layer.type == MaterialLayerType::ClearCoat;
                                }),
                            material->layers.end());
                        markLayoutDirty("Clear coat layer removed.");
                    }
                }

                Material::Layer* clearCoatLayer = FindMaterialLayer(*material, MaterialLayerType::ClearCoat);
                Material::Layer* baseLayer = FindBaseMaterialLayer(*material);

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
                    if (ImGui::SliderFloat("Clear Coat", &clearCoatLayer->weight, 0.0f, 1.0f)) {
                        markMaterialPartial("Clear coat strength changed.");
                    }
                    if (ImGui::SliderFloat("Coat Roughness", &clearCoatLayer->roughness, 0.0f, 0.25f)) {
                        markMaterialPartial("Clear coat roughness changed.");
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

                    int baseLayerType = BaseLayerComboIndex(*baseLayer);
                    if (ImGui::Combo("Base Layer", &baseLayerType, "Diffuse\0Metal\0Glass\0")) {
                        ApplyBaseLayerType(*baseLayer, baseLayerType);
                        markMaterialFull("Base layer changed.");
                    }
                    if (EditColor3Field("Base Color", baseLayer->color)) {
                        markMaterialPartial("Material color changed.");
                    }

                    if (baseLayer->type == MaterialLayerType::BaseDiffuse ||
                        baseLayer->type == MaterialLayerType::BaseMetal) {
                        if (ImGui::SliderFloat("Roughness", &baseLayer->roughness, 0.0f, 1.0f)) {
                            markMaterialPartial("Material roughness changed.");
                        }
                    }

                    if (baseLayer->type == MaterialLayerType::BaseDielectric) {
                        if (ImGui::SliderFloat("Transmission", &baseLayer->transmission, 0.0f, 1.0f)) {
                            markMaterialPartial("Material transmission changed.");
                        }
                        if (ImGui::SliderFloat("IOR", &baseLayer->ior, 1.0f, 2.6f)) {
                            markMaterialPartial("Material IOR changed.");
                        }
                        if (!pathTraceMode || ptThinWallEnabled) {
                            if (ImGui::Checkbox("Thin Walled", &baseLayer->thinWalled)) {
                                markMaterialPartial("Thin wall state changed.");
                            }
                        } else {
                            ImGui::BeginDisabled(true);
                            bool thinWalled = baseLayer->thinWalled;
                            ImGui::Checkbox("Thin Walled", &thinWalled);
                            ImGui::EndDisabled();
                            ImGui::TextDisabled("Thin-walled PT is currently sheet-only; closed primitives compile as solid glass.");
                        }
                        if (ImGui::SliderFloat("Transmission Roughness", &baseLayer->transmissionRoughness, 0.0f, 1.0f)) {
                            markMaterialPartial("Transmission roughness changed.");
                        }
                        if (EditColor3Field("Absorption Color", material->absorptionColor)) {
                            markMaterialPartial("Absorption color changed.");
                        }
                        if (ImGui::SliderFloat("Absorption Distance", &material->absorptionDistance, 0.1f, 10.0f)) {
                            markMaterialPartial("Absorption distance changed.");
                        }
                    }
                    ImGui::PopID();
                }

                if (EditColor3Field("Emission Color", material->emissionColor)) {
                    markMaterialPartial("Emission color changed.");
                }
                if (ImGui::SliderFloat("Emission Strength", &material->emissionStrength, 0.0f, 24.0f)) {
                    markMaterialPartial("Emission strength changed.");
                }

                if (pathTraceMode) {
                    ImGui::TextDisabled("PT layer foundation now supports clear coat over diffuse, metal, and dielectric bases.");
                }
                ImGui::TextDisabled("Thin film and subsurface remain deferred until later slices.");
            }
        }
    } else if (selection.type == SelectionType::Light) {
        Light* light = m_State.FindLight(selection.id);
        if (light != nullptr) {
            int lightType = static_cast<int>(light->type);
            if (ImGui::Combo("Type", &lightType, "Point\0Spot\0Area\0Directional\0")) {
                light->type = static_cast<LightType>(lightType);
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
            if (EditColor3Field("Color", light->color)) {
                m_State.MarkTransportDirty("Light color changed.", RenderContracts::ResetClass::PartialAccumulation);
            }
            if (ImGui::SliderFloat("Intensity", &light->intensity, 0.0f, 80.0f)) {
                m_State.MarkTransportDirty("Light intensity changed.", RenderContracts::ResetClass::PartialAccumulation);
            }
            if (ImGui::SliderFloat("Range", &light->range, 0.1f, 60.0f)) {
                m_State.MarkTransportDirty("Light range changed.");
            }
            if (ImGui::SliderFloat2("Area Size", &light->areaSize.x, 0.1f, 10.0f)) {
                m_State.MarkTransportDirty("Area light size changed.");
            }
            if (ImGui::SliderFloat("Inner Cone", &light->innerConeDegrees, 1.0f, 80.0f)) {
                light->outerConeDegrees = std::max(light->outerConeDegrees, light->innerConeDegrees);
                m_State.MarkTransportDirty("Spot inner cone changed.");
            }
            if (ImGui::SliderFloat("Outer Cone", &light->outerConeDegrees, 1.0f, 89.0f)) {
                light->outerConeDegrees = std::max(light->outerConeDegrees, light->innerConeDegrees);
                m_State.MarkTransportDirty("Spot outer cone changed.");
            }
            if (ImGui::Checkbox("Enabled", &light->enabled)) {
                m_State.MarkTransportDirty("Light enabled state changed.");
            }
        }
    } else {
        ImGui::TextWrapped("Select a primitive, light, or the render camera in the viewport or scene list.");
    }

    ImGui::Spacing();
    ImGui::Text("Active Constraints");
    ImGui::Separator();
    ImGui::BulletText("Viewport now renders from the compiled 3D scene contract.");
    ImGui::BulletText("CPU ray/BVH picking is authoritative for authoring selection.");
    ImGui::BulletText("Path Trace uses the same immutable snapshot and dual-slot accumulation contract.");
    ImGui::BulletText("Project payload remains Library-compatible and snapshot-versioned.");

    ImGui::EndChild();
}

void RenderTab::RenderViewportPanel(float width) {
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("RenderViewport", ImVec2(width, 0.0f), true, flags);
    ImGui::Text("Viewport");
    ImGui::Separator();

    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x = std::max(canvasSize.x, 64.0f);
    canvasSize.y = std::max(canvasSize.y, 64.0f);
    m_State.AccessAccumulationManager().UpdateViewportExtent(
        static_cast<int>(canvasSize.x),
        static_cast<int>(canvasSize.y),
        m_State.GetSettings());
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const bool compiledReady = SyncCompiledScene(m_LastViewportError);
    unsigned int viewportTextureId = 0;
    if (compiledReady) {
        std::string renderError;
        if (!m_RenderDelegator.RenderViewport(
                m_CompiledScene,
                m_State.GetSettings(),
                m_State.AccessAccumulationManager(),
                static_cast<int>(canvasSize.x),
                static_cast<int>(canvasSize.y),
                GetSelectedRuntimeObjectId(),
                viewportTextureId,
                renderError)) {
            m_LastViewportError = renderError;
        }
    }

    if (viewportTextureId != 0) {
        ImGui::Image(
            (ImTextureID)(intptr_t)viewportTextureId,
            canvasSize,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
    } else {
        ImGui::Dummy(canvasSize);
    }

    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton(
        "RenderViewportCanvas",
        canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImRect rect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (viewportTextureId == 0) {
        DrawViewportBackground(drawList, rect, m_State);
    }

    const RenderContracts::ViewportInputFrame inputFrame {
        rect,
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

    if (compiledReady) {
        bool openContextMenu = false;
        const RenderContracts::SceneChangeSet changeSet =
            m_ViewportController.HandleInput(inputFrame, m_ActiveSnapshot, m_CompiledScene, m_State, openContextMenu);
        ApplySceneChange(changeSet);
        if (openContextMenu) {
            ImGui::OpenPopup("RenderViewportContext");
        }

        m_ViewportController.DrawGizmo(drawList, inputFrame, m_ActiveSnapshot, m_CompiledScene, m_State);
    }

    DrawViewportOverlay(drawList, rect, m_State, m_ViewportController.IsNavigationActive());
    if (!m_LastViewportError.empty()) {
        drawList->AddRectFilled(
            ImVec2(rect.Min.x + 12.0f, rect.Min.y + 12.0f),
            ImVec2(rect.Min.x + 420.0f, rect.Min.y + 56.0f),
            IM_COL32(48, 16, 16, 210),
            8.0f);
        drawList->AddText(ImVec2(rect.Min.x + 24.0f, rect.Min.y + 22.0f), IM_COL32(255, 212, 200, 255), m_LastViewportError.c_str());
    }

    if (ImGui::BeginPopup("RenderViewportContext")) {
        if (ImGui::MenuItem("Add Sphere")) {
            m_State.AddPrimitive(PrimitiveType::Sphere);
        }
        if (ImGui::MenuItem("Add Cube")) {
            m_State.AddPrimitive(PrimitiveType::Cube);
        }
        if (ImGui::MenuItem("Add Plane")) {
            m_State.AddPrimitive(PrimitiveType::Plane);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Add Point Light")) {
            m_State.AddLight(LightType::Point);
        }
        if (ImGui::MenuItem("Add Spot Light")) {
            m_State.AddLight(LightType::Spot);
        }
        if (ImGui::MenuItem("Add Area Light")) {
            m_State.AddLight(LightType::Area);
        }
        if (ImGui::MenuItem("Add Directional Light")) {
            m_State.AddLight(LightType::Directional);
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

void RenderTab::RenderScenePanel(float width) {
    ImGui::BeginChild("RenderScenePanel", ImVec2(width, 0.0f), true);
    ImGui::Text("Scene");
    ImGui::Separator();
    const bool pathTraceMode = m_State.GetSettings().viewMode == ViewMode::PathTrace;

    if (EditStringField("Scene Label", m_State.GetSceneMetadata().label)) {
        m_State.MarkDisplayDirty("Scene label changed.");
    }
    if (EditStringField("Output Name", m_State.GetSettings().finalRender.outputName)) {
        m_State.MarkDisplayDirty("Output name changed.");
    }

    ImGui::Spacing();
    ImGui::Text("Primitives");
    ImGui::Separator();
    for (const Primitive& primitive : m_State.GetPrimitives()) {
        ImGui::PushID(static_cast<int>(primitive.id));
        const bool selected =
            m_State.GetSelection().type == SelectionType::Primitive &&
            m_State.GetSelection().id == primitive.id;
        const std::string label = std::string(PrimitiveTypeLabel(primitive.type)) + "  " + primitive.name;
        if (ImGui::Selectable(label.c_str(), selected)) {
            m_State.SelectPrimitive(primitive.id);
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Text("Lights");
    ImGui::Separator();
    for (const Light& light : m_State.GetLights()) {
        ImGui::PushID(static_cast<int>(light.id));
        const bool selected =
            m_State.GetSelection().type == SelectionType::Light &&
            m_State.GetSelection().id == light.id;
        const std::string label = std::string(LightTypeLabel(light.type)) + "  " + light.name;
        if (ImGui::Selectable(label.c_str(), selected)) {
            m_State.SelectLight(light.id);
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Text("Cameras");
    ImGui::Separator();
    const bool cameraSelected = m_State.GetSelection().type == SelectionType::Camera;
    if (ImGui::Selectable("Camera  Render Camera", cameraSelected)) {
        m_State.SelectCamera();
    }

    ImGui::Spacing();
    ImGui::Text("Materials");
    ImGui::Separator();
    for (const Material& material : m_State.GetMaterials()) {
        ImGui::BulletText("%s (%s)", material.name.c_str(), BaseMaterialLabel(material.baseMaterial));
    }

    ImGui::Spacing();
    ImGui::Text("Render Settings");
    ImGui::Separator();
    if (pathTraceMode) {
        ImGui::TextDisabled("Viewport accumulation is uncapped while Path Trace accumulation is enabled.");
    } else if (ImGui::SliderInt("Preview Samples", &m_State.GetSettings().previewSampleTarget, 1, 1024)) {
        m_State.MarkTransportDirty("Preview sample target changed.");
    }
    if (ImGui::Checkbox("Accumulate", &m_State.GetSettings().accumulationEnabled)) {
        m_State.MarkTransportDirty("Accumulation state changed.");
    }
    if (ImGui::SliderInt("Max Bounces", &m_State.GetSettings().maxBounceCount, 1, 16)) {
        m_State.MarkTransportDirty("Max bounce count changed.");
    }
    if (pathTraceMode) {
        int debugMode = static_cast<int>(m_State.GetSettings().pathTraceDebugMode);
        if (ImGui::Combo(
                "Debug View",
                &debugMode,
                "None\0Selected Ray Log\0Refracted Source Class\0Self-Hit Heatmap\0")) {
            m_State.GetSettings().pathTraceDebugMode = static_cast<PathTraceDebugMode>(debugMode);
            m_State.MarkDisplayDirty("Path-trace debug view changed.");
        }

        if (m_State.GetSettings().pathTraceDebugMode == PathTraceDebugMode::SelectedRayLog) {
            ImGui::TextDisabled("Set X/Y to -1 to trace the viewport center ray.");
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
            if (debugReadback.valid) {
                ImGui::TextWrapped(
                    "Selected ray pixel: (%d, %d)  Bounces logged: %d",
                    debugReadback.pixelX,
                    debugReadback.pixelY,
                    debugReadback.bounceCount);
                for (int bounceIndex = 0; bounceIndex < debugReadback.bounceCount; ++bounceIndex) {
                    const RenderPathTrace::PathTraceDebugBounce& bounce =
                        debugReadback.bounces[static_cast<std::size_t>(bounceIndex)];
                    ImGui::Separator();
                    ImGui::TextWrapped(
                        "Bounce %d  |  t %.4f  |  Object %d  |  Material %d  |  FrontFace %s  |  InsideMedium %s",
                        bounceIndex,
                        bounce.hitT,
                        bounce.objectId,
                        bounce.materialIndex,
                        bounce.frontFace ? "true" : "false",
                        bounce.insideMedium ? "true" : "false");
                    ImGui::TextWrapped(
                        "Eta %.3f -> %.3f  |  Ratio %.3f  |  Fresnel %.3f  |  Decision %s",
                        bounce.etaI,
                        bounce.etaT,
                        bounce.etaRatio,
                        bounce.fresnel,
                        PathTraceDebugDecisionLabel(bounce.decision));
                    ImGui::TextWrapped(
                        "Geom N (%.3f, %.3f, %.3f)  |  Shade N (%.3f, %.3f, %.3f)",
                        bounce.geometricNormal.x,
                        bounce.geometricNormal.y,
                        bounce.geometricNormal.z,
                        bounce.shadingNormal.x,
                        bounce.shadingNormal.y,
                        bounce.shadingNormal.z);
                    ImGui::TextWrapped(
                        "Spawn O (%.3f, %.3f, %.3f)  |  Spawn D (%.3f, %.3f, %.3f)",
                        bounce.spawnedOrigin.x,
                        bounce.spawnedOrigin.y,
                        bounce.spawnedOrigin.z,
                        bounce.spawnedDirection.x,
                        bounce.spawnedDirection.y,
                        bounce.spawnedDirection.z);
                }
            } else {
                ImGui::TextDisabled("Selected ray log populates after the next completed PT sample.");
            }
        } else if (m_State.GetSettings().pathTraceDebugMode == PathTraceDebugMode::RefractedSourceClass) {
            ImGui::TextDisabled("Green = emissive backdrop, red = dark backdrop, blue = miss.");
        } else if (m_State.GetSettings().pathTraceDebugMode == PathTraceDebugMode::SelfHitHeatmap) {
            ImGui::TextDisabled("Red regions indicate repeated tiny-t same-object hits.");
        }
    }
    if (ImGui::InputInt("Final Width", &m_State.GetSettings().finalRender.resolutionX)) {
        m_State.GetSettings().finalRender.resolutionX = std::clamp(m_State.GetSettings().finalRender.resolutionX, 320, 4096);
        m_State.MarkDisplayDirty("Final width changed.");
    }
    if (ImGui::InputInt("Final Height", &m_State.GetSettings().finalRender.resolutionY)) {
        m_State.GetSettings().finalRender.resolutionY = std::clamp(m_State.GetSettings().finalRender.resolutionY, 180, 4096);
        m_State.MarkDisplayDirty("Final height changed.");
    }
    if (ImGui::InputInt("Final Samples", &m_State.GetSettings().finalRender.sampleTarget)) {
        m_State.GetSettings().finalRender.sampleTarget = std::clamp(m_State.GetSettings().finalRender.sampleTarget, 1, 8192);
        m_State.MarkDisplayDirty("Final sample target changed.");
    }
    if (ImGui::InputInt("Final Bounces", &m_State.GetSettings().finalRender.maxBounceCount)) {
        m_State.GetSettings().finalRender.maxBounceCount = std::clamp(m_State.GetSettings().finalRender.maxBounceCount, 1, 32);
        m_State.MarkDisplayDirty("Final bounce count changed.");
    }

    ImGui::Spacing();
    ImGui::Text("Environment");
    ImGui::Separator();
    if (ImGui::Checkbox("Environment Enabled", &m_State.GetSceneMetadata().environmentEnabled)) {
        m_State.MarkTransportDirty("Environment enabled state changed.");
    }
    if (ImGui::SliderFloat("Environment Intensity", &m_State.GetSceneMetadata().environmentIntensity, 0.0f, 4.0f)) {
        m_State.MarkTransportDirty("Environment intensity changed.", RenderContracts::ResetClass::PartialAccumulation);
    }
    if (!pathTraceMode) {
        if (ImGui::Checkbox("Fog Enabled", &m_State.GetSceneMetadata().fogEnabled)) {
            m_State.MarkTransportDirty("Fog enabled state changed.");
        }
        if (EditColor3Field("Fog Color", m_State.GetSceneMetadata().fogColor)) {
            m_State.MarkTransportDirty("Fog color changed.");
        }
        if (ImGui::SliderFloat("Fog Density", &m_State.GetSceneMetadata().fogDensity, 0.0f, 1.0f)) {
            m_State.MarkTransportDirty("Fog density changed.");
        }
        if (ImGui::SliderFloat("Fog Anisotropy", &m_State.GetSceneMetadata().fogAnisotropy, -0.95f, 0.95f)) {
            m_State.MarkTransportDirty("Fog anisotropy changed.");
        }
    } else {
        ImGui::TextDisabled("Volumes and fog stay hidden in PT Core until the volume slice lands.");
    }

    ImGui::Spacing();
    ImGui::Text("Camera");
    ImGui::Separator();
    if (ImGui::Button("Select Render Camera")) {
        m_State.SelectCamera();
    }
    if (EditVec3Field("Camera Position", m_State.GetCamera().position)) {
        m_State.MarkTransportDirty("Camera position changed.");
    }
    if (ImGui::SliderFloat("Yaw", &m_State.GetCamera().yawDegrees, -180.0f, 180.0f)) {
        m_State.MarkTransportDirty("Camera yaw changed.");
    }
    if (ImGui::SliderFloat("Pitch", &m_State.GetCamera().pitchDegrees, -89.0f, 89.0f)) {
        m_State.MarkTransportDirty("Camera pitch changed.");
    }
    if (ImGui::SliderFloat("FOV", &m_State.GetCamera().fieldOfViewDegrees, 20.0f, 90.0f)) {
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
    if (ImGui::SliderFloat("Move Speed", &m_State.GetCamera().movementSpeed, 1.0f, 20.0f)) {
        m_State.MarkDisplayDirty("Camera move speed changed.");
    }

    ImGui::Spacing();
    ImGui::Text("Phase Notes");
    ImGui::Separator();
    ImGui::TextWrapped("This slice keeps Unlit as the fast authoring view and runs Path Trace through the spectral compute transport core for spheres, cubes, planes, baseline lights, rough/smooth dielectric glass, and the first ordered material-layer foundation with clear coat over diffuse, metal, and dielectric bases.");
    if (!m_LatestFinalAssetFileName.empty()) {
        ImGui::TextWrapped("Latest saved asset: %s", m_LatestFinalAssetFileName.c_str());
    }

    ImGui::EndChild();
}

void RenderTab::RenderResetScenePopup() {
    if (!m_OpenResetSceneConfirm) {
        return;
    }

    if (!ImGui::BeginPopupModal("Discard Unsaved Render Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Start a new render scene and discard the current unsaved foundation state?");
    ImGui::TextDisabled("This clears the current in-memory render scene.");
    ImGui::Spacing();

    if (ImGui::Button("Discard And Reset", ImVec2(160.0f, 0.0f))) {
        m_State.ResetToDefaultScene();
        m_ViewportController.Reset();
        m_LastViewportError.clear();
        m_LatestFinalAssetFileName.clear();
        m_StatusText = "Started a new foundation scene.";
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

    ImGui::SetNextWindowSize(ImVec2(360.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Render Camera", &m_ShowCameraWindow)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Select Camera")) {
        m_State.SelectCamera();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        m_State.GetCamera() = RenderFoundation::Camera {};
        m_State.MarkTransportDirty("Camera reset to the default view.");
    }

    ImGui::Separator();
    ImGui::TextWrapped("The render camera is now a first-class scene object. Edit it here or select it from the scene list and use the inspector.");

    if (EditVec3Field("Position##CameraWindow", m_State.GetCamera().position)) {
        m_State.MarkTransportDirty("Camera position changed.");
    }
    if (ImGui::SliderFloat("Yaw##CameraWindow", &m_State.GetCamera().yawDegrees, -180.0f, 180.0f)) {
        m_State.MarkTransportDirty("Camera yaw changed.");
    }
    if (ImGui::SliderFloat("Pitch##CameraWindow", &m_State.GetCamera().pitchDegrees, -89.0f, 89.0f)) {
        m_State.MarkTransportDirty("Camera pitch changed.");
    }
    if (ImGui::SliderFloat("FOV##CameraWindow", &m_State.GetCamera().fieldOfViewDegrees, 20.0f, 90.0f)) {
        m_State.MarkTransportDirty("Camera FOV changed.");
    }
    if (ImGui::SliderFloat("Exposure##CameraWindow", &m_State.GetCamera().exposure, 0.1f, 4.0f)) {
        m_State.MarkDisplayDirty("Camera exposure changed.");
    }
    if (ImGui::SliderFloat("Move Speed##CameraWindow", &m_State.GetCamera().movementSpeed, 1.0f, 20.0f)) {
        m_State.MarkDisplayDirty("Camera move speed changed.");
    }

    ImGui::End();
}

bool RenderTab::SyncCompiledScene(std::string& errorMessage) {
    m_ActiveSnapshot = m_State.BuildSnapshot();
    m_LastViewportError.clear();
    if (!m_SceneCompiler.Compile(m_ActiveSnapshot, m_State.GetSettings(), m_CompiledScene, errorMessage)) {
        m_CompiledScene.valid = false;
        return false;
    }
    return true;
}

void RenderTab::ApplySceneChange(const RenderContracts::SceneChangeSet& changeSet) {
    if (changeSet.resetClass == RenderContracts::ResetClass::None) {
        return;
    }

    m_State.ApplyExternalChange(changeSet);
    if (!changeSet.reason.empty()) {
        m_StatusText = changeSet.reason;
    }
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
