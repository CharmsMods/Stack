#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphSelectionExport.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Persistence/StackBinaryFormat.h"
#include "Presets/PresetManager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <imgui.h>
#include <string>
#include <vector>

namespace {

void PostNodeGraphPresetNotification(
    EditorModule* editor,
    UiNotificationSeverity severity,
    const std::string& message,
    const char* dedupeKey) {
    if (!editor || message.empty()) {
        return;
    }
    editor->ShowUiNotification(severity, message, dedupeKey ? dedupeKey : "");
}

ImU32 WithAlpha(ImVec4 color, float alpha) {
    color.w *= std::clamp(alpha, 0.0f, 1.0f) * ImGui::GetStyle().Alpha;
    return ImGui::ColorConvertFloat4ToU32(color);
}

std::string BuildPresetRevisionToken(const PresetEntry& preset) {
    return preset.id + "|" + preset.timestamp + "|" + preset.savedWithVersion + "|" + std::to_string(preset.nodeCount);
}

bool BuildPreviewGraphFromPayload(
    const StackBinaryFormat::json& graphPayload,
    EditorNodeGraph::Graph& outGraph,
    std::vector<std::shared_ptr<LayerBase>>& outLayers,
    std::string& outError) {
    outGraph.Clear();
    outLayers.clear();
    if (!graphPayload.is_object()) {
        outError = "Preset graph payload is missing.";
        return false;
    }

    const StackBinaryFormat::json layerArray = EditorNodeGraph::ExtractLayerArray(graphPayload);
    if (layerArray.is_array()) {
        for (const auto& layerData : layerArray) {
            const std::string type = layerData.value("type", "");
            std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
            if (!layer) {
                outLayers.push_back(nullptr);
                continue;
            }
            layer->Deserialize(layerData);
            outLayers.push_back(std::move(layer));
        }
    }

    EditorNodeGraph::DeserializeGraphPayload(
        graphPayload,
        outGraph,
        static_cast<int>(outLayers.size()),
        {},
        0,
        0,
        0);
    if (outGraph.GetNodes().empty() && outGraph.GetGroups().empty()) {
        outError = "Preset preview graph is empty.";
        return false;
    }
    return true;
}

struct PresetRowResult {
    bool pressed = false;
    bool hovered = false;
};

PresetRowResult RenderPresetRow(const PresetEntry& preset, float width, bool activePreview) {
    const float lineHeight = ImGui::GetTextLineHeight();
    const float rowHeight = lineHeight * 2.7f;
    ImGui::PushID(preset.id.c_str());
    PresetRowResult result;
    result.pressed = ImGui::InvisibleButton("##PresetRow", ImVec2(width, rowHeight));
    result.hovered = ImGui::IsItemHovered();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 muted = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const bool emphasized = activePreview || result.hovered;

    if (emphasized) {
        drawList->AddRectFilled(
            ImVec2(min.x + 2.0f, min.y + 2.0f),
            ImVec2(max.x - 2.0f, max.y - 2.0f),
            WithAlpha(accent, activePreview ? 0.10f : 0.06f),
            10.0f);
    }

    if (activePreview) {
        drawList->AddRectFilled(
            ImVec2(min.x, min.y + 6.0f),
            ImVec2(min.x + 3.0f, max.y - 6.0f),
            WithAlpha(accent, 0.95f),
            2.0f);
    }

    const std::string title = preset.displayName.empty() ? "Untitled Preset" : preset.displayName;
    const std::string detail =
        std::to_string(preset.nodeCount) + (preset.nodeCount == 1 ? " node" : " nodes") +
        " / " + std::to_string(preset.inputCount) + " in, " + std::to_string(preset.outputCount) + " out";

    drawList->AddText(
        ImVec2(min.x + 16.0f, min.y + 8.0f),
        WithAlpha(text, activePreview ? 1.0f : (result.hovered ? 0.98f : 0.92f)),
        title.c_str());
    drawList->AddText(
        ImVec2(min.x + 16.0f, min.y + 8.0f + lineHeight + 4.0f),
        WithAlpha(muted, result.hovered ? 0.90f : 0.80f),
        detail.c_str());

    if (result.hovered) {
        drawList->AddLine(
            ImVec2(min.x + 16.0f, max.y - 4.0f),
            ImVec2(min.x + std::min(width * 0.36f, 150.0f), max.y - 4.0f),
            WithAlpha(accent, 0.82f),
            2.0f);
    }

    ImGui::PopID();
    return result;
}

bool RenderFlatTextButton(const char* label, bool enabled = true) {
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    const bool pressed = ImGui::Button(label);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (enabled) {
        if (ImGui::IsItemHovered()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            drawList->AddLine(
                ImVec2(min.x, max.y),
                ImVec2(max.x, max.y),
                WithAlpha(accent, 0.82f),
                1.5f);
        }
    }
    if (!enabled) {
        ImGui::EndDisabled();
    }
    return enabled && pressed;
}

const PresetEntry* FindPresetById(
    const std::vector<std::shared_ptr<PresetEntry>>& presets,
    const std::string& presetId) {
    if (presetId.empty()) {
        return nullptr;
    }
    for (const auto& preset : presets) {
        if (preset && preset->id == presetId) {
            return preset.get();
        }
    }
    return nullptr;
}

void RenderUserPresetSection(
    EditorNodeGraphUI* ui,
    EditorModule* editor,
    const std::vector<std::shared_ptr<PresetEntry>>& presets,
    float rowWidth,
    const std::string& activePreviewId,
    char (&renameBuffer)[128]) {
    if (presets.empty()) {
        ImGui::TextWrapped("Select one or more nodes, right-click, then choose Save As Preset.");
        return;
    }

    for (const auto& preset : presets) {
        if (!preset) {
            continue;
        }

        const PresetRowResult row = RenderPresetRow(*preset, rowWidth, activePreviewId == preset->id);
        if (row.hovered) {
            ui->SetPresetPreviewHoverTarget(preset);
        }
        if (row.pressed) {
            StackBinaryFormat::json graphPayload;
            std::string error;
            if (PresetManager::Get().LoadPresetPayload(*preset, graphPayload, &error)) {
                std::string summary;
                if (ui->ApplyPresetPayload(editor, graphPayload, &summary)) {
                    PostNodeGraphPresetNotification(
                        editor,
                        UiNotificationSeverity::Success,
                        summary.empty() ? "Preset applied." : summary,
                        "editor-node-graph-preset");
                } else {
                    PostNodeGraphPresetNotification(
                        editor,
                        UiNotificationSeverity::Error,
                        summary.empty() ? "Preset could not be applied." : summary,
                        "editor-node-graph-preset");
                }
            } else {
                PostNodeGraphPresetNotification(
                    editor,
                    UiNotificationSeverity::Error,
                    error.empty() ? "Preset could not be loaded." : error,
                    "editor-node-graph-preset");
            }
        }

        ImGui::PushID(("PresetActions_" + preset->id).c_str());
        const bool hasSelection = editor && !editor->GetNodeGraph().GetSelectedNodeIds().empty();
        if (RenderFlatTextButton("Rename")) {
            ImGui::OpenPopup("Rename Preset");
        }
        ImGui::SameLine(0.0f, 18.0f);
        if (RenderFlatTextButton("Overwrite", hasSelection)) {
            auto exportResult = EditorNodeGraphSelectionExport::BuildExport(editor, editor->GetNodeGraph().GetSelectedNodeIds(), true, false);
            std::vector<StackBinaryFormat::NodePresetBoundarySocket> boundarySockets;
            for (const auto& socket : exportResult.boundarySockets) {
                StackBinaryFormat::NodePresetBoundarySocket out;
                out.nodeTitle = socket.nodeTitle;
                out.socketLabel = socket.socketLabel;
                out.direction = socket.direction;
                out.type = socket.type;
                boundarySockets.push_back(std::move(out));
            }
            std::string error;
            if (PresetManager::Get().OverwriteUserPreset(
                    *preset,
                    exportResult.clipboardPayload.value("payload", StackBinaryFormat::json::object()),
                    {},
                    boundarySockets,
                    exportResult.nodeCount,
                    &error)) {
                PostNodeGraphPresetNotification(editor, UiNotificationSeverity::Success, "Preset overwritten.", "editor-node-graph-preset");
            } else {
                PostNodeGraphPresetNotification(
                    editor,
                    UiNotificationSeverity::Error,
                    error.empty() ? "Preset overwrite failed." : error,
                    "editor-node-graph-preset");
            }
        }
        ImGui::SameLine(0.0f, 18.0f);
        if (RenderFlatTextButton("Delete", editor != nullptr)) {
            namespace N = Stack::Notifications;
            const auto id = preset->id;
            const auto revision = BuildPresetRevisionToken(*preset);
            N::NoticeSpec notice;
            notice.title = "Delete preset?";
            notice.message = "Delete \"" + preset->displayName + "\"?";
            notice.route = N::Route::Center;
            notice.foreground = true;
            notice.operationId = editor->GetNotifier().NewOperation();
            N::ActionSpec remove;
            remove.label = "Delete";
            remove.destructive = true;
            remove.canInvoke = [id, revision] {
                const auto* current = FindPresetById(PresetManager::Get().GetUserPresets(), id);
                return current && BuildPresetRevisionToken(*current) == revision;
            };
            remove.invoke = [preset] {
                std::string error;
                return PresetManager::Get().DeleteUserPreset(*preset, &error)
                    ? N::ActionResult::Success()
                    : N::ActionResult::Failure(error.empty() ? "The preset could not be deleted." : error);
            };
            N::ActionSpec cancel;
            cancel.label = "Cancel";
            cancel.safeCancel = true;
            cancel.invoke = [] { return N::ActionResult::Success(); };
            notice.actions = {std::move(remove), std::move(cancel)};
            editor->RequestNotificationDecision(std::move(notice));
        }

        if (ImGui::BeginPopupModal("Rename Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) {
                strncpy_s(renameBuffer, preset->displayName.c_str(), sizeof(renameBuffer) - 1);
                renameBuffer[sizeof(renameBuffer) - 1] = '\0';
            }
            ImGui::InputText("Name", renameBuffer, sizeof(renameBuffer));
            if (ImGui::Button("Rename", ImVec2(110.0f, 0.0f))) {
                std::string error;
                if (PresetManager::Get().RenameUserPreset(*preset, renameBuffer, &error)) {
                    PostNodeGraphPresetNotification(editor, UiNotificationSeverity::Success, "Preset renamed.", "editor-node-graph-preset");
                    ImGui::CloseCurrentPopup();
                } else {
                    PostNodeGraphPresetNotification(
                        editor,
                        UiNotificationSeverity::Error,
                        error.empty() ? "Preset rename failed." : error,
                        "editor-node-graph-preset");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.0f, 18.0f));
    }
}

} // namespace

bool EditorNodeGraphUI::EnsurePresetPreviewGraphLoaded(const PresetEntry& preset) {
    PreviewGraphCacheEntry& cache = m_PresetPreviewGraphCache[preset.id];
    const std::string revisionToken = BuildPresetRevisionToken(preset);
    if (cache.loadAttempted && cache.revisionToken == revisionToken) {
        return cache.error.empty();
    }

    StackBinaryFormat::json graphPayload;
    if (PresetManager::Get().TryGetPreloadedPresetPayload(preset, graphPayload)) {
        cache = {};
        cache.revisionToken = revisionToken;
        cache.loadAttempted = true;
    } else if (!preset.graphPayloadLoadError.empty()) {
        cache = {};
        cache.revisionToken = revisionToken;
        cache.loadAttempted = true;
        cache.error = preset.graphPayloadLoadError;
        return false;
    } else {
        return false;
    }

    if (!BuildPreviewGraphFromPayload(graphPayload, cache.graph, cache.layers, cache.error)) {
        return false;
    }
    return true;
}

void EditorNodeGraphUI::WarmPresetPreviewCache(EditorModule* editor, double budgetMs) {
    if (!editor) {
        return;
    }

    PresetManager& presetManager = PresetManager::Get();
    const auto& userPresets = presetManager.GetUserPresets();
    if (userPresets.empty()) {
        m_PresetPreviewWarmupCursor = 0;
        return;
    }

    using Clock = std::chrono::steady_clock;
    const auto startTime = Clock::now();
    const double clampedBudgetMs = std::max(0.0, budgetMs);
    const std::size_t presetCount = userPresets.size();
    std::size_t processedCount = 0;

    while (processedCount < presetCount) {
        const std::size_t presetIndex = (m_PresetPreviewWarmupCursor + processedCount) % presetCount;
        const auto& preset = userPresets[presetIndex];
        if (preset) {
            EnsurePresetPreviewGraphLoaded(*preset);
        }

        ++processedCount;
        if (processedCount < presetCount && clampedBudgetMs > 0.0) {
            const double elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - startTime).count();
            if (elapsedMs >= clampedBudgetMs) {
                m_PresetPreviewWarmupCursor = (presetIndex + 1) % presetCount;
                return;
            }
        }
    }

    m_PresetPreviewWarmupCursor = (m_PresetPreviewWarmupCursor + processedCount) % presetCount;
}

void EditorNodeGraphUI::SetPresetPreviewHoverTarget(const std::shared_ptr<PresetEntry>& preset) {
    if (!preset) {
        return;
    }

    const std::string presetId = preset->id;
    if (presetId.empty()) {
        return;
    }

    PreviewGraphCacheEntry& cache = m_PresetPreviewGraphCache[presetId];
    const std::string revisionToken = BuildPresetRevisionToken(*preset);
    if (!cache.revisionToken.empty() && cache.revisionToken != revisionToken) {
        cache = {};
    }

    if (m_DisplayedPresetPreviewId == presetId) {
        return;
    }

    m_PreviousPresetPreviewId = m_DisplayedPresetPreviewId;
    m_DisplayedPresetPreviewId = presetId;
    m_PresetPreviewFadeStartedAt = ImGui::GetTime();
}

void EditorNodeGraphUI::RenderPresetPreviewPane(EditorModule* editor, const ImVec2& availableSize) {
    if (!editor) {
        return;
    }

    const ImVec2 paneSize(std::max(320.0f, availableSize.x), std::max(320.0f, availableSize.y));
    ImGui::InvisibleButton("##PresetGraphPreviewSurface", paneSize);
    const ImVec2 paneMin = ImGui::GetItemRectMin();
    const ImVec2 paneMax = ImGui::GetItemRectMax();

    if (!m_PresetPreviewCurrentRenderer) {
        m_PresetPreviewCurrentRenderer = std::make_unique<EditorNodeGraphUI>();
        m_PresetPreviewCurrentRenderer->Initialize();
    }
    if (!m_PresetPreviewPreviousRenderer) {
        m_PresetPreviewPreviousRenderer = std::make_unique<EditorNodeGraphUI>();
        m_PresetPreviewPreviousRenderer->Initialize();
    }

    PresetManager& presetManager = PresetManager::Get();
    const auto& users = presetManager.GetUserPresets();
    const PresetEntry* currentPreset = FindPresetById(users, m_DisplayedPresetPreviewId);
    const PresetEntry* previousPreset = FindPresetById(users, m_PreviousPresetPreviewId);

    if (currentPreset) {
        EnsurePresetPreviewGraphLoaded(*currentPreset);
    }
    if (previousPreset && previousPreset != currentPreset) {
        EnsurePresetPreviewGraphLoaded(*previousPreset);
    }

    EditorNodeGraph::Graph emptyGraph;
    emptyGraph.Clear();
    bool renderedCurrent = false;
    bool renderedPrevious = false;

    const double now = ImGui::GetTime();
    const float fadeT = std::clamp(static_cast<float>((now - m_PresetPreviewFadeStartedAt) / 0.18), 0.0f, 1.0f);
    const bool showCrossfade = previousPreset && currentPreset && previousPreset->id != currentPreset->id && fadeT < 0.999f;

    if (showCrossfade) {
        if (const PreviewGraphCacheEntry* previousCache = GetPresetPreviewGraphCacheEntry(previousPreset->id)) {
            if (previousCache->error.empty()) {
                ImGui::PushID("PresetPreviewPrevious");
                m_PresetPreviewPreviousRenderer->RenderStaticGraphPreview(
                    editor,
                    const_cast<EditorNodeGraph::Graph&>(previousCache->graph),
                    const_cast<std::vector<std::shared_ptr<LayerBase>>*>(&previousCache->layers),
                    paneMin,
                    paneMax,
                    1.0f - fadeT);
                ImGui::PopID();
                renderedPrevious = true;
            }
        }
    }

    if (currentPreset) {
        if (const PreviewGraphCacheEntry* currentCache = GetPresetPreviewGraphCacheEntry(currentPreset->id)) {
            if (currentCache->error.empty()) {
                ImGui::PushID("PresetPreviewCurrent");
                m_PresetPreviewCurrentRenderer->RenderStaticGraphPreview(
                    editor,
                    const_cast<EditorNodeGraph::Graph&>(currentCache->graph),
                    const_cast<std::vector<std::shared_ptr<LayerBase>>*>(&currentCache->layers),
                    paneMin,
                    paneMax,
                    showCrossfade ? fadeT : 1.0f);
                ImGui::PopID();
                renderedCurrent = true;
            }
        }
    }

    if (!renderedCurrent && !renderedPrevious) {
        ImGui::PushID("PresetPreviewEmpty");
        m_PresetPreviewCurrentRenderer->RenderStaticGraphPreview(editor, emptyGraph, nullptr, paneMin, paneMax, 1.0f);
        ImGui::PopID();
    }

    const PresetEntry* messagePreset = currentPreset ? currentPreset : previousPreset;
    if (messagePreset) {
        if (const PreviewGraphCacheEntry* cache = GetPresetPreviewGraphCacheEntry(messagePreset->id)) {
            if (!cache->error.empty()) {
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(paneMin.x + 18.0f, paneMin.y + 18.0f),
                    WithAlpha(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), 0.92f),
                    cache->error.c_str());
            }
        }
    }
}

void EditorNodeGraphUI::RenderPresetsPanel(EditorModule* editor, float availableWidth) {
    if (!editor) {
        return;
    }

    PresetManager& presetManager = PresetManager::Get();
    presetManager.RefreshPresets();
    const float rowWidth = std::max(180.0f, availableWidth - 8.0f);
    const auto userPresets = presetManager.GetUserPresets();

    ImGui::TextDisabled("SAVED PRESETS");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    RenderUserPresetSection(this, editor, userPresets, rowWidth,
        m_DisplayedPresetPreviewId, m_RenamePresetBuffer);
}
