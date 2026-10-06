#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
using namespace Stack::Editor::RawLabInternal;

bool EditorModule::RenderRawWorkspaceLabLegacyZones(RawWorkspaceEditContext& context) {
    bool changed = false;
    auto& localRange = context.recipe.localRange;
    auto stopTargeting = [&]() {
        if (!m_RawWorkspaceLocalRangeTargetMode) {
            return;
        }
        const std::string selectedZoneId = m_RawWorkspaceLocalRangeTargetZoneId;
        m_RawWorkspaceLocalRangeOverlayMode =
            m_RawWorkspaceLocalRangeTargetPreviousOverlayMode;
        ClearRawWorkspaceLocalRangeOverlayState();
        ClearRawWorkspaceLocalRangeTargetState(false);
        m_RawWorkspaceLocalRangeTargetZoneId = selectedZoneId;
        MarkRenderRefreshDirty();
    };
    auto startTargeting = [&](bool createNewTarget) {
        if (!m_RawWorkspaceLocalRangeTargetMode) {
            m_RawWorkspaceLocalRangeTargetPreviousOverlayMode =
                m_RawWorkspaceLocalRangeOverlayMode;
        }
        m_RawWorkspaceLocalRangeTargetMode = true;
        m_RawWorkspaceLocalRangeOverlayMode = "target-outline";
        m_RawWorkspaceLocalRangeTargetCreateZone = createNewTarget;
        if (createNewTarget) {
            m_RawWorkspaceLocalRangeTargetZoneId.clear();
        }
        ClearRawWorkspaceLocalRangeOverlayState();
        MarkRenderRefreshDirty();
    };
    auto setPreviewMode = [&](const char* mode) {
        stopTargeting();
        if (m_RawWorkspaceLocalRangeOverlayMode != mode) {
            m_RawWorkspaceLocalRangeOverlayMode = mode;
            ClearRawWorkspaceLocalRangeOverlayState();
            MarkRenderRefreshDirty();
        }
    };

        if (BareTextButton("+ Add Target")) {
            startTargeting(true);
        }
        LabTooltip("Create an independent exposure target with the next image drag.");
        ImGui::SameLine(0.0f, 2.0f);
        const bool hasSelectedTarget = std::any_of(
            localRange.targetZones.begin(),
            localRange.targetZones.end(),
            [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
            });
        if (BareTextButton(
                m_RawWorkspaceLocalRangeTargetMode ? "Stop Editing" : "Edit on Image",
                m_RawWorkspaceLocalRangeTargetMode,
                m_RawWorkspaceLocalRangeTargetMode || hasSelectedTarget)) {
            if (m_RawWorkspaceLocalRangeTargetMode) {
                stopTargeting();
            } else {
                startTargeting(false);
            }
        }
        LabTooltip(
            "Drag vertically for exposure. Wheel changes tonal reach; Shift-wheel changes "
            "feather; Ctrl-wheel changes color reach. Shift-click adds an area.",
            ImGuiHoveredFlags_AllowWhenDisabled);

        ImGui::Spacing();
        ImGui::TextDisabled("Preview");
        const bool localRangeActive = Stack::RawRecipe::IsLocalRangeEnabled(localRange);
        const bool maskAvailable =
            localRange.regionMaskEnabled ||
            localRange.colorMaskEnabled ||
            !localRange.targetZones.empty();
        auto previewButton = [&](const char* label, const char* mode, bool available) {
            if (BareTextButton(
                    label,
                    m_RawWorkspaceLocalRangeOverlayMode == mode,
                    available)) {
                setPreviewMode(mode);
            }
        };
        previewButton("Final", "none", true);
        ImGui::SameLine(0.0f, 2.0f);
        previewButton("Affected", "affected-tones", localRangeActive);
        ImGui::SameLine(0.0f, 2.0f);
        previewButton("Delta", "delta-map", localRangeActive);
        ImGui::SameLine(0.0f, 2.0f);
        previewButton("Mask", "region-mask", maskAvailable);

        ImGui::Spacing();
        ImGui::TextDisabled("Targets");
        if (localRange.targetZones.empty()) {
            m_RawWorkspaceLocalRangeTargetZoneId.clear();
            ImGui::TextWrapped(
                "No targets yet. Choose + Add Target, then drag vertically over the image.");
        } else {
            const auto selectedIt = std::find_if(
                localRange.targetZones.begin(),
                localRange.targetZones.end(),
                [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                    return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
                });
            if (selectedIt == localRange.targetZones.end() &&
                !m_RawWorkspaceLocalRangeTargetCreateZone) {
                m_RawWorkspaceLocalRangeTargetZoneId = localRange.targetZones.front().id;
            }

            for (std::size_t index = 0; index < localRange.targetZones.size(); ++index) {
                const Stack::RawRecipe::RawLocalRangeTargetZone& zone =
                    localRange.targetZones[index];
                char valueText[32];
                std::snprintf(valueText, sizeof(valueText), "%+.2f EV", zone.deltaEv);
                std::string rowLabel = zone.name.empty()
                    ? "Target " + std::to_string(index + 1)
                    : zone.name;
                rowLabel += "    ";
                rowLabel += valueText;
                if (!zone.enabled) {
                    rowLabel += "  (off)";
                }
                ImGui::PushID(zone.id.c_str());
                if (BareTextButton(
                        rowLabel.c_str(),
                        zone.id == m_RawWorkspaceLocalRangeTargetZoneId,
                        true,
                        ImVec2(-1.0f, 0.0f))) {
                    m_RawWorkspaceLocalRangeTargetZoneId = zone.id;
                    m_RawWorkspaceLocalRangeTargetCreateZone = false;
                    m_RawWorkspaceLabUi.selectedZonePoint = -1;
                }
                ImGui::PopID();
            }

            auto zoneIt = std::find_if(
                localRange.targetZones.begin(),
                localRange.targetZones.end(),
                [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                    return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
                });
            if (zoneIt != localRange.targetZones.end()) {
                ImGui::Separator();
                ImGui::TextDisabled("Selected Target");
                ImGui::PushID(zoneIt->id.c_str());

                char nameBuffer[65] = {};
                std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", zoneIt->name.c_str());
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Name");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer))) {
                    zoneIt->name = nameBuffer;
                    changed = true;
                }

                bool zoneEnabled = zoneIt->enabled;
                if (ImGui::Checkbox("Enabled", &zoneEnabled)) {
                    zoneIt->enabled = zoneEnabled;
                    changed = true;
                }

                float deltaEv = zoneIt->deltaEv;
                if (BareSliderFloat(
                        "Exposure",
                        "RawLabTargetExposure",
                        &deltaEv,
                        -4.0f,
                        4.0f,
                        "%+.2f EV")) {
                    zoneIt->deltaEv = deltaEv;
                    changed = true;
                }
                float centerEv = zoneIt->centerEv;
                if (BareSliderFloat(
                        "Brightness",
                        "RawLabTargetCenter",
                        &centerEv,
                        localRange.minEv,
                        localRange.maxEv,
                        "%+.2f EV")) {
                    zoneIt->centerEv = centerEv;
                    changed = true;
                }
                float reach = zoneIt->coreHalfWidthEv;
                if (BareSliderFloat(
                        "Reach",
                        "RawLabTargetReach",
                        &reach,
                        0.05f,
                        4.0f,
                        "%.2f EV")) {
                    zoneIt->coreHalfWidthEv = reach;
                    changed = true;
                }
                float feather = zoneIt->featherEv;
                if (BareSliderFloat(
                        "Feather",
                        "RawLabTargetFeather",
                        &feather,
                        0.02f,
                        4.0f,
                        "%.2f EV")) {
                    zoneIt->featherEv = feather;
                    changed = true;
                }

                ImGui::TextDisabled("Affect");
                const bool selectedAreas =
                    zoneIt->scope == Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
                if (BareTextButton("Selected Areas", selectedAreas) && !selectedAreas) {
                    zoneIt->scope = Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
                    changed = true;
                }
                ImGui::SameLine(0.0f, 2.0f);
                if (BareTextButton("All Matches", !selectedAreas) && selectedAreas) {
                    zoneIt->scope = Stack::RawRecipe::RawLocalRangeTargetScope::AllMatches;
                    changed = true;
                }

                bool colorEnabled = zoneIt->colorEnabled;
                if (ImGui::Checkbox("Color Match", &colorEnabled)) {
                    zoneIt->colorEnabled = colorEnabled;
                    changed = true;
                }
                LabTooltip("Also require pixels to resemble the color sampled when the target was created.");
                if (zoneIt->colorEnabled) {
                    float colorReach = zoneIt->colorRadius;
                    if (BareSliderFloat(
                            "Color Reach",
                            "RawLabTargetColorReach",
                            &colorReach,
                            0.002f,
                            0.25f,
                            "%.3f")) {
                        zoneIt->colorRadius = colorReach;
                        changed = true;
                    }
                    float colorFeather = zoneIt->colorFeather;
                    if (BareSliderFloat(
                            "Color Feather",
                            "RawLabTargetColorFeather",
                            &colorFeather,
                            0.002f,
                            0.25f,
                            "%.3f")) {
                        zoneIt->colorFeather = colorFeather;
                        changed = true;
                    }
                }

                ImGui::TextDisabled("Combine Targets");
                auto combineButton = [&](const char* label, Stack::RawRecipe::RawLocalRangeZoneCombineMode mode) {
                    if (BareTextButton(label, localRange.targetZoneCombineMode == mode) &&
                        localRange.targetZoneCombineMode != mode) {
                        localRange.targetZoneCombineMode = mode;
                        changed = true;
                    }
                };
                combineButton("Add", Stack::RawRecipe::RawLocalRangeZoneCombineMode::Add);
                ImGui::SameLine(0.0f, 2.0f);
                combineButton("Strongest", Stack::RawRecipe::RawLocalRangeZoneCombineMode::Strongest);
                ImGui::SameLine(0.0f, 2.0f);
                combineButton("Blend", Stack::RawRecipe::RawLocalRangeZoneCombineMode::Blend);

                ImGui::Spacing();
                if (BareTextButton("Reset Exposure")) {
                    zoneIt->deltaEv = 0.0f;
                    changed = true;
                }
                ImGui::SameLine(0.0f, 2.0f);
                if (BareTextButton("Delete Target")) {
                    const std::size_t erasedIndex = static_cast<std::size_t>(
                        std::distance(localRange.targetZones.begin(), zoneIt));
                    localRange.targetZones.erase(zoneIt);
                    if (localRange.targetZones.empty()) {
                        stopTargeting();
                        m_RawWorkspaceLocalRangeTargetZoneId.clear();
                    } else {
                        const std::size_t nextIndex = std::min(
                            erasedIndex,
                            localRange.targetZones.size() - 1);
                        m_RawWorkspaceLocalRangeTargetZoneId =
                            localRange.targetZones[nextIndex].id;
                    }
                    changed = true;
                }

                ImGui::PopID();
            }
        }
    return changed;
}
