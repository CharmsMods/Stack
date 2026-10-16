#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Editor/Timeline/TimelineAnimation.h"
#include "Assets/CreatedAsset.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace Stack::Project {
inline constexpr int kRawLayerStackSchemaVersion = 8;

// Identities survive reorder and rename. Technical source settings remain
// project-owned; Background has the same creative graph as every other layer.
inline constexpr const char* kRawBackgroundId = "background";
struct RawMaskReference {
    std::string layerId;
    std::string outputId;
};

struct RawMaskOutput {
    // A selectable mask may name a shape source directly or an explicit Output
    // for a processed/shared result. Neither form requires a UI-only graph node.
    std::string id;
    std::string name;
    int nodeId = 0; // Transient lookup hint. Identity is always the UUID.
    std::string nodeUuid;
};

struct RawAdjustmentLayer {
    std::string id;
    std::string name;
    bool enabled = true;
    float opacity = 1.0f;
    std::optional<RawMaskReference> layerMask;
    EditorNodeGraph::Graph graph;
    Timeline::TimelineAnimationState animation;
    std::vector<RawMaskOutput> maskOutputs;
    // Project-canvas placement is independent of processing order.
    std::optional<EditorNodeGraph::Vec2> canvasOrigin;
    // Used only when the document has explicit project-canvas routing.
    // No endpoint means a disconnected layer input, not the previous layer.
    std::optional<GraphModel::Endpoint> imageInput;
};

RawAdjustmentLayer MakeRawLayer(std::string id, std::string name);

struct RawLayerStackState {
    RawAdjustmentLayer background = MakeRawLayer(kRawBackgroundId, "Background");
    std::uint64_t nextId = 1;
    // Evaluation order, from Background toward the final image.
    std::vector<RawAdjustmentLayer> layers;
    // Project assets have no layer owner. This document supplies transactions,
    // history and persistence until the future RAW layer document redesign.
    Assets::Library assets;
    bool explicitRouting = false;
    std::optional<GraphModel::Endpoint> imageOutput;
};

inline constexpr const char* kRawPreparedSourceGraph = "$raw-source";
GraphModel::Endpoint RawPreparedSourceEndpoint();
std::optional<GraphModel::Endpoint> RawLayerInputEndpoint(
    const RawLayerStackState& state, const std::string& layerId);
std::optional<GraphModel::Endpoint> RawMainOutputEndpoint(const RawLayerStackState& state);

std::vector<const RawAdjustmentLayer*> RawLayersInOrder(const RawLayerStackState& state);
EditorNodeGraph::Node* FindRawOperation(RawAdjustmentLayer& layer, RawRecipe::GraphOperationKind kind,
    const std::string& preferredUuid = {});
const EditorNodeGraph::Node* FindRawOperation(const RawAdjustmentLayer& layer, RawRecipe::GraphOperationKind kind,
    const std::string& preferredUuid = {});
const EditorNodeGraph::Node* FindRawRole(const RawAdjustmentLayer& layer, GraphModel::NodeRole role);
GraphModel::Endpoint RawLayerEndpoint(const RawAdjustmentLayer& layer, const EditorNodeGraph::Node& node,
    const std::string& port = "imageOut");
bool PublishRawLayerResult(RawLayerStackState& state, const GraphModel::Endpoint& producer,
    const std::string& recipientId, std::string& error);
bool SetRawOperationMask(RawLayerStackState& state, const std::string& layerId, const std::string& nodeUuid,
    const std::optional<RawMaskReference>& mask, std::string& error);
std::optional<RawMaskReference> GetRawOperationMask(const RawLayerStackState& state,
    const RawAdjustmentLayer& layer, const std::string& nodeUuid);
RawAdjustmentLayer* FindRawAdjustmentLayer(RawLayerStackState& state, const std::string& id);
const RawAdjustmentLayer* FindRawAdjustmentLayer(const RawLayerStackState& state, const std::string& id);
const RawMaskOutput* FindRawMaskOutput(const RawLayerStackState& state, const RawMaskReference& reference);
std::string AddRawAdjustmentLayer(RawLayerStackState& state, std::string name = {});
struct RawLocalAdjustment {
    std::string layerId;
    std::string operationUuid;
    RawMaskReference mask;
};
// Insert a neutral instance after this exact operation, with independent coverage
// in the same layer. A rejected insertion leaves the document unchanged.
RawLocalAdjustment AddRawLocalAdjustment(RawLayerStackState& state, const std::string& layerId,
    const std::string& afterOperationUuid, EditorNodeGraph::MaskGeneratorKind shape, std::string& error);
// RAW mask application preserves Background's global instance by inserting a
// fresh default operation. Other layers apply coverage to the selected instance.
RawLocalAdjustment ApplyRawShapeMask(RawLayerStackState& state, const std::string& layerId,
    const std::string& operationUuid, EditorNodeGraph::MaskGeneratorKind shape, std::string& error);
RawLocalAdjustment ApplyRawCapturedMask(RawLayerStackState& state, const std::string& layerId,
    const std::string& operationUuid, const Assets::FloatData& pixels, const std::string& name, std::string& error);
bool MoveRawAdjustmentLayer(RawLayerStackState& state, const std::string& id, std::size_t index, std::string& error);
RawMaskReference AddRawGeneratedMask(
    RawLayerStackState& state, const std::string& layerId,
    EditorNodeGraph::MaskGeneratorKind kind, std::string name);
RawMaskReference AddRawShapeMask(RawLayerStackState& state, const std::string& layerId,
    EditorNodeGraph::MaskGeneratorKind kind, std::string name);
GraphModel::Endpoint RawMaskEndpoint(const RawAdjustmentLayer& layer, const EditorNodeGraph::Node& node);
const EditorNodeGraph::Node* FindRawPublishedNode(const RawAdjustmentLayer& layer, const RawMaskOutput& output);
void RegisterRawMaskOutputs(RawLayerStackState& state, const std::string& layerId);

// Includes graph links, stage samples, tool masks and whole-layer masks in ONE
// dependency check, even for disabled layers. Incomplete mask graphs are valid
// authored documents and are diagnosed separately from dependency cycles.
bool ValidateRawLayerStack(const RawLayerStackState& state, std::string& error,
    const EditorNodeGraph::Graph* composition = nullptr, int sourceNodeId = 0);
RawRecipe::RawDevelopmentRecipe ReadRawLayerOperation(const RawAdjustmentLayer& layer, const std::string& uuid);
const EditorNodeGraph::Node* FindRawCoverageOwner(const RawAdjustmentLayer& layer, int operationId,
    const std::string& port, const std::string& coverageId);
bool WriteRawLayerOperation(RawAdjustmentLayer& layer, const std::string& uuid,
    const RawRecipe::RawDevelopmentRecipe& recipe, std::string& error);
nlohmann::json SerializeRawLayerStack(const RawLayerStackState& state, bool includeAssets = true);
bool SameRawLayerStackState(const RawLayerStackState& a, const RawLayerStackState& b);
bool DeserializeRawLayerStack(const nlohmann::json& value, RawLayerStackState& result, std::string& error);
bool ReadRawLayersFromPipeline(const nlohmann::json& pipeline, RawLayerStackState& result, std::string& error, bool required = false);

// History belongs to the project. A gesture can preview many valid candidates
// and creates one undo entry when committed. Rejected candidates change nothing.
enum class RawLayerHistoryAction { Undo, Redo, CancelGesture };
struct RawLayerSourceHistory {
    RawRecipe::RawDevelopmentRecipe recipe;
    // Empty identifies the single-image source. Merged settings retain the
    // exact source-set identity even if a different set is selected later.
    std::string sourceSetId;
};
class RawLayerStackDocument {
public:
    const RawLayerStackState& State() const { return m_State; }
    std::uint64_t Revision() const { return m_Revision; }
    std::uint64_t ProcessingRevision() const { return m_ProcessingRevision; }
    bool Apply(RawLayerStackState candidate, std::string& error, bool affectsPixels = true, bool completesAction = false);
    // Source preparation and graph changes share one undo entry. Source data
    // remains owned by the project; history holds only prior setting values.
    bool ApplySourceEdit(RawLayerStackState candidate, RawRecipe::RawDevelopmentRecipe& source,
        const RawRecipe::RawDevelopmentRecipe& nextSource, std::string& error,
        const EditorNodeGraph::Graph* composition = nullptr, int sourceNodeId = 0,
        const std::string& sourceSetId = {});
    bool Load(const nlohmann::json& value, std::string& error);
    bool CompleteMaskCapture(const std::string& layerId, const std::string& maskUuid,
        const std::string& token, const Assets::Record& asset, std::string& error);
    void BeginGesture();
    void EndGesture();
    bool CancelGesture(RawRecipe::RawDevelopmentRecipe* source = nullptr);
    const RawLayerSourceHistory* PendingSourceHistory(RawLayerHistoryAction action) const;
    bool Undo(std::string* error = nullptr, const EditorNodeGraph::Graph* composition = nullptr, int sourceNodeId = 0,
        RawRecipe::RawDevelopmentRecipe* source = nullptr);
    bool Redo(std::string* error = nullptr, const EditorNodeGraph::Graph* composition = nullptr, int sourceNodeId = 0,
        RawRecipe::RawDevelopmentRecipe* source = nullptr);
    bool CanUndo() const { return !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }
    bool GestureActive() const { return m_Gesture.has_value(); }
    void Reset();
private:
    struct HistoryEntry {
        RawLayerStackState state;
        bool affectsPixels = true;
        std::optional<RawLayerSourceHistory> source;
    };
    void PushUndo(RawLayerStackState state, bool affectsPixels,
        std::optional<RawLayerSourceHistory> source = std::nullopt);
    RawLayerStackState m_State;
    std::deque<HistoryEntry> m_Undo;
    std::deque<HistoryEntry> m_Redo;
    std::optional<RawLayerStackState> m_Gesture;
    std::optional<RawLayerSourceHistory> m_GestureSource;
    std::optional<RawRecipe::RawDevelopmentRecipe> m_GestureSourceAfter;
    bool m_GestureChanged = false;
    bool m_GestureAffectsPixels = false;
    std::uint64_t m_Revision = 0;
    std::uint64_t m_ProcessingRevision = 0;
};

} // namespace Stack::Project
