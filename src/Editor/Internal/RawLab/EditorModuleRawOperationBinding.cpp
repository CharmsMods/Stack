#include "Editor/EditorModule.h"
#include "Project/RawLayerStack.h"
#include "Raw/RawGraphParameters.h"

using namespace Stack::Project;
using namespace Stack::RawRecipe;

std::optional<GraphOperationKind> EditorModule::SelectedRawOperationKind() const {
    switch (m_RawWorkspaceLabUi.activeTool) {
        case RawLabTool::Calibration: return GraphOperationKind::Calibration;
        case RawLabTool::Exposure: return GraphOperationKind::Exposure;
        case RawLabTool::Zones: return GraphOperationKind::LocalEv;
        case RawLabTool::Tone: return m_RawWorkspaceLabUi.sceneToneView < 2
            ? GraphOperationKind::LuminanceTone : GraphOperationKind::RgbCurves;
        case RawLabTool::Color: return GraphOperationKind::ColorWarp;
        case RawLabTool::Detail: return GraphOperationKind::DetailContrast;
        default: return std::nullopt;
    }
}

const EditorNodeGraph::Node* EditorModule::SelectedRawOperation() const {
    const auto kind = SelectedRawOperationKind();
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
    if (!kind || !layer) return nullptr;
    const auto selected = m_Project->rawOperationSelection.find(layer->id + "/" + GraphOperationId(*kind));
    return FindRawOperation(*layer, *kind, selected == m_Project->rawOperationSelection.end()
        ? std::string{} : selected->second);
}

bool EditorModule::IsRawParameterDriven(const std::string& parameter) const {
    const auto* operation = SelectedRawOperation();
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
    return operation && layer && layer->graph.FindInputLink(operation->id, "param:" + parameter);
}

void EditorModule::BindRawOperationContext(RawWorkspaceEditContext& context) const {
    if (!SelectedRawOperationKind()) {
        if (!m_SelectedRawAdjustmentLayer.empty() && m_SelectedRawAdjustmentLayer != kRawBackgroundId &&
            (m_RawWorkspaceLabUi.activeTool == RawLabTool::Light || m_RawWorkspaceLabUi.activeTool == RawLabTool::Denoise ||
             m_RawWorkspaceLabUi.activeTool == RawLabTool::RgbDenoise)) {
            context.canEdit = false;
            context.error = "Select Background to edit camera preparation.";
        }
        return;
    }
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
    if (!layer) return;
    context.rawAdjustmentLayerId = layer->id;
    context.persistedRecipe = nullptr;
    const auto* operation = SelectedRawOperation();
    if (!operation) {
        context.canEdit = false;
        context.error = "This operation is absent. Add an operation or select another instance.";
        return;
    }
    context.rawOperationUuid = operation->instanceUuid;
    auto editing = ReadRawLayerOperation(*layer, operation->instanceUuid);
    editing.source = context.recipe.source;
    editing.technical.workingSpace = context.recipe.technical.workingSpace;
    editing.technical.processingVersion = context.recipe.technical.processingVersion;
    editing.cropRotation = context.recipe.cropRotation;
    context.recipe = std::move(editing);
}

void EditorModule::RenderRawOperationInstancePicker(RawWorkspaceEditContext& context) {
    const auto kind = SelectedRawOperationKind();
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
    if (!kind || !layer) return;
    struct Choice { std::string uuid, title; bool bypass; bool connected; };
    std::vector<Choice> choices;
    for (const auto& node : layer->graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawOperation && node.rawOperation.kind == *kind)
            choices.push_back({node.instanceUuid, node.title, !node.rawOperation.enabled,
                layer->graph.FindInputLink(node.id, "imageIn") != nullptr});
    }
    const std::string key = layer->id + "/" + GraphOperationId(*kind);
    const auto* current = SelectedRawOperation();
    if (!current || choices.size() > 1) {
        ImGui::TextUnformatted("Instance");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##RawOperationInstance", current ? current->title.c_str() : "Select an instance")) {
            int index = 0;
            for (const auto& choice : choices) {
                ImGui::PushID(choice.uuid.c_str());
                const auto label = choice.title + " " + std::to_string(++index);
                if (ImGui::Selectable(label.c_str(), choice.uuid == context.rawOperationUuid)) {
                    m_Project->rawOperationSelection[key] = choice.uuid;
                    BeginRawWorkspaceEditContext(context.source, context);
                    ClearRawWorkspaceGraphScopeReadbackCaches();
                    m_RawWorkspaceAnalysisPending = true;
                    m_RawWorkspaceAnalysisRequested = false;
                    m_RawWorkspaceAnalysisScopeRetryCount = 0;
                    m_RawWorkspaceAnalysisQuietUntilTime = ImGui::GetTime() + 0.25;
                    MarkRenderRefreshDirty();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    current = SelectedRawOperation();
    if (current) {
        for (const auto& parameter : GraphParameters(current->rawOperation.kind)) {
            const auto* link = layer->graph.FindInputLink(current->id, "param:" + parameter.id);
            if (!link) continue;
            const auto* source = layer->graph.FindNode(link->fromNodeId);
            ImGui::TextDisabled("%s is driven by %s", parameter.label.c_str(), source ? source->title.c_str() : "an unresolved node");
        }
        if (!current->rawOperation.enabled) ImGui::TextDisabled("Bypassed. These settings are retained.");
        if (!layer->graph.FindInputLink(current->id, "imageIn")) ImGui::TextDisabled("Image input is disconnected.");
    } else if (ImGui::Button("Add before Layer result")) {
        auto candidate = m_Project->rawLayers.State();
        auto* destination = FindRawAdjustmentLayer(candidate, layer->id);
        const auto* result = FindRawRole(*destination, Stack::GraphModel::NodeRole::LayerResult);
        const int resultId = result->id;
        const auto* incoming = destination->graph.FindInputLink(resultId, "imageIn");
        const std::optional<EditorNodeGraph::Link> old = incoming ? std::optional<EditorNodeGraph::Link>(*incoming) : std::nullopt;
        auto* added = destination->graph.AddRawOperationNode(*kind, {1800, 180});
        const auto uuid = added->instanceUuid;
        const int addedId = added->id;
        bool valid = true;
        if (old) valid = destination->graph.TryConnectSockets(old->fromNodeId, old->fromSocketId, addedId, "imageIn", &m_RawLayerStatus);
        if (valid) valid = destination->graph.TryConnectSockets(addedId, "imageOut", resultId, "imageIn", &m_RawLayerStatus);
        if (valid && ApplyRawLayerStackEdit(std::move(candidate))) {
            m_Project->rawOperationSelection[key] = uuid;
            BeginRawWorkspaceEditContext(context.source, context);
        }
    }
}

bool EditorModule::RawStartingPointGraphSupported(std::string& reason) const {
    const auto& state = m_Project->rawLayers.State();
    const auto& layer = state.background;
    const auto unavailable = [&]() {
        reason = "Starting Point needs the default Background tool order, with one active instance per tool and no graph-driven parameters or tool masks. Edit this graph with the ordinary controls.";
        return false;
    };
    if (!layer.enabled || layer.opacity != 1.f || layer.layerMask || !layer.animation.tracks.empty()) return unavailable();
    for (const auto& other : state.layers) if (other.enabled && other.opacity != 0.f) return unavailable();
    const auto* result = FindRawRole(layer,Stack::GraphModel::NodeRole::LayerResult);
    if (!result) return unavailable();
    int next = result->id;
    for (int i=static_cast<int>(GraphOperationKind::Count)-1;i>=0;--i) {
        const auto kind = static_cast<GraphOperationKind>(i);
        const auto* input = layer.graph.FindInputLink(next,"imageIn");
        const auto* node = input ? layer.graph.FindNode(input->fromNodeId) : nullptr;
        if (!node || node->kind != EditorNodeGraph::NodeKind::RawOperation || node->rawOperation.kind != kind ||
            !node->rawOperation.enabled || input->fromSocketId != "imageOut") return unavailable();
        const auto selected = m_Project->rawOperationSelection.find(layer.id + "/" + GraphOperationId(kind));
        const auto* edited = FindRawOperation(layer,kind,
            selected == m_Project->rawOperationSelection.end() ? std::string{} : selected->second);
        if (!edited || edited->instanceUuid != node->instanceUuid) return unavailable();
        for (const auto& connection : layer.graph.GetLinks()) {
            if (connection.toNodeId != node->id || connection.toSocketId == "imageIn") continue;
            // Direct graph-owned coverage has the same additive mathematics
            // in the recipe evaluator. Processed or referenced coverage does not.
            const auto* coverage = layer.graph.FindNode(connection.fromNodeId);
            if ((connection.toSocketId.rfind("gradient:",0) != 0 && connection.toSocketId.rfind("area:",0) != 0) ||
                !coverage || coverage->kind != EditorNodeGraph::NodeKind::MaskGenerator ||
                connection.fromSocketId != "maskOut" ||
                coverage->role != Stack::GraphModel::NodeRole::Ordinary || coverage->rawCoverage.empty()) return unavailable();
        }
        next = node->id;
    }
    const auto* input = layer.graph.FindInputLink(next,"imageIn");
    const auto* current = FindRawRole(layer,Stack::GraphModel::NodeRole::CurrentImage);
    if (!input || !current || input->fromNodeId != current->id) return unavailable();
    reason.clear(); return true;
}

RawDevelopmentRecipe EditorModule::ReadRawControlRecipe() const {
    auto result = BuildWorkspaceSourceRecipe(m_Project->rawRecipe);
    const auto& layer = m_Project->rawLayers.State().background;
    for (int i=0; i<static_cast<int>(GraphOperationKind::Count); ++i) {
        const auto kind = static_cast<GraphOperationKind>(i);
        const auto selection = m_Project->rawOperationSelection.find(layer.id + "/" + GraphOperationId(kind));
        const auto* node = FindRawOperation(layer, kind,
            selection == m_Project->rawOperationSelection.end() ? std::string{} : selection->second);
        if (!node) continue;
        const auto part = ReadRawLayerOperation(layer, node->instanceUuid);
        switch (kind) {
            case GraphOperationKind::Calibration: result.colorCalibration = part.colorCalibration; break;
            case GraphOperationKind::Exposure: result.preToneExposureEv = part.preToneExposureEv; break;
            case GraphOperationKind::LocalEv: result.localRange = part.localRange; result.evGradients = part.evGradients; break;
            case GraphOperationKind::LuminanceTone: result.finishTone.layerJson["luminanceTone"] = part.finishTone.layerJson.at("luminanceTone"); break;
            case GraphOperationKind::RgbCurves: {
                const auto luminance = result.finishTone.layerJson.at("luminanceTone");
                result.finishTone = part.finishTone; result.finishTone.layerJson["luminanceTone"] = luminance; break;
            }
            case GraphOperationKind::ColorWarp: result.colorWarp = part.colorWarp; break;
            case GraphOperationKind::DetailContrast: result.detailContrast = part.detailContrast; break;
            default: break;
        }
    }
    return result;
}

bool EditorModule::PrepareRawControlRecipeEdit(const RawDevelopmentRecipe& recipe,
    RawLayerStackState& candidate, bool& changed, std::string& error) const {
    changed = false;
    const auto previous = ReadRawControlRecipe();
    candidate = m_Project->rawLayers.State();
    for (int i=0; i<static_cast<int>(GraphOperationKind::Count); ++i) {
        const auto kind = static_cast<GraphOperationKind>(i);
        auto before = MakeGraphOperation(kind), after = before;
        WriteGraphOperation(before, previous); WriteGraphOperation(after, recipe);
        if (before.parameters == after.parameters) continue;
        auto& layer = candidate.background;
        const auto selection = m_Project->rawOperationSelection.find(layer.id + "/" + GraphOperationId(kind));
        const auto* node = FindRawOperation(layer, kind,
            selection == m_Project->rawOperationSelection.end() ? std::string{} : selection->second);
        if (!node) {
            error = std::string("Add a ") + GraphOperationLabel(kind) + " operation before applying this adjustment.";
            return false;
        }
        if (!WriteRawLayerOperation(layer, node->instanceUuid, recipe, error)) return false;
        changed = true;
    }
    return ValidateRawLayerStack(candidate, error, &m_Project->graph, ResolveRawWorkspaceStageOutputNodeId());
}
