#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Editor/Internal/RawLab/RawLabAreaGraph.h"
#include "Editor/Internal/RawLab/RawLabAreaMask.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cstdio>

using namespace Stack::Editor::RawLabInternal;

bool EditorModule::RenderRawWorkspaceLabAreas(RawWorkspaceEditContext& context, RawLabControlSection section) {
    auto& ui = m_RawWorkspaceLabUi.zoneAreas;
    const std::string identity = GetActiveRawWorkspacePreviewIdentity();
    if (ui.sourceKey != identity) {
        if (ui.overlayTexture) glDeleteTextures(1, &ui.overlayTexture);
        ui = {};
        ui.sourceKey = identity;
    }
    auto& areas = context.recipe.localRange.areas;
    for (auto it=ui.masks.begin(); it!=ui.masks.end();) {
        if (std::none_of(areas.begin(),areas.end(),[&](const auto& a){return a.id==it->first;})) it=ui.masks.erase(it);
        else ++it;
    }
    ui.history.Initialize(areas);
    bool changed = false;
    if (ShowsRawLabSettings(section)) {
        const bool commandKeys = CanConsumeEditorCommandKeys() && !ImGui::GetIO().WantTextInput &&
            !ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl;
        const bool undoKey = commandKeys && !ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false);
        const bool redoKey = commandKeys && ((ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) || ImGui::IsKeyPressed(ImGuiKey_Y, false));
        if ((BareTextButton("Undo", false, ui.history.CanUndo()) || undoKey) && ui.history.CanUndo()) {
            areas = ui.history.Undo(); changed = true;
        }
        ImGui::SameLine();
        if ((BareTextButton("Redo", false, ui.history.CanRedo()) || redoKey) && ui.history.CanRedo()) {
            areas = ui.history.Redo(); changed = true;
        }
        if (BareTextButton("+ Add Area", false, areas.size() < Stack::RawRecipe::kMaxZoneAreas)) {
            Stack::RawRecipe::RawZoneArea area;
            unsigned int index = 1;
            do { area.id = "area-" + std::to_string(index++); }
            while (std::any_of(areas.begin(), areas.end(), [&](const auto& a) {return a.id == area.id;}));
            area.name = "Area " + std::to_string(index - 1);
            // Keep the source aspect before authored rotation and crop.
            float width = float(m_ViewportOutputExpectedNativeWidth), height = float(m_ViewportOutputExpectedNativeHeight);
            const auto& crop = context.recipe.cropRotation;
            if (crop.cropEnabled) {width /= crop.cropWidth; height /= crop.cropHeight;}
            if (crop.rotationDegrees == 90 || crop.rotationDegrees == 270) std::swap(width, height);
            if (width > 0 && height > 0) area.sourceAspect = width / height;
            ui.selectedId = area.id;
            areas.push_back(std::move(area));
            ui.mode = 1; ui.showMask = true; ui.effectPreview = false;
            context.recipe.localRange.enabled = true;
            changed = true;
            m_RawWorkspaceLocalRangeTargetMode = false;
            m_RawWorkspaceLocalRangeOverlayMode = "none";
            ClearRawWorkspaceLocalRangeOverlayState();
        }
    }
    if (areas.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("Add an area, then paint over the image. Switch to Adjust to change its exposure.");
        return changed;
    }
    if (std::none_of(areas.begin(), areas.end(), [&](const auto& a) {return a.id == ui.selectedId;})) ui.selectedId = areas.front().id;
    if (ShowsRawLabSettings(section)) {
        for (auto& area : areas) {
            ImGui::PushID(area.id.c_str());
            if (ImGui::Checkbox("##enabled", &area.enabled)) changed = true;
            ImGui::SameLine();
            char label[192]; std::snprintf(label, sizeof(label), "%s    %+.2f EV", area.name.c_str(), area.offsetEv);
            if (BareTextButton(label, ui.selectedId == area.id)) {
                for (auto& entry : ui.graphs) entry.second.interaction = {};
                ui.selectedId = area.id;
            }
            ImGui::PopID();
        }
    }
    auto selected = std::find_if(areas.begin(), areas.end(), [&](const auto& a) {return a.id == ui.selectedId;});
    if (selected == areas.end()) return changed;
    auto& area = *selected;
    ImGui::PushID(area.id.c_str());
    if (ShowsRawLabSettings(section)) {
        ImGui::Spacing();
        for (int mode = 0; mode < 3; ++mode) {
            if (mode) ImGui::SameLine(0, 4);
            if (BareToolIslandButton(mode == 0 ? "Adjust" : mode == 1 ? "Add" : "Erase", ui.mode == mode)) {
                ui.mode = mode;
                m_RawWorkspaceLocalRangeTargetMode = false;
                m_RawWorkspaceLocalRangeOverlayMode = "none";
                ClearRawWorkspaceLocalRangeOverlayState();
            }
        }
        if (ui.mode != 0) {
            if (BareToolIslandButton("Soft", !ui.followEdges)) ui.followEdges = false;
            ImGui::SameLine(0,4);
            if (BareToolIslandButton("Follow edges", ui.followEdges)) ui.followEdges = true;
            if (ui.followEdges) {
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##edgeSensitivity", &ui.edgeSensitivity, 0, 1, "Edge sensitivity %.2f");
                ImGui::TextWrapped(RawLabAreaGuide(ui,context.recipe,m_RawWorkspaceGraphScopeReadback)
                    ? "Start inside the area. Soft Add/Erase makes direct corrections."
                    : "You can paint while the edge guide prepares.");
            }
            float radius = ui.radius * 100;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##brushSize", &radius, .1f, .01f, 100, "Radius %.2f%%")) ui.radius = radius / 100;
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##brushSoftness", &ui.softness, .005f, 0, 1, "Softness %.2f");
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##brushOpacity", &ui.opacity, .005f, 0, 1, "Opacity %.2f");
        }
        if (ui.maskPending) ImGui::TextDisabled("Updating mask...");
        if (!area.strokes.empty() && ImGui::TreeNode("Last stroke")) {
            auto& stroke=area.strokes.back();
            changed |= ImGui::Checkbox("Follow edges##lastStroke", &stroke.followEdges);
            if (stroke.followEdges) {
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderFloat("##lastStrokeSensitivity", &stroke.edgeSensitivity, 0, 1, "Edge sensitivity %.2f");
            }
            ImGui::TreePop();
        }
        ImGui::Checkbox("Overlay", &ui.showMask);
        ImGui::SameLine(); ImGui::Checkbox("Brush cursor", &ui.showCursor);
        if (BareTextButton("Mask", !ui.effectPreview)) ui.effectPreview = false;
        ImGui::SameLine();
        if (BareTextButton("Effect", ui.effectPreview)) ui.effectPreview = true;
        char name[129]; std::snprintf(name, sizeof(name), "%s", area.name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##areaName", name, sizeof(name))) {area.name = name; changed = true;}
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##areaOffset", &area.offsetEv, .01f, -16, 16, "%+.2f EV offset")) changed = true;
    }
    const auto& scope = m_RawWorkspaceGraphScopeReadback;
    const auto stats = std::find_if(scope.zoneAreas.begin(), scope.zoneAreas.end(), [&](const auto& s) {return s.areaId == area.id;});
    if (ShowsRawLabGraph(section)) changed |= DrawRawLabAreaGraph(area, ui.graphs[area.id], stats == scope.zoneAreas.end() ? nullptr : &*stats, ui.active || ImGui::IsAnyItemActive());
    bool remove = false;
    if (ShowsRawLabSettings(section)) {
        if (BareTextButton("Reset gain")) {area.offsetEv = 0; for (auto& p : area.points) {p.deltaEv = 0; p.incoming = {}; p.outgoing = {};} changed = true;}
        ImGui::SameLine();
        remove = BareTextButton("Delete area");
    }
    ImGui::PopID();
    if (remove) {
        ui.masks.erase(area.id);
        ui.graphs.erase(area.id); areas.erase(selected);
        ui.selectedId = areas.empty() ? std::string() : areas.front().id;
        changed = true;
    }
    return changed;
}
