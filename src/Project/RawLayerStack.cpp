#include "Project/RawLayerStack.h"
#include "Project/RawCapturedMask.h"
#include "NodeMath/ToneCurveParameters.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace Stack::Project {
namespace {
void ClearPendingAnalysis(RawLayerStackState& state) {
    const auto clear=[](RawAdjustmentLayer& layer) {
        for (auto& node:layer.graph.GetNodes()) if (node.kind==EditorNodeGraph::NodeKind::RawOperation &&
            node.rawOperation.kind==RawRecipe::GraphOperationKind::ToneCurve)
            NodeMath::ClearPendingToneCurveAnalysis(node.rawOperation.parameters);
    };
    clear(state.background);for (auto& layer:state.layers) clear(layer);
}
std::string NextIdentity(RawLayerStackState& state, const char* prefix) {
    if (state.nextId == 0 || state.nextId == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("RAW layer identity space is exhausted.");
    for (;;) {
        const auto id = std::string(prefix) + std::to_string(state.nextId++);
        bool used = FindRawAdjustmentLayer(state, id) != nullptr;
        for (const auto* layer : RawLayersInOrder(state))
            used |= std::any_of(layer->maskOutputs.begin(), layer->maskOutputs.end(),
                [&](const auto& output) { return output.id == id; });
        if (!used) return id;
        if (state.nextId == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("RAW layer identity space is exhausted.");
    }
}
} // namespace

RawAdjustmentLayer* FindRawAdjustmentLayer(RawLayerStackState& state, const std::string& id) {
    if (id.empty() || id == kRawBackgroundId) return &state.background;
    const auto it = std::find_if(state.layers.begin(), state.layers.end(),
        [&](const auto& layer) { return layer.id == id; });
    return it == state.layers.end() ? nullptr : &*it;
}
const RawAdjustmentLayer* FindRawAdjustmentLayer(const RawLayerStackState& state, const std::string& id) {
    if (id.empty() || id == kRawBackgroundId) return &state.background;
    const auto it = std::find_if(state.layers.begin(), state.layers.end(),
        [&](const auto& layer) { return layer.id == id; });
    return it == state.layers.end() ? nullptr : &*it;
}
const RawMaskOutput* FindRawMaskOutput(const RawLayerStackState& state, const RawMaskReference& reference) {
    const auto* layer = FindRawAdjustmentLayer(state, reference.layerId);
    if (!layer) return nullptr;
    const auto it = std::find_if(layer->maskOutputs.begin(), layer->maskOutputs.end(),
        [&](const auto& output) { return output.id == reference.outputId; });
    return it == layer->maskOutputs.end() ? nullptr : &*it;
}

std::string AddRawAdjustmentLayer(RawLayerStackState& state, std::string name) {
    const auto input = RawMainOutputEndpoint(state);
    auto layer = MakeRawLayer(NextIdentity(state, "layer-"),
        name.empty() ? "Layer " + std::to_string(state.layers.size() + 1) : std::move(name));
    const auto id = layer.id;
    if (state.explicitRouting) {
        layer.imageInput = input;
        state.imageOutput = RawLayerEndpoint(layer,*FindRawRole(layer,GraphModel::NodeRole::LayerResult));
    }
    state.layers.push_back(std::move(layer));
    return id;
}

bool MoveRawAdjustmentLayer(RawLayerStackState& state, const std::string& id,
    std::size_t index, std::string& error) {
    if (id.empty() || id == kRawBackgroundId || index >= state.layers.size() || !FindRawAdjustmentLayer(state, id)) {
        error = "The layer or destination no longer exists.";
        return false;
    }
    auto candidate = state;
    if (candidate.explicitRouting) {
        // The list represents a simple main chain. Branches are reordered by
        // their visible graph wires instead of silently rerouting other outputs.
        auto expected = RawLayerEndpoint(candidate.background,*FindRawRole(candidate.background,GraphModel::NodeRole::LayerResult));
        for (const auto& layer : candidate.layers) {
            if (!layer.imageInput || !(*layer.imageInput == expected)) {
                error = "Reorder this branched chain by connecting its layer caps in Graph.";
                return false;
            }
            expected = RawLayerEndpoint(layer,*FindRawRole(layer,GraphModel::NodeRole::LayerResult));
        }
        if (!candidate.imageOutput || !(*candidate.imageOutput == expected)) {
            error = "Reorder this chain in Graph."; return false;
        }
    }
    const auto oldIndex = static_cast<std::size_t>(FindRawAdjustmentLayer(candidate, id) - candidate.layers.data());
    auto moved = std::move(candidate.layers[oldIndex]);
    candidate.layers.erase(candidate.layers.begin() + oldIndex);
    candidate.layers.insert(candidate.layers.begin() + index, std::move(moved));
    if (candidate.explicitRouting) {
        auto previous = RawLayerEndpoint(candidate.background,*FindRawRole(candidate.background,GraphModel::NodeRole::LayerResult));
        for (auto& layer : candidate.layers) {
            layer.imageInput = previous;
            previous = RawLayerEndpoint(layer,*FindRawRole(layer,GraphModel::NodeRole::LayerResult));
        }
        candidate.imageOutput = previous;
    }
    if (!ValidateRawLayerStack(candidate, error)) return false;
    state = std::move(candidate);
    return true;
}

RawMaskReference AddRawGeneratedMask(RawLayerStackState& state, const std::string& layerId,
    EditorNodeGraph::MaskGeneratorKind kind, std::string name) {
    auto* layer = FindRawAdjustmentLayer(state, layerId);
    if (!layer) return {};
    const auto id = NextIdentity(state, "mask-");
    const float y = static_cast<float>(layer->maskOutputs.size()) * 230.0f;
    const int generatorId = layer->graph.AddMaskGeneratorNode(kind, {0.0f, y})->id;
    const int outputId = layer->graph.AddOutputNode({320.0f, y})->id;
    layer->graph.FindNode(outputId)->title = name;
    layer->graph.FindNode(outputId)->outputSettings.maskOutput = true;
    std::string error;
    if (!layer->graph.TryConnectSockets(generatorId, "maskOut", outputId, "imageIn", &error))
        throw std::runtime_error("Cannot connect the generated RAW mask: " + error);
    layer->maskOutputs.push_back({id, std::move(name), outputId, layer->graph.FindNode(outputId)->instanceUuid});
    return {layer->id, id};
}

const EditorNodeGraph::Node* FindRawPublishedNode(const RawAdjustmentLayer& layer, const RawMaskOutput& output) {
    for (const auto& node : layer.graph.GetNodes())
        if (node.instanceUuid == output.nodeUuid) return &node;
    return nullptr;
}

RawMaskReference AddRawShapeMask(RawLayerStackState& state, const std::string& layerId,
    EditorNodeGraph::MaskGeneratorKind kind, std::string name) {
    using Kind = EditorNodeGraph::MaskGeneratorKind;
    if (kind != Kind::LinearGradient && kind != Kind::RadialGradient && kind != Kind::Square)
        throw std::invalid_argument("A drawn mask needs a geometric shape.");
    auto* layer = FindRawAdjustmentLayer(state,layerId);
    if (!layer) return {};
    const auto id = NextIdentity(state,"mask-");
    auto* source = layer->graph.AddMaskTextureNode(kind,{0,230.f * layer->maskOutputs.size()});
    source->title = name + " mask texture";
    // The owning RAW project supplies the canvas. No adjustment supplies pixels
    // to this source, and its shape remains editable independently of consumers.
    source->rawCoverage = {{"canvas","project"}};
    layer->maskOutputs.push_back({id,std::move(name),source->id,source->instanceUuid});
    return {layer->id,id};
}

GraphModel::Endpoint RawMaskEndpoint(const RawAdjustmentLayer& layer, const EditorNodeGraph::Node& node) {
    return RawLayerEndpoint(layer,node,(node.kind == EditorNodeGraph::NodeKind::MaskGenerator || node.kind == EditorNodeGraph::NodeKind::MaskTexture || node.kind == EditorNodeGraph::NodeKind::CustomMask) ? "maskOut" : "imageOut");
}

void RegisterRawMaskOutputs(RawLayerStackState& state, const std::string& layerId) {
    auto* layer = FindRawAdjustmentLayer(state, layerId);
    if (!layer) return;
    for (const auto& node : layer->graph.GetNodes()) {
        const bool shape = (node.kind == EditorNodeGraph::NodeKind::MaskGenerator || node.kind == EditorNodeGraph::NodeKind::MaskTexture || node.kind == EditorNodeGraph::NodeKind::CustomMask) &&
            node.role == GraphModel::NodeRole::Ordinary && node.rawCoverage.value("canvas",std::string{}) == "project";
        if (!shape && (node.kind != EditorNodeGraph::NodeKind::Output || !node.outputSettings.maskOutput)) continue;
        const auto found = std::find_if(layer->maskOutputs.begin(), layer->maskOutputs.end(),
            [&](const auto& output) { return output.nodeUuid == node.instanceUuid; });
        if (found == layer->maskOutputs.end())
            layer->maskOutputs.push_back({NextIdentity(state, "mask-"), node.title, node.id, node.instanceUuid});
        else found->name = node.title;
    }
}

bool RawLayerStackDocument::CompleteMaskCapture(const std::string& layerId, const std::string& maskUuid,
    const std::string& token, const Assets::Record& asset, std::string& error) try {
    if (!asset.data || asset.kind!=Assets::Kind::Mask) {error="Capture has no coverage pixels.";return false;}
    const auto payload=CapturedMaskPayload(asset.data);
    const auto complete=[&](RawLayerStackState& state) {
        auto* layer=FindRawAdjustmentLayer(state,layerId);
        if(!layer) return false;
        for(auto& node:layer->graph.EditNodes()) if(node.instanceUuid==maskUuid &&
            node.rawCoverage.value("captureToken",std::string{})==token) {
            node.customMask=payload;node.title=asset.name;node.rawCoverage.erase("captureToken");
            if(std::none_of(state.assets.begin(),state.assets.end(),[&](const auto& a){return a.id==asset.id;})) state.assets.push_back(asset);
            return true;
        }
        return false;
    };
    auto next=m_State;
    if(!complete(next)) {error="The unfinished mask was removed or replaced.";return false;}
    if(!ValidateRawLayerStack(next,error)) return false;
    // Fill the same logical capture in history too. Undoing a later curve edit
    // must not restore an unfinished black placeholder or require a second undo
    // merely to remove asynchronous completion.
    auto undo=m_Undo,redo=m_Redo;auto gesture=m_Gesture;
    for(auto& entry:undo) complete(entry.state);
    for(auto& entry:redo) complete(entry.state);
    if(gesture) complete(*gesture);
    m_State=std::move(next);m_Undo=std::move(undo);m_Redo=std::move(redo);m_Gesture=std::move(gesture);
    ++m_Revision;++m_ProcessingRevision;error.clear();return true;
} catch (const std::exception& failure) {
    error=failure.what();return false;
}

void RawLayerStackDocument::PushUndo(RawLayerStackState state, bool affectsPixels,
    std::optional<RawLayerSourceHistory> source) {
    constexpr std::size_t limit = 64;
    if (m_Undo.size() == limit) m_Undo.pop_front();
    m_Undo.push_back({std::move(state), affectsPixels, std::move(source)});
}
bool RawLayerStackDocument::Apply(RawLayerStackState candidate, std::string& error, bool affectsPixels, bool completesAction) {
    if (!ValidateRawLayerStack(candidate, error)) return false;
    if (m_Gesture) { m_GestureChanged = true; m_GestureAffectsPixels |= affectsPixels; }
    else if (!completesAction) {
        PushUndo(std::move(m_State), affectsPixels);
        m_Redo.clear();
    }
    m_State = std::move(candidate);
    ++m_Revision;
    if (affectsPixels) ++m_ProcessingRevision;
    return true;
}
bool RawLayerStackDocument::ApplySourceEdit(RawLayerStackState candidate,
    RawRecipe::RawDevelopmentRecipe& source, const RawRecipe::RawDevelopmentRecipe& nextSource,
    std::string& error, const EditorNodeGraph::Graph* composition, int sourceNodeId,
    const std::string& sourceSetId) {
    if (!ValidateRawLayerStack(candidate,error,composition,sourceNodeId)) return false;
    if (m_GestureSource && m_GestureSource->sourceSetId != sourceSetId) {
        error = "Finish the current source gesture before editing another source.";
        return false;
    }
    auto prepared = RawRecipe::BuildWorkspaceSourceRecipe(nextSource);
    // A settings edit cannot replace the immutable source asset binding.
    prepared.source = source.source;
    const bool sourceChanged = RawRecipe::SerializeWorkspaceSourceRecipe(source) !=
        RawRecipe::SerializeWorkspaceSourceRecipe(prepared);
    if (!sourceChanged && SameRawLayerStackState(candidate,m_State)) return true;
    auto before = sourceChanged ? std::optional<RawLayerSourceHistory>(RawLayerSourceHistory{source,sourceSetId}) : std::nullopt;
    if (m_Gesture) {
        if (!m_GestureSource && before) m_GestureSource = std::move(before);
        if (m_GestureSource) m_GestureSourceAfter = prepared;
        m_GestureChanged = true;
        m_GestureAffectsPixels = true;
    } else {
        PushUndo(std::move(m_State),true,std::move(before));
        m_Redo.clear();
    }
    m_State = std::move(candidate);
    source = std::move(prepared);
    ++m_Revision;
    ++m_ProcessingRevision;
    return true;
}
bool RawLayerStackDocument::Load(const nlohmann::json& value, std::string& error) {
    RawLayerStackState candidate;
    if (!DeserializeRawLayerStack(value, candidate, error)) return false;
    m_State = std::move(candidate);
    m_Undo.clear();
    m_Redo.clear();
    m_Gesture.reset();
    m_GestureSource.reset();
    m_GestureSourceAfter.reset();
    m_GestureChanged = false;
    m_GestureAffectsPixels = false;
    ++m_Revision;
    ++m_ProcessingRevision;
    return true;
}
void RawLayerStackDocument::BeginGesture() {
    if (m_Gesture) return;
    m_Gesture = m_State;
    m_GestureChanged = false;
    m_GestureAffectsPixels = false;
}
void RawLayerStackDocument::EndGesture() {
    if (!m_Gesture) return;
    const bool sourceChanged = m_GestureSource && m_GestureSourceAfter &&
        RawRecipe::SerializeWorkspaceSourceRecipe(m_GestureSource->recipe) !=
        RawRecipe::SerializeWorkspaceSourceRecipe(*m_GestureSourceAfter);
    if (m_GestureChanged && (sourceChanged || !SameRawLayerStackState(*m_Gesture,m_State))) {
        PushUndo(std::move(*m_Gesture), m_GestureAffectsPixels,sourceChanged ? std::move(m_GestureSource) : std::nullopt);
        m_Redo.clear();
    }
    m_Gesture.reset();
    m_GestureSource.reset();
    m_GestureSourceAfter.reset();
    m_GestureChanged = false;
    m_GestureAffectsPixels = false;
}
const RawLayerSourceHistory* RawLayerStackDocument::PendingSourceHistory(RawLayerHistoryAction action) const {
    if (action == RawLayerHistoryAction::CancelGesture) return m_GestureSource ? &*m_GestureSource : nullptr;
    const auto& entries = action == RawLayerHistoryAction::Undo ? m_Undo : m_Redo;
    return !entries.empty() && entries.back().source ? &*entries.back().source : nullptr;
}
bool RawLayerStackDocument::CancelGesture(RawRecipe::RawDevelopmentRecipe* source) {
    if (!m_Gesture) return false;
    if (m_GestureSource && !source) return false;
    const bool changed = m_GestureChanged;
    const bool pixelsChanged = m_GestureAffectsPixels;
    m_State = std::move(*m_Gesture);
    ClearPendingAnalysis(m_State);
    if (m_GestureSource) *source = std::move(m_GestureSource->recipe);
    m_Gesture.reset();
    m_GestureSource.reset();
    m_GestureSourceAfter.reset();
    m_GestureChanged = false;
    m_GestureAffectsPixels = false;
    if (changed) { ++m_Revision; if (pixelsChanged) ++m_ProcessingRevision; }
    return changed;
}
bool RawLayerStackDocument::Undo(std::string* error, const EditorNodeGraph::Graph* composition, int sourceNodeId,
    RawRecipe::RawDevelopmentRecipe* source) {
    EndGesture();
    if (m_Undo.empty()) return false;
    if (m_Undo.back().source && !source) { if (error) *error = "Source settings are required to undo this edit."; return false; }
    std::string reason;
    if (!ValidateRawLayerStack(m_Undo.back().state,reason,composition,sourceNodeId)) { if (error) *error = reason; return false; }
    const bool affectsPixels = m_Undo.back().affectsPixels;
    auto currentSource = m_Undo.back().source ? std::optional<RawLayerSourceHistory>(RawLayerSourceHistory{*source,m_Undo.back().source->sourceSetId}) : std::nullopt;
    m_Redo.push_back({std::move(m_State), affectsPixels, std::move(currentSource)});
    m_State = std::move(m_Undo.back().state);
    ClearPendingAnalysis(m_State);
    if (m_Undo.back().source) *source = std::move(m_Undo.back().source->recipe);
    m_Undo.pop_back();
    ++m_Revision;
    if (affectsPixels) ++m_ProcessingRevision;
    return true;
}
bool RawLayerStackDocument::Redo(std::string* error, const EditorNodeGraph::Graph* composition, int sourceNodeId,
    RawRecipe::RawDevelopmentRecipe* source) {
    EndGesture();
    if (m_Redo.empty()) return false;
    if (m_Redo.back().source && !source) { if (error) *error = "Source settings are required to redo this edit."; return false; }
    std::string reason;
    if (!ValidateRawLayerStack(m_Redo.back().state,reason,composition,sourceNodeId)) { if (error) *error = reason; return false; }
    const bool affectsPixels = m_Redo.back().affectsPixels;
    auto currentSource = m_Redo.back().source ? std::optional<RawLayerSourceHistory>(RawLayerSourceHistory{*source,m_Redo.back().source->sourceSetId}) : std::nullopt;
    PushUndo(std::move(m_State), affectsPixels, std::move(currentSource));
    m_State = std::move(m_Redo.back().state);
    ClearPendingAnalysis(m_State);
    if (m_Redo.back().source) *source = std::move(m_Redo.back().source->recipe);
    m_Redo.pop_back();
    ++m_Revision;
    if (affectsPixels) ++m_ProcessingRevision;
    return true;
}
void RawLayerStackDocument::Reset() {
    m_State = {};
    m_Undo.clear();
    m_Redo.clear();
    m_Gesture.reset();
    m_GestureSource.reset();
    m_GestureSourceAfter.reset();
    m_GestureChanged = false;
    m_GestureAffectsPixels = false;
    ++m_Revision;
    ++m_ProcessingRevision;
}
} // namespace Stack::Project
