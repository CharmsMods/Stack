#include "Editor/NodeEditors/NodeEditorRegistry.h"
#include "Editor/NodeEditors/CoreAdjustmentEditors.h"
#include "Editor/NodeEditors/StylizeEditors.h"
#include "Editor/NodeEditors/ToneCurveEditor.h"
#include "Editor/NodeEditors/FilmGrainEditor.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/Internal/RawLab/RawCalibrationEditor.h"
#include "Editor/Internal/RawLab/DetailContrastEditor.h"
#include "Utils/FloatingSlider.h"
#include <algorithm>

namespace Stack::Editor {
using namespace RawLabInternal;
using Tool = EditorModuleTypes::RawLabTool;
using Kind = RawRecipe::GraphOperationKind;

// Adapters for the existing designs. Each new design registers here by node
// definition identity; the host does not need another type switch.
struct NodeEditorRegistration {
    static std::vector<NodeEditorDefinition> Build() {
        std::vector<NodeEditorDefinition> result=CoreAdjustmentEditors();
        for (auto& editor:StylizeEditors()) result.push_back(std::move(editor));
        if (const auto* definition=EditorNodeGraphDefinitions::FindLiveNodeDefinition(
                EditorNodeGraph::NodeKind::MultiFrameHdr,0)) {
            NodeEditorDefinition bracket;
            bracket.definitionId=definition->identity.id;
            bracket.legacyTool=Tool::MultiFrame;
            bracket.preferredSize={420,440};
            bracket.primary=[](EditorModule& e,RawWorkspaceEditContext&) {
                e.RenderBracketingContributionUI();return false;
            };
            bracket.settings=[](EditorModule& e,RawWorkspaceEditContext&) {
                e.RenderBracketingSidePanelUI();return false;
            };
            result.push_back(std::move(bracket));
        }
        if(const auto* definition=EditorNodeGraphDefinitions::FindLiveNodeDefinition(
                EditorNodeGraph::NodeKind::Layer,static_cast<int>(LayerType::Noise))) {
            NodeEditorDefinition grain;grain.definitionId=definition->identity.id;
            grain.preferredSize={720,690};grain.primary=DrawFilmGrain;grain.reset=ResetFilmGrain;
            result.push_back(std::move(grain));
        }
        const auto add = [&](Kind kind, Tool tool, ImVec2 size,
            NodeEditorDefinition::Draw primary, NodeEditorDefinition::Draw settings,
            NodeEditorDefinition::Draw reset, int tone = -1,
            RawDevelopmentGraphScopeStage analysis = RawDevelopmentGraphScopeStage::None) {
            const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(
                EditorNodeGraph::NodeKind::RawOperation,static_cast<int>(kind));
            if (definition) {
                result.push_back({definition->identity.id,tool,tone,size,primary,settings,reset});
                result.back().inputAnalysis=analysis;
            }
        };
        add(Kind::Calibration,Tool::Calibration,{400,340},
            [](EditorModule& e,RawWorkspaceEditContext& c){ return e.RenderRawWorkspaceLabCalibrationSurface(c); },
            [](EditorModule& e,RawWorkspaceEditContext& c){ return e.RenderRawWorkspaceLabCalibrationSettings(c); },nullptr);
        add(Kind::Exposure,Tool::Exposure,{400,80},
            [](EditorModule& e,RawWorkspaceEditContext& c){
                if (c.IsParameterDriven("ev")) {
                    const auto value=c.uniformParameters.find("ev");
                    if (value!=c.uniformParameters.end() && value->second.available)
                        ImGui::TextDisabled("Exposure driven: %.2f EV",value->second.value);
                    else ImGui::TextWrapped("%s",value==c.uniformParameters.end()
                        ? "Waiting for connected Exposure." : value->second.reason.c_str());
                    return false;
                }
                ImGui::BeginDisabled(c.IsParameterDriven("ev"));
                const Project::NodeEditTarget target=c.nodeTarget.value_or(Project::NodeEditTarget{e.GetProjectDocumentId(),"source","exposure"});
                ImGuiExtras::FloatingSliderScope sliders(e.m_NodeEditorState.Get<ImGuiExtras::FloatingSliderState>(target));
                ImGui::PushID(c.rawOperationUuid.c_str());
                const bool changed=ImGuiExtras::FloatingSliderFloat("Exposure","##exposure",&c.recipe.preToneExposureEv,-10,10,"%.2f EV",ImGui::GetContentRegionAvail().x);
                ImGui::PopID();
                ImGui::EndDisabled(); return changed;
            },nullptr,[](EditorModule&,RawWorkspaceEditContext& c){c.recipe.preToneExposureEv=0;return true;});
        add(Kind::LocalEv,Tool::Zones,{400,300},
            [](EditorModule& e,RawWorkspaceEditContext& c){return e.RenderRawWorkspaceLabZonesSurface(c,RawLabControlSection::Graph);},
            [](EditorModule& e,RawWorkspaceEditContext& c){
                bool changed=e.RenderRawWorkspaceLabZonesSurface(c,RawLabControlSection::Settings);
                changed|=e.RenderRawWorkspaceLabZonesSecondaryControls(c);return changed;
            },[](EditorModule& e,RawWorkspaceEditContext& c){
                if(e.m_RawWorkspaceLabUi.zonesTargetedView && !e.m_RawWorkspaceLabUi.zoneAreas.legacy) {
                    for(auto& area:c.recipe.localRange.areas) if(area.id==e.m_RawWorkspaceLabUi.zoneAreas.selectedId) {
                        area.offsetEv=0;
                        for(auto& point:area.points){point.deltaEv=0;point.incoming={};point.outgoing={};}
                    }
                } else if(e.m_RawWorkspaceLabUi.zonesTargetedView) {
                    for(auto& zone:c.recipe.localRange.targetZones) if(zone.id==e.m_RawWorkspaceLocalRangeTargetZoneId) zone.deltaEv=0;
                } else c.recipe.localRange=RawRecipe::ApplyLocalRangePreset(c.recipe.localRange,RawRecipe::RawLocalRangePreset::Reset);
                return true;
            },-1,RawDevelopmentGraphScopeStage::LocalRangeInput);
        add(Kind::ToneCurve,Tool::Tone,{400,400},DrawToneCurve,DrawToneCurveSettings,ResetToneCurve,
            -1,RawDevelopmentGraphScopeStage::FinishToneInput);
        add(Kind::ColorWarp,Tool::Color,{400,560},
            [](EditorModule& e,RawWorkspaceEditContext& c){return e.RenderRawWorkspaceLabColorSurface(c,RawLabControlSection::Graph);},
            [](EditorModule& e,RawWorkspaceEditContext& c){
                bool changed=e.RenderRawWorkspaceLabColorSurface(c,RawLabControlSection::Settings);
                changed|=e.RenderRawWorkspaceLabColorSecondaryControls(c);return changed;
            },[](EditorModule& e,RawWorkspaceEditContext& c){
                c.recipe.colorWarp={};e.m_RawWorkspaceLabUi.selectedColorWarpPin=-1;
                e.m_RawWorkspaceLabUi.colorWarpProvisionalPinValid=false;
                e.m_RawWorkspaceLabUi.colorWarpProvisionalSceneEvValid=false;
                e.m_RawWorkspaceLabUi.colorWarpDraggingProvisional=false;return true;
            },-1,RawDevelopmentGraphScopeStage::ColorWarpInput);
        // Captureless callbacks keep registration immutable and project-free.
        add(Kind::DetailContrast,Tool::Detail,{400,560},
            [](EditorModule& e,RawWorkspaceEditContext& c){
                auto& state=c.nodeTarget ? c.State<DetailContrastEditorState>() : e.m_RawWorkspaceLabUi.detailContrastEditor;
                return DrawDetailContrastEditor(c.recipe.detailContrast,state,[&](const auto& id){return c.IsParameterDriven(id);},RawLabControlSection::Graph);
            },[](EditorModule& e,RawWorkspaceEditContext& c){
                auto& state=c.nodeTarget ? c.State<DetailContrastEditorState>() : e.m_RawWorkspaceLabUi.detailContrastEditor;
                return DrawDetailContrastEditor(c.recipe.detailContrast,state,[&](const auto& id){return c.IsParameterDriven(id);},RawLabControlSection::Settings);
            },[](EditorModule&,RawWorkspaceEditContext& c){c.recipe.detailContrast={};return true;});
        // A texture's editor lives on the image. It needs no duplicate sliders.
        for(auto shape:{EditorNodeGraph::MaskGeneratorKind::LinearGradient,EditorNodeGraph::MaskGeneratorKind::RadialGradient,EditorNodeGraph::MaskGeneratorKind::Square}) {
            const auto* definition=EditorNodeGraphDefinitions::FindLiveNodeDefinition(EditorNodeGraph::NodeKind::MaskTexture,static_cast<int>(shape));
            if(!definition) continue;
            NodeEditorDefinition editor;editor.definitionId=definition->identity.id;editor.preferredSize={80,80};
            editor.hasFloatingSurface=false;
            editor.primary=[](EditorModule&,RawWorkspaceEditContext&){return false;};
            editor.overlay=[](EditorModule& e,RawWorkspaceEditContext& c,ImVec2 minimum,ImVec2 maximum) {
                e.RenderRawLayerMaskHandles(c,minimum,maximum);return false;
            };
            result.push_back(std::move(editor));
        }
        return result;
    }
};
const std::vector<NodeEditorDefinition>& NodeEditorDefinitions() {
    static const auto definitions=NodeEditorRegistration::Build();
    return definitions;
}
const NodeEditorDefinition& FindNodeEditor(const EditorNodeGraph::Node& node) {
    const auto* definition=EditorNodeGraphDefinitions::FindLiveNodeDefinition(node);
    if(definition && node.definitionResolved &&
        (node.kind!=EditorNodeGraph::NodeKind::MultiFrameHdr || node.multiFrameHdr.bracketingSource)) {
        for(const auto& editor:NodeEditorDefinitions())
            if(editor.definitionId==definition->identity.id) return editor;
    }
    if (node.kind==EditorNodeGraph::NodeKind::Compound && node.definitionResolved) {
        // Embedded recipes use the existing graph controls. Reset only local
        // fallbacks; a connected parameter retains its authored fallback.
        static const NodeEditorDefinition compoundControls=[] {
            NodeEditorDefinition editor;
            editor.reset=[](EditorModule&,RawWorkspaceEditContext& context) {
                if (!context.nodeDraft || context.nodeDraft->kind!=EditorNodeGraph::NodeKind::Compound)
                    return false;
                auto& overrides=context.nodeDraft->compound.instance.parameterOverrides;
                const auto previous=overrides.size();
                overrides.erase(std::remove_if(overrides.begin(),overrides.end(),[&](const auto& value) {
                    return context.drivenParameters.count(value.parameterId)==0;
                }),overrides.end());
                return overrides.size()!=previous;
            };
            return editor;
        }();
        return compoundControls;
    }
    static const NodeEditorDefinition graphControls;
    return graphControls;
}
const NodeEditorDefinition* FindNodeEditorForTool(Tool tool,int toneView) {
    for(const auto& editor:NodeEditorDefinitions()) if(editor.legacyTool==tool &&
        (editor.toneView<0 || (editor.toneView<2)==(toneView<2))) return &editor;
    return nullptr;
}
}
