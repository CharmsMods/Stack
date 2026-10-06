#include "Editor/Internal/RawLab/RawLabAreaImage.h"
#include "Editor/Internal/RawLab/RawLabAreaMask.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
using namespace RawRecipe;
RawLabAreaImageResult InteractRawLabAreaImage(EditorModuleTypes::RawZoneAreaUiState& ui,
    RawDevelopmentRecipe& recipe, const RawDevelopmentGraphScopeReadback& scope,
    const ImVec2& minimum, const ImVec2& maximum,const Async::ActivityMetadata& activity,
    const RawLabAreaImageMappings* mappings) {
    RawLabAreaImageResult result;
    const ImRect image(minimum, maximum);
    ImRect hit = image; hit.ClipWith(ImGui::GetCurrentWindow()->ClipRect);
    if (hit.GetWidth() <= 0 || hit.GetHeight() <= 0) {
        if (ui.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            result.finished = true;
            result.maskEdited = ui.changed && ui.gestureMode != 0;
            ui.active = false;
        }
        return result;
    }
    auto& areas = recipe.localRange.areas;
    auto findArea = [&](const std::string& id) {
        return std::find_if(areas.begin(), areas.end(), [&](const auto& a) {return a.id == id;});
    };
    const ImVec2 saved = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(hit.Min);
    ImGui::InvisibleButton("##ZoneAreaImage", hit.GetSize(), ImGuiButtonFlags_MouseButtonLeft);
    const bool held = ImGui::IsItemActive();
    const auto& io = ImGui::GetIO();
    const ImVec2 canvasPoint{
        std::clamp((io.MousePos.x - image.Min.x) / image.GetWidth(), 0.0f, 1.0f),
        std::clamp((io.MousePos.y - image.Min.y) / image.GetHeight(), 0.0f, 1.0f)};
    const auto pointFor = [&](const std::string& id) -> std::optional<RawZoneBrushPoint> {
        if (!mappings) return ZoneAreaSourcePoint(canvasPoint.x,canvasPoint.y,recipe.cropRotation,true);
        const auto mapping = mappings->area(id);
        if (!mapping) return std::nullopt;
        const auto p = mapping->Local(canvasPoint);
        return RawZoneBrushPoint{p.x,p.y};
    };
    bool changed = false;
    if (ImGui::IsItemActivated()) {
        if (ui.mode == 0) {
            float best = 0.05f;
            std::string selected;
            for (const auto& a : areas) {
                if (!a.enabled) continue;
                if (mappings && (!scope.valid || std::none_of(scope.zoneAreas.begin(),scope.zoneAreas.end(),[&](const auto& stats) {
                    return stats.areaId==a.id && stats.maskFingerprint==ZoneAreaMaskFingerprint(a) && stats.maskPreview;
                }))) continue;
                const auto mask=Stack::Editor::RawLabInternal::ResolveRawLabAreaMask(ui,a,recipe,scope,activity);
                const auto point = mappings ? mappings->measurement.Local(canvasPoint) : ImVec2{};
                const auto authored = pointFor(a.id);
                float coverage = mask && authored ? Stack::Editor::RawLabInternal::SampleRawLabAreaMask(*mask,
                    mappings ? point.x : authored->u,mappings ? point.y : authored->v,recipe.cropRotation) : 0;
                if (a.id == ui.selectedId && coverage > 0.05f) coverage += 0.05f;
                if (coverage > best) { best = coverage; selected = a.id; }
            }
            if (!selected.empty()) ui.selectedId = selected;
            else { ImGui::ClearActiveID(); ImGui::SetCursorScreenPos(saved); return result; }
        }
        auto area = findArea(ui.selectedId);
        if (area != areas.end()) {
            const auto point = pointFor(area->id);
            if (!point) { ImGui::ClearActiveID(); ImGui::SetCursorScreenPos(saved); return result; }
            if (ui.mode != 0 && area->strokes.size() >= kMaxZoneStrokes) {
                ImGui::ClearActiveID();
                ImGui::SetCursorScreenPos(saved);
                return result;
            }
            ui.beforeGesture = recipe;
            ui.gestureAreaId = area->id; ui.pressY = io.MousePos.y; ui.pressOffsetEv = area->offsetEv;
            ui.active = true; ui.changed = false; ui.gestureMode = ui.mode;
            if (ui.mode != 0 && area->strokes.size() < kMaxZoneStrokes) {
                if (area->strokes.empty()) {
                    if (mappings) area->sourceAspect = mappings->area(area->id)->sourceAspect;
                    else {
                        const auto& crop = recipe.cropRotation;
                        float w = image.GetWidth() / (crop.cropEnabled ? crop.cropWidth : 1);
                        float h = image.GetHeight() / (crop.cropEnabled ? crop.cropHeight : 1);
                        if (crop.rotationDegrees == 90 || crop.rotationDegrees == 270) std::swap(w, h);
                        area->sourceAspect = w / h;
                    }
                }
                RawZoneBrushStroke stroke;
                stroke.radius = ui.radius; stroke.softness = ui.softness; stroke.opacity = ui.opacity;
                stroke.erase = ui.mode == 2; stroke.path.push_back(*point);
                stroke.followEdges=ui.followEdges; stroke.edgeSensitivity=ui.edgeSensitivity;
                area->strokes.push_back(std::move(stroke));
                changed = true;
            }
        }
    }
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (ui.active && (held || released) && ImGui::IsMousePosValid()) {
        auto area = findArea(ui.gestureAreaId);
        const auto point = area != areas.end() ? pointFor(area->id) : std::nullopt;
        if (area != areas.end() && point) {
            if (ui.gestureMode == 0) {
                const float next = std::clamp(ui.pressOffsetEv + (ui.pressY - io.MousePos.y) / 120.0f, -16.0f, 16.0f);
                changed |= next != area->offsetEv; area->offsetEv = next;
            } else if (!area->strokes.empty()) {
                auto& stroke = area->strokes.back();
                const auto& last = stroke.path.back();
                const float d = std::hypot((point->u-last.u)*std::max(1.0f,area->sourceAspect),
                    (point->v-last.v)*std::max(1.0f,1/area->sourceAspect));
                if (d >= (released ? .000001f : std::max(.00001f, stroke.radius*.05f)) && stroke.path.size() < kMaxZoneStrokePoints) {
                    stroke.path.push_back(*point); changed = true;
                }
            }
        }
    }
    const bool cancel = ui.active && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || io.AppFocusLost);
    if (cancel) {
        recipe = ui.beforeGesture;
        ImGui::ClearActiveID();
        result.changed = true; result.finished = true; result.cancelled = true;
        if (ui.maskWorker) ui.maskWorker->Cancel();
        ui.masks.erase(ui.gestureAreaId);
        ui.active = false; ui.changed = false; ui.overlayKey = 0;
    } else if (ui.active) {
        ui.changed |= changed;
        if (changed) recipe.localRange.enabled = true;
        result.changed = changed;
        result.active = held;
        result.finished = !held;
        result.maskEdited = !held && ui.changed && ui.gestureMode != 0;
        if (!held) ui.active = false;
    }
    ImGui::SetCursorScreenPos(saved);
    return result;
}
}
