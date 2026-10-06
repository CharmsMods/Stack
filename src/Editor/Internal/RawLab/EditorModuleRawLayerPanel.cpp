#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "App/WorkspacePresentation.h"

#include <algorithm>
#include <cstdio>

using namespace Stack::Project;
using namespace Stack::Editor::RawLabInternal;


void EditorModule::RenderRawLayerPanelContents() {
    const auto phase = m_Project->lifecycle.Phase();
    ImGui::BeginDisabled(Stack::Workspace::IsPreview() ||
        phase == ProjectLifecyclePhase::Conflict || phase == ProjectLifecyclePhase::ReadOnlyRecovery);
    if (ImGui::SmallButton("+ Adjustment")) {
        auto candidate = m_Project->rawLayers.State();
        const auto id = AddRawAdjustmentLayer(candidate);
        if (ApplyRawLayerStackEdit(std::move(candidate))) SelectRawAdjustmentLayer(id);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_Project->rawLayers.CanUndo());
    if (ImGui::SmallButton("Undo")) UndoRawLayerEdit();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_Project->rawLayers.CanRedo());
    if (ImGui::SmallButton("Redo")) RedoRawLayerEdit();
    ImGui::EndDisabled();

    auto* selected = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
    if (!selected && !m_SelectedRawAdjustmentLayer.empty()) SelectRawAdjustmentLayer({});
    ImGui::BeginDisabled(!selected);
    float opacity = selected ? selected->opacity * 100 : 100;
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 60));
    const bool opacityChanged = ImGui::SliderFloat("Opacity", &opacity, 0, 100, "%.0f%%");
    if (ImGui::IsItemActivated()) m_Project->rawLayers.BeginGesture();
    if (opacityChanged) {
        auto candidate = m_Project->rawLayers.State();
        if (auto* layer = FindRawAdjustmentLayer(candidate, m_SelectedRawAdjustmentLayer)) {
            layer->opacity = opacity / 100;
            ApplyRawLayerStackEdit(std::move(candidate));
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) m_Project->rawLayers.EndGesture();
    ImGui::EndDisabled();

    struct Row {
        std::string id, name;
        bool enabled;
        std::optional<RawMaskReference> mask;
        bool toolMasks;
    };
    std::vector<Row> rows;
    for (const auto& layer : m_Project->rawLayers.State().layers)
        rows.push_back({layer.id, layer.name, layer.enabled, layer.layerMask,
            std::any_of(layer.graph.GetLinks().begin(), layer.graph.GetLinks().end(), [](const auto& link) { return link.toSocketId == "maskIn"; })});
    const float rowHeight = std::max(46.0f, ImGui::GetFontSize() * 2 + 12);
    const float thumbnailSize = rowHeight - 8;
    const float available = ImGui::GetContentRegionAvail().y;
    const float listHeight = std::max(rowHeight, available - 62);
    std::string deleteId;
    const auto beginRename = [&](const Row& row) {
        m_RawLayerPanel.renameId = row.id;
        std::snprintf(m_RawLayerPanel.renameBuffer, sizeof(m_RawLayerPanel.renameBuffer), "%s", row.name.c_str());
        m_RawLayerPanel.focusRename = true;
    };
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4,4));
    ImGui::BeginChild("RawLayerRows", ImVec2(0, listHeight), false);
    const std::string dragOwner = std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "/";
    const auto dragSource = [&](const Row& row) {
        if (ImGui::BeginDragDropSource()) {
            const auto payload = dragOwner + row.id;
            ImGui::SetDragDropPayload("RAW_ADJUSTMENT_LAYER", payload.c_str(), payload.size()+1);
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::EndDragDropSource();
        }
    };
    for (std::size_t reverse = rows.size()+1; reverse > 0; --reverse) {
        const bool background = reverse == 1;
        const Row row = background ? Row{kRawBackgroundId, "Background", true, m_Project->rawLayers.State().background.layerMask, false} : rows[reverse-2];
        const std::string selectionId = background ? std::string{} : row.id;
        const bool active = m_SelectedRawAdjustmentLayer == selectionId;
        const bool maskActive = active && row.mask && m_EditingRawLayerMask &&
            row.mask->layerId == m_EditingRawLayerMask->layerId && row.mask->outputId == m_EditingRawLayerMask->outputId;
        ImGui::PushID(row.id.c_str());
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const ImRect bounds(start, ImVec2(start.x+width, start.y+rowHeight));
        if (active) ImGui::GetWindowDrawList()->AddRectFilled(bounds.Min, bounds.Max,
            ImGui::GetColorU32(ImGuiCol_Header), 3);
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y+(rowHeight-20)*.5f));
        ImGui::BeginDisabled(background);
        if (BypassEyeButton("visibility", row.enabled, "Hide layer", "Show layer")) {
            auto candidate = m_Project->rawLayers.State();
            if (auto* layer = FindRawAdjustmentLayer(candidate, row.id)) {
                layer->enabled = !layer->enabled;
                ApplyRawLayerStackEdit(std::move(candidate));
            }
        }
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(ImVec2(start.x+30, start.y+4));
        if (RenderRawLayerThumbnail(row.id, {}, ImVec2(thumbnailSize,thumbnailSize), active && !m_EditingRawLayerMask))
            SelectRawAdjustmentLayer(selectionId);
        if (!background) dragSource(row);
        float textX = start.x + 36 + thumbnailSize;
        if (row.mask) {
            ImGui::SetCursorScreenPos(ImVec2(textX, start.y+4));
            if (RenderRawLayerThumbnail(row.mask->layerId, row.mask->outputId,
                    ImVec2(thumbnailSize,thumbnailSize), maskActive)) {
                SelectRawAdjustmentLayer(selectionId);
                if (m_SelectedRawAdjustmentLayer == selectionId) {
                    m_EditingRawLayerMask = row.mask;
                    m_RawLayerMaskGenerator = -1;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OpenRawLayerMaskGraph(row.mask->layerId);
                }
            }
            textX += thumbnailSize + 6;
        }
        ImGui::SetCursorScreenPos(ImVec2(textX, start.y+4));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(row.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        if (ImGui::Selectable(row.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick,
                ImVec2(std::max(1.0f,bounds.Max.x-textX), ImGui::GetFontSize()+2))) {
            SelectRawAdjustmentLayer(selectionId);
            if (!background && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) beginRename(row);
        }
        ImGui::PopStyleColor();
        if (!background) dragSource(row);
        if (ImGui::BeginPopupContextItem("layer-actions")) {
            if (ImGui::MenuItem("Open layer Graph")) OpenRawLayerMaskGraph(row.id);
            if (!background) {
                if (ImGui::MenuItem("Rename")) beginRename(row);
                if (ImGui::MenuItem("Delete layer")) deleteId = row.id;
            } else ImGui::TextDisabled("Original image and base development");
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(ImVec2(textX,start.y+rowHeight-ImGui::GetFontSize()-5));
        ImGui::TextDisabled("%s", background ? "Original / base edits" : !row.enabled ? "Hidden" :
            row.toolMasks ? "Adjustment / tool masks" : "Adjustment");

        if (ImGui::BeginDragDropTargetCustom(bounds, ImGui::GetID("drop"))) {
            const bool above = !background && ImGui::GetMousePos().y < start.y+rowHeight*.5f;
            const float lineY = background || above ? start.y : bounds.Max.y;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x,lineY),ImVec2(bounds.Max.x,lineY),
                ImGui::GetColorU32(ImGuiCol_SliderGrabActive),2);
            if (const auto* payload = ImGui::AcceptDragDropPayload("RAW_ADJUSTMENT_LAYER", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                auto candidate = m_Project->rawLayers.State();
                const std::string payloadText(static_cast<const char*>(payload->Data), payload->DataSize-1);
                const std::string movedId = payloadText.compare(0,dragOwner.size(),dragOwner)==0 ?
                    payloadText.substr(dragOwner.size()) : std::string{};
                const auto* moved = FindRawAdjustmentLayer(candidate,movedId);
                if (moved && !movedId.empty() && movedId != kRawBackgroundId) {
                    const auto oldIndex = static_cast<std::size_t>(moved-candidate.layers.data());
                    std::size_t insertion = background ? 0 : reverse-2+(above ? 1 : 0);
                    if (oldIndex < insertion) --insertion;
                    if (MoveRawAdjustmentLayer(candidate,movedId,insertion,m_RawLayerStatus))
                        ApplyRawLayerStackEdit(std::move(candidate));
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(width,rowHeight));
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    if (m_RawLayerPanel.focusRename) ImGui::OpenPopup("Rename layer");
    if (ImGui::BeginPopup("Rename layer")) {
        if (m_RawLayerPanel.focusRename) { ImGui::SetKeyboardFocusHere(); m_RawLayerPanel.focusRename=false; }
        const bool enter = ImGui::InputText("##name",m_RawLayerPanel.renameBuffer,sizeof(m_RawLayerPanel.renameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (enter || ImGui::Button("Rename")) {
            auto candidate = m_Project->rawLayers.State();
            if (auto* layer = FindRawAdjustmentLayer(candidate,m_RawLayerPanel.renameId)) {
                layer->name = m_RawLayerPanel.renameBuffer;
                ApplyRawLayerStackEdit(std::move(candidate));
            }
            m_RawLayerPanel.renameId.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_RawLayerPanel.renameId.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!ImGui::IsPopupOpen("Rename layer")) m_RawLayerPanel.renameId.clear();
    selected = FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
    if (selected) {
        if (ImGui::SmallButton(selected->layerMask ? "Layer mask..." : "+ Layer mask")) ImGui::OpenPopup("layer-mask");
        if (ImGui::BeginPopup("layer-mask")) {
            RenderRawLayerMaskAttachment("Whole layer",true);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Graph")) OpenRawLayerMaskGraph(m_SelectedRawAdjustmentLayer);
        LabTooltip("Edit this layer's operations, masks and branches in Graph.");
        ImGui::SameLine();
        if (ImGui::SmallButton("...")) ImGui::OpenPopup("selected-layer-actions");
        if (ImGui::BeginPopup("selected-layer-actions")) {
            if (ImGui::MenuItem("Rename")) {
                const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
                if (layer) beginRename({layer->id,layer->name,layer->enabled,layer->layerMask,false});
            }
            if (ImGui::MenuItem("Delete layer")) deleteId=m_SelectedRawAdjustmentLayer;
            ImGui::EndPopup();
        }
    } else ImGui::TextDisabled("Background owns the base RAW development.");
    if (!deleteId.empty()) {
        auto candidate = m_Project->rawLayers.State();
        candidate.layers.erase(std::remove_if(candidate.layers.begin(),candidate.layers.end(),
            [&](const auto& layer){return layer.id==deleteId;}),candidate.layers.end());
        if (ApplyRawLayerStackEdit(std::move(candidate))) {
            if (m_RawLayerMaskWorkspace && m_RawLayerMaskWorkspace->layerId==deleteId) m_RawLayerMaskWorkspace.reset();
            if (m_SelectedRawAdjustmentLayer==deleteId) SelectRawAdjustmentLayer({});
        }
    }
    ImGui::EndDisabled();
    if (!m_RawLayerStatus.empty()) ImGui::TextWrapped("%s",m_RawLayerStatus.c_str());
}
