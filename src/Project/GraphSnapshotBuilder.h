#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/Timeline/TimelineFrameProducer.h"
#include "Project/ProcessedRawResult.h"
#include "Renderer/MaskRenderTypes.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Stack::Project {
struct RawProjectSnapshot;

// References are needed only for the duration of the call. Output snapshots
// retain immutable image buffers, with no editor or active-tab dependency.
struct GraphSnapshotInputs {
    const EditorNodeGraph::Graph& graph;
    const std::vector<std::shared_ptr<LayerBase>>& layers;
    const Timeline::TimelineAnimationState& timeline;
    const std::unordered_map<int, std::uint64_t>& nodeDirtyGenerations;
    const RawRecipe::RawDevelopmentRecipe& rawRecipe;
    const std::shared_ptr<const Raw::RawImageData>& singleRawSource;
    const std::vector<Timeline::AnimatableParameterTarget>& liveEditPreviewTargets;
    Timeline::TimelineFrameRequest frame;
    int liveEditPreviewFrame = -1;
    std::string graphId = "project";
    const RawProjectSnapshot* rawProject = nullptr;
    const MfdAdoptedRawResult* mfdResult = nullptr;
    const HdrAdoptedRawResult* hdrResult = nullptr;
    int stageOutputNodeId = 0;
    bool executionInspectionEnabled = false;
    // Graphs owned by a RAW layer retain authored processing settings without
    // creating editor/GL layer instances solely to capture a render snapshot.
    const nlohmann::json* layerSettings = nullptr;
    const EditorNodeGraph::GraphOutputContext* outputContextOverrides = nullptr;
};

struct GraphSnapshotResult {
    RenderGraphSnapshot snapshot;
    EditorNodeGraph::GraphOutputDescriptions outputDescriptions;
    // Stage-only requests and failed compound expansion do not replace the
    // ordinary graph's displayed semantic descriptions.
    bool publishSemantics = false;
};

GraphSnapshotResult BuildGraphSnapshot(const GraphSnapshotInputs& inputs);

std::vector<std::shared_ptr<LayerBase>> BuildGraphRenderLayers(
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::shared_ptr<LayerBase>>& layers);
std::vector<RenderLayerStep> BuildGraphRenderSteps(
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::shared_ptr<LayerBase>>& layers);
std::vector<RenderMaskSource> BuildGraphRenderMasks(
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::shared_ptr<LayerBase>>& layers);

} // namespace Stack::Project
