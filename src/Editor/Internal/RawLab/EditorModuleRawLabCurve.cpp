#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Renderer/GLHelpers.h"
#include "Restormer/RestormerClient.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/RawGallerySelectionVisuals.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Stack::Editor::RawLabInternal;
using RawCurveGraphUiState = Stack::EditorModuleTypes::RawCurveGraphUiState;
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include "Editor/Internal/RawLab/SceneToneEditor.h"

namespace {
float JsonFloat(const nlohmann::json& object, const char* key, float fallback) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return fallback;
    }
    const float value = it->get<float>();
    return std::isfinite(value) ? value : fallback;
}

int JsonInteger(const nlohmann::json& object, const char* key, int fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

bool JsonBoolean(const nlohmann::json& object, const char* key, bool fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

void EnsureFinishTone(nlohmann::json& finishTone) {
    if (!finishTone.is_object()) {
        finishTone = Stack::RawRecipe::DefaultFinishToneJson();
        return;
    }
    const nlohmann::json defaults = Stack::RawRecipe::DefaultFinishToneJson();
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!finishTone.contains(it.key())) {
            finishTone[it.key()] = it.value();
        }
    }
    finishTone = Stack::RawRecipe::SanitizeFinishTonePointCurveJson(
        std::move(finishTone));
}

void EnsureViewTransform(nlohmann::json& viewTransform) {
    if (!viewTransform.is_object()) {
        viewTransform = Stack::RawRecipe::DefaultViewTransformJson();
        return;
    }
    const nlohmann::json defaults = Stack::RawRecipe::DefaultViewTransformJson();
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!viewTransform.contains(it.key())) {
            viewTransform[it.key()] = it.value();
        }
    }
    viewTransform["contrastModel"] =
        Stack::RawRecipe::kViewContrastModelPivotedLogV2;
}

bool UsesPivotedViewContrast(const nlohmann::json&) {
    return true;
}

float EncodeViewGraphOutput(float linearValue, bool encodeSrgb) {
    const float value = std::clamp(linearValue, 0.0f, 1.0f);
    if (!encodeSrgb) {
        return value;
    }
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

Stack::RawRecipe::RawPointCurveChannel RawLabPointCurveChannel(int index) {
    return static_cast<Stack::RawRecipe::RawPointCurveChannel>(
        std::clamp(index, 0, 3));
}


} // namespace

bool EditorModule::RenderRawWorkspaceLabToneSurface(RawWorkspaceEditContext& context, RawLabControlSection section) {
    bool changed = false;
    if (ShowsRawLabSettings(section)) {
        if (!context.rawAdjustmentLayerId.empty()) {
            ImGui::TextUnformatted("Tone Curve");
        } else {
        ImGui::TextUnformatted(m_RawWorkspaceLabUi.selectedToneGradient >= 0
            ? "Local Tone Curve mask" : "Global Tone Curve");
        ImGui::SameLine();
        if (ImGui::SmallButton("Global##ToneGradientGlobal")) {
            m_RawWorkspaceLabUi.selectedToneGradient = -1;
            m_RawWorkspaceLabUi.gradientDrawShape = -1;
            for (auto& graph : m_RawWorkspaceLabUi.toneCurveGraphs) graph = {};
            m_RawWorkspaceLabUi.selectedTonePoint = -1;
            m_RawWorkspaceLabUi.sceneToneSelectedPoint = -1;
            m_RawWorkspaceLabUi.sceneToneDraggingPoint = -1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(m_RawWorkspaceLabUi.gradientOverlayVisible
                ? "Hide overlay##ToneGradientOverlay" : "Show overlay##ToneGradientOverlay"))
            m_RawWorkspaceLabUi.gradientOverlayVisible =
                !m_RawWorkspaceLabUi.gradientOverlayVisible;
        ImGui::SameLine();
        if (ImGui::SmallButton(m_RawWorkspaceLabUi.gradientGuidesVisible
                ? "Hide guides##ToneGradientGuides" : "Show guides##ToneGradientGuides"))
            m_RawWorkspaceLabUi.gradientGuidesVisible =
                !m_RawWorkspaceLabUi.gradientGuidesVisible;
        }
    }
    nlohmann::json* activeCurveJson = &context.recipe.finishTone.layerJson;
    if (m_RawWorkspaceLabUi.selectedToneGradient >= 0 &&
        m_RawWorkspaceLabUi.selectedToneGradient < static_cast<int>(context.recipe.toneGradients.size())) {
        activeCurveJson = &context.recipe.toneGradients[
            static_cast<std::size_t>(m_RawWorkspaceLabUi.selectedToneGradient)].curveJson;
    }
    EnsureFinishTone(*activeCurveJson);
    nlohmann::json& finishTone = *activeCurveJson;
    if (ShowsRawLabSettings(section) && context.rawAdjustmentLayerId.empty()) {
        for (int i=0;i<3;++i) {
            if (!context.rawAdjustmentLayerId.empty() &&
                ((m_RawWorkspaceLabUi.sceneToneView<2 && i==2) ||
                 (m_RawWorkspaceLabUi.sceneToneView==2 && i<2))) continue;
            const char* labels[]={"Curve","Contrast","RGB curves"};
            if(i) ContinueRawLabControlRow(labels[i], ImGui::GetStyle().ItemSpacing.x);
            if(FramelessTextButton(labels[i],m_RawWorkspaceLabUi.sceneToneView==i)) {
                m_RawWorkspaceLabUi.sceneToneView=i;
                // Switching RGB/luminance selects a different authored operation.
                // Rebind next frame before exposing its parameter controls.
                ClearRawWorkspaceGraphScopeReadbackCaches();
                MarkRenderRefreshDirty();
                return changed;
            }
        }
    }
    if(m_RawWorkspaceLabUi.sceneToneView<2) {
        auto tone=Stack::RawRecipe::ReadSceneTone(finishTone.value("luminanceTone",nlohmann::json::object()));
        const auto histogram=ShowsRawLabGraph(section) ? m_RawFloatingSurface.histogram.FinishTone(m_RawWorkspaceGraphScopeReadback,
            m_RawWorkspaceGraphScopeReadback.valid ? m_RawWorkspaceGraphScopeReadbackRevision : 0,
            RawDevelopmentGraphScopeStage::FinishToneInput,context.recipe.technical.workingSpace,1,-16,16,.18f) : RawLabGraphHistogram{};
        if(context.nodeTarget && context.editorState) {
            auto& state=context.State<CurveCanvasState>();
            if(!tone.direct && context.drivenParameters.empty()) {
                auto enabledTone=tone;enabledTone.enabled=true;
                if(!Stack::RawRecipe::IsSceneToneActive(enabledTone)) {
                    const bool enabled=tone.enabled;
                    tone=Stack::RawRecipe::SceneTone{};tone.enabled=enabled;
                }
            }
            if(!tone.direct) {
                // Keep saved slope curves exact until the user explicitly converts.
                if(ShowsRawLabSettings(section)) {
                    ImGui::TextWrapped("This saved edit uses the previous slope model. Converting makes an editable approximation; Undo restores the original.");
                    ImGui::BeginDisabled(!context.drivenParameters.empty());
                    if(ImGui::Button("Convert to editable points")) {
                        tone.curve.clear();
                        for(int i=0;i<=32;++i) {
                            const float x=float(i)/32, ev=-16+32*x;
                            tone.curve.push_back({x,std::clamp((Stack::RawRecipe::EvaluateSceneToneEv(tone,ev)+16)/32,0.f,1.f),{1},{1}});
                        }
                        tone.direct=true; changed=true;
                    }
                    ImGui::EndDisabled();
                    if(!context.drivenParameters.empty())ImGui::TextWrapped("Disconnect the legacy parameter inputs in Graph before converting.");
                }
                if(!tone.direct) changed|=DrawSceneToneEditor(tone,false,
                    m_RawWorkspaceLabUi.sceneToneSelectedPoint,m_RawWorkspaceLabUi.sceneToneDraggingPoint,
                    m_RawWorkspaceLabUi.sceneToneViewMin,m_RawWorkspaceLabUi.sceneToneViewMax,histogram,
                    [&context](const auto& id) { return context.IsParameterDriven(id); },section);
            }
            if(tone.direct) {
                if(ShowsRawLabSettings(section)) {
                    changed|=ImGui::Checkbox("Enabled",&tone.enabled);
                    if(ImGui::Button("Reset curve")) {tone=Stack::RawRecipe::SceneTone{};state={};changed=true;}
                    ImGui::TextWrapped("Double-click to add points. Drag points in either direction. Use the icons below the curve for selection and coordinated edits.");
                }
                if(ShowsRawLabGraph(section)) {
                    ImVec2 size=ImGui::GetContentRegionAvail();size.y=std::min(size.y,250.f);
                    changed|=DrawCurveCanvas("Tone",tone.curve,tone.curveAmount,state,true,histogram,size,64);
                }
            }
            if(changed) finishTone["luminanceTone"]=Stack::RawRecipe::SerializeSceneTone(tone);
        } else if(DrawSceneToneEditor(tone,m_RawWorkspaceLabUi.sceneToneView==1,
                m_RawWorkspaceLabUi.sceneToneSelectedPoint,m_RawWorkspaceLabUi.sceneToneDraggingPoint,
                m_RawWorkspaceLabUi.sceneToneViewMin,m_RawWorkspaceLabUi.sceneToneViewMax,histogram, [&context](const auto& id) { return context.IsParameterDriven(id); }, section)) {
            finishTone["luminanceTone"]=Stack::RawRecipe::SerializeSceneTone(tone);changed=true;
        }
        return changed;
    }
    int domain = std::clamp(JsonInteger(finishTone, "domain", 1), 0, 1);
    int activeCurve = std::clamp(m_RawWorkspaceLabUi.activePointCurve, 0, 3);
    const char* curveLabels[] = { "Point", "R", "G", "B" };
    const ImVec4 curveChannelColors[] = {
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
        ImVec4(1.0f, 0.18f, 0.18f, 1.0f),
        ImVec4(0.25f, 0.9f, 0.35f, 1.0f),
        ImVec4(0.25f, 0.48f, 1.0f, 1.0f),
    };

    const float surfaceStartX = ImGui::GetCursorPosX();
    const float surfaceWidth = ImGui::GetContentRegionAvail().x;
    if (ShowsRawLabSettings(section)) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float sceneButtonWidth =
            ImGui::CalcTextSize("Scene").x + style.FramePadding.x * 2.0f;
        const float logButtonWidth =
            ImGui::CalcTextSize("Log").x + style.FramePadding.x * 2.0f;
        constexpr float modeButtonGap = 4.0f;
        const float modeButtonGroupWidth =
            sceneButtonWidth + modeButtonGap + logButtonWidth;
        ImGui::SetCursorPosX(surfaceStartX + std::max(
            0.0f,
            (surfaceWidth - modeButtonGroupWidth) * 0.5f));
        if (FramelessTextButton("Scene", domain == 0)) {
            finishTone["domain"] = 0;
            domain = 0;
            changed = true;
        }
        ImGui::SameLine(0.0f, modeButtonGap);
        if (FramelessTextButton("Log", domain == 1)) {
            finishTone["domain"] = 1;
            domain = 1;
            changed = true;
        }

        ImGui::Spacing();
        constexpr float channelDotButtonSize = 26.0f;
        constexpr float channelDotGap = 6.0f;
        const float channelDotGroupWidth =
            channelDotButtonSize * 4.0f + channelDotGap * 3.0f;
        ImGui::SetCursorPosX(surfaceStartX + std::max(
            0.0f,
            (surfaceWidth - channelDotGroupWidth) * 0.5f));
        for (int index = 0; index < 4; ++index) {
            ImGui::PushID(index);
            if (ImGui::InvisibleButton(
                    "##RawLabCurveChannel",
                    ImVec2(channelDotButtonSize, channelDotButtonSize))) {
                for (RawCurveGraphUiState& graphState :
                     m_RawWorkspaceLabUi.toneCurveGraphs) {
                    ClearCurveSegmentSelection(
                        graphState,
                        m_RawWorkspaceLabUi.selectedTonePoint);
                }
                activeCurve = index;
                m_RawWorkspaceLabUi.activePointCurve = index;
                SaveRawWorkspaceAppState();
            }
            const ImRect selectorRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = activeCurve == index;
            const float alpha = selected ? 1.0f : hovered ? 0.9f : 0.68f;
            ImVec4 dotColor = curveChannelColors[index];
            dotColor.w = alpha;
            ImGui::GetWindowDrawList()->AddCircleFilled(
                selectorRect.GetCenter(),
                selected ? 10.0f : 9.0f,
                ImGui::GetColorU32(dotColor),
                24);
            LabTooltip(curveLabels[index]);
            ImGui::PopID();
            if (index + 1 < 4) {
                ImGui::SameLine(0.0f, channelDotGap);
            }
        }

        ImGui::Spacing();

    }
    const float minimumEv = JsonFloat(finishTone, "logMinEv", -10.0f);
    const float maximumEv = JsonFloat(finishTone, "logMaxEv", 6.0f);
    const float middleGrey = std::max(
        0.000001f,
        JsonFloat(finishTone, "middleGrey", 0.18f));
    const float evSpan = std::max(0.1f, maximumEv - minimumEv);
    Stack::RawRecipe::RawPointCurveComponent component =
        Stack::RawRecipe::PointCurveComponentFromFinishToneJson(
            finishTone,
            RawLabPointCurveChannel(activeCurve));
    bool componentChanged = false;
    if (ShowsRawLabGraph(section)) {
        const auto& histogram = m_RawFloatingSurface.histogram.FinishTone(
            m_RawWorkspaceGraphScopeReadback,
            m_RawWorkspaceGraphScopeReadback.valid ? m_RawWorkspaceGraphScopeReadbackRevision : 0,
            RawDevelopmentGraphScopeStage::FinishToneInput,
            context.recipe.technical.workingSpace,
            domain,
            minimumEv,
            maximumEv,
            middleGrey);
        const std::size_t activeCurveIndex = static_cast<std::size_t>(
            std::clamp(activeCurve, 0, 3));
        RawLabToneGraphViewRange fittedGraphRange;
        if (histogram.valid) {
            // Save the source-data fit separately from the displayed zoom. A
            // Finish Tone edit can briefly wait for a scope refresh, but the curve
            // must stay in the same display coordinates during that wait.
            fittedGraphRange = BuildRawLabToneGraphViewRange(
                histogram,
                activeCurve,
                1.0f);
            m_RawWorkspaceLabUi.toneGraphFittedMinimum[activeCurveIndex] =
                fittedGraphRange.minimum;
            m_RawWorkspaceLabUi.toneGraphFittedMaximum[activeCurveIndex] =
                fittedGraphRange.maximum;
            m_RawWorkspaceLabUi.toneGraphFittedRangeValid[activeCurveIndex] = true;
        } else if (m_RawWorkspaceLabUi.toneGraphFittedRangeValid[activeCurveIndex]) {
            fittedGraphRange.minimum =
                m_RawWorkspaceLabUi.toneGraphFittedMinimum[activeCurveIndex];
            fittedGraphRange.maximum =
                m_RawWorkspaceLabUi.toneGraphFittedMaximum[activeCurveIndex];
        }
        const RawLabToneGraphViewRange graphView = ApplyRawLabToneGraphViewZoom(
            fittedGraphRange,
            m_RawWorkspaceLabUi.toneGraphZoom);
        const RawLabGraphHistogram displayHistogram = CropRawLabGraphHistogram(
            histogram,
            graphView);

        constexpr float graphHorizontalInset = 8.0f;
        ImGui::SetCursorPosX(surfaceStartX);
        ImVec2 graphSize = CompactLabGraphSize(152.0f, 170.0f, 560.0f);
        graphSize.x = std::max(1.0f, graphSize.x - graphHorizontalInset * 2.0f);
        ImGui::SetCursorPosX(surfaceStartX + graphHorizontalInset);
        componentChanged = DrawFramelessToneCurveBezier(
                "##RawLabFinishToneGraph",
                component,
                m_RawWorkspaceLabUi.toneCurveGraphs[static_cast<std::size_t>(activeCurve)],
                m_RawWorkspaceLabUi.selectedTonePoint,
                activeCurve,
                displayHistogram,
                false,
                graphSize,
                graphView);

    }
    if (ShowsRawLabSettings(section)) {
        constexpr float graphHorizontalInset = 8.0f;
        ImGui::SetCursorPosX(surfaceStartX + graphHorizontalInset);
        if (DrawRawLabRingDial(
                "##RawLabToneGraphZoom",
                m_RawWorkspaceLabUi.toneGraphZoom,
                std::max(1.f, surfaceWidth - 2.f * graphHorizontalInset),
                "Graph view only. Drag horizontally to move the dial rings. Left shows the full range. Double-click restores the full range.",
                0.0f)) {
            for (RawCurveGraphUiState& graphState :
                 m_RawWorkspaceLabUi.toneCurveGraphs) {
                ClearCurveSegmentSelection(
                    graphState,
                    m_RawWorkspaceLabUi.selectedTonePoint);
            }
        }

        const int selected = m_RawWorkspaceLabUi.selectedTonePoint;
        if (selected >= 0 && selected < static_cast<int>(component.points.size())) {
            auto& point = component.points[static_cast<std::size_t>(selected)];
            int input = std::clamp(static_cast<int>(std::lround(point.x * 255.0f)), 0, 255);
            int output = std::clamp(static_cast<int>(std::lround(point.y * 255.0f)), 0, 255);
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Input");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(64.0f);
            const bool endpoint = selected == 0 ||
                selected + 1 == static_cast<int>(component.points.size());
            ImGui::BeginDisabled(endpoint);
            if (ImGui::InputInt("##RawLabCurveInput", &input, 0, 0)) {
                const float minimum = component.points[static_cast<std::size_t>(selected - 1)].x + 0.002f;
                const float maximum = component.points[static_cast<std::size_t>(selected + 1)].x - 0.002f;
                point.x = std::clamp(static_cast<float>(std::clamp(input, 0, 255)) / 255.0f, minimum, maximum);
                componentChanged = true;
            }
            ImGui::EndDisabled();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Output");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(64.0f);
            if (ImGui::InputInt("##RawLabCurveOutput", &output, 0, 0)) {
                point.y = static_cast<float>(std::clamp(output, 0, 255)) / 255.0f;
                componentChanged = true;
            }
            if (domain == 1) {
                const float inputEv = minimumEv + point.x * evSpan;
                const float outputEv = minimumEv + point.y * evSpan;
                ImGui::TextDisabled("%+.2f EV  ->  %+.2f EV", inputEv, outputEv);
            }
        }

    }
    if (componentChanged) {
        Stack::RawRecipe::StorePointCurveComponentInFinishToneJson(
            finishTone,
            RawLabPointCurveChannel(activeCurve),
            component);
        changed = true;
    }
    return changed;
}
